// World::Impl methods. Physical mutation is confined to command consumption below.
std::vector<state_tree::Value> controller_observations(EntityId id, const ConsoleControllerRuntime &controller)
{
	using state_tree::Type;
	using state_tree::Value;
	const ConsoleAddress address{id, controller.definition.console};
	const auto &brain = *controller.brain;
	std::vector<Value> values(brain.program.registry.observations.size());
	values[0] = Value::reference(Type::Entity, id);
	values[1] = Value::reference(Type::Console, knowledge.console_id(address));
	values[2] = Value::integer(ecs.get<Clock>().tick);
	for (std::size_t i = 0; i < controller.tree_channels.size(); ++i)
	{
		const auto field = controller.tree_channels[i];
		const auto amount = controller.commands[field].value;
		values[3 + i] = controller.definition.commands[field].type == KnowledgeType::Boolean
		                    ? Value::boolean(amount != 0)
		                    : Value::integer(amount);
	}
	const auto board = brain_snapshots.contains(address) ? brain_snapshots.at(address) : knowledge.project(address);
	for (std::size_t i = 0; i < brain.reads.size(); ++i)
	{
		const auto &binding = brain.reads[i];
		const auto &records = binding.shared ? board.shared : board.records;
		std::set<std::pair<KnowledgeId, KnowledgeId>> seen;
		for (auto record = records.rbegin(); record != records.rend(); ++record)
			if (record->value.schema == binding.schema &&
			    record->value.subject == (binding.subject ? binding.subject : id) &&
			    seen.emplace(record->value.provenance.source, record->publication).second && !record->value.withdrawn &&
			    !knowledge.stale(*record, ecs.get<Clock>().tick) && record->value.fields.contains(binding.field))
			{
				const auto &value = record->value.fields.at(binding.field);
				auto &target = values[3 + controller.tree_channels.size() + i];
				if (auto n = std::get_if<std::int64_t>(&value))
					target = Value::integer(*n);
				else if (auto b = std::get_if<bool>(&value))
					target = Value::boolean(*b);
				else
					target = Value::string(std::get<std::string>(value));
				break;
			}
	}
	return values;
}
void controller_output(EntityId id, std::size_t index, const state_tree::Output &output)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	const auto field = controller.tree_channels.at(output.action);
	const auto request = controller.definition.commands[field].semantics == CommandSemantics::Request;
	if (request)
	{
		std::string receipt = std::to_string(output.activation) + "/" + std::to_string(output.task);
		for (std::size_t i = 0; i < output.call_path.size(); ++i)
			receipt += "/" + std::to_string(output.call_path[i]) + ":" + std::to_string(output.call_activations.at(i));
		if (controller.effects.contains(receipt))
			return;
		if (controller.effects.size() >= 65536)
			throw std::runtime_error("Controller effect journal exhausted");
		controller.effects.insert(receipt);
	}
	const auto request_id = request ? controller.commands[field].request_id + 1 : 0;
	controller.result.available = true;
	controller.result.success = commit_controller({{id, controller.definition.console, controller.definition.id},
	                                               index,
	                                               field,
	                                               output.arguments.at(0).number,
	                                               controller.generation,
	                                               request_id,
	                                               false});
	controller.result.reason = controller.result.success ? "" : "Controller command refused";
}
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
		if (!controller.human && controller.brain)
		{
			const auto observations = controller_observations(id, controller);
			const auto result =
			    state_tree::interrupt(controller.brain->program, controller.brain->continuation, observations);
			for (const auto &output : result.outputs)
				controller_output(id, i, output);
		}
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
		if (controller.human)
			continue;
		if (controller.brain)
		{
			auto &brain = *controller.brain;
			if (brain.continuation.interrupted)
				state_tree::resume(brain.continuation);
			const auto observations = controller_observations(id, controller);
			const auto result = state_tree::step(brain.program, brain.continuation, observations);
			brain.debug = result.debug;
			for (const auto &output : result.outputs)
				controller_output(id, i, output);
		}
		if (controller.timed)
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
		row["effects"] = controller.effects;
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
		const auto &effects = row.at("effects");
		fail(effects.is_array() && effects.size() <= 65536);
		for (const auto &effect : effects)
		{
			fail(effect.is_string() && !effect.get_ref<const std::string &>().empty() &&
			     effect.get_ref<const std::string &>().size() <= 2048);
			fail(controller.effects.insert(effect.get<std::string>()).second);
		}
	}
	controls.profiles = std::move(profiles);
}
