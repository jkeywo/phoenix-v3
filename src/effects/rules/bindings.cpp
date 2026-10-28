module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.bindings;

namespace phoenix
{
RuleApplications::RuleApplications(const std::map<std::string, ModifierEffectDefinition> &modifiers,
                                   const PropertyCatalog &properties, Freeze freeze)
    : modifiers_(modifiers), properties_(properties), freeze_(std::move(freeze))
{
}
NativeRuleContext rule_applied_context(const NativeRuleContext &context)
{
	NativeRuleContext applied;
	applied.owner = context.owner;
	applied.source = context.source;
	applied.instigator = context.instigator;
	applied.subject = context.subject;
	applied.target = context.target;
	applied.hit = context.hit;
	applied.property_role = context.property_role;
	applied.property_filter = context.property_filter;
	applied.region = context.region;
	applied.owner_grant = context.owner_grant;
	applied.owner_effect_kind = context.owner_effect_kind;
	applied.owner_effect_id = context.owner_effect_id;
	applied.output = 0;
	return applied;
}

std::string rule_application_equipment(const NativeRuleContext &context, const std::string &binding)
{
	if (binding.empty() || binding.front() != '$')
		return binding;
	if (binding == "$hit")
		return context.hit.kind == PayloadOutcomeKind::Shield
		           ? "effect:" + std::to_string(context.hit.contribution) + ":" + context.hit.contribution_name
		           : context.hit.equipment;
	const auto actor = rule_actor(context, binding.substr(1));
	return actor.kind == PayloadOutcomeKind::Shield
	           ? "effect:" + std::to_string(actor.contribution) + ":" + actor.contribution_name
	           : actor.equipment;
}

bool RuleApplications::matches(const EffectOperation &operation, const NativeRuleContext &context) const
{
	if (context.property_role.empty() && context.property_filter.empty())
		return true;
	if (operation.kind != EffectOperationKind::Apply || !modifiers_.contains(operation.effect))
		return true;
	const auto role = operation.role == "$role" ? context.property_role : operation.role;
	for (const auto &contribution : modifiers_.at(operation.effect).contributions)
		if ((role.empty() || contribution.role == role) &&
		    (context.property_filter.empty() || contribution.property == context.property_filter))
			return true;
	return false;
}

RuleEvaluation RuleApplications::evaluate(const RuleDefinition &rule, const NativeRuleContext &context) const
{
	if ((!context.property_role.empty() || !context.property_filter.empty()) &&
	    std::none_of(rule.effects.operations.begin(), rule.effects.operations.end(),
	                 [&](const auto &op)
	                 {
		                 return matches(op, context);
	                 }))
		return {RuleEvaluationStatus::Unmatched, {}};
	auto result = evaluate_rule(rule, freeze_(rule, context));
	if (result.status == RuleEvaluationStatus::Ready)
		std::erase_if(result.quote.effects.operations,
		              [&](const auto &op)
		              {
			              return !matches(op, context);
		              });
	return result;
}

std::string RuleApplications::equipment(const NativeRuleContext &context, const EffectOperation &operation) const
{
	if (modifiers_.contains(operation.effect))
	{
		const auto &definition = modifiers_.at(operation.effect);
		const bool custom = std::any_of(definition.contributions.begin(), definition.contributions.end(),
		                                [&](const auto &row)
		                                {
			                                return properties_.contains(row.property);
		                                });
		if (custom)
		{
			auto actor = rule_actor(context, operation.target);
			if (!operation.officer.empty())
			{
				actor.equipment.clear();
				actor.officer = operation.officer;
				actor.compartment = -1;
			}
			if (operation.equipment.empty())
				return custom_modifier_target(actor);
			if (operation.equipment.front() == '$' && operation.equipment != "$hit")
				return custom_modifier_target(rule_actor(context, operation.equipment.substr(1)));
		}
	}
	return rule_application_equipment(context, operation.equipment);
}

bool RuleApplications::same(const RuleDefinition &rule, const NativeRuleContext &previous,
                            const NativeRuleContext &current) const
{
	for (const auto &operation : rule.effects.operations)
		if (operation.kind == EffectOperationKind::Apply)
		{
			auto before = rule_actor(previous, operation.target), after = rule_actor(current, operation.target);
			if (before.kind == PayloadOutcomeKind::Entity)
			{
				before.equipment.clear();
				before.officer.clear();
			}
			if (after.kind == PayloadOutcomeKind::Entity)
			{
				after.equipment.clear();
				after.officer.clear();
			}
			if (before != after || equipment(previous, operation) != equipment(current, operation))
				return false;
			if (modifiers_.contains(operation.effect) &&
			    (previous.property_filter != current.property_filter ||
			     (operation.role == "$role" && previous.property_role != current.property_role)))
				return false;
		}
	return true;
}

NativeRuleContext merge_rule_event_context(const NativeRuleSource &source, const NativeRuleContext &event)
{
	auto context = source.context;
	context.owner = source.owner;
	context.subject = event.subject;
	context.target = event.target;
	context.instigator = event.instigator;
	context.hit = event.hit;
	context.tick = event.tick;
	context.elapsed = event.elapsed;
	context.presented = event.presented;
	context.modified = event.modified;
	context.negated = event.negated;
	context.absorbed = event.absorbed;
	context.residual = event.residual;
	context.strength = event.strength;
	context.inflicted = event.inflicted;
	context.output = event.output;
	std::erase_if(context.captured,
	              [&](const auto &capture)
	              {
		              return capture.fact.role == RuleRole::Instigator ||
		                     (capture.fact.role == RuleRole::Owner && !context.capture_owner);
	              });
	for (const auto &capture : event.captured)
		if (capture.fact.role == RuleRole::Instigator)
			context.captured.push_back(capture);
	return context;
}
} // namespace phoenix
