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
static Json model_value(const state_tree::Value &v)
{
	return {{"type", static_cast<int>(v.type)}, {"number", v.number}, {"text", v.text}};
}
bool model_action_available(const ConsoleControllerRuntime &controller, const std::string &name,
                            std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> observed)
{
	for (const auto &spec : controller.actions)
		if (spec.id == name && spec.inputs.size() == inputs.size())
		{
			std::vector<state_tree::Value> values(observed.begin(), observed.end());
			for (std::size_t i = 0; i < inputs.size(); ++i)
				values.at(spec.inputs[i]) = inputs[i];
			state_tree::Instance check;
			const auto result = state_tree::step(spec.availability, check, values);
			return !result.debug.active.empty() && result.debug.active.back() == "available";
		}
	return false;
}
Json model_candidates(const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observed)
{
	Json result = Json::array();
	if (!controller.brain)
		return result;
	for (const auto &candidate : controller.candidates)
	{
		state_tree::Instance instance;
		const auto checked = state_tree::step(candidate.program, instance, observed);
		if (checked.debug.active.empty() || checked.debug.active.back() != "offer")
			continue;
		const auto evaluated = state_tree::step(candidate.program, instance, observed);
		if (evaluated.outputs.empty())
			continue;
		Json operations = Json::array();
		bool available = true;
		for (const auto &output : evaluated.outputs)
		{
			const auto &name = candidate.program.registry.actions.at(output.action).name;
			if (name.starts_with("select.") || name.starts_with("replace."))
				available =
				    available && model_action_available(controller, name.substr(name.starts_with("select.") ? 7 : 8),
				                                        std::span(output.arguments).subspan(1), observed);
			Json arguments = Json::array();
			for (const auto &v : output.arguments)
				arguments.push_back(model_value(v));
			operations.push_back({{"name", name}, {"arguments", arguments}});
			if (name.starts_with("select.") || name.starts_with("replace."))
				for (const auto &spec : controller.actions)
					if (spec.id == name.substr(name.starts_with("select.") ? 7 : 8))
					{
						operations.back()["label"] = spec.label;
						Json readable = Json::object();
						for (std::size_t i = 0; i < spec.inputs.size(); ++i)
						{
							const auto &value = output.arguments[i + 1];
							readable[spec.program.registry.observations.at(spec.inputs[i]).name.substr(6)] =
							    value.type == state_tree::Type::Text      ? Json(value.text)
							    : value.type == state_tree::Type::Boolean ? Json(value.number != 0)
							                                              : Json(value.number);
						}
						operations.back()["inputs"] = readable;
					}
		}
		if (available)
			result.push_back({{"id", candidate.id},
			                  {"description", candidate.description},
			                  {"deviation", candidate.deviation},
			                  {"operations", operations}});
	}
	return result;
}
Json model_basis(EntityId id, const ConsoleControllerRuntime &controller)
{
	const auto &bridge = entities.at(id).get<BridgeBlackboard>();
	return {{"generation", controller.generation},
	        {"provider_generation", controller.provider_generation},
	        {"action_serial", controller.action.serial},
	        {"action_status", controller.action.status},
	        {"orders",
	         {{"revision",
	           (bridge.epochs.contains(controller.definition.console) ? bridge.epochs.at(controller.definition.console)
	                                                                  : 0)},
	          {"alert", bridge.condition}}},
	        {"coordination", save_controller_coordination(controller)}};
}
Json model_snapshot(const ControllerAddress &address)
{
	auto *controller = model_controller(address);
	if (!controller || controller->human || controller->candidates.empty() || !controller->brain ||
	    encounter_owns_controller(address.entity, controller->definition))
		return nullptr;
	const auto observed = controller_observations(address.entity, *controller);
	auto all = model_candidates(*controller, observed);
	const auto total = all.size();
	// Candidate authors order standing commitments first. Preserve their current action
	// independently through the always-present continue option.
	if (all.size() > 24)
		all.erase(all.begin() + 24, all.end());
	all.push_back({{"id", "continue"}, {"description", "Continue the current activity and commitments."}});
	all.push_back({{"id", "defer_to_tree"}, {"description", "Ask the authored officer policy to decide."}});
	Json facts = Json::object();
	for (std::size_t i = 0; i < observed.size(); ++i)
	{
		const auto &v = observed[i];
		const auto &name = controller->brain->program.registry.observations[i].name;
		if (v.type == state_tree::Type::Missing || name.starts_with("input."))
			continue;
		facts[name] = v.type == state_tree::Type::Text      ? Json(v.text)
		              : v.type == state_tree::Type::Boolean ? Json(v.number != 0)
		                                                    : Json(v.number);
	}
	auto board = knowledge.project_json({address.entity, address.console});
	const auto actor = knowledge.controller_id(address);
	for (const auto *key : {"messages", "notifications"})
	{
		Json visible = Json::array();
		for (const auto &message : board.at(key))
			if (message.at("destination_kind") != static_cast<unsigned>(MessageDestinationKind::Controller) ||
			    message.at("destination") == actor || message.value("public", false))
				visible.push_back(message);
		board[key] = std::move(visible);
	}
	Json result{{"schema", 1},
	            {"ship", address.entity},
	            {"console", address.console},
	            {"controller", address.controller},
	            {"tick", ecs.get<Clock>().tick},
	            {"expires", ecs.get<Clock>().tick + 100},
	            {"sequence", controller->decision_sequence + 1},
	            {"provider", controller->selected_provider},
	            {"basis", model_basis(address.entity, *controller)},
	            {"facts", facts},
	            {"role", controller->definition.id},
	            {"mechanics", "Actions request work; command acceptance is not physical success. Distances are "
	                          "millimetres, headings millidegrees, time simulation ticks at 20 Hz."},
	            {"history", controller->decision_history},
	            {"candidates", all},
	            {"omitted_candidates", total > 24 ? total - 24 : 0}};
	Json omitted = Json::object();
	for (const auto &[key, limit] :
	     std::vector<std::pair<std::string, std::size_t>>{{"records", 32}, {"shared", 16}, {"messages", 16}})
	{
		auto &items = board[key];
		omitted[key] = items.size() > limit ? items.size() - limit : 0;
		if (items.size() > limit)
			items.erase(items.begin(), items.end() - limit);
	}
	for (auto &message : board["messages"])
	{
		message.erase("speaker_avatar");
		message.erase("speaker_portrait");
	}
	board.erase("notifications");
	board["omitted"] = omitted;
	result["role_context"] = controller->brain->model_context;
	result["current_action"] = {{"id", controller->action.id},
	                            {"invocation", controller->action.invocation},
	                            {"status", controller->action.status},
	                            {"reason", controller->action.reason},
	                            {"inputs", Json::array()}};
	for (const auto &input : controller->action.inputs)
		result["current_action"]["inputs"].push_back(model_value(input));
	// These are the same permitted records and messages supplied to this Console.
	result["evidence"] = board;
	// Oversized context is refused, never silently stripped of uncertainty or orders.
	if (result.dump().size() > 48000)
		return nullptr;
	return result;
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
