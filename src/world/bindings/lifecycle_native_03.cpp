module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::scenario_spawns() -> void
{
	if (!scenario)
		return;
	for (const auto &request : scenario->pending_spawns())
	{
		if (next_entity < 1 || next_entity > 1000000000ULL || request.entities.size() > 1000000001ULL - next_entity)
		{
			scenario->confirm_spawn(request.id, {}, "entity identity space exhausted");
			continue;
		}
		const auto allocator = next_entity;
		const auto previous_instances = instance_systems;
		const auto previous_stations = explicit_stations;
		auto previous = knowledge;
		const auto previous_participants = conversations.participants;
		std::vector<EntityId> ids;
		try
		{
			for (const auto &p : request.entities)
				if (!definitions.contains(p.definition))
					throw std::runtime_error("Uncompiled dynamic entity");
			for (const auto &p : request.entities)
			{
				const auto id = next_entity++;
				ids.push_back(id);
				create(id, p.definition, {{"x_mm", p.position.x}, {"y_mm", p.position.y}, {"heading_mdeg", p.heading}});
			}
			const auto parents = ids;
			initialise_craft(&ids);
			scenario->confirm_spawn(request.id, parents);
		}
		catch (const std::exception &error)
		{
			for (const auto id : ids)
			{
				if (entities.contains(id))
				{
					entities.at(id).destruct();
					entities.erase(id);
				}
			}
			knowledge = std::move(previous);
			conversations.participants = previous_participants;
			next_entity = allocator;
			instance_systems = previous_instances;
			explicit_stations = previous_stations;
			scenario->confirm_spawn(request.id, {}, error.what());
		}
	}
}

auto Runtime::scenario_publications() -> void
{
	if (!scenario)
		return;
	auto &runtime = scenario->runtime();
	const auto &definition = runtime.definition();
	for (const auto &publication : runtime.pending_publications())
	{
		const auto ship = runtime.resolve_actor(publication.instance, publication.recipients.ship);
		if (!ship || !entities.contains(*ship))
			continue;
		if (publication.objective)
		{
			const auto &objective = runtime.objectives().at(publication.objective);
			const auto &spec = definition.objectives.at(objective.definition);
			const ScenarioRegion region = spec.condition ? ScenarioRegion{} : definition.regions.at(spec.region);
			const auto &schema = knowledge.definition();
			const auto key = schema.schema("phoenix.objective");
			KnowledgeRecordInput value;
			value.schema = key;
			value.producer = schema.role("phoenix.scenario");
			value.subject = objective.id;
			value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Fact};
			const char *names[] = {"pending", "active", "completed", "failed", "cancelled"};
			value.fields = {
			    {schema.field(key, "id"), static_cast<std::int64_t>(objective.id)},
			    {schema.field(key, "status"), std::string(names[static_cast<unsigned>(publication.status)])},
			    {schema.field(key, "title"), spec.title_key},
			    {schema.field(key, "description"), spec.description_key},
			    {schema.field(key, "outcome"), objective.outcome},
			    {schema.field(key, "radius"), region.radius},
			    {schema.field(key, "region"), region.id},
			    {schema.field(key, "credited"),
			     objective.completion_credited || publication.status == ObjectiveStatus::Completed},
			    {schema.field(key, "destination_x"), region.centre.x},
			    {schema.field(key, "destination_y"), region.centre.y}};
			if (spec.condition)
				for (const auto *field : {"radius", "region", "destination_x", "destination_y"})
					value.fields.erase(schema.field(key, field));
			if (publication.recipients.audience == ScenarioAudience::Ship)
				knowledge.publish_ship(*ship, value, publication.tick);
			else
				for (const auto &console : publication.recipients.consoles)
					knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
			if (objective.status == ObjectiveStatus::Completed)
				runtime.acknowledge_credit(objective.id);
		}
		// Custom scenario publications are routed only to their declared recipients.
		else
		{
			try
			{
				const auto &schema = knowledge.definition();
				const auto key = schema.schema(publication.record);
				const auto &contract = schema.schemas.at(key - 1);
				KnowledgeRecordInput value;
				value.schema = key;
				value.producer = schema.role("phoenix.scenario");
				value.subject = *ship;
				value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Claim};
				for (const auto &[name, field] : publication.fields)
					value.fields[schema.field(key, name)] = field;
				if (publication.recipients.audience == ScenarioAudience::Ship)
					knowledge.publish_ship(*ship, value, publication.tick);
				else
					for (const auto &console : publication.recipients.consoles)
						knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
			}
			catch (const std::exception &)
			{
				continue;
			}
		}
		runtime.acknowledge_publication(publication.id);
	}
}

auto Runtime::refresh_control(EntityId ship, bool transitions) -> void
{
	refresh_controllers(ship, !transitions);
}

auto Runtime::prune_displays() -> void
{
	std::erase_if(displays,
	              [&](const auto &entry)
	              {
		              const auto &d = entry.second;
		              return !ownership.claims().contains(d.claimant) ||
		                     (ownership.claims().at(d.claimant) != d.address &&
		                      console_operator(d.address) != d.claimant);
	              });
}

auto Runtime::display_widget(const ConsoleAddress &address, const std::string &id) const -> const Json *
{
	const auto spec = seat(address.ship, address.console);
	if (!spec || !console_layouts.contains(spec->definition))
		return nullptr;
	for (const auto &w : console_layouts.at(spec->definition).at("widgets"))
		if (w.at("id") == id &&
		    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
			return &w;
	return nullptr;
}

auto Runtime::display_command(std::uint64_t claimant, const OperatingCommand &command, bool execute) -> bool
{
	if (command.action != "display.toggle" && command.action != "display.update")
		return false;
	const auto address = command_address(claimant, command);
	if (!ownership.claims().contains(claimant) || command.target != ownership.claims().at(claimant).ship ||
	    (address != ownership.claims().at(claimant) && console_operator(address) != claimant) ||
	    !command.controller.empty() || !command.channel.empty() || command.request_id || command.value ||
	    !display_widget(address, command.system) || command.arguments.size() != 3 || command.arguments[2] < 1)
		return false;
	const auto widget = display_widget(address, command.system);
	if (widget->at("kind") == "radar")
	{
		const auto view = capture_console_observation(address);
		if (!select_system(view.systems, OperatingSystem::Radar, widget->value("system", std::string{}), false))
			return false;
	}
	for (auto arg : command.arguments)
		if (arg < -1000000000 || arg > 1000000000)
			return false;
	const auto found = displays.find(address.ship);
	const bool same = found != displays.end() && found->second.claimant == claimant &&
	                  found->second.address == address && found->second.widget == command.system;
	if (command.action == "display.update")
	{
		if (!same || command.generation != found->second.revision)
			return false;
		if (execute)
			found->second.camera = command.arguments;
	}
	else
	{
		if (command.generation || display_revision >= 9007199254740991ULL)
			return false;
		if (execute)
		{
			++display_revision;
			if (same)
				displays.erase(address.ship);
			else
				displays[address.ship] = {claimant, display_revision, address, command.system, command.arguments};
		}
	}
	return true;
}

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
