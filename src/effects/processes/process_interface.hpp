// Included inside World::Impl. Process endpoints use existing Console command
// admission; observations contain only permitted, frozen execution state.
CustomPropertyAddress process_control_address(EntityId ship, const ProcessControlDefinition &control) const
{
	CustomPropertyAddress owner{ship};
	if (control.source == "equipment")
		owner.equipment = control.owner;
	else if (control.source == "officer")
		owner.officer = control.owner;
	else if (control.source == "compartment")
	{
		const auto &combat = definition(ship).combat;
		if (!combat)
			throw std::runtime_error("Process compartment owner unavailable");
		const auto at = std::find_if(combat->compartments.begin(), combat->compartments.end(),
		                             [&](const auto &room)
		                             {
			                             return room.id == control.owner;
		                             });
		if (at == combat->compartments.end())
			throw std::runtime_error("Process compartment owner unavailable");
		owner.compartment = std::distance(combat->compartments.begin(), at);
	}
	return owner;
}
const ProcessState *process_control_grant(EntityId ship, const ProcessControlDefinition &control) const
{
	const auto *runtime = process_runtime(process_control_address(ship, control));
	if (!runtime)
		return nullptr;
	const ProcessState *result = nullptr;
	for (const auto &[id, state] : runtime->processes)
		if (state.parent_process.empty() && state.slot == control.process &&
		    (!result || (!result->admitted && state.admitted)))
			result = &state;
	return result;
}
bool process_channel_bound(EntityId ship, std::size_t controller, std::size_t channel) const
{
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
			return true;
	return false;
}
static bool process_arguments_shape(std::span<const std::int64_t> arguments)
{
	return arguments.empty() || (arguments.size() == 1 && arguments.front() > 0 && arguments.front() <= 1000000000);
}
bool process_generation_valid(EntityId ship, std::size_t controller, std::size_t channel,
                              std::span<const std::int64_t> arguments) const
{
	if (!process_arguments_shape(arguments))
		return false;
	if (arguments.empty())
		return true;
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
		{
			const auto *state = process_control_grant(ship, binding);
			return state && state->generation == static_cast<std::uint64_t>(arguments.front());
		}
	return false;
}
bool process_channel_valid(EntityId ship, std::size_t controller, std::size_t channel, std::int64_t value) const
{
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
		{
			const auto *state = process_control_grant(ship, binding);
			return (value == 0 || value == 1) && state &&
			       (!value || state->admitted ||
			        process_conflict(process_control_address(ship, binding), *state).empty());
		}
	return true;
}
bool process_command_valid(EntityId ship, std::size_t controller, std::size_t channel,
                           const OperatingCommand &command) const
{
	return !process_channel_bound(ship, controller, channel) ||
	       (process_generation_valid(ship, controller, channel, command.arguments) &&
	        process_channel_valid(ship, controller, channel, command.value));
}
void consume_process_commands(EntityId ship)
{
	auto &controllers = entities.at(ship).get_mut<EntityControls>().controllers;
	for (const auto &binding : definition(ship).processes.controls)
	{
		auto &channel = controllers.at(binding.controller).commands.at(binding.enabled);
		if (channel.revision == channel.consumed)
			continue;
		channel.consumed = channel.revision;
		const auto *state = process_control_grant(ship, binding);
		channel.accepted = state &&
		                   process_generation_valid(ship, binding.controller, binding.enabled, channel.arguments) &&
		                   process_channel_valid(ship, binding.controller, binding.enabled, channel.value);
		if (channel.accepted)
			channel.accepted = process_request(process_control_address(ship, binding), state->id, channel.value != 0);
	}
}
bool process_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller, std::size_t channel,
                      Json &buttons) const
{
	const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
	for (const auto &binding : definition(address.ship).processes.controls)
	{
		if (&controller != &controllers.at(binding.controller) || controller.definition.console != address.console ||
		    binding.enabled != channel)
			continue;
		const auto *state = process_control_grant(address.ship, binding);
		if (!state)
			return true;
		const auto &spec = controller.definition.commands.at(channel);
		OperatingCommand command{address.ship,
		                         std::string(operating_action(spec.action).name),
		                         state->requested ? 0 : 1,
		                         {},
		                         controller.definition.id,
		                         spec.id,
		                         controller.generation};
		command.arguments = {static_cast<std::int64_t>(state->generation)};
		auto button = encode_operating_command(command);
		button["id"] = "process." + binding.id + "." + std::to_string(command.value);
		button["label"] = state->requested ? "Stop process" : "Start process";
		button["process_label"] = process_definition(*state).label;
		buttons.push_back(std::move(button));
		return true;
	}
	return false;
}
bool process_control_visible(const ConsoleAddress &address, const ProcessControlDefinition &binding) const
{
	return entities.at(address.ship).get<EntityControls>().controllers.at(binding.controller).definition.console ==
	       address.console;
}
bool process_rule_visible(const ConsoleAddress &address, const NativeRuleSource &source) const
{
	// Equipment feeds already permit every native process on that equipment.
	const auto *console = seat(address.ship, address.console);
	if (console && !source.owner.equipment.empty() &&
	    std::find(console->feed_systems.begin(), console->feed_systems.end(), source.owner.equipment) !=
	        console->feed_systems.end())
		return true;
	for (const auto &binding : definition(address.ship).processes.controls)
		if (process_control_visible(address, binding))
		{
			const auto owner = process_control_address(address.ship, binding);
			if (process_actor(owner) != source.owner)
				continue;
			if (const auto *state = process_control_grant(address.ship, binding))
				if (source.key == process_run_source(owner, *state) ||
				    source.key == "process-grant:" + process_instance_key(owner, *state))
					return true;
		}
	return false;
}

// A process endpoint permits its own execution state, not unrestricted facts
// about a remote patient, fault or craft. Reuse the native Console scope before
// publishing a job target and its progress.
bool process_job_details_visible(const ConsoleAddress &address, const CustomPropertyAddress &owner,
                                 const ProcessState &state, const Observation *permitted) const
{
	return phoenix::process_job_details_visible(address, owner, state, process_definition(state), permitted);
}
Json process_board(const ConsoleAddress &address, const Observation *permitted = nullptr) const
{
	Json rows = Json::array();
	std::set<std::string> published;
	const auto append = [&](const CustomPropertyAddress &owner, const ProcessState &state, const std::string &endpoint)
	{
		if (rows.size() >= 4096)
			return;
		const auto *parent = state.parent_process.empty() ? nullptr : process_state(owner, state.parent_process);
		if (!state.parent_process.empty() && !parent)
			return;
		const auto &spec = process_definition(state);
		const auto key = process_instance_key(owner, state);
		const bool driver = process_is_native_driver(spec, state);
		const bool details = !driver && process_job_details_visible(address, owner, state, permitted);
		const bool hidden_job = process_has_finite_work(spec) && !details && !driver;
		published.insert(key);
		rows.push_back({{"id", key + (endpoint.empty() ? "" : ":" + endpoint)},
		                {"control", endpoint},
		                {"process", state.definition},
		                {"slot", state.slot},
		                {"label", spec.label},
		                {"source", process_owner_kind(owner)},
		                {"owner", process_owner_name(owner)},
		                {"requested", state.requested},
		                {"phase", hidden_job ? (state.requested ? "paused" : "off") : state.phase},
		                {"blocker", hidden_job ? "Job details unavailable" : state.blocker},
		                {"generation", state.generation},
		                {"run", state.run},
		                {"output", hidden_job ? 0 : state.work_output},
		                {"upkeep_output", state.idle_output},
		                {"work", hidden_job ? 0 : state.actual_work},
		                {"work_unit", spec.work_unit},
		                {"display_scale", spec.work_divisor},
		                {"finite", process_has_finite_work(spec)},
		                {"driver", driver},
		                {"parent", state.parent_process.empty() ? std::string{} : process_instance_key(owner, *parent)},
		                {"details_available", details},
		                {"target", details ? state.target : std::string{}},
		                {"progress", details ? state.progress : 0},
		                {"required_work", details ? state.required_work : 0},
		                {"completed", details && state.completed},
		                {"ready", details && state.ready},
		                {"completion_delivered", details && state.completed},
		                {"properties", execution_observed_properties(process_actor(owner), spec.observations)}});
	};
	for (const auto &binding : definition(address.ship).processes.controls)
		if (process_control_visible(address, binding))
			if (const auto *state = process_control_grant(address.ship, binding))
			{
				const auto owner = process_control_address(address.ship, binding);
				append(owner, *state, binding.id);
				if (const auto *runtime = process_runtime(owner))
					for (const auto &[id, child] : runtime->processes)
						if (child.parent_process == state->id &&
						    !published.contains(process_instance_key(owner, child)) &&
						    process_job_details_visible(address, owner, child, permitted))
							append(owner, child, {});
			}
	// Native System readouts already express equipment-local visibility. Do not
	// require extra process endpoints just to preserve ordinary machinery panels.
	if (permitted)
		for (const auto &system : permitted->systems)
		{
			const CustomPropertyAddress owner{address.ship, system.id};
			if (const auto *runtime = process_runtime(owner))
				for (const auto &[id, state] : runtime->processes)
					if (!published.contains(process_instance_key(owner, state)) &&
					    (state.parent_process.empty() || process_job_details_visible(address, owner, state, permitted)))
						append(owner, state, {});
		}
	// Facilities are entity-owned rather than fitted equipment. A permitted
	// patient readout grants the corresponding recovery episode, not unrelated
	// entity processes or another department's patients.
	const CustomPropertyAddress entity_owner{address.ship};
	if (permitted)
		if (const auto *runtime = process_runtime(entity_owner))
			for (const auto &[id, state] : runtime->processes)
				if (process_definition(state).native == NativeProcessKind::Recovery && !state.parent_process.empty() &&
				    process_job_details_visible(address, entity_owner, state, permitted))
				{
					if (const auto *driver = process_state(entity_owner, state.parent_process);
					    driver && !published.contains(process_instance_key(entity_owner, *driver)))
						append(entity_owner, *driver, {});
					if (!published.contains(process_instance_key(entity_owner, state)))
						append(entity_owner, state, {});
				}
	return rows;
}
