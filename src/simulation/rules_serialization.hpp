// Included inside namespace phoenix, after native hit context serializers.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NativeRuleContext, owner, source, instigator, subject, target, hit, property_role,
                                   tick, elapsed, presented, modified, negated, absorbed, residual, strength, inflicted,
                                   output, captured)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleExecutionState, key, definition, binding, source_key, owner_kind, context,
                                   generation, started, next, last, eligible, active, stopped, credits)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleRuntime, next_generation, executions, regions, membership_initialised)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PendingRuleExecution, id, due, phase, rule, source_key, context)
