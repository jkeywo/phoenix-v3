module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.damage;
namespace phoenix
{
ShieldDamageBudget native_shield_budget(std::int64_t damage, std::int64_t bypass, std::int64_t *carry)
{
	const auto budget = damage * bypass + (carry ? *carry : 0);
	const auto bypassed = budget / 1000;
	if (carry)
		*carry = budget % 1000;
	return {bypassed, damage - bypassed};
}
bool native_shield_intercepts(const ShieldContribution &c, std::int64_t heading, std::int64_t ship_heading)
{
	return c.state.warmup && c.state.strength &&
	       std::abs(combat_delta(combat_angle(heading + 180000 - ship_heading), c.definition.bearing)) <=
	           c.definition.arc / 2;
}
void order_native_shield_layers(std::vector<ShieldContribution> &layers)
{
	std::sort(layers.begin(), layers.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tuple{a.definition.shield.layer, a.effect_id, a.definition.name} <
		                 std::tuple{b.definition.shield.layer, b.effect_id, b.definition.name};
	          });
}
ShieldLayerAllocation allocate_native_shield_layer(std::span<const ShieldContribution> layers, std::int64_t incoming,
                                                   std::span<const std::int64_t> factors)
{
	ShieldLayerAllocation result{incoming, std::vector<std::int64_t>(layers.size())};
	std::vector<std::int64_t> capacity(layers.size());
	for (std::size_t i = 0; i < layers.size(); ++i)
	{
		const auto &c = layers[i];
		const auto &d = c.definition.shield;
		const auto usable =
		    std::min(c.state.strength, c.state.strength * c.state.warmup / d.warmup_ticks * c.protection / 1000);
		capacity[i] = factors[i] == 0 ? incoming : std::min(incoming, ((usable + 1) * 1000 - 1) / factors[i]);
	}
	for (;;)
	{
		std::int64_t count = 0;
		for (std::size_t i = 0; i < capacity.size(); ++i)
			if (capacity[i] > result.absorbed[i])
				++count;
		if (!count || !result.remaining)
			break;
		const auto share = result.remaining / count, residue = result.remaining % count;
		std::int64_t rank = 0, used = 0;
		for (std::size_t i = 0; i < capacity.size(); ++i)
			if (capacity[i] > result.absorbed[i])
			{
				const auto amount = std::min(capacity[i] - result.absorbed[i], share + (rank++ < residue ? 1 : 0));
				result.absorbed[i] += amount;
				used += amount;
			}
		result.remaining -= used;
		if (!used)
			break;
	}
	return result;
}
std::int64_t apply_native_shield_share(ShieldContribution &captured, ShieldComponent *live, std::int64_t assigned,
                                       std::int64_t factor, std::int64_t *native_inflicted)
{
	const auto stopped = assigned * factor / 1000;
	captured.state.strength -= stopped;
	if (native_inflicted)
		*native_inflicted += stopped;
	if (live)
		for (auto &current : live->contributions)
			if (current.effect_id == captured.effect_id && current.definition.name == captured.definition.name)
				current.state.strength = captured.state.strength;
	return stopped;
}
bool apply_native_fault_damage(CombatStructure &s, const std::string &equipment, std::int64_t damage)
{
	const auto &definitions = s.definition.faults.at(equipment);
	auto &states = s.faults.at(equipment);
	bool struck = false;
	for (std::size_t i = 0; i < definitions.size(); ++i)
	{
		const auto &d = definitions[i];
		if (damage <= 0 || damage < d.threshold)
			continue;
		s.random = s.random * 6364136223846793005ULL + 1;
		if ((s.random >> 16) % 1000 >= static_cast<std::uint64_t>(d.chance))
			continue;
		states[i].active = true;
		states[i].diagnosed = false;
		states[i].work = 0;
		struck = true;
	}
	return struck;
}
std::int64_t native_compartment_absorption(const CombatStructure &s, std::size_t compartment, std::int64_t amount)
{
	return std::min(amount, s.compartments.at(compartment).hp);
}
bool apply_native_compartment_damage(CombatStructure &s, std::size_t compartment, std::int64_t amount)
{
	auto &c = s.compartments.at(compartment);
	c.hp = std::max<std::int64_t>(0, c.hp - amount);
	if (!c.hp)
		c.disabled = true;
	return std::all_of(s.compartments.begin(), s.compartments.end(),
	                   [](const auto &v)
	                   {
		                   return v.hp == 0;
	                   });
}
std::int64_t incoming_damage_absorbed(std::int64_t actual, std::int64_t presented, std::int64_t modified)
{
	return modified > 0 ? std::min(presented, (actual * presented + modified - 1) / modified) : 0;
}
std::optional<CollisionDamageImpact> native_collision_impact(const CombatDefinition &d, const Pose &pose,
                                                             const CollisionBody &a, const CollisionBody &b,
                                                             bool victim_is_first)
{
	const auto direction = fixed::normal(b.position - a.position);
	const auto closing = std::max<std::int64_t>(0, fixed::dot(a.velocity - b.velocity, direction) / fixed::unit);
	if (closing < d.collision_threshold)
		return {};
	const auto mass = a.mass && b.mass ? std::min(a.mass, b.mass) : std::max(a.mass, b.mass);
	const auto metres = std::min<std::int64_t>(closing / 1000, 1000);
	const auto damage = std::min<std::int64_t>(1000000, std::clamp<std::int64_t>(mass / 1000000, 1, 1000000) * metres *
	                                                        metres / 100 * d.collision_scale);
	const auto contact = combat_local(victim_is_first ? b.position : a.position, pose);
	std::size_t nearest = 0;
	std::int64_t best = std::numeric_limits<std::int64_t>::max();
	for (std::size_t i = 0; i < d.compartments.size(); ++i)
	{
		const auto &c = d.compartments[i];
		const auto distance = fixed::length(contact - fixed::Vec{c.x, c.y});
		if (distance < best)
		{
			best = distance;
			nearest = i;
		}
	}
	return CollisionDamageImpact{nearest, damage};
}
} // namespace phoenix
