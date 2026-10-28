module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

export module phoenix.effects.bindings;
export import phoenix.effects.contracts;
export import phoenix.effects.rules;

export namespace phoenix
{
NativeRuleContext rule_applied_context(const NativeRuleContext &);
std::string rule_application_equipment(const NativeRuleContext &, const std::string &binding);
NativeRuleContext merge_rule_event_context(const NativeRuleSource &, const NativeRuleContext &event);
class RuleApplications
{
public:
	using Freeze = std::function<RuleContext(const RuleDefinition &, const NativeRuleContext &)>;
	RuleApplications(const std::map<std::string, ModifierEffectDefinition> &, const PropertyCatalog &, Freeze);
	bool matches(const EffectOperation &, const NativeRuleContext &) const;
	RuleEvaluation evaluate(const RuleDefinition &, const NativeRuleContext &) const;
	std::string equipment(const NativeRuleContext &, const EffectOperation &) const;
	bool same(const RuleDefinition &, const NativeRuleContext &previous, const NativeRuleContext &current) const;

private:
	const std::map<std::string, ModifierEffectDefinition> &modifiers_;
	const PropertyCatalog &properties_;
	Freeze freeze_;
};
} // namespace phoenix
