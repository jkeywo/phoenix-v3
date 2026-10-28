// Included in World::Impl. Inference is external; these operations are deterministic.
ConsoleControllerRuntime *model_controller(const ControllerAddress &address)
{
	auto entity = entities.find(address.entity);
	if (entity == entities.end() || !entity->second.has<EntityControls>())
		return nullptr;
	for (auto &controller : entity->second.get_mut<EntityControls>().controllers)
		if (controller.definition.console == address.console && controller.definition.id == address.controller)
			return &controller;
	return nullptr;
}
Json model_basis(EntityId id, const ConsoleControllerRuntime &controller)
{
	return phoenix::model_basis(entities.at(id).get<BridgeBlackboard>(), controller);
}
Json model_snapshot(const ControllerAddress &address)
{
	auto *controller = model_controller(address);
	if (!controller || controller->human || controller->candidates.empty() || !controller->brain ||
	    encounter_owns_controller(address.entity, controller->definition))
		return nullptr;
	const auto observed = controller_observations(address.entity, *controller);
	return build_controller_snapshot(address, ecs.get<Clock>().tick, *controller, observed,
	                                 knowledge.project_json({address.entity, address.console}),
	                                 knowledge.controller_id(address),
	                                 entities.at(address.entity).get<BridgeBlackboard>());
}
void model_fallback(EntityId id, std::size_t index, const std::string &reason)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.effective_provider != "tree")
	{
		const auto observed = controller_observations(id, controller);
		cancel_controller_action(id, index, "provider_fallback", observed);
	}
	controller.effective_provider = "tree";
	controller.fallback_reason = reason;
	const std::uint64_t seconds =
	    std::min<std::uint64_t>(30, 2ULL << std::min<std::uint64_t>(controller.retry_count, 4));
	controller.next_decision = ecs.get<Clock>().tick + seconds * 20;
	controller.retry_count = std::min<std::uint64_t>(controller.retry_count + 1, 5);
}
void model_deviation(const ControllerAddress &address, ConsoleControllerRuntime &controller, const std::string &reason)
{
	const auto facts = controller_observations(address.entity, controller);
	const auto &stance = facts.at(controller.brain->own[ControllerOwnField::Stance]);
	bool orders = stance.type == state_tree::Type::Text && !stance.text.empty() &&
	              stance.text != controller.brain->routine_stance;
	for (std::size_t i = 0; i < facts.size(); ++i)
		if (controller.brain->program.registry.observations[i].name == "objective.status" && facts[i].text == "active")
			orders = true;
	if (!orders)
		return;
	for (std::size_t i = 0; i + 1 < controller.decision_history.size(); ++i)
		if (controller.decision_history[i].value("deviation", std::string{}) == reason &&
		    controller.decision_history[i].value("orders", Json::object()) ==
		        controller.decision_history.back().value("orders", Json::object()))
			return;
	const ConsoleAddress source{address.entity, address.console};
	CoordinationInput message;
	message.sender_system = knowledge.controller_id(address);
	message.destination = {MessageDestinationKind::Ship, address.entity};
	message.origin = ControlOrigin::AI;
	message.payload.schema = knowledge.definition().schema("phoenix.officer-decision");
	message.payload.producer =
	    knowledge.message_producer(source, address.controller, message.payload.schema, ControlOrigin::AI, true);
	message.payload.subject = address.entity;
	message.payload.provenance = {message.sender_system, ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0,
	                              EvidenceKind::Intent};
	message.payload.fields[knowledge.definition().field(message.payload.schema, "reason")] = reason;
	message.presentation.title_key = "Officer judgement";
	message.presentation.body_key = "Officer decision under standing orders: {reason}";
	message.presentation.parameters["reason"] = reason;
	const auto speaker = officer_speaker(address.entity, address.console, address.controller);
	message.presentation.speaker_name = speaker.first;
	message.presentation.speaker_portrait = speaker.second;
	message.expires_tick = ecs.get<Clock>().tick + 200;
	knowledge.admit_message(source, std::move(message), ecs.get<Clock>().tick);
}
