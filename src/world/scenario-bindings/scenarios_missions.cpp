module;
#include "native_headers.hpp"

module phoenix.simulation;
import :scenario_bindings;

namespace phoenix::world_detail
{
auto Runtime::truly_destroyed(EntityId id) const -> bool
{
	const auto *s = entities.at(id).try_get<Destructible>();
	return s && s->destroyed_tick >= 0;
}

auto Runtime::mission_subject(ScenarioId instance_id, const ScenarioActor &a) const -> EntityId
{
	EntityId id = 0;
	if (a.kind == ScenarioActorKind::Entity)
		id = a.entity;
	else if (a.kind == ScenarioActorKind::ShipSlot)
	{
		auto i = slot_entities.find(a.name);
		if (i != slot_entities.end())
			id = i->second;
	}
	else if (scenario && scenario->runtime().instances().contains(instance_id))
	{
		const auto &handles = scenario->runtime().instances().at(instance_id).entity_handles;
		auto i = handles.find(a.name);
		if (i != handles.end() && i->second.size() == 1)
			id = i->second[0];
	}
	return entities.contains(id) ? id : 0;
}

auto Runtime::lifecycle(EntityId id) const -> std::string
{
	return truly_destroyed(id) ? "destroyed" : departed(id) ? "departed" : "active";
}

auto Runtime::personnel_dispositions(EntityId ship) const -> std::map<std::string, std::string>
{
	std::map<std::string, std::string> postings;
	if (personnel_postings.contains(ship))
		for (const auto &[asset, posting] : personnel_postings.at(ship))
			postings.emplace(asset, posting.status);
	std::vector<std::string> dead;
	const auto &definition_personnel = definition(ship).personnel;
	const auto &personnel = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < definition_personnel.officers.size(); ++i)
		if (personnel.officers[i].condition == "dead")
			dead.push_back(definition_personnel.officers[i].id);
	const std::set<std::string> no_losses;
	const auto lost = mission_ledger.lost.find(ship);
	return away_operations.personnel_dispositions(ship, slot_entities, postings,
	                                              lost == mission_ledger.lost.end() ? no_losses : lost->second, dead);
}

auto Runtime::mission_fact(ScenarioId instance_id, const ScenarioFactBinding &b) const -> state_tree::Value
{
	using state_tree::Value;
	if (b.kind == "clock")
		return Value::integer(static_cast<std::int64_t>(ecs.get<Clock>().tick));
	if (b.kind == "event")
	{
		const bool filtered = b.subject.entity || !b.subject.name.empty();
		const auto subject = filtered ? mission_subject(instance_id, b.subject) : 0;
		if (filtered && !subject)
			return {};
		auto match = b.match;
		if (filtered)
		{
			const ScenarioParameter value{static_cast<std::int64_t>(subject)};
			const auto [field, inserted] = match.emplace("subject", value);
			if (!inserted && field->second != value)
				return Value::boolean(false);
		}
		return Value::boolean(scenario->runtime().has_event(b.event, match));
	}
	auto id = mission_subject(instance_id, b.subject);
	if (!id)
		return {};
	if (b.kind == "objective")
	{
		const ScenarioObjective *latest = nullptr;
		for (const auto &[key, o] : scenario->runtime().objectives())
			if (o.instance == instance_id && (o.handle == b.operation || o.definition == b.operation) &&
			    mission_subject(o.instance, o.actor) == id && (!latest || o.id > latest->id))
				latest = &o;
		if (b.field == "completed_tick")
			return latest && latest->status == ObjectiveStatus::Completed ? Value::integer(latest->completed_tick)
			                                                              : Value{};
		return latest ? Value::string(objective_status_name(latest->status)) : Value{};
	}
	if (b.kind == "entity")
	{
		if (b.field == "captured")
			return Value::boolean(captured_by.contains(id));
		if (b.field == "owner_faction")
			return Value::string(entities.at(id).get<FactionMembership>().id);
		return Value::string(b.field == "definition" ? entities.at(id).get<Definition>().name : lifecycle(id));
	}
	if (b.kind == "inventory")
	{
		const auto &d = definition(id).logistics;
		if (!entities.at(id).has<MaterialInventory>() || !d.materials.contains(b.operation))
			return {};
		for (std::size_t i = 0; i < d.stores.size(); ++i)
			if (d.stores[i].id == b.field)
				return Value::integer(material_quantity(id, i, b.operation));
		return {};
	}
	if (b.kind == "personnel")
	{
		const auto dispositions = personnel_dispositions(id);
		return Value::integer(std::count_if(dispositions.begin(), dispositions.end(),
		                                    [&](const auto &asset)
		                                    {
			                                    return asset.second == b.field;
		                                    }));
	}
	if (b.kind == "compartment")
	{
		const auto *s = entities.at(id).try_get<CombatStructure>();
		if (!s)
			return {};
		for (std::size_t i = 0; i < s->compartments.size(); ++i)
			if (s->definition.compartments[i].id == b.field)
				return Value::boolean(!truly_destroyed(id) &&
				                      s->compartments[i].hp == s->definition.compartments[i].maximum &&
				                      !s->compartments[i].disabled);
		return {};
	}
	if (b.kind == "away")
		for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
			if (away_ship(away_operations.definitions()[i]) == id && away_operations.definitions()[i].id == b.operation)
			{
				const auto &m = away_operations.missions()[i];
				if (b.field == "phase")
				{
					Json p = m.phase;
					return Value::string(p.get<std::string>());
				}
				if (b.field == "outcome")
					return Value::string(m.outcome);
				if (b.field == "crew_deaths")
					return Value::integer(m.crew_deaths);
				if (b.field == "ever_deployed")
					return Value::boolean(m.ever_deployed);
				if (b.field == "recovered")
					return Value::boolean(m.ever_deployed && m.phase == AwayPhase::Recovered);
				return Value::boolean(m.phase == AwayPhase::Lost);
			}
	return {};
}

auto Runtime::mission_snapshot(ScenarioId instance_id) const -> ScenarioFacts
{
	ScenarioFacts facts;
	for (const auto &[key, b] : scenario->runtime().definition().fact_bindings)
		facts.emplace(key, mission_fact(instance_id, b));
	for (const auto &[name, o] : scenario->runtime().definition().objectives)
		facts.emplace("actor." + name, state_tree::Value::integer(mission_subject(instance_id, o.actor)));
	return facts;
}

auto Runtime::mission_transition(std::string kind, EntityId source, EntityId subject, std::string operation,
                                 std::string detail) -> void
{
	if (!scenario)
		return;
	scenario->synchronize_time(ecs.get<Clock>().tick);
	auto &r = scenario->runtime();
	const auto t = append_mission_transition(mission_ledger, ecs.get<Clock>().tick, conversation_instance,
	                                         r.definition().maximum_events, std::move(kind), source, subject,
	                                         std::move(operation), std::move(detail));
	for (std::size_t i = 0; i < r.definition().event_bindings.size(); ++i)
	{
		const auto &b = r.definition().event_bindings[i];
		if (b.kind != t.kind || (!b.operation.empty() && b.operation != t.operation) ||
		    (!b.detail.empty() && b.detail != t.detail))
			continue;
		ScenarioId main = 0;
		for (const auto &[id, instance] : r.instances())
			if (instance.name == "main")
				main = id;
		if (b.subject && mission_subject(main, *b.subject) != t.subject)
			continue;
		const auto &result =
		    r.emit({mission_ledger.instance, t.id, static_cast<std::uint32_t>(0x7fff0000U + i)}, b.event,
		           {{"source", static_cast<std::int64_t>(source)},
		            {"subject", static_cast<std::int64_t>(subject)},
		            {"ship", static_cast<std::int64_t>(source)},
		            {"operation", t.operation},
		            {"detail", t.detail},
		            {"tick", static_cast<std::int64_t>(t.tick)},
		            {"kind", t.kind},
		            {"outcome", t.detail}});
		if (result.status == ScenarioActionStatus::Failed)
			throw std::runtime_error(result.reason);
	}
}

auto Runtime::mission_change(const std::string &key, const std::string &value, const std::string &kind, EntityId source,
                             EntityId subject, const std::string &operation, const std::string &detail) -> void
{
	if (!update_mission_state(mission_ledger, key, value))
		return;
	if (!kind.empty())
		mission_transition(kind, source, subject, operation, detail);
}

auto Runtime::resolve_lost_parties() -> void
{
	for (auto &[ship, postings] : personnel_postings)
		for (auto it = postings.begin(); it != postings.end();)
			if (truly_destroyed(it->second.target))
			{
				mission_ledger.lost[ship].insert(it->first);
				detained_asset(ship, it->first, false);
				if (it->first.starts_with("team:"))
					away_equipment(ship, it->first.substr(5)).get_mut<CombatMechanism>().get<TeamState>().condition =
					    "lost";
				it = postings.erase(it);
			}
			else
				++it;
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		auto &m = away_operations.missions()[i];
		const auto &d = away_operations.definitions()[i];
		auto ship = away_ship(d);
		const auto transport = away_transport_entity(ship, m.transport);
		const bool lost_transport =
		    m.aboard_craft && transport &&
		    (combat_dead(entities.at(transport)) || entities.at(transport).get<CraftState>().phase == CraftPhase::Lost);
		AwayLossFacts loss_facts;
		loss_facts.origin_assigned = static_cast<bool>(ship);
		loss_facts.lost_transport = lost_transport;
		loss_facts.transport_present = static_cast<bool>(transport);
		// Capture only facts reachable under the original short-circuit guards.
		// Retired targets may be absent on already-lost or returning missions.
		if (ship && m.phase != AwayPhase::Lost && !(m.phase == AwayPhase::Inbound && m.aboard_craft && !lost_transport))
		{
			loss_facts.target_destroyed = truly_destroyed(d.target);
			if ((loss_facts.target_destroyed || lost_transport) &&
			    !((m.deployed() || m.phase == AwayPhase::Captured) && loss_facts.target_destroyed) && !lost_transport &&
			    m.aboard_craft && transport)
				loss_facts.torpedo_launched = torpedo_launched(entities.at(transport));
		}
		const auto decision = away_operations.loss_decision(m, loss_facts);
		if (decision == AwayLossDecision::None)
			continue;
		if (decision == AwayLossDecision::Lost)
		{
			for (const auto &[slot, a] : m.assignments)
			{
				mission_ledger.lost[ship].insert(a);
				if (a.starts_with("team:"))
				{
					auto &team = away_equipment(ship, a.substr(5)).get_mut<CombatMechanism>().get<TeamState>();
					team.condition = "lost";
					team.treatment = 0;
					++team.revision;
				}
			}
			for (const auto &o : m.accompanying)
				mission_ledger.lost[ship].insert("officer:" + o);
			auto &personnel = entities.at(ship).get_mut<PersonnelState>();
			const auto &p = definition(ship).personnel;
			for (std::size_t o = 0; o < p.officers.size(); ++o)
				if (mission_ledger.lost[ship].contains("officer:" + p.officers[o].id))
				{
					personnel.officers[o].present = false;
					personnel.officers[o].duty.clear();
					personnel.officers[o].remaining = 0;
					personnel.officers[o].edge_to = -1;
				}
			++personnel.revision;
			if (transport && transport != ship)
			{
				auto &craft = entities.at(transport).get_mut<CraftState>();
				craft.mission.clear();
				if (torpedo_launched(entities.at(transport)) && craft.phase != CraftPhase::Lost)
				{
					craft.phase = CraftPhase::Consumed;
					craft.order = "idle";
					craft.pending_order.clear();
					craft.pending_target = 0;
					craft.progress = 0;
					set_craft_active(entities.at(transport), false);
				}
			}
			m.care.clear();
			m.assignments.clear();
			m.accompanying.clear();
			return_away_escorts(m);
			m.phase = AwayPhase::Lost;
			m.transfer = 0;
			m.outcome = lost_transport ? "fail:transport_destroyed" : "fail:target_destroyed";
			m.aboard_craft = false;
			++m.revision;
			mission_transition("personnel.lost", ship, d.target, d.id, "target_destroyed");
		}
		else if (decision == AwayLossDecision::Pickup)
		{
			request_craft_pickup(transport);
			continue;
		}
		else if (decision == AwayLossDecision::Reopen)
		{
			m.phase = AwayPhase::Open;
			m.transfer = 0;
			++m.revision;
		}
	}
}

auto Runtime::update_mission_facts() -> void
{
	resolve_lost_parties();
	if (!scenario ||
	    (scenario->runtime().definition().fact_bindings.empty() &&
	     scenario->runtime().definition().event_bindings.empty() && scenario->runtime().definition().reports.empty()))
		return;
	for (const auto &[id, e] : entities)
		if (id < field_identity_base)
		{
			const auto state = lifecycle(id);
			mission_change("entity/" + std::to_string(id), state, state == "active" ? "" : "entity." + state, id, id);
		}
	for (const auto &[id, entity] : entities)
		if (id < field_identity_base)
			for (auto child : entity.get<EquipmentIndex>().values)
				if (child.has<FtlReserve>())
				{
					const auto &system = child.get<Equipment>().id;
					const bool ready = ftl_blocker(id, child).empty();
					mission_change("ftl/" + std::to_string(id) + "/" + system, ready ? "ready" : "blocked",
					               ready ? "ftl.ready" : "", id, id, system);
				}
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
		auto ship = away_ship(d);
		if (!ship)
			continue;
		const auto &m = away_operations.missions()[i];
		Json p = m.phase;
		const auto phase = p.get<std::string>();
		const auto key = "away/" + std::to_string(ship) + "/" + d.id;
		mission_change(key + "/phase", phase,
		               phase == "available"   ? "away.available"
		               : phase == "deployed"  ? "away.deployed"
		               : phase == "recovered" ? "away.recovered"
		                                      : "",
		               ship, d.target, d.id);
		mission_change(key + "/outcome", m.outcome,
		               m.outcome.starts_with("complete:") ? "away.completed"
		               : m.outcome.starts_with("fail:")   ? "away.failed"
		                                                  : "",
		               ship, d.target, d.id, m.outcome);
	}
	std::map<ScenarioId, ScenarioFacts> snapshots;
	for (const auto &[id, instance] : scenario->runtime().instances())
		snapshots.emplace(id, mission_snapshot(id));
	scenario->evaluate_conditions(ecs.get<Clock>().tick, snapshots);
	for (const auto &[id, o] : scenario->runtime().objectives())
	{
		const auto state = objective_status_name(o.status);
		mission_change("objective/" + std::to_string(id), state, "objective." + std::string(state),
		               mission_subject(o.instance, o.actor), mission_subject(o.instance, o.actor), o.definition);
	}
	scenario_publications();
}

auto Runtime::authoritative_report(EntityId ship, Json legacy) const -> Json
{
	if (!scenario)
		return legacy;
	const auto &r = scenario->runtime();
	for (const auto &spec : r.definition().reports)
	{
		ScenarioId instance_id = 0;
		for (const auto &[id, instance] : r.instances())
			if (instance.name == spec.instance)
				instance_id = id;
		if (!instance_id || mission_subject(instance_id, spec.ship) != ship)
			continue;
		const auto facts = mission_snapshot(instance_id);
		MissionReportDetails details;
		if (const auto *structure = entities.at(ship).try_get<CombatStructure>())
			for (std::size_t i = 0; i < structure->compartments.size(); ++i)
				details.ship_condition.push_back({{"name", structure->definition.compartments[i].id},
				                                  {"hp", structure->compartments[i].hp},
				                                  {"maximum", structure->definition.compartments[i].maximum}});
		const auto &pd = definition(ship).personnel;
		const auto &ps = entities.at(ship).get<PersonnelState>();
		for (std::size_t i = 0; i < pd.officers.size(); ++i)
			details.crew.push_back({{"name", pd.officers[i].name}, {"condition", ps.officers[i].condition}});
		for (const auto &entry : mission_ledger.transitions)
			if (entry.source == ship && entry.kind == "mission.casualty")
				details.asset_labels["officer:" + entry.operation] =
				    away_asset_label(ship, "officer:" + entry.operation);
		for (const auto &[asset, disposition] : personnel_dispositions(ship))
		{
			Json row{{"name", away_asset_label(ship, asset)}, {"status", disposition}};
			if (personnel_postings.contains(ship) && personnel_postings.at(ship).contains(asset))
			{
				const auto target = personnel_postings.at(ship).at(asset).target;
				const auto participant = "ship:" + std::to_string(target);
				row["location"] = conversations.participants.contains(participant)
				                      ? conversations.participants.at(participant).name
				                      : entities.at(target).get<Definition>().name;
			}
			details.personnel.push_back(std::move(row));
		}
		return build_mission_report(r, spec, instance_id, facts, mission_ledger, ship, details, std::move(legacy));
	}
	return legacy;
}

auto Runtime::finalise_destroyed_missions() -> void
{
	for (const auto &[slot, ship] : slot_entities)
		if (truly_destroyed(ship) && !entities.at(ship).has<MissionDeparture>())
		{
			Json report{{"reason", "destroyed"},
			            {"tick", ecs.get<Clock>().tick},
			            {"objectives", Json::array()},
			            {"left_behind", ftl_left_behind(ship)}};
			entities.at(ship).set<MissionDeparture>({ftl_projection::report(authoritative_report(ship, report))});
			retire_ship_processes(ship);
			retire_process_dependants();
		}
}

auto Runtime::validate_mission_ledger() const -> void
{
	const auto &m = mission_ledger;
	std::vector<EntityId> identities;
	identities.reserve(entities.size());
	for (const auto &[id, entity] : entities)
		identities.push_back(id);
	const bool valid_instance = !m.instance || (scenario && scenario->runtime().instances().contains(m.instance) &&
	                                            m.instance == conversation_instance);
	validate_mission_history(m, ecs.get<Clock>().tick, identities, valid_instance);
	for (const auto &[ship, assets] : m.lost)
	{
		if (!entities.contains(ship) || assets.size() > 256)
			throw std::runtime_error("Invalid lost personnel");
		for (const auto &a : assets)
		{
			bool valid = false;
			if (a.starts_with("team:"))
			{
				auto team = away_equipment(ship, a.substr(5));
				valid = team.is_valid() && team.has<CombatMechanism>() &&
				        std::holds_alternative<TeamState>(team.get<CombatMechanism>().state);
			}
			else if (a.starts_with("officer:"))
				for (const auto &o : definition(ship).personnel.officers)
					valid |= a == "officer:" + o.id;
			if (!valid)
				throw std::runtime_error("Invalid lost asset");
		}
	}
}
} // namespace phoenix::world_detail
