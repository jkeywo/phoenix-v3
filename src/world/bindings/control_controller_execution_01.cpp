module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::controller_arguments_valid(EntityId ship, std::size_t controller, std::size_t channel,
                                         OperatingAction action, std::int64_t value,
                                         std::span<const std::int64_t> arguments, bool restoring) const -> bool
{
	if (ability_channel_bound(ship, controller, channel))
		return restoring ? process_arguments_shape(arguments)
		                 : ability_generation_valid(ship, controller, channel, arguments);
	if (process_channel_bound(ship, controller, channel))
		return restoring ? process_arguments_shape(arguments)
		                 : process_generation_valid(ship, controller, channel, arguments);
	return operating_arguments_valid(action, value, arguments);
}

auto Runtime::selection_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute) -> bool
{
	if (command.action == "store.select")
	{
		auto selection = command;
		selection.action = "logistics.select";
		return logistics_command(address, selection, execute);
	}
	if (command.action != "magazine.select" && command.action != "processor.select")
		return false;
	const auto ship = entities.at(address.ship);
	if (!command.channel.empty() || command.request_id || !command.arguments.empty() || !command.speaker_name.empty() ||
	    !command.speaker_avatar.empty() || command.text.empty() || command.text.size() > 256 || command.value < 1 ||
	    command.value >= 9007199254740991LL || combat_dead(ship) || !manual_workload(address, "logistics"))
		return false;
	std::string equipment = command.system;
	if (command.action == "processor.select")
	{
		const auto *processors = ship.try_get<ShipProcessors>();
		if (!processors || !processors->values.contains(command.system))
			return false;
		const auto &processor = processors->values.at(command.system);
		equipment = processor.definition().equipment;
		if (static_cast<std::uint64_t>(command.value) != processor.state().selection_revision)
			return false;
	}
	const auto fitted = instance(ship, equipment);
	const auto *controls = ship.try_get<EntityControls>();
	const auto *console = seat(address.ship, address.console);
	if (!fitted || !controls || !console || enabled_system_owner(address.ship, equipment) != address ||
	    std::find(console->feed_systems.begin(), console->feed_systems.end(), equipment) == console->feed_systems.end())
		return false;
	bool authority = false;
	for (const auto &controller : controls->controllers)
		if (controller.definition.console == address.console && controller.definition.id == command.controller &&
		    controller.human && controller.generation == command.generation)
			for (const auto &channel : controller.definition.commands)
				for (const auto consumer : channel.consumers)
					authority |= definition(address.ship).fitted->values.at(consumer).id == equipment;
	if (!authority)
		return false;
	if (command.action == "processor.select")
		return select_processor(ship, command.system, command.text, execute);
	if (fitted->kind != OperatingSystem::Magazine)
		return false;
	const auto &key = fitted->entity.get<CombatMechanism>().get<MagazineState>().stock;
	if (static_cast<std::uint64_t>(command.value) != physical_stock(ship, key).revision())
		return false;
	return select_physical_resource(ship, key, command.text, execute);
}

auto Runtime::clear_controller_settings(EntityId ship, std::size_t controller_index,
                                        std::initializer_list<std::size_t> channels) -> void
{
	auto &controller = entities.at(ship).get_mut<EntityControls>().controllers.at(controller_index);
	for (auto channel : channels)
	{
		const auto &spec = controller.definition.commands.at(channel);
		auto &state = controller.commands.at(channel);
		if (spec.semantics == CommandSemantics::Request)
			throw std::runtime_error("Cannot reset request as a Controller setting");
		if (state.revision == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Controller command revision exhausted");
		state.value = spec.initial;
		state.arguments.clear();
		++state.revision;
	}
	std::erase_if(controller_pending,
	              [&](const auto &p)
	              {
		              return p.address.entity == ship && p.controller == controller_index &&
		                     std::find(channels.begin(), channels.end(), p.channel) != channels.end();
	              });
}

auto Runtime::controller_observations(EntityId id, const ConsoleControllerRuntime &controller,
                                      const Observation *projected, const GameplayKnowledgeProjection *projected_board)
    -> std::vector<state_tree::Value>
{
	using state_tree::Type;
	using state_tree::Value;
	const ConsoleAddress address{id, controller.definition.console};
	const auto &brain = *controller.brain;
	std::vector<Value> values(brain.program.registry.observations.size());
	if (brain.complexity_stars)
	{
		const auto &controls = entities.at(id).get<EntityControls>();
		values.at(*brain.complexity_stars) =
		    Value::integer(controls.layout.profiles.at(controls.profiles.at(address.console)).stars);
	}
	values[brain.own[ControllerOwnField::Entity]] = Value::reference(Type::Entity, id);
	values[brain.own[ControllerOwnField::OwnIdentity]] = Value::integer(id);
	values[brain.own[ControllerOwnField::Console]] = Value::reference(Type::Console, knowledge.console_id(address));
	values[brain.own[ControllerOwnField::Tick]] = Value::integer(ecs.get<Clock>().tick);
	EntityId scan_objective = 0;
	for (const auto &finding : sensor_findings.definitions())
		if (!finding.requires_event.empty() && finding_available(finding) &&
		    !sensor_findings.discoveries().contains({id, finding.target, finding.id}))
		{
			scan_objective = finding.target;
			break;
		}
	values[brain.own[ControllerOwnField::ObjectiveTarget]] = Value::integer(scan_objective);
	const auto &bridge = entities.at(id).get<BridgeBlackboard>();
	values[brain.own[ControllerOwnField::PriorityObjective]] = Value::integer(bridge.priority_objective);
	const auto &bridge_spec = definition(id);
	if (!bridge_spec.bridge.conditions.empty())
	{
		values[brain.own[ControllerOwnField::AlertId]] =
		    Value::string(bridge_spec.bridge.conditions.at(bridge.condition).id);
		values[brain.own[ControllerOwnField::AlertSeverity]] = Value::integer(bridge.condition);
	}
	if (bridge.stances.contains(address.console))
		values[brain.own[ControllerOwnField::Stance]] = Value::string(
		    bridge_spec.console_bridge.at(address.console).stances.at(bridge.stances.at(address.console)).id);
	const auto fallback = projected || decision_frame.observations().contains(address)
	                          ? std::optional<Observation>{}
	                          : capture_decision_observation(address);
	const auto &observation = projected ? *projected : fallback ? *fallback : decision_frame.observations().at(address);
	if (brain.logistics_revision)
		values.at(*brain.logistics_revision) = Value::integer(observation.gameplay.at("logistics").at("revision"));
	if (brain.docking_revision)
		values.at(*brain.docking_revision) = Value::integer(observation.gameplay.at("docking").at("revision"));
	append_controller_execution_facts(id, controller, entities.at(id).get<EntityControls>(), observation,
	                                  definition(id).abilities.controls, definition(id).processes.controls, values);
	values[brain.own[ControllerOwnField::X]] = Value::integer(observation.own.x);
	values[brain.own[ControllerOwnField::Y]] = Value::integer(observation.own.y);
	values[brain.own[ControllerOwnField::Vx]] = Value::integer(observation.own.vx);
	values[brain.own[ControllerOwnField::Vy]] = Value::integer(observation.own.vy);
	values[brain.own[ControllerOwnField::Heading]] = Value::integer(observation.own.heading);
	if (const auto *g = entities.at(id).try_get<CraftGuidance>())
	{
		values[brain.own[ControllerOwnField::CraftX]] = Value::integer(g->destination.x);
		values[brain.own[ControllerOwnField::CraftY]] = Value::integer(g->destination.y);
		values[brain.own[ControllerOwnField::CraftAltitude]] = Value::integer(g->altitude);
		values[brain.own[ControllerOwnField::CraftSpeed]] = Value::integer(g->speed);
		values[brain.own[ControllerOwnField::CraftTarget]] = Value::integer(g->target);
		values[brain.own[ControllerOwnField::CraftNavigate]] = Value::boolean(g->navigate);
		values[brain.own[ControllerOwnField::CraftHeading]] = Value::integer(g->heading);
	}
	values[brain.own[ControllerOwnField::CraftOrder]] = Value::string("");
	values[brain.own[ControllerOwnField::CraftReserveLow]] = Value::boolean(false);
	values[brain.own[ControllerOwnField::CraftArmed]] = Value::boolean(false);
	values[brain.own[ControllerOwnField::CraftTargetPresent]] = Value::boolean(false);
	if (const auto *s = entities.at(id).try_get<CraftState>())
	{
		values[brain.own[ControllerOwnField::CraftOrder]] = Value::string(s->order);
		values[brain.own[ControllerOwnField::CraftTargetPresent]] = Value::boolean(
		    std::any_of(observation.contacts.begin(), observation.contacts.end(),
		                [&](const auto &c)
		                {
			                return c.id == s->target && (s->order != "bomb" || c.disposition == "hostile");
		                }));
		if (const auto *p = entities.at(id).try_get<ShipResources>())
		{
			std::int64_t energy = 0, capacity = 0;
			for (std::size_t i = 0; i < p->network.stores().size(); ++i)
			{
				energy += p->network.stores()[i].energy();
				capacity += p->definition.network.stores[i].capacity;
			}
			values[brain.own[ControllerOwnField::CraftReserveLow]] =
			    Value::boolean(capacity && energy * 1000 <= capacity * definition(id).craft->return_reserve);
		}
		bool armed = false;
		for (const auto &weapon : combat_observation(observation).at("weapons"))
		{
			const auto child = away_equipment(id, weapon.at("id"));
			const auto &m = child.get<CombatMechanism>();
			if (!child.get<Equipment>().enabled || structurally_disabled(entities.at(id), m.definition.id) ||
			    weapon_payload(entities.at(id), m).durability <= 0)
				continue;
			for (const auto &option : weapon.at("ammunition_options"))
				armed |= option.at("available").get<bool>();
		}
		values[brain.own[ControllerOwnField::CraftArmed]] = Value::boolean(armed);
	}
	if (brain.own[ControllerOwnField::FtlReady])
	{
		bool ready = false;
		auto [completed, visitors] = ftl_local_facts(id);
		for (auto child : entities.at(id).get<EquipmentIndex>().values)
			if (child.has<FtlReserve>() && child.get<Equipment>().id == brain.ftl_system &&
			    ftl_owner(id, child.get<Equipment>().id) == address)
				ready = ftl_blocker(id, child).empty();
		values[brain.own[ControllerOwnField::FtlReady]] = Value::boolean(ready);
		values[brain.own[ControllerOwnField::Visitors]] = Value::integer(visitors);
		values[brain.own[ControllerOwnField::LocalWorkComplete]] = Value::boolean(completed);
	}
	const auto &navigation = bridge.navigation;
	values[brain.own[ControllerOwnField::Blocked]] = Value::boolean(navigation.blocked);
	values[brain.own[ControllerOwnField::LeftHeading]] = Value::integer(navigation.left_heading);
	values[brain.own[ControllerOwnField::LateralAvoidance]] = Value::integer(navigation.lateral_thrust);
	values[brain.own[ControllerOwnField::LateralSafe]] = Value::boolean(navigation.lateral_safe);
	const auto fallback_board = projected_board || decision_frame.knowledge().contains(address)
	                                ? std::optional<GameplayKnowledgeProjection>{}
	                                : knowledge.project(address);
	const auto &board = projected_board  ? *projected_board
	                    : fallback_board ? *fallback_board
	                                     : decision_frame.knowledge().at(address);
	const ControllerAddress recipient{id, controller.definition.console, controller.definition.id};
	const auto fallback_inbox = decision_frame.inboxes().contains(recipient)
	                                ? std::optional<std::vector<CoordinationDelivery>>{}
	                                : knowledge.peek_controller(recipient);
	const auto &inbox = fallback_inbox ? *fallback_inbox : decision_frame.inboxes().at(recipient);
	const ControllerObservationSources bindings{id,
	                                            ecs.get<Clock>().tick,
	                                            bridge.priority_objective,
	                                            entities.at(id).get<EntityControls>(),
	                                            observation,
	                                            board,
	                                            decision_frame,
	                                            knowledge.definition(),
	                                            inbox,
	                                            [this](const KnowledgeRecord &record, KnowledgeTick tick)
	                                            {
		                                            return knowledge.stale(record, tick);
	                                            },
	                                            [this](KnowledgeId key)
	                                            {
		                                            return knowledge.request_key_name(key);
	                                            }};
	append_controller_observations(controller, bindings, values);
	controller_decision_facts(controller, observation, values);
	controller_coordination_facts(controller, observation, values);
	return values;
}

auto Runtime::controller_output(EntityId id, std::size_t index, const state_tree::Output &output,
                                std::span<const state_tree::Value> observations, const state_tree::Program *execution)
    -> void
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	const auto &action = controller.brain->program.registry.actions.at(output.action);
	if (action.name.starts_with("select.") || action.name.starts_with("replace."))
	{
		const bool replace = action.name.starts_with("replace.");
		auto invocation = output.arguments.at(0).text + "/" + std::to_string(output.activation) +
		                  (replace ? "/replace/" + std::to_string(output.task) : std::string{});
		for (std::size_t i = 0; i < output.call_path.size(); ++i)
			invocation +=
			    "/" + std::to_string(output.call_path[i]) + ":" + std::to_string(output.call_activations.at(i));
		controller.result.available = true;
		controller.result.success =
		    admit_controller_decision({{id, controller.definition.console, controller.definition.id},
		                               controller.generation,
		                               "select",
		                               action.name.substr(replace ? 8 : 7),
		                               invocation,
		                               {output.arguments.begin() + 1, output.arguments.end()}},
		                              observations);
		controller.result.reason = controller.result.success ? "" : "action_unavailable";
		return;
	}
	if (action.name == "action.cancel")
	{
		cancel_controller_action(id, index, output.arguments.at(0).text, observations);
		return;
	}
	if (action.name == "action.succeed" || action.name == "action.fail")
	{
		if (!execution)
			throw std::runtime_error("Only an execution tree can complete a Controller action");
		controller.action.status = action.name == "action.succeed" ? "succeeded" : "failed";
		controller.action.reason = output.arguments.at(0).text;
		return;
	}
	if (action.effect &&
	    !admit_effect(controller.effects, output,
	                  execution ? "action/" + std::to_string(controller.action.serial) + "/" : std::string{}))
		return;
	if (!action.name.starts_with("write."))
	{
		controller.result = {};
		controller.result.available = true;
		controller.result.success = false;
		controller.result.reason.clear();
		try
		{
			if (action.name.starts_with("coordination."))
			{
				controller.result.success =
				    admit_controller_decision({{id, controller.definition.console, controller.definition.id},
				                               controller.generation,
				                               action.name,
				                               {},
				                               {},
				                               output.arguments},
				                              observations);
				return;
			}
			const ControllerAddress address{id, controller.definition.console, controller.definition.id};

			const ConsoleAddress source{id, controller.definition.console};
			if (action.name == "ftl.depart")
			{
				OperatingCommand command{id, "ftl.depart", 0, output.arguments.at(0).text, controller.definition.id,
				                         {}};
				command.generation = controller.generation;
				if (!departures.admit(departure_admission_facts(), source, command, true, true))
					throw std::runtime_error("FTL departure refused");
			}
			else if (action.name.starts_with("logistics.") || action.name.starts_with("dock."))
			{
				OperatingCommand command{id, action.name, output.arguments.at(2).number, output.arguments.at(0).text};
				command.text = output.arguments.at(1).text;
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				const bool accepted = action.name.starts_with("logistics.")
				                          ? logistics_command(source, command, true, true)
				                          : docking_command(source, command, true, true);
				if (!accepted)
					throw std::runtime_error("Supply/docking command refused");
			}
			else if (action.name.starts_with("team."))
			{
				OperatingCommand command{id, action.name, output.arguments.at(2).number, output.arguments.at(0).text};
				command.text = output.arguments.at(1).text;
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				team_command(source, command, true, true);
			}
			else if (action.name == "craft.return_home")
			{
				if (controller.human || !entities.at(id).has<CraftState>() ||
				    std::none_of(controller.definition.commands.begin(), controller.definition.commands.end(),
				                 [](const auto &command)
				                 {
					                 return command.action == OperatingAction::Speed;
				                 }))
					throw std::runtime_error("Craft return requires its flight Controller");
				auto e = entities.at(id);
				const auto &s = e.get<CraftState>();
				if (s.order != "return" && s.order != "pickup")
				{
					if (s.mission.empty())
						craft_return(e);
					else
						request_craft_pickup(id);
				}
			}
			else if (action.name.starts_with("away.") || action.name.starts_with("craft."))
			{
				OperatingCommand command{id, action.name, output.arguments.at(2).number, output.arguments.at(0).text};
				command.text = output.arguments.at(1).text;
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				if (!(action.name.starts_with("craft.") ? craft_command(source, command, true, true)
				                                        : away_command(source, command, true, true)))
					throw std::runtime_error("Away command refused");
			}
			else if (action.name.starts_with("publish."))
			{
				controller.result.id =
				    knowledge.activate_publication(source, action.name.substr(8), ecs.get<Clock>().tick);
				controller.result.publication = controller.result.id;
			}
			else if (action.name == "stop_publication")
				knowledge.stop_publication(source, output.arguments.at(0).number);
			else if (action.name == "withdraw_request")
				knowledge.withdraw_request(source, controller.definition.id,
				                           knowledge.request_key(output.arguments.at(0).text), ControlOrigin::AI, true);
			else if (action.name == "acknowledge_message")
				knowledge.acknowledge_controller(address, output.arguments.at(0).number);
			else if (action.name.starts_with("send."))
			{
				const auto name = action.name.substr(5);
				const auto &message = knowledge.message_definition(name);
				const ConsoleAddress source{id, controller.definition.console};
				KnowledgeRecordInput payload;
				payload.schema = message.schema;
				payload.producer = knowledge.message_producer(source, controller.definition.id, message.schema,
				                                              ControlOrigin::AI, true);
				payload.subject = id;
				payload.provenance = {knowledge.controller_id(address), ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0,
				                      EvidenceKind::Intent};
				GameplayKnowledgeMessageContext context;
				context.amount = output.arguments.at(0).number;
				context.flag = output.arguments.at(1).number != 0;
				context.detail = output.arguments.at(2).text;
				context.program = execution ? execution : &controller.brain->program;
				context.observations = observations;
				context.parameters = &output.captured_parameters;
				context.memory = &output.captured_memory;
				context.captured_intents = &output.captured_intents;
				context.elapsed = output.captured_elapsed;
				const auto bound = evaluate_gameplay_message(message, context);
				payload.fields = bound.fields;
				const auto speaker = officer_speaker(id, controller.definition.console, controller.definition.id);
				controller.result.id = knowledge.admit_template(
				    source, controller.definition.id, name, payload, ControlOrigin::AI, ecs.get<Clock>().tick,
				    bound.parameters, true, true, speaker.first, {}, speaker.second);
				controller.result.message = controller.result.id;
			}
			else
				throw std::runtime_error("unknown Controller message action");
			controller.result.success = true;
		}
		catch (const std::exception &error)
		{
			controller.result.reason = error.what();
		}
		return;
	}
	const auto field = controller.brain->commands.at(output.action).channel_index;
	const auto request = controller.definition.commands[field].semantics == CommandSemantics::Request;
	const auto request_id = request ? controller.commands[field].request_id + 1 : 0;
	controller.result = {};
	controller.result.available = true;
	std::vector<std::int64_t> arguments;
	for (std::size_t i = 1; i < output.arguments.size(); ++i)
		arguments.push_back(output.arguments[i].number);
	controller.result.success = commit_controller({{id, controller.definition.console, controller.definition.id},
	                                               index,
	                                               field,
	                                               output.arguments.at(0).number,
	                                               controller.generation,
	                                               request_id,
	                                               false,
	                                               arguments});
	controller.result.reason = controller.result.success ? "" : "Controller command refused";
	if (execution && controller.result.success)
		controller.action_writes.insert(field);
	if (execution && !controller.result.success && controller.action.status == "running")
	{
		controller.action.status = "failed";
		controller.action.reason = "command_refused";
	}
}

auto Runtime::controller_proposal(const ConsoleAddress &address, OperatingCommand command, bool human) const
    -> std::optional<ControllerProposal>
{
	const auto entity = entities.at(address.ship);
	const auto &controls = entity.get<EntityControls>();
	if (controls.layout.converted && command.controller.empty() && command.channel.empty() && !command.generation &&
	    !command.request_id)
	{
		// Compatibility ends here: queued work still carries an owned channel and operator generation.
		const ConsoleControllerRuntime *owner = nullptr;
		const ControllerCommandSpec *selected = nullptr;
		for (const auto &controller : controls.controllers)
			if (controller.definition.console == address.console)
				for (const auto &field : controller.definition.commands)
					if (operating_action(field.action).name == command.action &&
					    (command.system.empty() || std::any_of(field.consumers.begin(), field.consumers.end(),
					                                           [&](auto i)
					                                           {
						                                           return (*fittings(entity)).values[i].id ==
						                                                  command.system;
					                                           })))
					{
						if (selected)
							return {};
						owner = &controller;
						selected = &field;
					}
		if (!selected || selected->semantics == CommandSemantics::Request)
			return {};
		command.controller = owner->definition.id;
		command.channel = selected->id;
		command.generation = owner->generation;
		command.system.clear();
	}
	if (!command.system.empty())
		return {};
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
			if (human && station_stars(address) < spec.individual_stars)
				return {};
			if (spec.action == OperatingAction::RepairSystem && !spec.hull_priority && entity.has<CombatStructure>() &&
			    command.value ==
			        static_cast<std::int64_t>(entity.get<CombatStructure>().definition.equipment_compartments.size()) +
			            1)
				return {};
			if (!command.action.empty() && command.action != operating_action(spec.action).name)
				return {};
			if (command.value < spec.minimum || command.value > spec.maximum ||
			    (!spec.choices.empty() && !spec.choices.contains(command.value)) ||
			    !controller_arguments_valid(address.ship, i, field, spec.action, command.value, command.arguments))
				return {};
			if (!ability_command_valid(address.ship, i, field, command) ||
			    !process_command_valid(address.ship, i, field, command))
				return {};
			// Capture the current grant for ordinary Controller outputs too. A
			// queued command cannot jump to a replacement grant after revocation.
			if (command.arguments.empty())
				for (const auto &binding : definition(address.ship).abilities.controls)
					if (binding.controller == i && (binding.enabled == field || binding.target == field))
						if (const auto *grant = ability_control_grant(address.ship, binding))
							command.arguments = {static_cast<std::int64_t>(grant->generation)};
			if (command.arguments.empty())
				for (const auto &binding : definition(address.ship).processes.controls)
					if (binding.controller == i && binding.enabled == field)
						if (const auto *process = process_control_grant(address.ship, binding))
							command.arguments = {static_cast<std::int64_t>(process->generation)};
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
					                       (pending.request_id != command.request_id ||
					                        pending.value != command.value || pending.arguments != command.arguments);
				                }) ||
				    !command.request_id || command.request_id < state.request_id ||
				    (command.request_id == state.request_id &&
				     (command.value != state.value || command.arguments != state.arguments)))
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
			                          human,
			                          command.arguments};
		}
	}
	return {};
}

auto Runtime::controller_command_effect_matches(flecs::entity entity, const ControllerCommandSpec &spec,
                                                const ControllerProposal &proposal) const -> bool
{
	// Navigation can change assistance as a physical side effect. Reasserting
	// the owned mode setting must still take control back from that guidance.
	bool same_effect = true;
	if (spec.action == OperatingAction::DirectedOperatingMode ||
	    spec.action == OperatingAction::DirectedTargetingMode || spec.action == OperatingAction::DirectedTarget ||
	    spec.action == OperatingAction::SensorTarget)
		for (const auto consumer : spec.consumers)
		{
			const auto child = entity.get<EquipmentIndex>().values.at(consumer);
			if (const auto *directed = child.try_get<DirectedRuntime>())
			{
				const auto actual = spec.action == OperatingAction::DirectedOperatingMode   ? directed->operating_mode
				                    : spec.action == OperatingAction::DirectedTargetingMode ? directed->targeting_mode
				                                                                            : directed->target;
				same_effect &= actual == static_cast<std::uint64_t>(proposal.value);
			}
		}
	if (spec.action == OperatingAction::Assisted)
		for (const auto consumer : spec.consumers)
		{
			const auto child = entity.get<EquipmentIndex>().values.at(consumer);
			if (const auto *flight = child.try_get<FlightRuntime>();
			    flight && flight->state.assisted != (proposal.value != 0))
				same_effect = false;
		}
	// Command values can be stale after another authorized target adapter runs.
	if (spec.action == OperatingAction::WeaponTarget || spec.action == OperatingAction::SensorTarget ||
	    spec.action == OperatingAction::DirectedTarget)
		for (const auto consumer : spec.consumers)
		{
			const auto child = entity.get<EquipmentIndex>().values.at(consumer);
			if (const auto *directed = child.try_get<DirectedRuntime>();
			    directed && directed->target != static_cast<EntityId>(proposal.value))
				same_effect = false;
		}
	if (spec.action == OperatingAction::RepairCompartment || spec.action == OperatingAction::RepairSystem)
		for (const auto consumer : spec.consumers)
		{
			const auto &team = entity.get<EquipmentIndex>().values.at(consumer).get<CombatMechanism>().get<TeamState>();
			if (spec.action == OperatingAction::RepairCompartment)
			{
				const auto system = entity.get<EquipmentIndex>().values.at(consumer).get<Equipment>().id;
				const bool assigned = proposal.value == 0 &&
				                      manual_team_destination(enabled_system_owner(proposal.address.entity, system));
				same_effect &= assigned ? team.compartment_assignment && !team.repair_auto
				                        : team.destination == proposal.value - 1;
			}
			else
			{
				const auto hull =
				    static_cast<std::int64_t>(entity.get<CombatStructure>().definition.equipment_compartments.size()) +
				    1;
				same_effect &= team.repair_system == (proposal.value == hull ? -2 : proposal.value - 1);
			}
		}

	return same_effect;
}

auto Runtime::commit_controller(const ControllerProposal &proposal) -> bool
{
	if (!entities.contains(proposal.address.entity))
		return false;
	const auto entity = entities.at(proposal.address.entity);
	const ControllerCommandChecks checks{
	    [this](const ControllerProposal &value)
	    {
		    return ability_channel_valid(value.address.entity, value.controller, value.channel, value.value,
		                                 value.request_id) &&
		           process_channel_valid(value.address.entity, value.controller, value.channel, value.value);
	    },
	    [this, id = proposal.address.entity](std::size_t controller, std::size_t channel, OperatingAction action,
	                                         std::int64_t value, std::span<const std::int64_t> arguments)
	    {
		    return controller_arguments_valid(id, controller, channel, action, value, arguments);
	    },
	    [this, entity](const ControllerCommandSpec &spec, const ControllerProposal &value)
	    {
		    return controller_command_effect_matches(entity, spec, value);
	    }};
	return commit_controller_command(entity.get_mut<EntityControls>(), proposal, checks);
}

auto Runtime::refresh_controllers(EntityId id, bool validate) -> void
{
	auto entity = entities.at(id);
	auto &controls = entity.get_mut<EntityControls>();
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		const ConsoleAddress address{id, controller.definition.console};
		const auto claimant = console_operator(address);
		const bool present = claimant != 0;
		const bool human = present && controls.layout.profiles.at(controls.profiles.at(address.console)).human.at(i);
		const ControllerHandoverPorts ports{
		    [this, id, &controller]
		    {
			    return controller_observations(id, controller);
		    },
		    [this, id, i](std::span<const state_tree::Value> observations)
		    {
			    cancel_controller_action(id, i, "operator_changed", observations);
		    },
		    [this, id, i](const state_tree::Output &output, std::span<const state_tree::Value> observations)
		    {
			    controller_output(id, i, output, observations);
		    },
		    [this, id, &controller](const OfficerRequest &request)
		    {
			    officer_message(id, controller, request, true);
		    },
		    [entity, console = address.console]() mutable
		    {
			    advance_controller_stance_epoch(entity.get_mut<BridgeBlackboard>(), console);
		    },
		    [this, id, address, &controller](bool current_present, bool current_human)
		    {
			    knowledge.set_controller_control({id, address.console, controller.definition.id}, current_present,
			                                     current_human);
		    }};
		refresh_controller_operator(controller, claimant, human, human && !validate ? station_stars(address) : 0,
		                            validate, ports);
	}
	// Presence also matters for readers and human-seeking offers on Consoles
	// that own no command Controller. Update after outgoing AI cleanup.
	for (const auto &console : definition(id).consoles)
	{
		const ConsoleAddress address{id, console.id};
		const bool present = console_operator(address) != 0;
		knowledge.set_console_presence(address, present);
	}
}

auto Runtime::coordinated_generation(const ConsoleAddress &a, const std::string &group) const -> std::uint64_t
{
	std::uint64_t generation = 0;
	for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
		if (controller.definition.console == a.console)
			for (const auto &command : controller.definition.commands)
				if (!command.coordinated_group.empty() && command.coordinated_group == group)
					generation += controller.generation;
	return generation;
}
} // namespace phoenix::world_detail
