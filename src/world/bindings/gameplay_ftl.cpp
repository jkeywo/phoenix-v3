module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::departure_member_authorised(EntityId leader, EntityId member) const -> bool
{
	if (operational_owner(leader) == operational_owner(member))
		return true;
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
		if (interdiction_book.state(i).captured &&
		    interdiction_book.departure_members_allowed(i, leader, member,
		                                                operational_owner(interdiction_book.definition(i).target)))
			return true;
	return false;
}

auto Runtime::discard_obsolete_encounter_departures() -> void
{
	std::vector<EntityId> obsolete_groups;
	for (const auto &[leader, group] : departures.groups())
	{
		bool obsolete = false;
		for (std::size_t i = 0; i < interdiction_book.size(); ++i)
			if (interdiction_book.state(i).captured &&
			    interdiction_book.obsolete_departure(i, leader, group,
			                                         operational_owner(interdiction_book.definition(i).target)))
				obsolete = true;
		if (obsolete)
			obsolete_groups.push_back(leader);
	}
	for (const auto leader : obsolete_groups)
		departures.erase_group(leader, true);
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

auto Runtime::departure_group_blocker(EntityId ship) const -> std::string
{
	const auto leader = departure_leader(ship);
	if (!departures.groups().contains(leader))
		return {};
	for (const auto member : departures.groups().at(leader))
		if (!truly_destroyed(member) && !departed(member))
		{
			if (!departure_member_authorised(leader, member))
				return "Departure group ownership changed";
			if (member != leader && ftl_local_facts(member).second)
				return "Friendly visitors await recovery";
			const auto drive = departure_drive(member);
			if (!drive || !ftl_blocker(member, drive).empty())
				return "Departure group not ready";
		}
	return {};
}

auto Runtime::departed(EntityId id) const -> bool
{
	return entities.contains(id) && entities.at(id).has<MissionDeparture>() &&
	       entities.at(id).get<MissionDeparture>().report.value("reason", "ftl") == "ftl";
}

auto Runtime::ftl_owner(EntityId id, const std::string &system) const -> ConsoleAddress
{
	const auto &d = definition(id);
	if (!d.ftl.contains(system) || !d.layout)
		return {};
	for (const auto &ctrl : d.layout->controllers)
		for (const auto &channel : ctrl.commands)
			if (channel.action == OperatingAction::SystemEnabled)
				for (const auto i : channel.consumers)
					if (d.fitted->values.at(i).id == system)
						return {id, ctrl.console};
	return {};
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
	bool displacement = false;
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
		if ((away_ship(away_operations.definitions()[i]) == id || away_operations.definitions()[i].target == id) &&
		    away_ship(away_operations.definitions()[i]) &&
		    definition(away_ship(away_operations.definitions()[i]))
		        .displacements.contains(away_operations.missions()[i].transport) &&
		    away_operations.missions()[i].transfer > 0 &&
		    (away_operations.missions()[i].phase == AwayPhase::Outbound ||
		     away_operations.missions()[i].phase == AwayPhase::Inbound))
		{
			displacement = true;
			break;
		}
	return ftl_reserve_blocker(
	    child.get<FtlReserve>(), ship.get<ShipResources>(), child.get<ResourceBinding>().slot,
	    {fit.enabled, structurally_disabled(ship, fit.id), combat_dead(ship), fault_effectiveness(ship, fit.id), 0},
	    displacement);
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
				blocker = departure_group_blocker(a.ship);
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

auto Runtime::ftl_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai) -> bool
{
	if (c.action != "ftl.depart" || departed(a.ship) || c.value || !c.channel.empty() || c.request_id ||
	    !c.arguments.empty() || !c.text.empty() || !c.speaker_name.empty() || !c.speaker_avatar.empty())
		return false;
	auto ship = entities.at(a.ship);
	if (ftl_owner(a.ship, c.system) != a)
		return false;
	bool authority = false;
	for (const auto &ctrl : ship.get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller && ctrl.human != ai &&
		    ctrl.generation == c.generation)
			for (const auto &channel : ctrl.definition.commands)
				for (const auto i : channel.consumers)
					authority |= ship.get<EquipmentIndex>().values.at(i).get<Equipment>().id == c.system;
	if (!authority)
		return false;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (child.has<FtlReserve>() && child.get<Equipment>().id == c.system)
		{
			if (!ftl_blocker(a.ship, child).empty() || !departure_group_blocker(a.ship).empty() ||
			    departure_leader(a.ship) != a.ship)
				return false;
			if (!execute)
				return true;
			departures.request(a, c, ai);
			return true;
		}
	return false;
}

auto Runtime::commit_departures() -> void
{
	auto pending = departures.take_requests();
	std::sort(pending.begin(), pending.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::get<0>(a).ship < std::get<0>(b).ship;
	          });
	std::vector<std::pair<ConsoleAddress, OperatingCommand>> accepted;
	std::set<EntityId> seen;
	for (const auto &[a, c, ai] : pending)
		if (!seen.contains(a.ship) && ftl_command(a, c, false, ai))
		{
			accepted.emplace_back(a, c);
			seen.insert(a.ship);
		}
	if (accepted.empty())
		return;
	// Readiness and ownership are rechecked as one batch before any member leaves.
	const auto leaders = accepted;
	for (const auto &[a, command] : leaders)
		if (departures.groups().contains(a.ship))
			for (auto member : departures.groups().at(a.ship))
				if (member != a.ship && !truly_destroyed(member) && !departed(member) && !seen.contains(member))
				{
					const auto drive = departure_drive(member);
					OperatingCommand companion{member, "ftl.depart", 0, drive.get<Equipment>().id};
					accepted.emplace_back(ftl_owner(member, companion.system), companion);
					seen.insert(member);
				}
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
