// World::Impl methods. Physical mutation is confined to command consumption below.
std::optional<ControllerProposal> controller_proposal(const ConsoleAddress &address, const OperatingCommand &command,
                                                      bool human) const
{
	const auto entity = entities.at(address.ship);
	const auto &controls = entity.get<EntityControls>();
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		const auto &controller = controls.controllers[i];
		if (controller.definition.console != address.console || controller.definition.id != command.controller)
			continue;
		if (controller.human != human || command.generation != controller.generation)
			return {};
		for (std::size_t field = 0; field < controller.definition.commands.size(); ++field)
		{
			const auto &spec = controller.definition.commands[field];
			if (spec.id != command.channel)
				continue;
			if (command.value < spec.minimum || command.value > spec.maximum)
				return {};
			const auto &state = controller.commands[field];
			if (spec.semantics == CommandSemantics::Request)
			{
				if ((!state.accepted && command.request_id == state.request_id) ||
				    (state.consumed < state.revision && state.request_id && command.request_id != state.request_id) ||
				    std::any_of(controller_pending.begin(), controller_pending.end(),
				                [&](const auto &pending)
				                {
					                return pending.address.entity == address.ship && pending.controller == i &&
					                       pending.channel == field &&
					                       (pending.request_id != command.request_id || pending.value != command.value);
				                }) ||
				    !command.request_id || command.request_id < state.request_id ||
				    (command.request_id == state.request_id && command.value != state.value))
					return {};
			}
			else if (command.request_id)
				return {};
			return ControllerProposal{{address.ship, address.console, controller.definition.id},
			                          i,
			                          field,
			                          command.value,
			                          command.generation,
			                          command.request_id,
			                          human};
		}
	}
	return {};
}
bool commit_controller(const ControllerProposal &proposal)
{
	if (!entities.contains(proposal.address.entity))
		return false;
	auto &controls = entities.at(proposal.address.entity).get_mut<EntityControls>();
	if (proposal.controller >= controls.controllers.size())
		return false;
	auto &controller = controls.controllers[proposal.controller];
	if (controller.definition.console != proposal.address.console ||
	    controller.definition.id != proposal.address.controller || controller.generation != proposal.generation ||
	    controller.human != proposal.human || proposal.channel >= controller.commands.size())
		return false;
	const auto &spec = controller.definition.commands[proposal.channel];
	auto &state = controller.commands[proposal.channel];
	if (proposal.value < spec.minimum || proposal.value > spec.maximum)
		return false;
	if (spec.semantics == CommandSemantics::Request)
	{
		if (!proposal.request_id || proposal.request_id < state.request_id ||
		    (state.request_id && state.consumed < state.revision && proposal.request_id != state.request_id))
			return false;
		if (proposal.request_id == state.request_id)
			return proposal.value == state.value;
	}
	else if (state.value == proposal.value && state.accepted)
		return true;
	if (state.revision == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Controller command revision exhausted");
	state.value = proposal.value;
	state.request_id = proposal.request_id;
	++state.revision;
	return true;
}
void refresh_controllers(EntityId id, bool validate = false)
{
	auto entity = entities.at(id);
	auto &controls = entity.get_mut<EntityControls>();
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		const ConsoleAddress address{id, controller.definition.console};
		const bool present =
		    !(away.contains(address) && away.at(address)) && std::any_of(claims.begin(), claims.end(),
		                                                                 [&](const auto &claim)
		                                                                 {
			                                                                 return claim.second == address;
		                                                                 });
		const bool human = present && controls.layout.profiles.at(controls.profiles.at(address.console)).human.at(i);
		if (validate && human != controller.human)
			throw std::runtime_error("Saved Controller operator differs from profile/claims");
		if (human == controller.human)
			continue;
		if (controller.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Controller operator generation exhausted");
		++controller.generation;
		controller.human = human;
		for (std::size_t j = 0; j < controller.commands.size(); ++j)
		{
			const auto action = controller.definition.commands[j].action;
			if (action == OperatingAction::Longitudinal || action == OperatingAction::Lateral ||
			    action == OperatingAction::ManualSteering || action == OperatingAction::Boost)
			{
				auto &command = controller.commands[j];
				command.value = 0;
				++command.revision;
			}
		}
	}
}
bool controller_meta(const ConsoleAddress &address, const OperatingCommand &command, bool execute)
{
	auto &controls = entities.at(address.ship).get_mut<EntityControls>();
	if (command.action == "away")
	{
		if (command.value < 0 || command.value > 1)
			return false;
		if (execute)
		{
			away[address] = command.value != 0;
			refresh_controllers(address.ship);
		}
		return true;
	}
	if (command.action == "preset")
	{
		if (command.value < 0 || static_cast<std::size_t>(command.value) >= controls.layout.profiles.size())
			return false;
		if (execute)
		{
			controls.profiles[address.console] = static_cast<std::size_t>(command.value);
			refresh_controllers(address.ship);
		}
		return true;
	}
	return false;
}
void run_console_controllers(flecs::entity entity)
{
	const auto id = entity.get<StableId>().id;
	auto &controls = entity.get_mut<EntityControls>();
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		if (controller.human || !controller.timed)
			continue;
		for (const auto &output : step_tree(programs[controller.timed->program], controller.timed->cursor))
			for (std::size_t j = 0; j < controller.definition.commands.size(); ++j)
				if (controller.definition.commands[j].action == output.action)
					commit_controller({{id, controller.definition.console, controller.definition.id},
					                   i,
					                   j,
					                   output.value,
					                   controller.generation,
					                   controller.definition.commands[j].semantics == CommandSemantics::Request
					                       ? controller.commands[j].request_id + 1
					                       : 0,
					                   false});
	}
	// Consume changes, not repeated method calls: speed writes must not cancel Match Speed every tick.
	for (auto &controller : controls.controllers)
		for (std::size_t i = 0; i < controller.commands.size(); ++i)
		{
			auto &value = controller.commands[i];
			const auto &command = controller.definition.commands[i];
			if (value.consumed == value.revision)
				continue;
			if (command.semantics == CommandSemantics::Request && !value.request_id)
			{
				value.consumed = value.revision;
				continue;
			}
			bool admitted = true;
			for (const auto consumer : command.consumers)
			{
				const auto child = entity.get<EquipmentIndex>().values.at(consumer);
				admitted &= operate(entity, child.get<Equipment>().id, {command.action, value.value}, false);
			}
			if (admitted)
				for (const auto consumer : command.consumers)
				{
					const auto child = entity.get<EquipmentIndex>().values.at(consumer);
					operate(entity, child.get<Equipment>().id, {command.action, value.value}, true);
				}
			value.accepted = admitted;
			value.consumed = value.revision;
		}
}
Json project_controllers(const ConsoleAddress &address) const
{
	const auto &controls = entities.at(address.ship).get<EntityControls>();
	Json result = Json::array();
	for (const auto &controller : controls.controllers)
	{
		if (controller.definition.console != address.console)
			continue;
		Json commands = Json::object();
		for (std::size_t i = 0; i < controller.commands.size(); ++i)
		{
			const auto &spec = controller.definition.commands[i];
			const auto &state = controller.commands[i];
			commands[spec.id] = {{"value", state.value},
			                     {"revision", state.revision},
			                     {"semantics", spec.semantics == CommandSemantics::Setting ? "setting" : "request"},
			                     {"accepted", state.accepted},
			                     {"request_id", state.request_id}};
		}
		result.push_back({{"id", controller.definition.id},
		                  {"console", address.console},
		                  {"human", controller.human},
		                  {"generation", controller.generation},
		                  {"commands", commands}});
	}
	return result;
}
Json save_controllers(flecs::entity entity) const
{
	const auto &controls = entity.get<EntityControls>();
	Json result{{"profiles", controls.profiles}, {"controllers", Json::array()}};
	for (const auto &controller : controls.controllers)
	{
		Json row{{"id", controller.definition.id},
		         {"console", controller.definition.console},
		         {"generation", controller.generation},
		         {"human", controller.human},
		         {"commands", Json::array()}};
		for (const auto &command : controller.commands)
			row["commands"].push_back({{"value", command.value},
			                           {"revision", command.revision},
			                           {"consumed", command.consumed},
			                           {"request_id", command.request_id},
			                           {"accepted", command.accepted}});
		if (controller.timed)
			row["timed"] = save_tree(programs[controller.timed->program], controller.timed->cursor);
		if (controller.brain)
			row["brain"] = state_tree::save(controller.brain->program, controller.brain->continuation);
		result["controllers"].push_back(std::move(row));
	}
	return result;
}
void restore_controllers(flecs::entity entity, const Json &saved)
{
	auto &controls = entity.get_mut<EntityControls>();
	const auto fail = [](bool valid)
	{
		if (!valid)
			throw std::runtime_error("Saved Controller state differs from layout or bounds");
	};
	auto profiles = saved.at("profiles").get<std::map<std::string, std::size_t>>();
	fail(profiles.size() == controls.profiles.size());
	for (const auto &[console, profile] : profiles)
		fail(controls.profiles.contains(console) && profile < controls.layout.profiles.size());
	fail(saved.at("controllers").is_array() && saved.at("controllers").size() == controls.controllers.size());
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		const auto &row = saved.at("controllers").at(i);
		fail(row.at("id") == controller.definition.id && row.at("console") == controller.definition.console);
		fail(row.at("generation").is_number_unsigned() && row.at("generation").get<std::uint64_t>() > 0);
		controller.generation = row.at("generation").get<std::uint64_t>();
		fail(row.at("human").is_boolean());
		controller.human = row.at("human").get<bool>();
		fail(row.at("commands").is_array() && row.at("commands").size() == controller.commands.size());
		for (std::size_t j = 0; j < controller.commands.size(); ++j)
		{
			auto &state = controller.commands[j];
			const auto &spec = controller.definition.commands[j];
			const auto &value = row.at("commands").at(j);
			fail(value.at("value").is_number_integer() && value.at("value") >= spec.minimum &&
			     value.at("value") <= spec.maximum);
			state.value = value.at("value").get<std::int64_t>();
			for (const auto *key : {"revision", "consumed", "request_id"})
				fail(value.at(key).is_number_unsigned());
			state.revision = value.at("revision").get<std::uint64_t>();
			state.consumed = value.at("consumed").get<std::uint64_t>();
			state.request_id = value.at("request_id").get<std::uint64_t>();
			fail(state.revision > 0 && state.consumed <= state.revision &&
			     (spec.semantics == CommandSemantics::Request || state.request_id == 0));
			fail(value.at("accepted").is_boolean());
			state.accepted = value.at("accepted").get<bool>();
		}
		fail(row.contains("timed") == controller.timed.has_value() &&
		     row.contains("brain") == controller.brain.has_value());
		if (controller.timed)
			controller.timed->cursor = load_tree(programs[controller.timed->program], row.at("timed").get<TreeState>());
		if (controller.brain)
			controller.brain->continuation = state_tree::load(controller.brain->program, row.at("brain"));
	}
	controls.profiles = std::move(profiles);
}
