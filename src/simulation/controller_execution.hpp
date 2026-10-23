// Reset only the selected settings; a shared Controller can serve other duties.
void clear_controller_settings(EntityId ship, std::size_t controller_index, std::initializer_list<std::size_t> channels)
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
// World::Impl methods. Physical mutation is confined to command consumption below.
std::vector<state_tree::Value> controller_observations(EntityId id, const ConsoleControllerRuntime &controller)
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
	values[brain.own[ControllerOwnField::Console]] = Value::reference(Type::Console, knowledge.console_id(address));
	values[brain.own[ControllerOwnField::Tick]] = Value::integer(ecs.get<Clock>().tick);
	EntityId scan_objective = 0;
	for (const auto &finding : sensor_findings)
		if (!finding.requires_event.empty() && finding_available(finding) &&
		    !sensor_discoveries.contains({id, finding.target, finding.id}))
		{
			scan_objective = finding.target;
			break;
		}
	values[brain.own[ControllerOwnField::ObjectiveTarget]] = Value::integer(scan_objective);
	const auto &bridge = entities.at(id).get<BridgeBlackboard>();
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
	const auto fallback =
	    observation_snapshots.contains(address) ? std::optional<Observation>{} : capture_console_observation(address);
	const auto &observation = fallback ? *fallback : observation_snapshots.at(address);
	if (brain.logistics_revision)
		values.at(*brain.logistics_revision) = Value::integer(observation.gameplay.at("logistics").at("revision"));
	if (brain.docking_revision)
		values.at(*brain.docking_revision) = Value::integer(observation.gameplay.at("docking").at("revision"));
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
				energy += p->network.stores()[i].energy;
				capacity += p->definition.network.stores[i].capacity;
			}
			values[brain.own[ControllerOwnField::CraftReserveLow]] =
			    Value::boolean(capacity && energy * 1000 <= capacity * definition(id).craft->return_reserve);
		}
		bool armed = false;
		for (const auto &weapon : observation.gameplay.value("combat", Json::object()).value("weapons", Json::array()))
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
	for (std::size_t i = 0; i < brain.commands.size(); ++i)
	{
		const auto field = brain.commands[i].channel_index;
		const auto &controllers = entities.at(id).get<EntityControls>().controllers;
		const auto owner = static_cast<std::size_t>(&controller - controllers.data());
		const auto amount = command_snapshots.contains(id) ? command_snapshots.at(id).at(owner).at(field).value
		                                                   : controller.commands[field].value;
		values[brain.commands[i].destination] = controller.definition.commands[field].type == KnowledgeType::Boolean
		                                            ? Value::boolean(amount != 0)
		                                            : Value::integer(amount);
	}
	const auto board = brain_snapshots.contains(address) ? brain_snapshots.at(address) : knowledge.project(address);
	std::vector<const KnowledgeRecord *> evidence(brain.reads.size(), nullptr);
	for (std::size_t i = 0; i < brain.reads.size(); ++i)
	{
		const auto &binding = brain.reads[i];
		const auto &records = binding.shared ? board.shared : board.records;
		KnowledgeId subject = binding.subject ? binding.subject : id;
		if (binding.latest)
		{
			const auto latest = std::find_if(records.rbegin(), records.rend(),
			                                 [&](const auto &record)
			                                 {
				                                 return record.value.schema == binding.schema;
			                                 });
			subject = latest == records.rend() ? 0 : latest->value.subject;
		}
		std::set<std::pair<KnowledgeId, KnowledgeId>> seen;
		for (auto record = records.rbegin(); record != records.rend(); ++record)
			if (record->value.schema == binding.schema && record->value.subject == subject &&
			    seen.emplace(record->value.provenance.source, record->publication).second)
			{
				if (!evidence[i])
					evidence[i] = &*record;
				if (record->value.withdrawn || knowledge.stale(*record, ecs.get<Clock>().tick) ||
				    !record->value.fields.contains(binding.field))
					continue;
				evidence[i] = &*record;
				const auto &value = record->value.fields.at(binding.field);
				auto &target = values[binding.destination];
				if (auto n = std::get_if<std::int64_t>(&value))
					target = Value::integer(*n);
				else if (auto b = std::get_if<bool>(&value))
					target = Value::boolean(*b);
				else
					target = Value::string(std::get<std::string>(value));
				break;
			}
	}
	for (std::size_t i = 0; i < brain.telemetry_reads.size(); ++i)
	{
		const auto &binding = brain.telemetry_reads[i].source;
		for (const auto &system : observation.systems)
			if (system.id == binding.system && system.telemetry.contains(binding.field))
			{
				const auto number = system.telemetry.at(binding.field);
				values[brain.telemetry_reads[i].destination] =
				    binding.type == Type::Boolean ? Value::boolean(number != 0) : Value::integer(number);
				break;
			}
	}
	for (std::size_t i = 0; i < brain.command_reads.size(); ++i)
	{
		const auto &read = brain.command_reads[i];
		const auto target = read.controller, channel = read.channel;
		const auto &source = entities.at(id).get<EntityControls>().controllers.at(target);
		const auto number = command_snapshots.contains(id) ? command_snapshots.at(id).at(target).at(channel).value
		                                                   : source.commands.at(channel).value;
		values[read.destination] = source.definition.commands.at(channel).type == KnowledgeType::Boolean
		                               ? Value::boolean(number != 0)
		                               : Value::integer(number);
	}
	const ControllerAddress recipient{id, controller.definition.console, controller.definition.id};
	const auto inbox = controller_inboxes.contains(recipient) ? controller_inboxes.at(recipient)
	                                                          : knowledge.peek_controller(recipient);
	values[brain.inbox[ControllerInboxField::Available]] = Value::boolean(!inbox.empty());
	if (!inbox.empty())
	{
		const auto &delivery = inbox.front();
		const auto &message = delivery.message;
		const auto &payload = message.value.payload;
		values[brain.inbox[ControllerInboxField::Id]] = Value::reference(Type::Message, message.id);
		values[brain.inbox[ControllerInboxField::Source]] = Value::integer(message.value.sender_system);
		values[brain.inbox[ControllerInboxField::SourceConsole]] =
		    Value::reference(Type::Console, message.sender_console);
		values[brain.inbox[ControllerInboxField::Schema]] =
		    Value::string(knowledge.definition().schemas.at(payload.schema - 1).name);
		values[brain.inbox[ControllerInboxField::Subject]] = Value::reference(Type::Entity, payload.subject);
		values[brain.inbox[ControllerInboxField::ObservedTick]] = Value::integer(payload.provenance.observed_tick);
		values[brain.inbox[ControllerInboxField::DeliveredTick]] = Value::integer(delivery.delivered_tick);
		values[brain.inbox[ControllerInboxField::Uncertainty]] = Value::integer(payload.provenance.uncertainty);
		values[brain.inbox[ControllerInboxField::Kind]] =
		    Value::integer(static_cast<unsigned>(payload.provenance.kind));
		values[brain.inbox[ControllerInboxField::Urgency]] = Value::integer(message.value.presentation.urgency);
		values[brain.inbox[ControllerInboxField::RequestKey]] =
		    Value::string(knowledge.request_key_name(message.value.request_key));
		for (std::size_t i = 0; i < brain.message_reads.size(); ++i)
		{
			const auto &binding = brain.message_reads[i];
			if (binding.schema != payload.schema || !payload.fields.contains(binding.field))
				continue;
			const auto &field = payload.fields.at(binding.field);
			auto &target = values[binding.destination];
			if (const auto number = std::get_if<std::int64_t>(&field))
				target = Value::integer(*number);
			else if (const auto flag = std::get_if<bool>(&field))
				target = Value::boolean(*flag);
			else
				target = Value::string(std::get<std::string>(field));
		}
	}
	const auto &controllers = entities.at(id).get<EntityControls>().controllers;
	const auto owner = static_cast<std::size_t>(&controller - controllers.data());
	for (const auto &binding : brain.commands)
	{
		const auto field = binding.channel_index;
		const auto &state =
		    command_snapshots.contains(id) ? command_snapshots.at(id).at(owner).at(field) : controller.commands[field];
		for (std::size_t i = 0; i < binding.arguments.size(); ++i)
			values.at(binding.arguments[i]) = Value::integer(state.arguments.at(i));
	}
	for (std::size_t i = 0; i < evidence.size(); ++i)
	{
		const auto *record = evidence[i];
		const auto &fields = brain.reads[i].metadata;
		values.at(fields[ControllerRecordField::Available]) =
		    Value::boolean(record && !record->value.withdrawn && !knowledge.stale(*record, ecs.get<Clock>().tick) &&
		                   record->value.fields.contains(brain.reads[i].field));
		if (!record)
			continue;
		values.at(fields[ControllerRecordField::ObservedTick]) = Value::integer(record->value.provenance.observed_tick);
		values.at(fields[ControllerRecordField::ReceivedTick]) = Value::integer(record->received_tick);
		values.at(fields[ControllerRecordField::Source]) = Value::integer(record->value.provenance.source);
		values.at(fields[ControllerRecordField::Uncertainty]) = Value::integer(record->value.provenance.uncertainty);
		values.at(fields[ControllerRecordField::Kind]) =
		    Value::integer(static_cast<unsigned>(record->value.provenance.kind));
		values.at(fields[ControllerRecordField::Withdrawn]) = Value::boolean(record->value.withdrawn);
		values.at(fields[ControllerRecordField::Stale]) =
		    Value::boolean(knowledge.stale(*record, ecs.get<Clock>().tick));
		values.at(fields[ControllerRecordField::Revision]) = Value::integer(record->value.provenance.revision);
		values.at(fields[ControllerRecordField::Subject]) = Value::integer(record->value.subject);
	}
	for (const auto &binding : brain.commands)
	{
		const auto field = binding.channel_index;
		const auto &state =
		    command_snapshots.contains(id) ? command_snapshots.at(id).at(owner).at(field) : controller.commands[field];
		values.at(binding.metadata[ControllerCommandField::Accepted]) = Value::boolean(state.accepted);
		values.at(binding.metadata[ControllerCommandField::Revision]) = Value::integer(state.revision);
		values.at(binding.metadata[ControllerCommandField::Consumed]) = Value::integer(state.consumed);
		values.at(binding.metadata[ControllerCommandField::RequestId]) = Value::integer(state.request_id);
		values.at(binding.metadata[ControllerCommandField::Pending]) = Value::boolean(state.consumed < state.revision);
	}
	for (const auto &binding : brain.contact_reads)
	{
		const auto target = binding.controller, channel = binding.channel;
		const auto &source = controllers.at(target);
		const auto &state = command_snapshots.contains(id) ? command_snapshots.at(id).at(target).at(channel)
		                                                   : source.commands.at(channel);
		const auto selected = source.definition.commands.at(channel).action == OperatingAction::FollowContact
		                          ? state.arguments.at(0)
		                          : state.value;
		const auto contact = std::find_if(observation.contacts.begin(), observation.contacts.end(),
		                                  [&](const auto &candidate)
		                                  {
			                                  return candidate.id == static_cast<EntityId>(selected);
		                                  });
		const auto &fields = binding.fields;
		values.at(fields[ControllerContactField::Available]) = Value::boolean(contact != observation.contacts.end());
		const auto bearing = std::find_if(observation.bearing_contacts.begin(), observation.bearing_contacts.end(),
		                                  [&](const auto &b)
		                                  {
			                                  return b.id == static_cast<EntityId>(selected);
		                                  });
		values.at(fields[ControllerContactField::BearingAvailable]) =
		    Value::boolean(bearing != observation.bearing_contacts.end());
		if (bearing != observation.bearing_contacts.end())
		{
			values.at(fields[ControllerContactField::Id]) = Value::integer(bearing->id);
			values.at(fields[ControllerContactField::BearingMdeg]) = Value::integer(bearing->bearing_mdeg);
			values.at(fields[ControllerContactField::ObservedTick]) = Value::integer(bearing->observed_tick);
		}
		if (contact == observation.contacts.end())
			continue;
		values.at(fields[ControllerContactField::Id]) = Value::integer(contact->id);
		values.at(fields[ControllerContactField::X]) = Value::integer(contact->x);
		values.at(fields[ControllerContactField::Y]) = Value::integer(contact->y);
		values.at(fields[ControllerContactField::Vx]) = Value::integer(contact->vx);
		values.at(fields[ControllerContactField::Vy]) = Value::integer(contact->vy);
		values.at(fields[ControllerContactField::Heading]) = Value::integer(contact->heading);
		values.at(fields[ControllerContactField::Speed]) = Value::integer(fixed::length({contact->vx, contact->vy}));
		values.at(fields[ControllerContactField::ObservedTick]) = Value::integer(observation.observed_tick);
		values.at(fields[ControllerContactField::Uncertainty]) = Value::integer(0);
	}

	const auto &result = controller.result;
	values[brain.result[ControllerResultField::Available]] = Value::boolean(result.available);
	values[brain.result[ControllerResultField::Success]] = Value::boolean(result.success);
	values[brain.result[ControllerResultField::Id]] = Value::integer(result.id);
	values[brain.result[ControllerResultField::Publication]] = Value::integer(result.publication);
	values[brain.result[ControllerResultField::Message]] =
	    result.message ? Value::reference(Type::Message, result.message) : Value{};
	values[brain.result[ControllerResultField::Reason]] = Value::string(result.reason);
	values.at(brain.action_base) = Value::string(controller.action.status);
	values.at(brain.action_base + 1) = Value::string(controller.action.reason);
	values.at(brain.action_base + 2) = Value::string(controller.action.id);
	controller_decision_facts(controller, observation, values);
	controller_coordination_facts(controller, observation, values);
	return values;
}
void controller_output(EntityId id, std::size_t index, const state_tree::Output &output,
                       std::span<const state_tree::Value> observations, const state_tree::Program *execution = nullptr)
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
				if (!ftl_command(source, command, true, true))
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
std::optional<ControllerProposal> controller_proposal(const ConsoleAddress &address, OperatingCommand command,
                                                      bool human) const
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
			    !operating_arguments_valid(spec.action, command.value, command.arguments))
				return {};
			if (!psychic_channel_valid(address.ship, i, field, command.value))
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
bool commit_controller(const ControllerProposal &proposal)
{
	if (!entities.contains(proposal.address.entity))
		return false;
	auto entity = entities.at(proposal.address.entity);
	auto &controls = entity.get_mut<EntityControls>();
	if (proposal.controller >= controls.controllers.size())
		return false;
	auto &controller = controls.controllers[proposal.controller];
	if (controller.definition.console != proposal.address.console ||
	    controller.definition.id != proposal.address.controller || controller.generation != proposal.generation ||
	    controller.human != proposal.human || proposal.channel >= controller.commands.size())
		return false;
	if (!psychic_channel_valid(proposal.address.entity, proposal.controller, proposal.channel, proposal.value))
		return false;
	const auto &spec = controller.definition.commands[proposal.channel];
	auto &state = controller.commands[proposal.channel];
	if (proposal.value < spec.minimum || proposal.value > spec.maximum ||
	    (!spec.choices.empty() && !spec.choices.contains(proposal.value)) ||
	    !operating_arguments_valid(spec.action, proposal.value, proposal.arguments))
		return false;
	if (spec.semantics == CommandSemantics::Request)
	{
		if (!proposal.request_id || proposal.request_id < state.request_id ||
		    (state.request_id && state.consumed < state.revision && proposal.request_id != state.request_id))
			return false;
		if (proposal.request_id == state.request_id)
			return proposal.value == state.value && proposal.arguments == state.arguments;
	}
	else if (state.value == proposal.value && state.arguments == proposal.arguments && state.accepted)
	{
		// Navigation can change assistance as a physical side effect. Reasserting
		// the owned mode setting must still take control back from that guidance.
		bool same_effect = true;
		if (spec.action == OperatingAction::Assisted)
			for (const auto consumer : spec.consumers)
			{
				const auto child = entity.get<EquipmentIndex>().values.at(consumer);
				if (const auto *flight = child.try_get<FlightRuntime>();
				    flight && flight->state.assisted != (proposal.value != 0))
					same_effect = false;
			}
		// A sibling weapon can retarget the shared turret. Reasserting this
		// weapon's old setting must still retarget the physical mount.
		if (spec.action == OperatingAction::WeaponTarget)
			for (const auto consumer : spec.consumers)
			{
				const auto child = entity.get<EquipmentIndex>().values.at(consumer);
				if (const auto *weapon = child.try_get<CombatMechanism>();
				    weapon && weapon->target() != static_cast<EntityId>(proposal.value))
					same_effect = false;
			}
		if (spec.action == OperatingAction::RepairCompartment || spec.action == OperatingAction::RepairSystem)
			for (const auto consumer : spec.consumers)
			{
				const auto &team =
				    entity.get<EquipmentIndex>().values.at(consumer).get<CombatMechanism>().get<TeamState>();
				if (spec.action == OperatingAction::RepairCompartment)
					same_effect &= team.destination == proposal.value - 1;
				else
				{
					const auto hull = static_cast<std::int64_t>(
					                      entity.get<CombatStructure>().definition.equipment_compartments.size()) +
					                  1;
					same_effect &= team.repair_system == (proposal.value == hull ? -2 : proposal.value - 1);
				}
			}
		if (same_effect)
		{
			if (proposal.human && !spec.shared_target_controller.empty())
				state.target_override = true;
			return true;
		}
	}
	if (state.revision == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Controller command revision exhausted");
	state.value = proposal.value;
	state.arguments = proposal.arguments;
	state.request_id = proposal.request_id;
	if (proposal.human && !spec.shared_target_controller.empty())
		state.target_override = true;
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
		const auto claimant = console_operator(address);
		const bool present = claimant != 0;
		const bool human = present && controls.layout.profiles.at(controls.profiles.at(address.console)).human.at(i);
		if (validate && human != controller.human)
			throw std::runtime_error("Saved Controller operator differs from profile/claims");
		if (validate)
			controller.operator_claimant = claimant;
		if (human == controller.human && (!human || claimant == controller.operator_claimant))
		{
			controller.operator_claimant = claimant;
			knowledge.set_controller_control({id, address.console, controller.definition.id}, present, human);
			continue;
		}
		if (!controller.human && controller.brain)
		{
			const auto observations = controller_observations(id, controller);
			cancel_controller_action(id, i, "operator_changed", observations);
			const auto result =
			    state_tree::interrupt(controller.brain->program, controller.brain->continuation, observations);
			for (const auto &output : result.outputs)
				controller_output(id, i, output, observations);
			cancel_controller_action(id, i, "operator_changed", observations);
			for (auto &request : controller.incoming_requests)
				if (request.status == OfficerStatus::Accepted || request.status == OfficerStatus::Completed)
				{
					request.status = OfficerStatus::Failed;
					request.reason = "operator_changed";
					officer_message(id, controller, request, true);
				}
			for (auto &request : controller.outgoing_requests)
				if (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted ||
				    request.status == OfficerStatus::Completed)
				{
					request.status = OfficerStatus::Cancelled;
					request.reason = "operator_changed";
					officer_message(id, controller, request, true);
				}
		}
		if (controller.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Controller operator generation exhausted");
		++controller.generation;
		auto &bridge = entity.get_mut<BridgeBlackboard>();
		if (bridge.epochs.contains(address.console))
		{
			auto &epoch = bridge.epochs.at(address.console);
			if (epoch >= 9007199254740991ULL)
				throw std::runtime_error("Console stance authority generation exhausted");
			++epoch;
		}
		controller.human = human;
		controller.operator_claimant = claimant;
		knowledge.set_controller_control({id, address.console, controller.definition.id}, present, human);
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
	// Presence also matters for readers and human-seeking offers on Consoles
	// that own no command Controller. Update after outgoing AI cleanup.
	for (const auto &console : definition(id).consoles)
	{
		const ConsoleAddress address{id, console.id};
		const bool present = console_operator(address) != 0;
		knowledge.set_console_presence(address, present);
	}
}
void set_console_complexity(EntityControls &controls, const std::string &console, std::size_t profile)
{
	const auto group = controls.layout.complexity_groups.find(console);
	for (auto &[id, selected] : controls.profiles)
		if (id == console ||
		    (group != controls.layout.complexity_groups.end() && controls.layout.complexity_groups.contains(id) &&
		     controls.layout.complexity_groups.at(id) == group->second))
			selected = profile;
}
std::uint64_t coordinated_generation(const ConsoleAddress &a, const std::string &group) const
{
	std::uint64_t generation = 0;
	for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
		if (controller.definition.console == a.console)
			for (const auto &command : controller.definition.commands)
				if (!command.coordinated_group.empty() && command.coordinated_group == group)
					generation += controller.generation;
	return generation;
}
bool coordinated_impulse(const ConsoleAddress &a, const OperatingCommand &c, bool execute)
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
			if (!controller.human || spec.action != OperatingAction::Impulse || c.request_id < state.request_id ||
			    (c.request_id == state.request_id && c.value != state.value) ||
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
bool controller_meta(const ConsoleAddress &address, const OperatingCommand &command, bool execute)
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
			set_console_complexity(controls, address.console, static_cast<std::size_t>(command.value));
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
			process_controller_coordination(id, i);
			auto &brain = *controller.brain;
			if (brain.continuation.interrupted)
				state_tree::resume(brain.continuation);
			const auto observations = controller_observations(id, controller);

			if (controller.effective_provider == "tree")
			{
				const auto result = state_tree::step(brain.program, brain.continuation, observations);
				brain.debug = result.debug;
				for (const auto &output : result.outputs)
					controller_output(id, i, output, observations);
			}
			run_controller_action(id, i, observations);
		}
	}
	apply_docking_controller(entity);
	consume_controller_commands(entity);
}
void consume_controller_commands(flecs::entity entity)
{
	auto &controls = entity.get_mut<EntityControls>();
	// Shared targeting is an authored command relationship. Independent human
	// orders persist until this channel returns to assisted operation.
	for (auto &controller : controls.controllers)
		for (std::size_t i = 0; i < controller.commands.size(); ++i)
		{
			const auto &spec = controller.definition.commands[i];
			auto &state = controller.commands[i];
			if (spec.shared_target_controller.empty())
				continue;
			if (!controller.human)
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
Json project_controllers(const ConsoleAddress &address)
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
			                     {"arguments", state.arguments},
			                     {"action", std::string(operating_action(spec.action).name)},
			                     {"minimum", spec.minimum},
			                     {"maximum", spec.maximum},
			                     {"revision", state.revision},
			                     {"consumed", state.consumed},
			                     {"pending", state.consumed < state.revision},
			                     {"semantics", spec.semantics == CommandSemantics::Setting ? "setting" : "request"},
			                     {"accepted", state.accepted},
			                     {"request_id", state.request_id}};
			auto &consumers = commands[spec.id]["consumers"] = Json::array();
			for (const auto index : spec.consumers)
				consumers.push_back((*fittings(entities.at(address.ship))).values[index].id);
		}
		result.push_back({{"id", controller.definition.id},
		                  {"console", address.console},
		                  {"human", controller.human},
		                  {"generation", controller.generation},
		                  {"commands", commands}});
		result.back()["conversation_authority"] =
		    comms_controller(address.ship, controller.definition) && conversation_operator(address.ship, controller);
		bool contributes = false;
		for (const auto &command : controller.definition.commands)
			for (auto consumer : command.consumers)
				contributes |= definition(address.ship).fitted->values.at(consumer).kind == OperatingSystem::Team ||
				               definition(address.ship).fitted->values.at(consumer).kind == OperatingSystem::ShuttleBay;
		result.back()["contribution_authority"] = contributes && console_operator(address) != 0;
		bool bay = false;
		for (const auto &command : controller.definition.commands)
			for (const auto consumer : command.consumers)
				bay |= definition(address.ship).fitted->values.at(consumer).kind == OperatingSystem::ShuttleBay;
		result.back()["craft_authority"] = bay && assisted_workload(address, "craft");
		result.back()["transport_authority"] = bay && assisted_workload(address, "transport");
		result.back()["action"] = {
		    {"id", controller.action.id}, {"status", controller.action.status}, {"reason", controller.action.reason}};
		auto &available = result.back()["actions"] = Json::array();
		if (controller.brain)
			for (const auto &action : controller.actions)
			{
				Json inputs = Json::array();
				for (const auto index : action.inputs)
				{
					const auto &field = controller.brain->program.registry.observations.at(index);
					inputs.push_back({{"name", field.name.substr(6)},
					                  {"type", field.type == state_tree::Type::Text      ? "text"
					                           : field.type == state_tree::Type::Boolean ? "boolean"
					                                                                     : "integer"}});
				}
				bool usable = false;
				if (!controller.human)
				{
					const auto observed = controller_observations(address.ship, controller);
					state_tree::Instance probe;
					const auto check = state_tree::step(action.availability, probe, observed);
					usable = !check.debug.active.empty() && check.debug.active.back() == "available";
				}
				available.push_back({{"id", action.id},
				                     {"definition", action.tree},
				                     {"label", action.label},
				                     {"inputs", inputs},
				                     {"available", usable}});
			}
		result.back()["provider"] = {{"selected", controller.selected_provider},
		                             {"effective", controller.effective_provider},
		                             {"reason", controller.fallback_reason}};
		result.back()["action"]["invocation"] = controller.action.invocation;
		result.back()["coordination"] = save_controller_coordination(controller);
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
			                           {"arguments", command.arguments},
			                           {"revision", command.revision},
			                           {"consumed", command.consumed},
			                           {"request_id", command.request_id},
			                           {"accepted", command.accepted},
			                           {"target_override", command.target_override}});
		if (controller.brain)
			row["brain"] = state_tree::save(controller.brain->program, controller.brain->continuation);

		row["result"] = {{"available", controller.result.available},
		                 {"success", controller.result.success},
		                 {"id", controller.result.id},
		                 {"publication", controller.result.publication},
		                 {"message", controller.result.message},
		                 {"reason", controller.result.reason}};
		row["model"] = {{"selected", controller.selected_provider}, {"effective", controller.effective_provider},
		                {"reason", controller.fallback_reason},     {"generation", controller.provider_generation},
		                {"sequence", controller.decision_sequence}, {"next", controller.next_decision},
		                {"retries", controller.retry_count},        {"last", controller.last_decision_tick},
		                {"history", controller.decision_history}};
		row["effects"] = controller.effects;
		if (!controller.action.id.empty())
			row["action"] = save_controller_action(controller);
		if (!controller.incoming_requests.empty() || !controller.outgoing_requests.empty() ||
		    !controller.coordination_receipts.empty())
			row["coordination"] = save_controller_coordination(controller);
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
	for (const auto &[console, group] : controls.layout.complexity_groups)
		for (const auto &[other, other_group] : controls.layout.complexity_groups)
			if (group == other_group)
				fail(profiles.at(console) == profiles.at(other));
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
			fail(spec.choices.empty() || spec.choices.contains(state.value));
			const auto arguments = value.value("arguments", Json::array());
			fail(arguments.is_array() && arguments.size() <= 3);
			state.arguments.clear();
			for (const auto &argument : arguments)
			{
				fail(argument.is_number_integer() && argument >= -1000000000 && argument <= 1000000000);
				state.arguments.push_back(argument.get<std::int64_t>());
			}
			fail(operating_arguments_valid(spec.action, state.value, state.arguments));
			for (const auto *key : {"revision", "consumed", "request_id"})
				fail(value.at(key).is_number_unsigned());
			state.revision = value.at("revision").get<std::uint64_t>();
			state.consumed = value.at("consumed").get<std::uint64_t>();
			state.request_id = value.at("request_id").get<std::uint64_t>();
			fail(state.revision > 0 && state.consumed <= state.revision &&
			     (spec.semantics == CommandSemantics::Request || state.request_id == 0));
			fail(value.at("accepted").is_boolean());
			state.accepted = value.at("accepted").get<bool>();
			state.target_override = value.value("target_override", false);
			fail(!state.target_override || !spec.shared_target_controller.empty());
		}
		fail(!row.contains("timed") && row.contains("brain") == controller.brain.has_value());
		if (controller.brain)
			controller.brain->continuation = state_tree::load(controller.brain->program, row.at("brain"));
		if (row.contains("model"))
		{
			const auto &m = row.at("model");
			controller.selected_provider = m.at("selected");
			controller.effective_provider = m.at("effective");
			for (const auto *p : {&controller.selected_provider, &controller.effective_provider})
				fail(*p == "tree" || *p == "anyjev" || *p == "llamacpp" || *p == "jev");
			fail(controller.effective_provider == "tree" ||
			     controller.effective_provider == controller.selected_provider);
			fail(controller.selected_provider == "tree" || !controller.candidates.empty());
			controller.provider_generation =
			    checked_unsigned(m.at("generation"), 1, 9007199254740991ULL, "model generation");
			controller.decision_sequence =
			    checked_unsigned(m.at("sequence"), 0, 9007199254740991ULL, "decision sequence");
			controller.next_decision = checked_unsigned(m.at("next"), 0, 9007199254740991ULL, "decision tick");
			controller.last_decision_tick = checked_unsigned(m.at("last"), 0, 9007199254740991ULL, "last decision");
			controller.retry_count = checked_unsigned(m.at("retries"), 0, 5, "decision retries");
			controller.fallback_reason = m.at("reason");
			fail(controller.fallback_reason.size() <= 256);
			controller.decision_history = m.at("history");
			fail(controller.decision_history.is_array() && controller.decision_history.size() <= 16 &&
			     controller.decision_history.dump().size() <= 8192);
		}
		if (row.contains("action"))
			restore_controller_action(controller, row.at("action"));
		else
			controller.action = {};
		if (row.contains("coordination"))
			restore_controller_coordination(entity.get<StableId>().id, controller, row.at("coordination"));
		else
		{
			controller.incoming_requests.clear();
			controller.outgoing_requests.clear();
			controller.coordination_receipts.clear();
		}

		const auto &result = row.at("result");
		fail(result.at("available").is_boolean() && result.at("success").is_boolean() &&
		     result.at("id").is_number_unsigned() && result.at("publication").is_number_unsigned() &&
		     result.at("message").is_number_unsigned() && result.at("reason").is_string() &&
		     result.at("reason").get_ref<const std::string &>().size() <= 4096);
		controller.result = {result.at("available").get<bool>(),
		                     result.at("success").get<bool>(),
		                     result.at("reason").get<std::string>(),
		                     result.at("id").get<std::uint64_t>(),
		                     result.at("publication").get<std::uint64_t>(),
		                     result.at("message").get<std::uint64_t>()};
		fail((!controller.result.publication || controller.result.publication == controller.result.id) &&
		     (!controller.result.message || controller.result.message == controller.result.id) &&
		     !(controller.result.publication && controller.result.message) &&
		     controller.result.id <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
		const auto &effects = row.at("effects");
		fail(effects.is_array() && effects.size() <= effect_receipt_limit);
		for (const auto &effect : effects)
		{
			fail(effect.is_string() && !effect.get_ref<const std::string &>().empty() &&
			     effect.get_ref<const std::string &>().size() <= 2048);
			fail(controller.effects.insert(effect.get<std::string>()).second);
		}
	}
	controls.profiles = std::move(profiles);
}
