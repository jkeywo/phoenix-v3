module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::active_rule_rows() const -> const std::vector<flecs::entity> &
{
	return active_rules.rows();
}

auto Runtime::active_rule_snapshot() const -> std::vector<ActiveRuleEffect>
{
	return active_rules.snapshot();
}

auto Runtime::active_rule_entity(std::uint64_t id) const -> flecs::entity
{
	return active_rules.find(id);
}

auto Runtime::active_rule_mutation_observer() -> ActiveRuleMutationObserver
{
	return {[this](const ActiveRuleEffect &retired)
	        {
		        mark_custom_property_rates_dirty();
		        for (const auto &[key, state] : retired.clocks)
			        erase_custom_property_rate_source(key);
	        }};
}

auto Runtime::erase_active_rule_effect(flecs::entity entity) -> void
{
	active_rules.erase(entity, rule_clocks, active_rule_mutation_observer());
}

auto Runtime::commit_active_rule_effects(const std::vector<ActiveRuleEffect> &prepared) -> void
{
	active_rules.commit(
	    ecs, prepared,
	    [this](const PayloadHitContext &context)
	    {
		    return payload_target(context);
	    },
	    rule_clocks, active_rule_mutation_observer());
}

auto Runtime::active_rule_effect_exists(const std::vector<ActiveRuleEffect> *prepared, const std::string &source,
                                        EntityId target) const -> bool
{
	return active_rules.exists(prepared, source, target);
}

auto Runtime::active_rule_target_validity() const -> RuleTargetValidity
{
	return [this](const PayloadHitContext &context, flecs::entity body)
	{
		return payload_context_available(context, body);
	};
}

auto Runtime::active_rule_target_available(const ActiveRuleEffect &effect) const -> bool
{
	return active_rules.target_available(effect, active_rule_target_validity());
}

auto Runtime::payload_capture_layout(const std::vector<std::string> &payloads) const -> std::vector<RuleFactBinding>
{
	return phoenix::payload_capture_layout(
	    payloads, {payload_templates, rule_templates, native_effect_rules, periodic_templates});
}

auto Runtime::capture_payload_rule_facts(EntityId source, const std::string &equipment,
                                         const std::vector<std::string> &payloads) const
    -> std::vector<RuleCapturedFact>
{
	NativeRuleContext context;
	context.owner = context.source = context.instigator = {source, equipment};
	context.tick = ecs.get<Clock>().tick;
	context.capture_owner = true;
	std::vector<RuleCapturedFact> result;
	for (const auto &field : payload_capture_layout(payloads))
		result.push_back({field, read_rule_fact(field, context)});
	return result;
}

auto Runtime::validate_rule_captures(const std::vector<RuleCapturedFact> &captures,
                                     const std::vector<std::string> &payloads) const -> void
{
	const auto layout = payload_capture_layout(payloads);
	phoenix::validate_rule_captures(captures, layout);
}

auto Runtime::payload_trigger(PayloadOutcomeKind kind) -> RuleTrigger
{
	switch (kind)
	{
	case PayloadOutcomeKind::Shield:
		return RuleTrigger::PostShieldDamage;
	case PayloadOutcomeKind::Compartment:
		return RuleTrigger::PostCompartmentDamage;
	case PayloadOutcomeKind::System:
		return RuleTrigger::PostSystemDamage;
	case PayloadOutcomeKind::Entity:
		return RuleTrigger::PostEntityDamage;
	}
	throw std::runtime_error("Unknown payload outcome");
}

auto Runtime::queue_payload_outcome(const Projectile &projectile, const PayloadHitContext &hit, std::int64_t damage)
    -> void
{
	for (const auto &payload : projectile.payloads)
		for (const auto &binding : payload_templates.at(payload).rules)
			if (rule_templates.at(binding.rule).trigger == payload_trigger(hit.kind))
			{
				NativeRuleContext context;
				context.owner = context.source = context.instigator = {projectile.source, projectile.weapon};
				context.subject = context.target = rule_hit_actor(hit);
				context.hit = hit;
				context.tick = ecs.get<Clock>().tick;
				context.elapsed = projectile.age;
				context.presented = context.modified = context.inflicted = damage;
				context.absorbed = hit.kind == PayloadOutcomeKind::Shield ? damage : 0;
				context.captured = projectile.rule_captures;
				context.capture_owner = true;
				if (!rule_binding_matches(binding, context))
					continue;
				context = bind_rule_context(binding, std::move(context));
				const auto source =
				    "payload." + std::to_string(projectile.source) + "." + projectile.weapon + "." + payload;
				enqueue_rule(binding.rule, rule_key(source, binding.id, context.subject), context,
				             RuleEventPhase::Payload, context.tick + 1);
			}
}

auto Runtime::prepare_active_rule_effect(std::vector<ActiveRuleEffect> &prepared, std::uint64_t &next,
                                         const EffectOperation &operation, const std::string &source,
                                         EntityId original_source, const PayloadHitContext &context,
                                         const std::string &group_source, const std::string &original_equipment,
                                         const NativeRuleContext *origin) -> void
{
	validate_effect_identity(source, {}, false);
	if (operation.kind == EffectOperationKind::Remove)
	{
		prepare_active_rule_grant(prepared, next, operation, source, original_source, context, group_source,
		                          original_equipment, nullptr, 0, origin);
		return;
	}
	if (!payload_context_available(context))
		throw std::runtime_error("Active rule target unavailable");
	prepare_active_rule_grant(prepared, next, operation, source, original_source, context, group_source,
	                          original_equipment, &periodic_templates.at(operation.effect),
	                          static_cast<std::int64_t>(ecs.get<Clock>().tick), origin);
}

auto Runtime::payload_target(const PayloadHitContext &c) const -> flecs::entity
{
	if (c.ordnance)
	{
		for (auto row : projectile_rows())
			if (row.get<Projectile>().id == c.target)
				return row;
		return {};
	}
	const auto found = entities.find(c.target);
	return found == entities.end() ? flecs::entity{} : found->second;
}

auto Runtime::payload_context_available(const PayloadHitContext &c, flecs::entity resolved) const -> bool
{
	const auto ship = resolved ? resolved : payload_target(c);
	if (!ship || combat_dead(ship))
		return false;
	if (c.ordnance)
		return c.kind == PayloadOutcomeKind::Entity && ship.get<Destructible>().durability > 0;
	if (c.kind == PayloadOutcomeKind::Entity)
		return ship.has<Destructible>();
	if (c.kind == PayloadOutcomeKind::Shield)
	{
		const auto *shields = ship.try_get<ShieldComponent>();
		return shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
		                              [&](const auto &s)
		                              {
			                              return s.effect_id == c.contribution &&
			                                     s.definition.name == c.contribution_name;
		                              });
	}
	const auto *structure = ship.try_get<CombatStructure>();
	if (!structure || c.compartment < 0 || c.compartment >= static_cast<std::int64_t>(structure->compartments.size()))
		return false;
	if (c.kind == PayloadOutcomeKind::System)
	{
		const auto at = structure->definition.equipment_compartments.find(c.equipment);
		return at != structure->definition.equipment_compartments.end() &&
		       structure->definition.compartment_ids.at(at->second) == static_cast<std::size_t>(c.compartment);
	}
	return c.kind == PayloadOutcomeKind::Compartment;
}

auto Runtime::capture_directed_payloads(flecs::entity ship, const CombatMechanism &mechanism,
                                        const std::string &resource) const -> std::vector<std::string>
{
	std::vector<std::string> result;
	const auto *mode = directed_mode(ship.get<StableId>().id, mechanism.definition.id);
	if (mode)
		result = mode->payloads;
	const auto append = [&](const std::string &payload)
	{
		if (std::find(result.begin(), result.end(), payload) == result.end())
			result.push_back(payload);
	};
	if (mechanism.definition.kind == OperatingSystem::Weapon)
	{
		const auto &profile = mode && mode->operation == DirectedOperationKind::Launch
		                          ? weapon_payload(ship, mechanism)
		                          : mechanism.definition.ammunition().projectile;
		for (const auto &payload : profile.payloads)
			append(payload);
	}
	if (mode && mode->operation == DirectedOperationKind::Launch && mode->resource_payloads && !resource.empty())
		for (const auto &effect : resource_catalogue->at(resource).effects)
			if (const auto at = payload_templates.find(effect); at != payload_templates.end())
				append(at->first);
	return result;
}
} // namespace phoenix::world_detail
