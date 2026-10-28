// Included inside World::Impl. Each port exposes one native policy; prepared
// copies and opcode dispatch belong to the effects preparation module.
EffectPreparation effect_preparation()
{
	EffectPreparationPorts ports;
	ports.find = [this](EntityId id, bool ordnance)
	{
		if (ordnance)
		{
			PayloadHitContext context{id};
			context.ordnance = true;
			return payload_target(context);
		}
		const auto found = entities.find(id);
		return found == entities.end() ? flecs::entity{} : found->second;
	};
	ports.terminal = [this](flecs::entity body)
	{
		return combat_dead(body);
	};
	ports.hit_available = [this](const PayloadHitContext &hit)
	{
		return payload_context_available(hit);
	};
	ports.definitions = [this](EntityId id)
	{
		return native_owner_definitions(id);
	};
	ports.compartment = [this](EntityId id, const std::string &name) -> std::optional<std::size_t>
	{
		const auto &combat = definition(id).combat;
		if (!combat || !combat->compartment_ids.contains(name))
			return {};
		return combat->compartment_ids.at(name);
	};
	ports.remove_owner =
	    [this](PreparedEffectGroup &quote, const EffectGroupInvocation &frame, const EffectOperation &op)
	{
		prepare_owner_effect_removal(quote, frame, op);
	};
	ports.native_outcome =
	    [this](PreparedEffectGroup &quote, const EffectGroupInvocation &frame, const EffectOperation &op)
	{
		prepare_native_outcome(quote, frame, op);
	};
	ports.property_rate_owner_live = [this](const NativeRuleContext &context)
	{
		return native_property_rate_owner_live(context);
	};
	ports.ability = [this](AbilityRuntime &runtime, EntityId id, const EffectOperation &op, const std::string &source)
	{
		prepare_ability_grant(runtime, id, op, source);
	};
	ports.process_binding = [this](const CustomPropertyAddress &address, const ProcessGrantDefinition &grant)
	{
		return validate_process_grant(address, grant);
	};
	ports.active_exists = [this](const std::vector<ActiveRuleEffect> *prepared, const std::string &source, EntityId id)
	{
		return active_rule_effect_exists(prepared, source, id);
	};
	ports.active_snapshot = [this]()
	{
		return active_rule_snapshot();
	};
	ports.modifier_compatible =
	    [this](flecs::entity body, const std::string &equipment, const ModifierEffectDefinition &def)
	{
		return modifier_target_compatible(body, equipment, def);
	};
	return EffectPreparation({shield_templates, modifier_templates, ability_grant_templates, process_grant_templates,
	                          periodic_templates, *process_catalog, *property_catalogue},
	                         std::move(ports), rate);
}
