module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::meta(std::uint64_t claimant, const OperatingCommand &command, bool execute) -> bool
{
	if (!ownership.claims().contains(claimant) || command.target != ownership.claims().at(claimant).ship)
		return false;
	const auto address = command_address(claimant, command);
	if (address != ownership.claims().at(claimant) && console_operator(address) != claimant)
		return false;
	const auto &console = *seat(address.ship, address.console);
	const auto &spec = definition(address.ship);
	if (departed(address.ship) && command.action != "camera_mode" && command.action != "camera_direction")
		return false;
	if (command.action.starts_with("ftl."))
		return departures.admit(departure_admission_facts(), address, command, execute);
	if (command.action == "flight.impulse")
		return coordinated_impulse(address, command, execute);
	if (command.action.starts_with("away."))
		return away_command(address, command, execute);
	if (command.action.starts_with("dock."))
		return docking_command(address, command, execute);
	if (command.action == "magazine.select" || command.action == "processor.select" || command.action == "store.select")
		return selection_command(address, command, execute);
	if (command.action.starts_with("logistics."))
		return logistics_command(address, command, execute);
	if (command.action.starts_with("team."))
		return team_command(address, command, execute);
	if (command.action.starts_with("craft."))
		return craft_command(address, command, execute);
	if (command.action.starts_with("personnel."))
		return personnel_command(address, command, execute);
	if (command.action == "alert.set" || command.action == "stance.set" || command.action == "objective.priority")
		return bridge_command(address, command, execute);
	if (command.action.starts_with("comms."))
		return comms_command(address, command, execute);
	if (command.action.starts_with("display."))
		return display_command(claimant, command, execute);
	if (command.action == "camera_mode" || command.action == "camera_direction")
	{
		if (!command.system.empty() || !command.controller.empty() || !command.channel.empty() || command.generation ||
		    command.request_id || !command.arguments.empty() || command.value < 0 ||
		    command.value > (command.action == "camera_mode" ? 1 : 7))
			return false;
		const auto &kinds = camera_widgets.at(console.definition);
		if (std::find(kinds.begin(), kinds.end(), command.action) == kinds.end())
			return false;
		if (execute)
		{
			auto camera = ship_camera_state(knowledge.public_board(address.ship));
			if (command.action == "camera_mode")
				camera.mode = static_cast<int>(command.value);
			else
				camera.direction = static_cast<int>(command.value);
			knowledge.set_camera(address.ship, camera, ecs.get<Clock>().tick);
		}
		return true;
	}
	if (entities.at(address.ship).has<EntityControls>() && !command.action.starts_with("message.") &&
	    !command.action.starts_with("publish."))
		return controller_meta(address, command, execute);
	if (command.action.starts_with("publish."))
	{
		if (!command.system.empty() || !command.controller.empty() || !command.channel.empty() || command.generation ||
		    command.request_id || command.value)
			return false;
		const auto name = command.action.substr(8);
		if (std::find(spec.publications.begin(), spec.publications.end(), name) == spec.publications.end())
			return false;
		try
		{
			if (!execute)
			{
				knowledge.activate_publication(address, name, ecs.get<Clock>().tick, false);
				return true;
			}
			auto candidate = knowledge;
			candidate.activate_publication(address, name, ecs.get<Clock>().tick);
			if (execute)
				knowledge = std::move(candidate);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
	if (command.action.starts_with("message."))
	{
		const bool controller_sender = !command.controller.empty();
		if (!controller_sender)
			return false;
		const auto &sender = controller_sender ? command.controller : command.system;
		if (controller_sender)
		{
			if (!entities.at(address.ship).has<EntityControls>() || !command.system.empty())
				return false;
			const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
			if (std::none_of(controllers.begin(), controllers.end(),
			                 [&](const auto &controller)
			                 {
				                 return controller.definition.console == address.console &&
				                        controller.definition.id == sender && controller.human &&
				                        controller.generation == command.generation;
			                 }))
				return false;
		}
		const auto name = command.action.substr(8);
		if (std::find(spec.message_templates.begin(), spec.message_templates.end(), name) ==
		    spec.message_templates.end())
			return false;
		try
		{
			const auto &message = knowledge.message_definition(name);
			KnowledgeRecordInput payload;
			payload.schema = message.schema;
			payload.producer =
			    knowledge.message_producer(address, sender, message.schema, ControlOrigin::Human, controller_sender);
			payload.subject = address.ship;
			payload.provenance = {controller_sender ? knowledge.controller_id({address.ship, address.console, sender})
			                                        : knowledge.system_id(address.ship, sender),
			                      ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0, EvidenceKind::Intent};
			GameplayKnowledgeMessageContext context;
			context.amount = command.value;
			context.flag = command.value != 0;
			context.detail = std::to_string(command.value);
			const auto bound = evaluate_gameplay_message(message, context);
			payload.fields = bound.fields;
			if (!execute)
			{
				knowledge.admit_template(address, sender, name, payload, ControlOrigin::Human, ecs.get<Clock>().tick,
				                         bound.parameters, false, controller_sender);
				return true;
			}
			// Only actual commands need a transactional copy.
			auto candidate = knowledge;
			candidate.admit_template(address, sender, name, payload, ControlOrigin::Human, ecs.get<Clock>().tick,
			                         bound.parameters, true, controller_sender, command.speaker_name,
			                         command.speaker_avatar);
			if (execute)
				knowledge = std::move(candidate);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
	return false;
}
} // namespace phoenix::world_detail
