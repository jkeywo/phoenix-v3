module;
#include "native_headers.hpp"

module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::departure_capabilities(EntityId id) const -> DepartureCapabilities
{
	DepartureCapabilities result;
	const auto actor = entities.at(id);
	const auto &fitted = actor.get<EquipmentIndex>().values;
	for (auto child : fitted)
		if (child.has<FtlReserve>())
		{
			const auto &system = child.get<Equipment>().id;
			result.drives.push_back({system, ftl_owner(id, system)});
		}
	if (const auto *controls = actor.try_get<EntityControls>())
		for (const auto &view : controller_physical_grants(controls->controllers))
		{
			DepartureControllerGrant grant{view.console, view.controller, view.human, view.generation, {}};
			for (const auto index : view.physical_consumers)
				grant.physical_consumers.push_back(fitted.at(index).get<Equipment>().id);
			result.controllers.push_back(std::move(grant));
		}
	return result;
}
auto Runtime::departure_admission_facts() const -> DepartureAdmissionFacts
{
	DepartureAdmissionFacts facts;
	facts.departed = [this](EntityId id)
	{
		return departed(id);
	};
	facts.unavailable = [this](EntityId id)
	{
		return truly_destroyed(id) || departed(id);
	};
	facts.capabilities = [this](EntityId id)
	{
		return departure_capabilities(id);
	};
	facts.member_authorised = [this](EntityId leader, EntityId member)
	{
		return departure_member_authorised(leader, member);
	};
	facts.visitors = [this](EntityId id)
	{
		return ftl_local_facts(id).second;
	};
	facts.reserve_blocker = [this](EntityId id, const std::string &system)
	{
		return ftl_blocker(id, entities.at(id).get<EquipmentIndex>().by_id.at(system));
	};
	return facts;
}

auto Runtime::departure_member_authorised(EntityId leader, EntityId member) const -> bool
{
	if (operational_owner(leader) == operational_owner(member))
		return true;
	return interdiction_book.permits_departure_member(leader, member,
	                                                  [this](EntityId id)
	                                                  {
		                                                  return operational_owner(id);
	                                                  });
}

auto Runtime::discard_obsolete_encounter_departures() -> void
{
	departures.discard_obsolete_groups(
	    [this](EntityId leader, const std::set<EntityId> &members)
	    {
		    return interdiction_book.obsolete_departure_group(leader, members,
		                                                      [this](EntityId id)
		                                                      {
			                                                      return operational_owner(id);
		                                                      });
	    });
}

auto Runtime::assign_departure_group(EntityId leader, const std::set<EntityId> &members) -> void
{
	departures.assign(leader, members);
}

auto Runtime::prune_departure_groups() -> void
{
	departures.prune({[this](EntityId id)
	                  {
		                  return truly_destroyed(id) || departed(id);
	                  },
	                  [this](EntityId id, EntityId leader)
	                  {
		                  return entities.at(id).get<FactionMembership>().id ==
		                         entities.at(leader).get<FactionMembership>().id;
	                  }});
}

auto Runtime::departure_leader(EntityId member) const -> EntityId
{
	return departures.leader(member,
	                         [this](EntityId leader)
	                         {
		                         return !truly_destroyed(leader) && !departed(leader);
	                         });
}

auto Runtime::departure_drive(EntityId ship) const -> flecs::entity
{
	for (auto child : entities.at(ship).get<EquipmentIndex>().values)
		if (child.has<FtlReserve>())
			return child;
	return {};
}

auto Runtime::departed(EntityId id) const -> bool
{
	return entities.contains(id) && entities.at(id).has<MissionDeparture>() &&
	       entities.at(id).get<MissionDeparture>().report.value("reason", "ftl") == "ftl";
}

auto Runtime::ftl_owner(EntityId id, const std::string &system) const -> ConsoleAddress
{
	const auto &selected = definition(id);
	if (!selected.ftl.contains(system) || !selected.layout)
		return {};
	std::vector<std::string> fitted_system_ids;
	fitted_system_ids.reserve(selected.fitted->values.size());
	for (const auto &fit : selected.fitted->values)
		fitted_system_ids.push_back(fit.id);
	return departure_console_owner(id, system, *selected.layout, fitted_system_ids);
}

auto Runtime::retire_departed(EntityId id) -> void
{
	retire_ship_processes(id);
	auto ship = entities.at(id);
	ship.disable();
	for (auto child : ship.get<EquipmentIndex>().values)
		child.disable();
	displays.erase(id);
}

auto Runtime::ftl_left_behind(EntityId id) const -> std::vector<std::string>
{
	std::vector<std::string> names;
	for (const auto &[asset, disposition] : personnel_dispositions(id))
		if (disposition == "deployed" || disposition == "freed")
			names.push_back(away_asset_label(id, asset));
	for (const auto &[craft, e] : craft_rows())
		if (e.get<CraftState>().home == id && !craft_dormant(e))
			names.push_back(e.get<Definition>().name + " #" + std::to_string(craft));
	return names;
}

auto Runtime::ftl_blocker(EntityId id, flecs::entity child) const -> std::string
{
	if (departed(id))
		return "Departed via FTL";
	if (dock_busy(id))
		return "Undock before FTL departure";
	const auto ship = entities.at(id);
	const auto &fit = child.get<Equipment>();
	return ftl_reserve_blocker(
	    child.get<FtlReserve>(), ship.get<ShipResources>(), child.get<ResourceBinding>().slot,
	    {fit.enabled, structurally_disabled(ship, fit.id), combat_dead(ship), fault_effectiveness(ship, fit.id), 0},
	    active_displacement_transfer(id));
}

auto Runtime::prepare_ftl(flecs::entity ship) -> void
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *ftl = child.try_get<FtlReserve>())
		{
			const auto entity = ship.get<StableId>().id;
			const auto &equipment = child.get<Equipment>().id;
			prepare_ftl_reserve(*ftl, power, child.get<ResourceBinding>().slot,
			                    {child.get<Equipment>().enabled, structurally_disabled(ship, equipment),
			                     combat_dead(ship), fault_effectiveness(ship, equipment),
			                     native_process_rate(entity, "ftl", equipment)});
		}
}

auto Runtime::actuate_ftl(flecs::entity ship) -> void
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *ftl = child.try_get_mut<FtlReserve>())
			actuate_ftl_reserve(*ftl, power.network, rate);
}

auto Runtime::finalise_ftl_processes() -> void
{
	for (const auto &[id, ship] : entities)
		if (ship.has<ShipResources>())
			for (auto child : ship.get<EquipmentIndex>().values)
				if (const auto *ftl = child.try_get<FtlReserve>())
				{
					const auto &equipment = child.get<Equipment>().id;
					const auto &network = ship.get<ShipResources>().network;
					const auto energy = network.stores().at(ftl->definition.store).energy();
					const auto capacity = network.definition().stores.at(ftl->definition.store).capacity;
					const auto address = custom_property_address(RuleActor{id, equipment});
					auto *runtime = process_runtime(address);
					if (!runtime)
						continue;
					std::vector<std::string> processes;
					for (const auto &[key, state] : runtime->processes)
						if (process_definition(state).native == NativeProcessKind::Ftl && state.native_id == equipment)
							processes.push_back(key);
					for (const auto &key : processes)
					{
						auto *state = process_state(address, key);
						if (!state)
							continue;
						const bool ready = state->requested && state->admitted && child.get<Equipment>().enabled &&
						                   !combat_dead(ship) && !departed(id) && energy == capacity &&
						                   network.operation_ready(ftl->definition.store);
						if (state->completed && energy < capacity && state->requested)
						{
							// A genuinely depleted/unavailable reserve begins another preparation
							// episode; it must earn readiness and its completion again.
							process_request(address, key, true);
							state = process_state(address, key);
						}
						state->target = "ftl:" + equipment;
						state->origin.target = state->origin.subject = RuleActor{id, equipment};
						state->progress = energy;
						state->required_work = capacity;
						state->ready = ready;
						if (ready && !state->completed)
						{
							const auto generation = state->generation, run = state->run;
							commit_finite_process_completion(address, key, state->target);
							// Completion consequences can spend or alter the reserve, or
							// remove this run. Publish its resulting physical facts now;
							// another demand episode is considered on a later tick.
							state = process_state(address, key);
							if (!state || state->generation != generation || state->run != run)
								continue;
							const auto &actual = ship.get<ShipResources>().network;
							state->progress = actual.stores().at(ftl->definition.store).energy();
							state->required_work = actual.definition().stores.at(ftl->definition.store).capacity;
							state->ready = state->requested && state->admitted && child.get<Equipment>().enabled &&
							               !combat_dead(ship) && !departed(id) &&
							               state->progress == state->required_work &&
							               actual.operation_ready(ftl->definition.store);
						}
					}
				}
}

auto Runtime::ftl_local_facts(EntityId id) const -> std::pair<bool, std::int64_t>
{
	bool completed = false;
	std::int64_t visitors = 0;
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
		if (away_operations.definitions()[i].target == id)
		{
			const auto &m = away_operations.missions()[i];
			completed |= m.outcome.starts_with("complete:");
			if (m.deployed() && !m.boarding.prize_crew)
				visitors += m.assignments.size() + m.accompanying.size();
			if (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound)
				++visitors;
		}
	return {completed, visitors};
}

auto Runtime::ftl_board(const ConsoleAddress &a) const -> Json
{
	Json rows = Json::array();
	const auto ship = entities.at(a.ship);
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *ftl = child.try_get<FtlReserve>(); ftl && ftl_owner(a.ship, child.get<Equipment>().id) == a)
		{
			const auto &p = ship.get<ShipResources>();
			const auto &s = p.network.stores().at(ftl->definition.store);
			auto blocker = ftl_blocker(a.ship, child);
			if (blocker.empty())
				blocker = departures.group_blocker(departure_admission_facts(), a.ship);
			rows.push_back({{"system", child.get<Equipment>().id},
			                {"energy", s.energy()},
			                {"capacity", p.definition.network.stores.at(ftl->definition.store).capacity},
			                {"enabled", child.get<Equipment>().enabled},
			                {"ready", blocker.empty()},
			                {"power_draw", p.network.telemetry().at(ftl->definition.store).accepted_incoming * rate},
			                {"heat", s.heat},
			                {"blocker", blocker},
			                {"left_behind", ftl_left_behind(a.ship)},
			                {"visitors", ftl_local_facts(a.ship).second},
			                {"work_completed", ftl_local_facts(a.ship).first}});
		}
	return rows;
}

auto Runtime::commit_departures() -> void
{
	auto accepted = departures.prepare_pending(departure_admission_facts());
	if (accepted.empty())
		return;
	for (const auto &[a, c] : accepted)
	{
		auto ship = entities.at(a.ship);
		for (auto child : ship.get<EquipmentIndex>().values)
			if (child.has<FtlReserve>() && child.get<Equipment>().id == c.system)
			{
				Json report{{"reason", "ftl"},
				            {"tick", ecs.get<Clock>().tick},
				            {"objectives", Json::array()},
				            {"left_behind", ftl_left_behind(a.ship)}};
				std::map<std::uint64_t, Json> objectives;
				for (const auto &r : knowledge.public_board(a.ship).value("shared", Json::array()))
					if (r.value("schema", "") == "phoenix.objective")
						objectives[r.at("fields").at("id").get<std::uint64_t>()] = r.at("fields");
				for (const auto &[id, o] : objectives)
					report["objectives"].push_back(
					    {{"title", o.value("title", "")}, {"status", o.value("status", "active")}});
				auto &p = ship.get_mut<ShipResources>();
				const auto store = child.get<FtlReserve>().definition.store;
				p.network.dissipate(store, p.network.stores().at(store).energy(), false);
				ship.set<MissionDeparture>({ftl_projection::report(report)});
				for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
					if (away_ship(away_operations.definitions()[i]) == a.ship)
					{
						auto &m = away_operations.missions()[i];
						if (!m.transport.starts_with("craft:") &&
						    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound))
						{
							m.phase = m.phase == AwayPhase::Inbound ? AwayPhase::Deployed : AwayPhase::Open;
							m.transfer = 0;
							++m.revision;
						}
					}
				SceneAudio cue{SceneAudioKind::Shot, "ftl:departure", definition(a.ship).audio.ftl_departure};
				cue.retention_ticks = 3 * rate; // Keep the retired source until its departure sound finishes.
				emit_audio(ship, std::move(cue));
				retire_departed(a.ship);
			}
	}
	update_mission_facts();
	for (const auto &[a, c] : accepted)
	{
		auto ship = entities.at(a.ship);
		ship.set<MissionDeparture>(
		    {ftl_projection::report(authoritative_report(a.ship, ship.get<MissionDeparture>().report))});
		if (scenario)
			scenario->runtime().withdraw_actor(a.ship);
	}
}
} // namespace phoenix::world_detail
