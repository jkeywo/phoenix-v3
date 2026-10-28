module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.repairs;
import phoenix.gameplay.combat.faults;
namespace phoenix
{
RepairCandidate native_repair_candidate(const CombatStructure &s, std::size_t compartment, const std::string &equipment,
                                        const std::set<RepairFault> &claimed)
{
	const auto &c = s.compartments[compartment];
	const auto &d = s.definition.compartments[compartment];
	RepairCandidate best;
	if (equipment.empty() && c.hp < d.maximum)
		best.score = c.disabled ? 10000 : (d.maximum - c.hp) * 1000 / d.maximum;
	for (const auto &[id, defs] : s.definition.faults)
	{
		if (equipment_destroyed(&s, id) || (!equipment.empty() && equipment != id) ||
		    s.definition.compartment_ids.at(s.definition.equipment_compartments.at(id)) != compartment)
			continue;
		for (std::size_t i = 0; i < defs.size(); ++i)
		{
			const RepairFault key{id, i};
			if (!s.faults.at(id)[i].active || defs[i].destroys || claimed.contains(key))
				continue;
			const auto score =
			    (defs[i].effect == "disabled" || defs[i].effect == "feed" || !defs[i].effectiveness ? 10000 : 1000) +
			    defs[i].severity;
			if (score > best.score)
				best = {score, key};
		}
	}
	return best;
}
bool advance_native_repair_team(CombatStructure &s, CombatMechanism &m, bool edge_open)
{
	auto &r = m.get<TeamState>();
	if (r.travel > 0)
	{
		if (!edge_open)
			return false;
		if (--r.travel > 0)
			return false;
		r.location = r.edge_to;
		r.edge_to = -1;
		r.edge_total = 0;
	}
	if (m.definition.get<TeamDefinition>().capabilities.contains("repair"))
		for (auto &[equipment, states] : s.faults)
			if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) ==
			    static_cast<std::size_t>(r.location))
				for (auto &state : states)
					if (state.active)
						state.diagnosed = true;
	return true;
}
void select_automatic_repair_destination(const CombatStructure &s, TeamState &team, std::span<const RepairRoute> routes,
                                         const std::set<RepairFault> &claimed)
{
	std::tuple<std::int64_t, std::int64_t, std::int64_t> best{0, std::numeric_limits<std::int64_t>::min(), 0};
	team.destination = -1;
	for (const auto &route : routes)
	{
		const auto i = route.compartment;
		if (i != static_cast<std::size_t>(team.location) && route.next < 0)
			continue;
		const auto score = native_repair_candidate(s, i, {}, claimed).score;
		if (!score)
			continue;
		const auto key = std::tuple{score, -route.cost, -static_cast<std::int64_t>(i)};
		if (key > best)
		{
			best = key;
			team.destination = static_cast<std::int64_t>(i);
		}
	}
}
RepairCandidate select_native_repair_task(const CombatStructure &s, const TeamState &team,
                                          const std::set<RepairFault> &claimed)
{
	std::string priority;
	if (team.repair_system >= 0)
	{
		auto at = s.definition.equipment_compartments.begin();
		std::advance(at, team.repair_system);
		priority = at->first;
	}
	const auto index = static_cast<std::size_t>(team.location);
	auto candidate = native_repair_candidate(s, index, priority, claimed);
	if (!priority.empty() && !candidate.fault)
		candidate = native_repair_candidate(s, index, {}, claimed);
	if (team.repair_system == -2 && s.compartments[index].hp < s.definition.compartments[index].maximum)
		candidate = {10000, {}};
	return candidate;
}
std::int64_t native_repair_contribution(const TeamDefinition &team, std::int64_t effective_work, std::size_t rank)
{
	return effective_work / (1000 + team.stack_penalty * static_cast<std::int64_t>(rank));
}
RepairMaterialPlan plan_native_repair_materials(std::int64_t contribution, std::int64_t credit,
                                                const std::optional<std::vector<EffectCostDefinition>> &unit_costs,
                                                std::span<const std::int64_t> available)
{
	if (!unit_costs && credit <= 0)
		return {};
	const auto costs = unit_costs.value_or(std::vector<EffectCostDefinition>{});
	const auto work_unit = resource_tick_hz * 1000;
	auto desired = unit_costs ? contribution : std::min(contribution, credit);
	auto purchases = costs.empty() ? 0 : std::max<std::int64_t>(0, desired - credit + work_unit - 1) / work_unit;
	for (std::size_t i = 0; i < costs.size(); ++i)
		if (costs[i].amount > 0)
			purchases = std::min(purchases, available[i] / costs[i].amount);
	if (!costs.empty())
		desired = std::min(desired, credit + purchases * work_unit);
	RepairMaterialPlan result{desired, purchases, costs};
	for (auto &cost : result.costs)
		cost.amount *= purchases;
	return result;
}
RepairMaterialPlan reprice_native_repair_materials(std::int64_t work, std::int64_t credit,
                                                   std::span<const EffectCostDefinition> unit_costs)
{
	const auto work_unit = resource_tick_hz * 1000;
	const auto purchases =
	    unit_costs.empty() ? 0 : std::max<std::int64_t>(0, work - credit + work_unit - 1) / work_unit;
	RepairMaterialPlan result{work, purchases, {unit_costs.begin(), unit_costs.end()}};
	for (auto &cost : result.costs)
		cost.amount *= purchases;
	return result;
}
std::int64_t remaining_native_repair_credit(std::int64_t credit, std::int64_t purchases, std::int64_t work)
{
	return credit + purchases * resource_tick_hz * 1000 - work;
}
} // namespace phoenix
