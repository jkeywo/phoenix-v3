// Included inside namespace phoenix, before native ordnance serializers.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PayloadHitContext, target, kind, compartment, equipment, contribution,
                                   contribution_name, ordnance)
