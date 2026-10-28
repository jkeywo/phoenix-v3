// Included inside World::Impl. Native fact capture is the only runtime port
// needed by authored rule application binding and identity preparation.
RuleApplications rule_applications() const
{
	return RuleApplications(modifier_templates, *property_catalogue,
	                        [this](const RuleDefinition &rule, const NativeRuleContext &context)
	                        {
		                        return freeze_rule_context(rule, context);
	                        });
}
RuleEvaluation evaluate_source_rule(const RuleDefinition &rule, const NativeRuleContext &context) const
{
	return rule_applications().evaluate(rule, context);
}
std::string rule_operation_equipment(const NativeRuleContext &context, const EffectOperation &operation) const
{
	return rule_applications().equipment(context, operation);
}
bool same_rule_application(const RuleDefinition &rule, const NativeRuleContext &previous,
                           const NativeRuleContext &current) const
{
	return rule_applications().same(rule, previous, current);
}
