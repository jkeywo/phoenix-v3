module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::damage_rule_context(const PayloadHitContext &hit, std::int64_t presented, EntityId attacker,
                                  const Projectile *payload, const NativeRuleContext *origin) const -> NativeRuleContext
{
	NativeRuleContext context = origin ? *origin : NativeRuleContext{};
	context.hit = hit;
	context.subject = rule_hit_actor(hit);
	context.target = context.subject;
	context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	context.presented = context.modified = presented;
	context.negated = context.absorbed = context.residual = context.inflicted = context.strength = 0;
	if (payload)
	{
		context.source = {payload->source, payload->weapon};
		context.instigator = context.source;
		context.captured = payload->rule_captures;
	}
	else if (!origin)
		context.source = context.instigator = RuleActor{attacker};
	return context;
}

auto Runtime::freeze_damage_rules(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &event,
                                  const ShieldContribution *shield, const std::vector<NativeRuleSource> *sources,
                                  const NativeRuleContext *shield_origin) const -> std::vector<DamageRuleFrame>
{
	std::vector<DamageRuleFrame> frames;
	visit_damage_rule_bindings(
	    ship, trigger, event, shield,
	    [&](const auto &source, const auto &rule, const auto &context)
	    {
		    auto evaluation = evaluate_rule(rule, freeze_rule_context(rule, context));
		    if (evaluation.status == RuleEvaluationStatus::Ready)
			    frames.push_back({source, context, std::move(evaluation)});
	    },
	    sources, shield_origin);
	return frames;
}

auto Runtime::damage_rule_factor(const std::vector<DamageRuleFrame> &frames) -> std::int64_t
{
	std::int64_t factor = 1000;
	for (const auto &frame : frames)
		if (!frame.disabled)
			factor = factor * frame.evaluation.quote.damage_permille / 1000;
	return factor;
}

auto Runtime::damage_rule_invocation(const DamageRuleFrame &frame) -> EffectGroupInvocation
{
	EffectGroupInvocation invocation;
	invocation.execution = frame.evaluation.quote.effects;
	invocation.protected_ship = frame.context.subject.entity;
	invocation.attacker = frame.context.instigator.entity;
	invocation.source = frame.source;
	invocation.original_equipment = frame.context.source.equipment;
	invocation.hit_context = frame.context.hit;
	invocation.rule_context = frame.context;
	return invocation;
}

auto Runtime::apply_pre_damage_frames(std::vector<DamageRuleFrame> &frames, NativeRuleContext &context) -> std::int64_t
{
	for (;;)
	{
		std::vector<EffectGroupInvocation> invocations;
		std::vector<std::size_t> indices;
		for (std::size_t i = 0; i < frames.size(); ++i)
			if (!frames[i].disabled)
			{
				indices.push_back(i);
				invocations.push_back(damage_rule_invocation(frames[i]));
			}
		if (invocations.empty())
			break;
		auto quote = prepare_effect_groups(invocations);
		quote.defer_damage = true;
		if (!quote.failure.empty())
		{
			if (quote.failed_invocation >= indices.size())
			{
				for (auto &frame : frames)
					frame.disabled = true;
				break;
			}
			frames[indices[quote.failed_invocation]].disabled = true;
			continue;
		}
		const auto failure = commit_effect_group(quote);
		if (!failure.empty())
		{
			for (auto &frame : frames)
				frame.disabled = true;
		}
		break;
	}
	context.modified = context.presented * damage_rule_factor(frames) / 1000;
	context.negated = context.presented - context.modified;
	return context.modified;
}

auto Runtime::apply_pre_damage_rules(flecs::entity ship, RuleTrigger trigger, NativeRuleContext &context,
                                     const std::vector<NativeRuleSource> *sources) -> std::int64_t
{
	auto frames = freeze_damage_rules(ship, trigger, context, nullptr, sources);
	return apply_pre_damage_frames(frames, context);
}

auto Runtime::emit_damage_rules(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &context,
                                const ShieldContribution *shield, const std::vector<NativeRuleSource> *sources,
                                const NativeRuleContext *shield_origin) -> void
{
	visit_damage_rule_bindings(
	    ship, trigger, context, shield,
	    [&](const auto &source, const auto &rule, const auto &bound)
	    {
		    // Queue pressure records a bounded failed receipt; committed native
		    // damage is never rolled back or interrupted by a post notification.
		    (void)enqueue_rule(rule.id, source, bound, RuleEventPhase::Shield,
		                       static_cast<std::int64_t>(ecs.get<Clock>().tick) + 1);
	    },
	    sources, shield_origin);
}
} // namespace phoenix::world_detail
