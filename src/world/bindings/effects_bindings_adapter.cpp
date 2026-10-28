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
