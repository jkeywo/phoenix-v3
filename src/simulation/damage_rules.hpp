// Included inside World::Impl. Native damage owns geometry and allocation;
// authored hooks supply one frozen quote per reached native subject.
struct DamageRuleFrame
{
	std::string source;
	NativeRuleContext context;
	RuleEvaluation evaluation;
	bool disabled = false;
};
NativeRuleContext damage_rule_context(const PayloadHitContext &hit, std::int64_t presented, EntityId attacker = 0,
                                      const Projectile *payload = nullptr,
                                      const NativeRuleContext *origin = nullptr) const
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
template <class Callback>
void visit_damage_rule_bindings(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &event,
                                const ShieldContribution *shield, Callback &&callback,
                                const std::vector<NativeRuleSource> *sources = nullptr,
                                const NativeRuleContext *shield_origin = nullptr) const
{
	const auto visit = [&](const NativeRuleSource &source, const std::vector<RuleBinding> &bindings)
	{
		for (const auto &binding : bindings)
		{
			const auto found = rule_templates.find(binding.rule);
			if (found == rule_templates.end() || found->second.trigger != trigger)
				continue;
			auto context = merge_rule_event_context(source, event);
			if (!rule_binding_matches(binding, context))
				continue;
			context = bind_rule_context(binding, std::move(context));
			callback(rule_key(source.key, binding.id, event.subject), found->second, context);
		}
	};
	const auto local_sources = sources ? std::vector<NativeRuleSource>{} : rule_sources(ship);
	for (const auto &source : sources ? *sources : local_sources)
		if (source.eligible && source.bindings)
			visit(source, *source.bindings);
	if (shield)
	{
		NativeRuleSource source;
		source.kind = RuleOwnerKind::Shield;
		source.key = "shield:" + std::to_string(ship.get<StableId>().id) + ":" + std::to_string(shield->effect_id) +
		             ":" + shield->definition.name;
		source.owner = event.subject;
		source.context = shield_origin
		                     ? *shield_origin
		                     : native_rule_origin({ship.get<StableId>().id, shield->equipment}, shield->source,
		                                          NativeEffectKind::Shield, shield->effect_id);
		visit(source, shield->definition.shield.rules);
	}
}
std::vector<DamageRuleFrame> freeze_damage_rules(flecs::entity ship, RuleTrigger trigger,
                                                 const NativeRuleContext &event,
                                                 const ShieldContribution *shield = nullptr,
                                                 const std::vector<NativeRuleSource> *sources = nullptr,
                                                 const NativeRuleContext *shield_origin = nullptr) const
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
static std::int64_t damage_rule_factor(const std::vector<DamageRuleFrame> &frames)
{
	std::int64_t factor = 1000;
	for (const auto &frame : frames)
		if (!frame.disabled)
			factor = factor * frame.evaluation.quote.damage_permille / 1000;
	return factor;
}
static EffectGroupInvocation damage_rule_invocation(const DamageRuleFrame &frame)
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
std::int64_t apply_pre_damage_frames(std::vector<DamageRuleFrame> &frames, NativeRuleContext &context)
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
std::int64_t apply_pre_damage_rules(flecs::entity ship, RuleTrigger trigger, NativeRuleContext &context,
                                    const std::vector<NativeRuleSource> *sources = nullptr)
{
	auto frames = freeze_damage_rules(ship, trigger, context, nullptr, sources);
	return apply_pre_damage_frames(frames, context);
}
void emit_damage_rules(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &context,
                       const ShieldContribution *shield = nullptr,
                       const std::vector<NativeRuleSource> *sources = nullptr,
                       const NativeRuleContext *shield_origin = nullptr)
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
