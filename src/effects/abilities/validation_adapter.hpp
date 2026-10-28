// Included inside World::Impl. The effects validator owns continuation
// invariants; native identities and accepted Controller intent remain here.
void validate_ability_runtime(EntityId ship, const AbilityRuntime &state, const AbilityRuntime &initial) const
{
	AbilityValidationPorts ports;
	ports.source_exists = [this, ship](const std::string &source, const std::string &owner)
	{
		return ability_source_exists(ship, source, owner);
	};
	ports.target_exists = [this](EntityId id)
	{
		return entities.contains(id);
	};
	ports.credit = [this](const RuleWorkCredit &credit)
	{
		validate_saved_work_credit(Json(credit));
	};
	ports.control = [this, ship](const AbilityGrantState &grant)
	{
		return resolve_ability_control(ship, grant);
	};
	ports.intent = [this, ship](const AbilityControlDefinition &control)
	{
		const auto &controller = entities.at(ship).get<EntityControls>().controllers.at(control.controller);
		const auto &intent = controller.commands.at(control.enabled);
		return AbilityValidationIntent{controller.definition.commands.at(control.enabled).semantics ==
		                                   CommandSemantics::Request,
		                               intent.value, intent.request_id, intent.accepted};
	};
	phoenix::validate_ability_runtime(ship, ecs.get<Clock>().tick, state, initial, definition(ship).abilities,
	                                  ability_grant_templates, ports, fixed::unit);
}
