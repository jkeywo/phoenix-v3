// Included inside namespace phoenix, before native ordnance serializers.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PayloadHitContext, target, kind, compartment, equipment, contribution,
                                   contribution_name, ordnance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeriodicEffectInstance, id, original_source, source, definition, group_source,
                                   original_equipment, context, expiry, next)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PendingPayload, id, original_source, source, payload, binding, original_equipment,
                                   context, due)
