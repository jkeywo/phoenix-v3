module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.repairs;
export import phoenix.gameplay.combat.state;
export namespace phoenix
{
using RepairFault = std::pair<std::string, std::size_t>;
struct RepairCandidate
{
	std::int64_t score = 0;
	std::optional<RepairFault> fault;
};
struct RepairRoute
{
	std::size_t compartment = 0;
	std::int64_t next = -1, cost = 0;
};
RepairCandidate native_repair_candidate(const CombatStructure &, std::size_t, const std::string &equipment = {},
                                        const std::set<RepairFault> &claimed = {});
bool advance_native_repair_team(CombatStructure &, CombatMechanism &, bool edge_open);
void select_automatic_repair_destination(const CombatStructure &, TeamState &, std::span<const RepairRoute>,
                                         const std::set<RepairFault> &claimed);
RepairCandidate select_native_repair_task(const CombatStructure &, const TeamState &,
                                          const std::set<RepairFault> &claimed);
std::int64_t native_repair_contribution(const TeamDefinition &, std::int64_t effective_work, std::size_t rank);
struct RepairMaterialPlan
{
	std::int64_t work = 0, purchases = 0;
	std::vector<EffectCostDefinition> costs;
};
RepairMaterialPlan plan_native_repair_materials(std::int64_t contribution, std::int64_t credit,
                                                const std::optional<std::vector<EffectCostDefinition>> &unit_costs,
                                                std::span<const std::int64_t> available);
RepairMaterialPlan reprice_native_repair_materials(std::int64_t accepted_work, std::int64_t credit,
                                                   std::span<const EffectCostDefinition> unit_costs);
std::int64_t remaining_native_repair_credit(std::int64_t credit, std::int64_t purchases, std::int64_t work);
} // namespace phoenix
