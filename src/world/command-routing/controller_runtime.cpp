module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::coordinated_impulse(const ConsoleAddress &a, const OperatingCommand &c, bool execute) -> bool
{
	if (!c.controller.empty() || !c.channel.empty() || !c.arguments.empty() || !c.text.empty() ||
	    !c.speaker_name.empty() || !c.speaker_avatar.empty() || !c.request_id || c.value < 0 || c.value > 1 ||
	    c.request_id >= 9007199254740991ULL || c.generation != coordinated_generation(a, c.system))
		return false;
	const auto &controls = entities.at(a.ship).get<EntityControls>();
	std::vector<ControllerProposal> proposals;
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		const auto &controller = controls.controllers[i];
		if (controller.definition.console != a.console)
			continue;
		for (std::size_t j = 0; j < controller.commands.size(); ++j)
		{
			const auto &spec = controller.definition.commands[j];
			const auto &state = controller.commands[j];
			if (spec.coordinated_group.empty() || spec.coordinated_group != c.system)
				continue;
			if (!controller_command_human(controller, j) || spec.action != OperatingAction::Impulse ||
			    c.request_id < state.request_id || (c.request_id == state.request_id && c.value != state.value) ||
			    (state.consumed < state.revision && c.request_id != state.request_id))
				return false;
			proposals.push_back({{a.ship, a.console, controller.definition.id},
			                     i,
			                     j,
			                     c.value,
			                     controller.generation,
			                     c.request_id,
			                     true,
			                     {}});
		}
	}
	if (proposals.empty())
		return false;
	if (execute)
		for (const auto &proposal : proposals)
			if (!commit_controller(proposal))
				throw std::runtime_error("Coordinated impulse admission changed during execution");
	return true;
}

auto Runtime::controller_meta(const ConsoleAddress &address, const OperatingCommand &command, bool execute) -> bool
{
	if (!command.controller.empty() || !command.channel.empty() || command.generation || command.request_id ||
	    !command.system.empty())
		return false;
	auto &controls = entities.at(address.ship).get_mut<EntityControls>();
	if (command.action == "away")
	{
		if (command.value < 0 || command.value > 1)
			return false;
		if (execute)
		{
			ownership.set_away(address, command.value != 0);
			refresh_controllers(address.ship);
		}
		return true;
	}
	if (command.action == "preset")
	{
		if (command.value < 0 || static_cast<std::size_t>(command.value) >= controls.layout.profiles.size())
			return false;
		if (controls.layout.fixed_profiles.contains(address.console) &&
		    controls.layout.fixed_profiles.at(address.console) != static_cast<std::size_t>(command.value))
			return false;
		if (execute)
		{
			set_console_complexity(controls, address.console, static_cast<std::size_t>(command.value));
			refresh_controllers(address.ship);
		}
		return true;
	}
	return false;
}

auto Runtime::run_console_controllers(flecs::entity entity) -> void
{
	const auto id = entity.get<StableId>().id;
	auto &controls = entity.get_mut<EntityControls>();
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		if (!controller_ai_running(controller))
			continue;
		if (encounter_owns_controller(id, controller.definition))
		{
			if (controller.action.status == "running")
			{
				const auto observations = controller_observations(id, controller);
				cancel_controller_action(id, i, "operation_owned", observations);
			}
			continue;
		}
		if (controller.brain)
		{
			process_controller_coordination(id, i);
			auto &brain = *controller.brain;
			if (brain.continuation.interrupted && controller.definition.widget.empty())
				state_tree::resume(brain.continuation);
			const auto observations = controller_observations(id, controller);

			std::optional<Observation> fallback;
			const auto ports = controller_action_ports(id, i, observations, fallback);
			step_controller_policy(controller, observations, ports);
		}
	}
	apply_docking_controller(entity);
	consume_controller_commands(entity);
}

auto Runtime::consume_controller_commands(flecs::entity entity) -> void
{
	auto &controls = entity.get_mut<EntityControls>();
	// Shared targeting is an authored command relationship. Independent human
	// orders persist until this channel returns to assisted operation.
	for (auto &controller : controls.controllers)
		for (std::size_t i = 0; i < controller.commands.size(); ++i)
		{
			const auto &spec = controller.definition.commands[i];
			auto &state = controller.commands[i];
			if (spec.shared_target_controller.empty() ||
			    (!controller_command_human(controller, i) &&
			     encounter_owns_controller(entity.get<StableId>().id, controller.definition)))
				continue;
			if (!controller_command_human(controller, i))
				state.target_override = false;
			if (state.target_override)
				continue;
			for (const auto &source : controls.controllers)
				if (source.definition.console == controller.definition.console &&
				    source.definition.id == spec.shared_target_controller)
					for (std::size_t j = 0; j < source.commands.size(); ++j)
						if (source.definition.commands[j].id == spec.shared_target_channel &&
						    (state.value != source.commands[j].value ||
						     state.arguments != source.commands[j].arguments))
						{
							if (state.revision == std::numeric_limits<std::uint64_t>::max())
								throw std::runtime_error("Controller command revision exhausted");
							state.value = source.commands[j].value;
							state.arguments = source.commands[j].arguments;
							++state.revision;
						}
		}
	std::set<std::size_t> destinations;
	std::set<std::size_t> failed_actions;
	// Consume changes, not repeated method calls: speed writes must not cancel Match Speed every tick.
	for (auto &controller : controls.controllers)
		for (std::size_t i = 0; i < controller.commands.size(); ++i)
		{
			auto &value = controller.commands[i];
			const auto &command = controller.definition.commands[i];
			if (command.action == OperatingAction::AbilityActivate ||
			    process_channel_bound(entity.get<StableId>().id,
			                          static_cast<std::size_t>(&controller - controls.controllers.data()), i))
				continue; // The bound ability consumes its request in the physical preparation phase.
			if (value.consumed == value.revision)
				continue;
			if (command.semantics == CommandSemantics::Request && !value.request_id)
			{
				value.consumed = value.revision;
				continue;
			}
			bool admitted = true;
			if (command.action == OperatingAction::Waypoint)
				publish_waypoint(entity.get<StableId>().id, value, false);
			for (const auto consumer : command.consumers)
			{
				const auto child = entity.get<EquipmentIndex>().values.at(consumer);
				admitted &=
				    operate(entity, child.get<Equipment>().id, {command.action, value.value, value.arguments}, false);
			}
			if (admitted)
				for (const auto consumer : command.consumers)
				{
					const auto child = entity.get<EquipmentIndex>().values.at(consumer);
					operate(entity, child.get<Equipment>().id, {command.action, value.value, value.arguments}, true);
				}
			if (admitted && command.action == OperatingAction::Destination && value.value)
				destinations.insert(command.consumers.begin(), command.consumers.end());
			value.accepted = admitted;
			value.consumed = value.revision;
			if (!admitted && controller.action.status == "running" && controller.action_writes.contains(i))
				failed_actions.insert(static_cast<std::size_t>(&controller - controls.controllers.data()));
		}
	for (const auto index : failed_actions)
	{
		auto &controller = controls.controllers[index];
		const auto observations = controller_observations(entity.get<StableId>().id, controller);
		cancel_controller_action(entity.get<StableId>().id, index, "command_refused", observations);
		controller.action.status = "failed";
		controller.action.reason = "command_refused";
	}
	// A destination chooses guidance, while a bound cruise channel remains the
	// persistent speed setting. Compose them after all changed modes are consumed,
	// independent of Controller/channel order. Destination-only fittings retain
	// the physical mechanism's default cruise speed.
	if (!destinations.empty())
		for (const auto &controller : controls.controllers)
			for (std::size_t i = 0; i < controller.commands.size(); ++i)
			{
				const auto &command = controller.definition.commands[i];
				const auto &value = controller.commands[i];
				if (command.action != OperatingAction::Speed || !value.accepted)
					continue;
				for (const auto consumer : command.consumers)
					if (destinations.contains(consumer))
					{
						const auto child = entity.get<EquipmentIndex>().values.at(consumer);
						const auto *flight = child.try_get<FlightRuntime>();
						if (flight && flight->state.navigating)
							operate(entity, child.get<Equipment>().id, {command.action, value.value}, true);
					}
			}
}

auto Runtime::project_controllers(const ConsoleAddress &address, const Observation &projected) -> Json
{
	const auto entity = entities.at(address.ship);
	const auto &controls = entity.get<EntityControls>();
	Json result = Json::array();
	const auto board = knowledge.project(address);
	for (const auto &controller : controls.controllers)
	{
		if (controller.definition.console != address.console)
			continue;
		ControllerViewFacts facts;
		if (const auto fitted = fittings(entity))
			for (const auto &system : fitted->values)
				facts.systems.push_back(system.id);
		facts.conversation_authority =
		    comms_controller(address.ship, controller.definition) && conversation_operator(address.ship, controller);
		bool contributes = false, bay = false;
		for (const auto &command : controller.definition.commands)
			for (const auto consumer : command.consumers)
			{
				const auto kind = definition(address.ship).fitted->values.at(consumer).kind;
				contributes |= kind == OperatingSystem::Team || kind == OperatingSystem::ShuttleBay;
				bay |= kind == OperatingSystem::ShuttleBay;
			}
		facts.contribution_authority = !controller.definition.widget.empty()
		                                   ? controller_operation_human(controller, "contribution")
		                                   : contributes && console_operator(address) != 0;
		facts.craft_authority = !controller.definition.widget.empty() ? controller_operation_human(controller, "craft")
		                                                              : bay && assisted_workload(address, "craft");
		facts.transport_authority = !controller.definition.widget.empty()
		                                ? controller_operation_human(controller, "transport")
		                                : bay && assisted_workload(address, "transport");
		const auto observations = controller.brain && controller_ai_running(controller) && !controller.actions.empty()
		                              ? controller_observations(address.ship, controller, &projected, &board)
		                              : std::vector<state_tree::Value>{};
		result.push_back(project_controller_runtime(controller, facts, observations));
	}
	return result;
}

auto Runtime::restore_controllers(flecs::entity entity, const Json &saved) -> void
{
	const auto id = entity.get<StableId>().id;
	auto &controls = entity.get_mut<EntityControls>();
	std::vector<KnowledgeId> peers;
	for (const auto &controller : controls.controllers)
		peers.push_back(knowledge.controller_id({id, controller.definition.console, controller.definition.id}));
	phoenix::restore_controllers(
	    controls, saved,
	    [this, id](std::size_t controller, std::size_t channel, OperatingAction action, std::int64_t value,
	               std::span<const std::int64_t> arguments)
	    {
		    return controller_arguments_valid(id, controller, channel, action, value, arguments, true);
	    },
	    peers);
}
} // namespace phoenix::world_detail
