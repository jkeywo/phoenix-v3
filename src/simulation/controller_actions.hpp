// World::Impl: shared action execution, independent of the selecting policy.
void controller_decision_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                               std::vector<state_tree::Value> &values) const
{
	std::array<std::int64_t, 13> facts{};
	facts[2] = observed.own.heading;
	facts[6] = facts[7] = 1000;
	EntityId current = 0;
	std::int64_t repair = 0, repair_system = 0;
	for (std::size_t i = 0; i < controller.commands.size(); ++i)
	{
		const auto &spec = controller.definition.commands[i];
		if (spec.action == OperatingAction::WeaponTarget)
			current = controller.commands[i].value;
		if (spec.action == OperatingAction::RepairCompartment)
			repair = controller.commands[i].value;
		if (spec.action == OperatingAction::RepairSystem)
			repair_system = controller.commands[i].value;
		if (spec.action == OperatingAction::Speed)
		{
			facts[9] = spec.maximum / 2;
			facts[10] = spec.maximum / 5;
		}
	}
	const BodyView *target = nullptr;
	std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
	for (const auto &contact : observed.contacts)
		if (contact.radar_icon == RadarIcon::ship && contact.disposition == "hostile")
		{
			const auto distance = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
			if (!target || contact.id == current ||
			    (target->id != current && (distance < nearest || (distance == nearest && contact.id < target->id))))
			{
				target = &contact;
				nearest = distance;
			}
		}
	if (target)
	{
		facts[0] = target->id;
		facts[1] = nearest;
		facts[2] = combat_bearing({target->x - observed.own.x, target->y - observed.own.y});
	}
	const auto combat = observed.gameplay.value("combat", Json::object());
	// Equipment indices are static fitting metadata. Candidate damage and duties
	// come only from this Console's permitted snapshot and its accepted requests.
	const auto &structure = definition(observed.own.id).combat;
	std::set<std::int64_t> disabled_compartments;
	for (const auto &compartment : combat.value("compartments", Json::array()))
		if (compartment.at("disabled").get<bool>())
			disabled_compartments.insert(compartment.at("index").get<std::int64_t>());
	const auto needed = [&](std::int64_t compartment)
	{
		return std::any_of(controller.incoming_requests.begin(), controller.incoming_requests.end(),
		                   [&](const auto &request)
		                   {
			                   return request.operation == OfficerOperation::Repair &&
			                          request.status == OfficerStatus::Accepted && request.target == compartment &&
			                          request.expires > ecs.get<Clock>().tick;
		                   });
	};
	const auto priority_for = [&](std::int64_t compartment, bool disabled)
	{
		return disabled ? (needed(compartment) ? 3 : 2) : 1;
	};
	const auto better =
	    [](int priority, int best_priority, std::int64_t candidate, std::int64_t best, std::int64_t current)
	{
		return priority > best_priority || (priority == best_priority &&
		                                    (candidate == current || (best != current && (!best || candidate < best))));
	};
	int best_priority = -1, best_system_priority = -1;
	for (const auto &compartment : combat.value("compartments", Json::array()))
	{
		if (compartment.at("hp") >= compartment.at("maximum"))
			continue;
		const auto candidate = compartment.at("index").get<std::int64_t>();
		const auto priority = priority_for(candidate, disabled_compartments.contains(candidate));
		if (better(priority, best_priority, candidate, facts[4], repair))
		{
			best_priority = priority;
			facts[4] = candidate;
		}
	}
	for (const auto &fault : combat.value("faults", Json::array()))
	{
		if (fault.value("destroyed", false))
			continue;
		const auto candidate = fault.value("compartment_index", std::int64_t{0});
		if (!candidate)
			continue;
		const auto system_id = fault.at("system").get<std::string>();
		bool disabled = disabled_compartments.contains(candidate);
		for (const auto &system : observed.systems)
			if (system.id == system_id)
				disabled =
				    disabled || (system.telemetry.contains("enabled") && !system.telemetry.at("enabled")) ||
				    (system.telemetry.contains("structural_disabled") && system.telemetry.at("structural_disabled"));
		const auto priority = priority_for(candidate, disabled);
		if (better(priority, best_priority, candidate, facts[4], repair))
		{
			best_priority = priority;
			facts[4] = candidate;
		}
		if (structure)
		{
			const auto at = structure->equipment_compartments.find(system_id);
			if (at != structure->equipment_compartments.end())
			{
				const auto system =
				    static_cast<std::int64_t>(std::distance(structure->equipment_compartments.begin(), at)) + 1;
				if (better(priority, best_system_priority, system, facts[5], repair_system))
				{
					best_system_priority = priority;
					facts[5] = system;
				}
			}
		}
	}
	bool point_defence = false;
	const auto owns = [&](const std::string &system)
	{
		const auto &entity = entities.at(observed.own.id);
		for (const auto &command : controller.definition.commands)
			for (const auto index : command.consumers)
				if ((*fittings(entity)).values[index].id == system)
					return true;
		return false;
	};
	for (const auto &system : observed.systems)
		if (owns(system.id))
		{
			const auto &t = system.telemetry;
			if (t.contains("supply_limited") && t.at("supply_limited") && t.contains("reserved_power"))
				facts[11] += t.at("reserved_power");
			if (t.contains("thermal_stopped") && t.at("thermal_stopped"))
				facts[12] = 1;
		}
	for (const auto &shield : combat.value("shields", Json::array()))
	{
		const auto capacity = shield.at("capacity").get<std::int64_t>();
		const auto fraction = capacity > 0 ? shield.at("strength").get<std::int64_t>() * 1000 / capacity : 0;
		facts[7] = std::min(facts[7], fraction);
		if (owns(shield.at("id")))
			facts[6] = fraction;
	}
	facts[8] = facts[6] - facts[7];
	for (const auto &weapon : combat.value("weapons", Json::array()))
		if (owns(weapon.at("id")))
		{
			point_defence = weapon.value("point_defence", false);
			facts[3] = weapon.value("selected", std::int64_t{0});
			std::int64_t best = std::numeric_limits<std::int64_t>::max();
			for (const auto &option : weapon.value("ammunition_options", Json::array()))
				if (option.at("available").get<bool>() && option.at("range_mm").get<std::int64_t>() >= facts[1] &&
				    option.at("range_mm").get<std::int64_t>() < best)
				{
					best = option.at("range_mm");
					facts[3] = option.at("index");
				}
		}
	if (point_defence)
		facts[0] = 0;
	for (std::size_t i = 0; i < facts.size(); ++i)
		values.at(controller.brain->decision_base + i) = state_tree::Value::integer(facts[i]);
	values.at(controller.brain->decision_base + facts.size()) = state_tree::Value::boolean(point_defence);
}
const ControllerActionSpec &controller_action_spec(const ConsoleControllerRuntime &controller) const
{
	for (const auto &spec : controller.actions)
		if (spec.id == controller.action.id)
			return spec;
	throw std::runtime_error("Unknown active Controller action");
}
std::vector<state_tree::Value> action_observations(const ConsoleControllerRuntime &controller,
                                                   std::span<const state_tree::Value> source) const
{
	std::vector<state_tree::Value> result(source.begin(), source.end());
	const auto &spec = controller_action_spec(controller);
	for (std::size_t i = 0; i < spec.inputs.size(); ++i)
		result.at(spec.inputs[i]) = controller.action.inputs.at(i);
	const auto &brain = *controller.brain;
	std::optional<std::int64_t> x, y, contact;
	for (const auto index : spec.inputs)
	{
		const auto &name = brain.program.registry.observations[index].name;
		if (name == "input.destination_x")
			x = result[index].number;
		if (name == "input.destination_y")
			y = result[index].number;
		if (name == "input.contact")
			contact = result[index].number;
	}
	result.at(brain.action_base + 3) =
	    state_tree::Value::integer(x && y ? fixed::length({*x - source[brain.own[ControllerOwnField::X]].number,
	                                                       *y - source[brain.own[ControllerOwnField::Y]].number})
	                                      : 0);
	result.at(brain.action_base + 4) = state_tree::Value::integer(fixed::length(
	    {source[brain.own[ControllerOwnField::Vx]].number, source[brain.own[ControllerOwnField::Vy]].number}));
	bool visible = false;
	if (contact)
	{
		const ConsoleAddress address{static_cast<EntityId>(source[brain.own[ControllerOwnField::Entity]].number),
		                             controller.definition.console};
		const auto fallback = observation_snapshots.contains(address) ? std::optional<Observation>{}
		                                                              : capture_console_observation(address);
		const auto &observed = fallback ? *fallback : observation_snapshots.at(address);
		visible = std::any_of(observed.contacts.begin(), observed.contacts.end(),
		                      [&](const auto &body)
		                      {
			                      return body.id == *contact;
		                      });
	}
	result.at(brain.action_base + 5) = state_tree::Value::boolean(visible);
	return result;
}
void cancel_controller_action(EntityId id, std::size_t index, const std::string &reason,
                              std::span<const state_tree::Value> source)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.action.status != "running")
		return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = action_observations(controller, source);
	const auto result = state_tree::interrupt(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs)
		controller_output(id, index, output, observations, &spec.program);
	controller.action.status = "cancelled";
	controller.action.reason = reason;
}
bool select_controller_action(EntityId id, std::size_t index, const std::string &name, const std::string &invocation,
                              std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> source,
                              std::uint64_t generation)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.human || generation != controller.generation || !controller.brain || invocation.empty() ||
	    invocation.size() > 1024)
		return false;
	const auto found = std::find_if(controller.actions.begin(), controller.actions.end(),
	                                [&](const auto &spec)
	                                {
		                                return spec.id == name;
	                                });
	if (found == controller.actions.end() || inputs.size() != found->inputs.size())
		return false;
	for (std::size_t i = 0; i < inputs.size(); ++i)
		if (inputs[i].type != controller.brain->program.registry.observations.at(found->inputs[i]).type)
			return false;
	auto &active = controller.action;
	const std::vector<state_tree::Value> arguments(inputs.begin(), inputs.end());
	if (active.id == name && active.invocation == invocation && active.inputs == arguments &&
	    !(active.status == "cancelled" && active.reason == "operator_changed"))
		return true;
	std::vector<state_tree::Value> observations(source.begin(), source.end());
	for (std::size_t i = 0; i < inputs.size(); ++i)
		observations.at(found->inputs[i]) = inputs[i];
	state_tree::Instance check;
	const auto availability = state_tree::step(found->availability, check, observations);
	if (availability.debug.active.empty() || availability.debug.active.back() != "available")
		return false;
	cancel_controller_action(id, index, "replaced", source);
	if (active.serial == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Action serial exhausted");
	const auto serial = active.serial + 1;
	active = {name, invocation, "running", {}, arguments, {}, serial};
	return true;
}
bool admit_controller_decision(const ControllerDecisionProposal &proposal,
                               std::span<const state_tree::Value> observations)
{
	auto entity = entities.find(proposal.address.entity);
	if (entity == entities.end() || !entity->second.has<EntityControls>())
		return false;
	auto &controllers = entity->second.get_mut<EntityControls>().controllers;
	for (std::size_t index = 0; index < controllers.size(); ++index)
	{
		auto &controller = controllers[index];
		if (controller.definition.console != proposal.address.console ||
		    controller.definition.id != proposal.address.controller)
			continue;
		if (controller.human || controller.generation != proposal.generation)
			return false;
		if (proposal.operation == "select")
			return select_controller_action(proposal.address.entity, index, proposal.action, proposal.invocation,
			                                proposal.inputs, observations, proposal.generation);
		if (proposal.operation == "continue")
			return true;
		if (proposal.operation == "cancel")
		{
			cancel_controller_action(proposal.address.entity, index, proposal.invocation, observations);
			return true;
		}
		if (proposal.operation.starts_with("coordination."))
			return controller_coordination_action(proposal.address.entity, index, proposal.operation, proposal.inputs);
		return false;
	}
	return false;
}
void run_controller_action(EntityId id, std::size_t index, std::span<const state_tree::Value> source)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	controller.action_writes.clear();
	if (controller.action.status != "running")
		return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = action_observations(controller, source);
	const auto result = state_tree::step(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs)
	{
		controller_output(id, index, output, observations, &spec.program);
		if (controller.action.status != "running")
			break;
	}
	if (controller.action.status != "running")
	{
		const auto status = controller.action.status, reason = controller.action.reason;
		const auto cleanup = state_tree::interrupt(spec.program, controller.action.continuation, observations);
		for (const auto &output : cleanup.outputs)
			controller_output(id, index, output, observations, &spec.program);
		controller.action.status = status;
		controller.action.reason = reason;
	}
}
Json save_controller_action(const ConsoleControllerRuntime &controller) const
{
	const auto &a = controller.action;
	Json result{{"id", a.id},         {"invocation", a.invocation}, {"status", a.status},
	            {"reason", a.reason}, {"serial", a.serial},         {"inputs", Json::array()}};
	for (const auto &v : a.inputs)
		result["inputs"].push_back({{"type", static_cast<int>(v.type)}, {"number", v.number}, {"text", v.text}});
	if (!a.id.empty())
		result["continuation"] = state_tree::save(controller_action_spec(controller).program, a.continuation);
	return result;
}
void restore_controller_action(ConsoleControllerRuntime &controller, const Json &saved)
{
	auto &a = controller.action;
	a.id = saved.at("id");
	a.invocation = saved.at("invocation");
	a.status = saved.at("status");
	a.reason = saved.at("reason");
	a.serial = saved.at("serial");
	if (a.reason.size() > 4096 || a.invocation.size() > 1024 ||
	    (a.status != "idle" && a.status != "running" && a.status != "succeeded" && a.status != "failed" &&
	     a.status != "cancelled"))
		throw std::runtime_error("Invalid action status");
	if (a.id.empty())
	{
		if (a.status != "idle" || a.serial || !saved.at("inputs").empty() || saved.contains("continuation"))
			throw std::runtime_error("Invalid idle action");
		return;
	}
	if (!controller.brain || !a.serial || a.invocation.empty() || a.status == "idle" ||
	    (controller.human && a.status == "running"))
		throw std::runtime_error("Invalid action ownership");
	const auto &spec = controller_action_spec(controller);
	if (!saved.at("inputs").is_array() || saved.at("inputs").size() != spec.inputs.size())
		throw std::runtime_error("Invalid saved action inputs");
	a.inputs.clear();
	for (std::size_t i = 0; i < spec.inputs.size(); ++i)
	{
		const auto &v = saved.at("inputs").at(i);
		const auto type = spec.program.registry.observations.at(spec.inputs[i]).type;
		if (v.at("type") != static_cast<int>(type) || !v.at("number").is_number_integer() ||
		    !v.at("text").is_string() || v.at("text").get_ref<const std::string &>().size() > 4096)
			throw std::runtime_error("Invalid saved action input type");
		if (type == state_tree::Type::Boolean && v.at("number") != 0 && v.at("number") != 1)
			throw std::runtime_error("Invalid saved boolean action input");
		a.inputs.push_back({type, v.at("number"), v.at("text")});
	}
	a.continuation = state_tree::load(spec.program, saved.at("continuation"));
}
