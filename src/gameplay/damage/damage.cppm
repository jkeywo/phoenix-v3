module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.damage;
export import phoenix.gameplay.combat.state;
export namespace phoenix
{
struct ShieldDamageBudget
{
	std::int64_t bypassed = 0, remaining = 0;
};
struct ShieldLayerAllocation
{
	std::int64_t remaining = 0;
	std::vector<std::int64_t> absorbed;
};
ShieldDamageBudget native_shield_budget(std::int64_t damage, std::int64_t bypass, std::int64_t *carry);
bool native_shield_intercepts(const ShieldContribution &, std::int64_t incoming_heading, std::int64_t ship_heading);
void order_native_shield_layers(std::vector<ShieldContribution> &);
// Factors are frozen by the authored pre-damage rule boundary. Native equal-share
// allocation is recalculated after a failed effect quote without changing reserves.
ShieldLayerAllocation allocate_native_shield_layer(std::span<const ShieldContribution>, std::int64_t incoming,
                                                   std::span<const std::int64_t> factors);
std::int64_t apply_native_shield_share(ShieldContribution &captured, ShieldComponent *live, std::int64_t assigned,
                                       std::int64_t factor, std::int64_t *native_inflicted = nullptr);
bool apply_native_fault_damage(CombatStructure &, const std::string &equipment, std::int64_t damage);
std::int64_t native_compartment_absorption(const CombatStructure &, std::size_t, std::int64_t amount);
bool apply_native_compartment_damage(CombatStructure &, std::size_t, std::int64_t amount);
std::int64_t incoming_damage_absorbed(std::int64_t actual, std::int64_t presented, std::int64_t modified);
struct CollisionDamageImpact
{
	std::size_t compartment = 0;
	std::int64_t damage = 0;
};
std::optional<CollisionDamageImpact> native_collision_impact(const CombatDefinition &, const Pose &victim,
                                                             const CollisionBody &first, const CollisionBody &second,
                                                             bool victim_is_first);
} // namespace phoenix
