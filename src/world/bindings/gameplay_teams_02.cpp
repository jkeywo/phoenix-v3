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
auto Runtime::team_map_allowed(const ConsoleAddress &a) const -> bool
{
	for (auto child : entities.at(a.ship).get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team &&
		                                                      enabled_system_owner(a.ship, m->definition.id) == a &&
		                                                      !m->definition.get<TeamDefinition>().orders.empty())
			return true;
	return false;
}

auto Runtime::advance_away_medical(std::size_t index) -> void
{
	auto &mission = away_operations.missions()[index];
	const auto &definition_mission = away_operations.definitions()[index];
	const auto origin = away_ship(definition_mission), target = definition_mission.target;
	const auto &pd = definition(target).personnel;
	if (pd.compartments.empty())
		return;
	std::set<std::pair<EntityId, std::size_t>> claimed;
	for (const auto &[slot, asset] : mission.assignments)
		if (asset.starts_with("team:"))
		{
			const auto team = away_equipment(origin, asset.substr(5));
			const auto &mechanism = team.get<CombatMechanism>();
			const auto &td = mechanism.definition.get<TeamDefinition>();
			if (!team.get<Equipment>().enabled || !team_available(mechanism) || !td.capabilities.contains("medical"))
				continue;
			auto &care = mission.care[mechanism.definition.id];
			if (care.order.empty())
			{
				care.location = mission.entry_compartment;
				const auto first = std::find_if(td.orders.begin(), td.orders.end(),
				                                [](const auto &o)
				                                {
					                                return o.target == "patients";
				                                });
				if (first == td.orders.end())
				{
					mission.care.erase(mechanism.definition.id);
					continue;
				}
				care.order = first->id;
			}
			const auto *order = team_order_definition(mechanism, care.order);
			if (!order)
				continue; // Restore validates saved identities.
			if (care.travel > 0 && !physical_edge_open(target, care.location, care.edge_to))
				continue;
			if (care.travel > 0 && --care.travel == 0)
			{
				care.location = care.edge_to;
				care.edge_to = -1;
			}
			// Away field care supports treatment/stabilisation, not facility escorts.
			auto destination = [&]() -> std::optional<std::size_t>
			{
				const auto activity = care.activity == "stabilise" ? "stabilise" : "treat";
				std::optional<std::size_t> found;
				for (std::size_t c = 0; c < pd.compartments.size(); ++c)
					if (!clinical_work_done(target, c, *order, activity))
						found = c;
				return found;
			};
			auto at = destination();
			if (at && care.done)
			{
				care.done = care.work_done = false;
				care.activity.clear();
				care.continuation = {};
			}
			const bool arrived = !care.travel && (!at || care.location == static_cast<std::int64_t>(*at));
			step_team_order(*order, care.continuation, care.activity, arrived, care.work_done, at.has_value(), origin,
			                mechanism.definition.id, target, care.location);
			if (care.activity == "complete")
			{
				care.done = true;
				continue;
			}
			at = destination();
			if (at && !care.travel && care.location != static_cast<std::int64_t>(*at))
			{
				const auto route = physical_route(target, pd.compartments.size(), pd.links, care.location, *at);
				if (route.first >= 0)
				{
					care.edge_to = route.first;
					care.travel = route.second;
				}
			}
			if (care.travel || (at && care.location != static_cast<std::int64_t>(*at)))
				continue;
			if (entities.at(target).has<CombatStructure>() &&
			    entities.at(target).get<CombatStructure>().compartments.at(care.location).disabled)
				continue;
			if (care.activity == "treat" || care.activity == "stabilise")
			{
				if (step_clinical_work(origin, mechanism, target, care.location, *order, care.activity, {}, claimed))
					if (const auto conversation = away_conversation(definition_mission))
						conversations.report(conversation, "medical");
				care.work_done = clinical_work_done(target, std::nullopt, *order, care.activity);
			}
		}
}

auto Runtime::validate_medical_state() const -> void
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &[id, ship] : entities)
		if (ship.has<PersonnelState>())
		{
			const auto &pd = definition(id).personnel;
			const auto &ps = ship.get<PersonnelState>();
			for (const auto &o : ps.officers)
			{
				if (o.casualty_tick > tick ||
				    (o.condition == "stunned" && (o.stun_until < tick || o.stun_until > tick + 20 * rate)) ||
				    (o.condition == "healthy" && (o.treatment || o.recovery || o.stabilised)) ||
				    (o.facility.empty() && o.recovery))
					throw std::runtime_error("Invalid clinical timing");
				if (!o.escort.empty())
				{
					const auto fit = instance(ship, o.escort);
					if (!fit || fit->kind != OperatingSystem::Team || o.condition != "severe" || !o.stabilised ||
					    o.present || !o.facility.empty())
						throw std::runtime_error("Invalid patient escort");
					const auto &r = fit->entity.get<CombatMechanism>().get<TeamState>();
					if (r.order.empty() || r.activity != "recover" ||
					    r.location != static_cast<std::int64_t>(o.location) || r.travel != o.remaining ||
					    r.edge_to != o.edge_to || away_reserved(id, "team:" + o.escort))
						throw std::runtime_error("Inconsistent patient escort");
				}
			}
			for (auto child : ship.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
				{
					const auto &r = m->get<TeamState>();
					if (r.medical_compartment < -1 ||
					    r.medical_compartment >= static_cast<std::int64_t>(pd.compartments.size()))
						throw std::runtime_error("Invalid assigned medical compartment");
					const auto patients = clinical_patients(id);
					if (!r.patient.empty() && std::none_of(patients.begin(), patients.end(),
					                                       [&](const auto &o)
					                                       {
						                                       return o.id == r.patient;
					                                       }))
						throw std::runtime_error("Unknown priority patient");
					if ((r.condition != "healthy" && r.condition != "injured" && r.condition != "lost") ||
					    r.treatment < 0 || r.treatment > 1728000000LL || (r.condition != "injured" && r.treatment))
						throw std::runtime_error("Invalid team clinical state");
				}
		}
}

auto Runtime::duty_affects_system(EntityId id, const PersonnelDuty &duty, const std::string &system) const -> bool
{
	if (duty.team == system || duty.choir == system || duty.effects.contains(system))
		return true;
	const auto key = std::pair{entities.at(id).get<Definition>().name, duty.id};
	auto found = duty_systems_.find(key);
	if (found == duty_systems_.end())
	{
		std::set<std::string> systems;
		const auto &resolved = definition(id);
		for (const auto &c : resolved.layout->controllers)
			if (c.console == duty.console && c.id == duty.controller)
				for (const auto &command : c.commands)
					for (const auto consumer : command.consumers)
						systems.insert(resolved.fitted->values.at(consumer).id);
		found = duty_systems_.emplace(key, std::move(systems)).first;
	}
	return found->second.contains(system);
}
} // namespace phoenix::world_detail
