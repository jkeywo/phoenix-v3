module;
#include "native_headers.hpp"

module phoenix.simulation;
import :effect_applications;

namespace phoenix::world_detail
{
auto Runtime::prepare_effect_application(flecs::entity ship, const EffectApplication &application,
                                         ShieldComponent *shields, PropertyEffects *properties, std::int64_t tick)
    -> void
{
	EffectApplicationTarget target;
	target.available = !combat_dead(ship);
	target.properties = ship.has<PropertyEffectOwner>();
	target.shields = ship.has<ShieldComponent>();
	target.modifier_compatible = [&](const std::string &equipment, const ModifierEffectDefinition &definition)
	{
		return modifier_target_compatible(ship, equipment, definition);
	};
	phoenix::prepare_effect_application(application, shields, properties, tick,
	                                    {shield_templates, modifier_templates, *property_catalogue}, target);
}

auto Runtime::apply_scenario_effect(const ScenarioEffectRequest &request) -> void
{
	const auto target = entities.find(request.target);
	if (target == entities.end())
		throw std::runtime_error("Effect target unavailable");
	auto ship = target->second;
	std::optional<ShieldComponent> shields;
	std::optional<PropertyEffects> properties;
	const auto kind =
	    request.kind == ScenarioEffectKind::Shield ? EffectApplicationKind::Shield : EffectApplicationKind::Modifier;
	if (kind == EffectApplicationKind::Shield)
	{
		if (!ship.has<ShieldComponent>())
			throw std::runtime_error("Incompatible shield target");
		shields = ship.get<ShieldComponent>();
	}
	else
	{
		if (!ship.has<PropertyEffectOwner>())
			throw std::runtime_error("Incompatible modifier target");
		properties = read_property_effects(ship);
	}
	prepare_effect_application(ship,
	                           {kind, request.operation, scenario_effect_source(request.token.instance, request.grant),
	                            request.equipment, request.definition},
	                           shields ? &*shields : nullptr, properties ? &*properties : nullptr,
	                           ecs.get<Clock>().tick);
	if (shields)
		ship.set<ShieldComponent>(std::move(*shields));
	if (properties)
		commit_property_effects(ship, *properties);
}
} // namespace phoenix::world_detail
