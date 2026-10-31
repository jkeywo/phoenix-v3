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
LocalRepairObservation capture_local_repair_observation(const CombatStructure &structure, std::size_t compartment)
{
	const auto &state = structure.compartments.at(compartment);
	LocalRepairObservation result{compartment, state.hp, structure.definition.compartments.at(compartment).maximum,
	                              state.disabled};
	for (const auto &[system, definitions] : structure.definition.faults)
	{
		if (structure.definition.compartment_ids.at(structure.definition.equipment_compartments.at(system)) !=
		    compartment)
			continue;
		const bool destroyed = equipment_destroyed(&structure, system);
		for (std::size_t index = 0; index < definitions.size(); ++index)
		{
			const auto &definition = definitions[index];
			result.faults.push_back(
			    {{system, index},
			     structure.faults.at(system).at(index).active,
			     definition.destroys,
			     destroyed,
			     definition.effect == "disabled" || definition.effect == "feed" || !definition.effectiveness,
			     definition.severity});
		}
	}
	return result;
}
RepairCandidate local_repair_candidate(const LocalRepairObservation &observed, const std::string &equipment,
                                       const std::set<RepairFault> &claimed)
{
	RepairCandidate best;
	if (equipment.empty() && observed.hp < observed.maximum)
		best.score = observed.disabled ? 10000 : (observed.maximum - observed.hp) * 1000 / observed.maximum;
	for (const auto &fault : observed.faults)
	{
		if (!fault.active || fault.destroys || fault.system_destroyed || claimed.contains(fault.id) ||
		    (!equipment.empty() && equipment != fault.id.first))
			continue;
		const auto score = (fault.disables ? 10000 : 1000) + fault.severity;
		if (score > best.score)
			best = {score, fault.id};
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
RepairCandidate select_local_repair_task(const LocalRepairObservation &observed, const std::string &priority,
                                         bool hull_priority, const std::set<RepairFault> &claimed)
{
	auto candidate = local_repair_candidate(observed, priority, claimed);
	if (!priority.empty() && !candidate.fault)
		candidate = local_repair_candidate(observed, {}, claimed);
	if (hull_priority && observed.hp < observed.maximum)
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
