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
		if (state.slot == control.process && (!result || (!result->admitted && state.admitted)))
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
		const auto owner = process_control_address(address.ship, binding);
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
Json process_board(const ConsoleAddress &address, const Observation *permitted = nullptr) const
{
	Json rows = Json::array();
	std::set<std::string> published;
	const auto append = [&](const CustomPropertyAddress &owner, const ProcessState &state, const std::string &endpoint)
	{
		if (rows.size() >= 4096)
			return;
		const auto &spec = process_definition(state);
		const auto key = process_instance_key(owner, state);
		published.insert(key);
		rows.push_back({{"id", key + (endpoint.empty() ? "" : ":" + endpoint)},
		                {"control", endpoint},
		                {"process", state.definition},
		                {"slot", state.slot},
		                {"label", spec.label},
		                {"source", process_owner_kind(owner)},
		                {"owner", process_owner_name(owner)},
		                {"requested", state.requested},
		                {"phase", state.phase},
		                {"blocker", state.blocker},
		                {"generation", state.generation},
		                {"run", state.run},
		                {"output", state.work_output},
		                {"upkeep_output", state.idle_output},
		                {"work", state.actual_work},
		                {"work_unit", spec.work_unit},
		                {"display_scale", spec.work_divisor},
		                {"properties", execution_observed_properties(process_actor(owner), spec.observations)}});
	};
	for (const auto &binding : definition(address.ship).processes.controls)
		if (process_control_visible(address, binding))
			if (const auto *state = process_control_grant(address.ship, binding))
				append(process_control_address(address.ship, binding), *state, binding.id);
	// Native System readouts already express equipment-local visibility. Do not
	// require extra process endpoints just to preserve ordinary machinery panels.
	if (permitted)
		for (const auto &system : permitted->systems)
		{
			const CustomPropertyAddress owner{address.ship, system.id};
			if (const auto *runtime = process_runtime(owner))
				for (const auto &[id, state] : runtime->processes)
					if (!published.contains(process_instance_key(owner, state)))
						append(owner, state, {});
		}
	return rows;
}
Json process_decision_facts(const ConsoleAddress &, const Observation &permitted) const
{
	Json result = Json::object();
	if (const auto rows = permitted.gameplay.find("processes"); rows != permitted.gameplay.end())
		for (const auto &row : *rows)
		{
			const auto endpoint = row.at("control").get<std::string>();
			if (endpoint.empty())
				continue;
			result[endpoint] = {{"phase", row.at("phase")},
			                    {"requested", row.at("requested")},
			                    {"running", row.at("phase") == "running"},
			                    {"paused", row.at("phase") == "paused"},
			                    {"blocked", !row.at("blocker").get<std::string>().empty()},
			                    {"activity", row.at("output")},
			                    {"work", row.at("work")},
			                    {"properties", row.at("properties")}};
		}
	return result;
}
