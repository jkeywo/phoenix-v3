module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.bindings;

namespace phoenix
{
std::vector<RuleFactBinding> payload_capture_layout(const std::vector<std::string> &payloads,
                                                    const PayloadCaptureDefinitions &definitions)
{
	std::vector<RuleFactBinding> result;
	std::set<std::pair<std::string, bool>> seen_rules;
	std::set<std::string> seen_effects;
	const auto add = [&](const RuleFactBinding &value)
	{
		if (std::find(result.begin(), result.end(), value) != result.end())
			return;
		if (result.size() >= rule_expression_node_limit)
			throw std::runtime_error("Projectile rule capture layout exceeds supported facts");
		result.push_back(value);
	};
	const auto visit = [&](auto &&self, const std::string &id, bool owner) -> void
	{
		if (!seen_rules.emplace(id, owner).second)
			return;
		const auto &rule = definitions.rules.at(id);
		for (const auto &field : rule.facts)
			if (field.role == RuleRole::Source || field.role == RuleRole::Instigator ||
			    (owner && field.role == RuleRole::Owner))
				add(field);
		for (const auto &op : rule.effects.operations)
			if (op.kind == EffectOperationKind::Apply && seen_effects.insert(op.effect).second)
			{
				if (const auto native = definitions.native_rules.find(op.effect);
				    native != definitions.native_rules.end())
					for (const auto &binding : native->second)
						self(self, binding.rule, false);
				if (const auto active = definitions.periodic.find(op.effect); active != definitions.periodic.end())
					for (const auto &binding : active->second.rules)
						self(self, binding.rule, false);
			}
	};
	for (const auto &payload : payloads)
		for (const auto &binding : definitions.payloads.at(payload).rules)
			visit(visit, binding.rule, true);
	return result;
}

void validate_rule_captures(const std::vector<RuleCapturedFact> &captures, std::span<const RuleFactBinding> layout)
{
	if (captures.size() != layout.size())
		throw std::runtime_error("Invalid projectile captured rule facts");
	for (std::size_t i = 0; i < captures.size(); ++i)
	{
		const auto &capture = captures[i];
		if (capture.fact != layout[i] || capture.value.number < -9007199254740991LL ||
		    capture.value.number > 9007199254740991LL || capture.value.text.size() > 4096 ||
		    (capture.value.type != state_tree::Type::Missing && capture.value.type != capture.fact.type) ||
		    (capture.value.type == state_tree::Type::Boolean && capture.value.number != 0 &&
		     capture.value.number != 1) ||
		    (capture.value.type != state_tree::Type::Text && !capture.value.text.empty()))
			throw std::runtime_error("Invalid captured rule value/type");
	}
}
} // namespace phoenix
