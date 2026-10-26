// Included inside namespace phoenix, after native hit context serializers.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NativeRuleContext, owner, source, instigator, subject, target, hit, property_role,
                                   property_filter, region, owner_grant, owner_effect_kind, owner_effect_id,
                                   capture_owner, tick, elapsed, presented, modified, negated, absorbed, residual,
                                   strength, inflicted, output, captured)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleExecutionState, key, definition, binding, source_key, owner_kind, context,
                                   applied_context, generation, started, next, last, lease_tick, lease_permille,
                                   stop_tick, eligible, active, stopped, credits, output, blocker)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleRuntime, next_generation, executions, regions, membership_initialised)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ActiveRuleEffect, id, source, definition, group_source, context, origin, expiry,
                                   clocks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PendingRuleExecution, id, due, phase, rule, source_key, damage, context)
