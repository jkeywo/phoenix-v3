module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::rule_applications() const -> RuleApplications
{
	return RuleApplications(modifier_templates, *property_catalogue,
	                        [this](const RuleDefinition &rule, const NativeRuleContext &context)
	                        {
		                        return freeze_rule_context(rule, context);
	                        });
}

auto Runtime::evaluate_source_rule(const RuleDefinition &rule, const NativeRuleContext &context) const -> RuleEvaluation
{
	return rule_applications().evaluate(rule, context);
}

auto Runtime::rule_operation_equipment(const NativeRuleContext &context, const EffectOperation &operation) const
    -> std::string
{
	return rule_applications().equipment(context, operation);
}

auto Runtime::same_rule_application(const RuleDefinition &rule, const NativeRuleContext &previous,
                                    const NativeRuleContext &current) const -> bool
{
	return rule_applications().same(rule, previous, current);
}
} // namespace phoenix::world_detail
