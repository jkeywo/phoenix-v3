// Generic world-fact adapter. No scenario/content identities belong here.
MissionLedger mission_ledger;
std::vector<std::tuple<ConsoleAddress, OperatingCommand, bool>> departure_requests;
bool truly_destroyed(EntityId id) const
{
	const auto *s = entities.at(id).try_get<Destructible>();
	return s && s->destroyed_tick >= 0;
}
EntityId mission_subject(ScenarioId instance_id, const ScenarioActor &a) const
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
std::string lifecycle(EntityId id) const
{
	return truly_destroyed(id) ? "destroyed" : departed(id) ? "departed" : "active";
}
static const char *objective_status_name(ObjectiveStatus s)
{
	static constexpr const char *names[]{"pending", "active", "completed", "failed", "cancelled"};
	return names[static_cast<unsigned>(s)];
}
std::map<std::string, std::string> personnel_dispositions(EntityId ship) const
{
	std::map<std::string, std::string> dispositions;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_ship(away_definitions[i]) == ship)
		{
			const auto &m = away_missions[i];
			for (const auto &asset : m.participants)
				dispositions.try_emplace(asset, "recovered");
			if (m.absent())
			{
				for (const auto &[slot, asset] : m.assignments)
					dispositions[asset] = "deployed";
				for (const auto &officer : m.accompanying)
					dispositions["officer:" + officer] = "deployed";
			}
		}
	// Loss is permanent and takes precedence over history or a current manifest.
	if (auto lost = mission_ledger.lost.find(ship); lost != mission_ledger.lost.end())
		for (const auto &asset : lost->second)
			dispositions[asset] = "lost";
	const auto &pd = definition(ship).personnel;
	const auto &ps = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < pd.officers.size(); ++i)
		if (ps.officers[i].condition == "dead" && dispositions.contains("officer:" + pd.officers[i].id))
			dispositions["officer:" + pd.officers[i].id] = "dead";
	return dispositions;
}
state_tree::Value mission_fact(ScenarioId instance_id, const ScenarioFactBinding &b) const
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
		return Value::string(b.field == "definition" ? entities.at(id).get<Definition>().name : lifecycle(id));
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
		for (std::size_t i = 0; i < away_missions.size(); ++i)
			if (away_ship(away_definitions[i]) == id && away_definitions[i].id == b.operation)
			{
				const auto &m = away_missions[i];
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
ScenarioFacts mission_snapshot(ScenarioId instance_id) const
{
	ScenarioFacts facts;
	for (const auto &[key, b] : scenario->runtime().definition().fact_bindings)
		facts.emplace(key, mission_fact(instance_id, b));
	for (const auto &[name, o] : scenario->runtime().definition().objectives)
		facts.emplace("actor." + name, state_tree::Value::integer(mission_subject(instance_id, o.actor)));
	return facts;
}
void mission_transition(std::string kind, EntityId source, EntityId subject, std::string operation = {},
                        std::string detail = {})
{
	if (!scenario)
		return;
	scenario->synchronize_time(ecs.get<Clock>().tick);
	auto &r = scenario->runtime();
	if (mission_ledger.transitions.size() >= r.definition().maximum_events)
		throw std::runtime_error("Mission transition journal exhausted");
	if (!mission_ledger.instance)
		mission_ledger.instance = conversation_instance;
	MissionTransition t{mission_ledger.next++, ecs.get<Clock>().tick, source,           subject,
	                    std::move(kind),       std::move(operation),  std::move(detail)};
	mission_ledger.transitions.push_back(t);
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
void mission_change(const std::string &key, const std::string &value, const std::string &kind, EntityId source,
                    EntityId subject, const std::string &operation = {}, const std::string &detail = {})
{
	auto &old = mission_ledger.states[key];
	if (old == value)
		return;
	old = value;
	if (!kind.empty())
		mission_transition(kind, source, subject, operation, detail);
}
void resolve_lost_parties()
{
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		const auto &d = away_definitions[i];
		auto ship = away_ship(d);
		const auto transport = away_transport_entity(ship, m.transport);
		const bool lost_transport =
		    m.aboard_craft && transport &&
		    (combat_dead(entities.at(transport)) || entities.at(transport).get<CraftState>().phase == CraftPhase::Lost);
		if (!ship || m.phase == AwayPhase::Lost ||
		    (m.phase == AwayPhase::Inbound && m.aboard_craft && !lost_transport) ||
		    (!truly_destroyed(d.target) && !lost_transport))
			continue;
		if ((m.deployed() && truly_destroyed(d.target)) || lost_transport ||
		    (m.aboard_craft && transport && torpedo_launched(entities.at(transport)) && truly_destroyed(d.target)))
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
			m.phase = AwayPhase::Lost;
			m.transfer = 0;
			m.outcome = lost_transport ? "fail:transport_destroyed" : "fail:target_destroyed";
			m.aboard_craft = false;
			++m.revision;
			mission_transition("personnel.lost", ship, d.target, d.id, "target_destroyed");
		}
		else if (m.phase == AwayPhase::Outbound)
		{
			if (m.aboard_craft && transport)
			{
				request_craft_pickup(transport);
				continue;
			}
			m.phase = AwayPhase::Open;
			m.transfer = 0;
			++m.revision;
		}
	}
}
void update_mission_facts()
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
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &d = away_definitions[i];
		auto ship = away_ship(d);
		if (!ship)
			continue;
		const auto &m = away_missions[i];
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
Json authoritative_report(EntityId ship, Json legacy) const
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
		legacy["version"] = 2;
		legacy["title"] = spec.title_key;
		legacy["result"] = spec.fallback;
		legacy["result_title"] = spec.fallback_title_key;
		legacy["explanation"] = spec.fallback_explanation_key;
		legacy["facts"] = Json::array();
		legacy["personnel"] = Json::array();
		legacy["objectives"] = Json::array();
		legacy["assessments"] = Json::array();
		legacy["log"] = Json::array();
		legacy["crew"] = Json::array();
		legacy["ship_condition"] = Json::array();
		if (const auto *structure = entities.at(ship).try_get<CombatStructure>())
			for (std::size_t i = 0; i < structure->compartments.size(); ++i)
				legacy["ship_condition"].push_back({{"name", structure->definition.compartments[i].id},
				                                    {"hp", structure->compartments[i].hp},
				                                    {"maximum", structure->definition.compartments[i].maximum}});
		const auto &pd = definition(ship).personnel;
		const auto &ps = entities.at(ship).get<PersonnelState>();
		for (std::size_t i = 0; i < pd.officers.size(); ++i)
			legacy["crew"].push_back({{"name", pd.officers[i].name}, {"condition", ps.officers[i].condition}});
		for (const auto &entry : mission_ledger.transitions)
			if (entry.source == ship && entry.kind == "mission.log")
				legacy["log"].push_back({{"tick", entry.tick}, {"text", entry.detail}, {"name", ""}});
			else if (entry.source == ship && entry.kind == "mission.casualty")
				legacy["log"].push_back(
				    {{"tick", entry.tick},
				     {"text", entry.detail == "dead" ? "Duty officer killed" : "Duty officer injured"},
				     {"name", away_asset_label(ship, "officer:" + entry.operation)}});
		for (const auto &[event, text] : spec.log_events)
			for (const auto &entry : r.events(event))
				legacy["log"].push_back({{"tick", entry.tick}, {"text", text}, {"name", ""}});
		std::stable_sort(legacy["log"].begin(), legacy["log"].end(),
		                 [](const auto &a, const auto &b)
		                 {
			                 return a.at("tick") < b.at("tick");
		                 });
		for (const auto &a : spec.assessments)
		{
			Json result{{"title", a.title_key},
			            {"result", a.fallback},
			            {"result_title", a.fallback_title_key},
			            {"explanation", a.fallback_explanation_key}};
			for (const auto &rule : a.rules)
				if (scenario_condition(rule.when, facts).value_or(false))
				{
					result["result"] = rule.result;
					result["result_title"] = rule.title_key;
					result["explanation"] = rule.explanation_key;
					break;
				}
			legacy["assessments"].push_back(std::move(result));
		}
		for (const auto &rule : spec.rules)
			if (scenario_condition(rule.when, facts).value_or(false))
			{
				legacy["result"] = rule.result;
				legacy["result_title"] = rule.title_key;
				legacy["explanation"] = rule.explanation_key;
				break;
			}
		for (const auto &name : spec.objectives)
		{
			const auto &d = r.definition().objectives.at(name);
			const ScenarioObjective *latest = nullptr;
			for (const auto &[id, o] : r.objectives())
				if (o.instance == instance_id && o.definition == name && (!latest || o.id > latest->id))
					latest = &o;
			legacy["objectives"].push_back({{"id", name},
			                                {"title", d.title_key},
			                                {"status", latest ? objective_status_name(latest->status) : "pending"},
			                                {"optional", d.optional}});
		}
		for (const auto &[name, label] : spec.facts)
		{
			Json value = nullptr;
			const auto &v = facts.at(name);
			if (v.type == state_tree::Type::Text)
				value = v.text;
			else if (v.type == state_tree::Type::Boolean)
				value = v.number != 0;
			else if (v.type == state_tree::Type::Integer)
				value = v.number;
			legacy["facts"].push_back({{"label", label}, {"value", value}});
		}
		for (const auto &[asset, disposition] : personnel_dispositions(ship))
			legacy["personnel"].push_back({{"name", away_asset_label(ship, asset)}, {"status", disposition}});
		return legacy;
	}
	return legacy;
}

void validate_mission_ledger() const
{
	const auto &m = mission_ledger;
	if (!m.next || m.transitions.size() > 16384 || m.states.size() > 65536 || m.lost.size() > entities.size())
		throw std::runtime_error("Invalid mission ledger bounds");
	std::uint64_t last = 0;
	for (const auto &t : m.transitions)
	{
		if (t.id <= last || t.id >= m.next || t.tick > ecs.get<Clock>().tick || t.kind.size() > 128 ||
		    t.operation.size() > 256 || t.detail.size() > 512 || (t.subject && !entities.contains(t.subject)) ||
		    (t.source && !entities.contains(t.source)))
			throw std::runtime_error("Invalid mission transition");
		last = t.id;
	}
	if (m.instance &&
	    (!scenario || !scenario->runtime().instances().contains(m.instance) || m.instance != conversation_instance))
		throw std::runtime_error("Invalid mission event source");
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
