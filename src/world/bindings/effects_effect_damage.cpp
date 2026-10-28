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
auto Runtime::damage_native_durability(flecs::entity ship, std::int64_t amount, const PayloadHitContext &hit,
                                       const NativeRuleContext *origin, const Projectile *payload) -> std::int64_t
{
	const auto *before = ship.try_get<Destructible>();
	if (!before || before->durability <= 0)
		return 0;
	const auto sources = rule_sources(rule_hit_actor(hit));
	auto context = damage_rule_context(hit, amount, 0, payload, origin);
	const auto actual = std::min(before->durability, std::max<std::int64_t>(0, amount));
	auto &durability = ship.get_mut<Destructible>();
	durability.durability -= actual;
	if (!durability.durability)
	{
		durability.destroyed_tick = ecs.get<Clock>().tick;
		emit_native_destroy_rules(ship, rule_hit_actor(hit), &sources);
	}
	context.inflicted = actual;
	context.residual = std::max<std::int64_t>(0, amount) - actual;
	emit_damage_rules(ship, RuleTrigger::PostEntityDamage, context, nullptr, &sources);
	return actual;
}

auto Runtime::apply_payload_damage(flecs::entity ship, std::int64_t amount, const PayloadHitContext &hit,
                                   const NativeRuleContext *origin, std::optional<std::int64_t> incoming_heading)
    -> void
{
	if (!payload_context_available(hit) || amount <= 0)
		return;
	const auto sources = rule_sources(rule_hit_actor(hit));
	auto context = damage_rule_context(hit, amount, 0, nullptr, origin);
	if (hit.kind == PayloadOutcomeKind::Shield)
	{
		const auto &contributions = ship.get<ShieldComponent>().contributions;
		const auto found = std::find_if(contributions.begin(), contributions.end(),
		                                [&](const auto &entry)
		                                {
			                                return entry.effect_id == hit.contribution &&
			                                       entry.definition.name == hit.contribution_name;
		                                });
		if (found == contributions.end())
			return;
		auto snapshot = *found;
		const auto shield_origin = native_rule_origin({ship.get<StableId>().id, snapshot.equipment}, snapshot.source,
		                                              NativeEffectKind::Shield, snapshot.effect_id);
		context.strength = snapshot.state.strength;
		auto frames =
		    freeze_damage_rules(ship, RuleTrigger::PreShieldDamage, context, &snapshot, &sources, &shield_origin);
		const auto modified = apply_pre_damage_frames(frames, context);
		const auto actual = std::min(snapshot.state.strength, modified);
		snapshot.state.strength -= actual;
		if (auto *live = ship.try_get_mut<ShieldComponent>())
			for (auto &entry : live->contributions)
				if (entry.effect_id == snapshot.effect_id && entry.definition.name == snapshot.definition.name)
					entry.state.strength = snapshot.state.strength;
		context.absorbed = actual;
		context.residual = modified - actual;
		context.strength = snapshot.state.strength;
		emit_damage_rules(ship, RuleTrigger::PostShieldDamage, context, &snapshot, &sources, &shield_origin);
	}
	else if (hit.kind == PayloadOutcomeKind::System)
		damage_faults(ship, static_cast<std::size_t>(hit.compartment), amount, nullptr, hit.equipment, &context,
		              &sources);
	else if (hit.kind == PayloadOutcomeKind::Compartment)
		combat_damage(ship, static_cast<std::size_t>(hit.compartment), amount, nullptr, &context, &sources);
	else if (ship.has<CombatStructure>())
	{
		std::int64_t native_inflicted = 0;
		const auto remaining =
		    absorb_shields(ship, incoming_heading.value_or(ship.get<Pose>().heading), amount, 0, nullptr,
		                   context.instigator.entity, nullptr, &context, &sources, &native_inflicted);
		if (remaining)
			combat_damage(ship, 0, remaining, nullptr, &context, &sources, &native_inflicted);
		context.inflicted = native_inflicted;
		context.residual = std::max<std::int64_t>(0, amount - context.inflicted);
		emit_damage_rules(ship, RuleTrigger::PostEntityDamage, context, nullptr, &sources);
	}
	else
		damage_native_durability(ship, amount, hit, origin);
}
} // namespace phoenix::world_detail
