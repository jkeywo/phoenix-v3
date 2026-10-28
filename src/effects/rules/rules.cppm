module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.rules;
export import phoenix.content.effects.types;

export namespace phoenix
{
bool evaluate_rule_expression(const RuleExpression &, const RuleContext &, state_tree::Value &);
std::optional<EffectGroupDefinition> materialize_rule(const RuleDefinition &, const RuleContext &);
RuleEvaluation evaluate_rule(const RuleDefinition &, const RuleContext &);
} // namespace phoenix
