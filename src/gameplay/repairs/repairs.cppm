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
struct LocalRepairFault
{
	RepairFault id;
	bool active = false, destroys = false, system_destroyed = false, disables = false;
	std::int64_t severity = 0;
};
struct LocalRepairObservation
{
	std::size_t compartment = 0;
	std::int64_t hp = 0, maximum = 0;
	bool disabled = false;
	std::vector<LocalRepairFault> faults;
};
// Trusted physical observation producer. The local selector below cannot read
// any other compartment, even though the producer owns physical structure state.
LocalRepairObservation capture_local_repair_observation(const CombatStructure &, std::size_t compartment);
RepairCandidate local_repair_candidate(const LocalRepairObservation &, const std::string &equipment = {},
                                       const std::set<RepairFault> &claimed = {});
bool advance_native_repair_team(CombatStructure &, CombatMechanism &, bool edge_open);
RepairCandidate select_local_repair_task(const LocalRepairObservation &, const std::string &priority,
                                         bool hull_priority, const std::set<RepairFault> &claimed);
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
