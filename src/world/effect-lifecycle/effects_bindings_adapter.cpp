module;
#include "native_headers.hpp"

module phoenix.simulation;
import :effect_lifecycle;

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

} // namespace phoenix::world_detail
