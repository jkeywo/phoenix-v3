module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
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
struct PayloadCaptureDefinitions
{
	const std::map<std::string, PayloadDefinition> &payloads;
	const RulesCatalog &rules;
	const std::map<std::string, std::vector<RuleBinding>> &native_rules;
	const std::map<std::string, PeriodicEffectDefinition> &periodic;
};
std::vector<RuleFactBinding> payload_capture_layout(const std::vector<std::string> &,
                                                    const PayloadCaptureDefinitions &);
void validate_rule_captures(const std::vector<RuleCapturedFact> &, std::span<const RuleFactBinding>);

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
