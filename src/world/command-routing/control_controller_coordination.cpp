module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::equipment_alert_recipients(EntityId ship, const std::string &source, const std::string &affected) const
    -> std::vector<ControllerAddress>
{
	std::vector<ControllerAddress> recipients;
	const auto &data = definition(ship);
	if (!data.layout)
		return recipients;
	for (const auto &controller : data.layout->controllers)
	{
		const auto uses = [&](const std::string &system)
		{
			const auto contains = [&](const auto &systems)
			{
				return std::find(systems.begin(), systems.end(), system) != systems.end();
			};
			return std::any_of(controller.commands.begin(), controller.commands.end(),
			                   [&](const auto &command)
			                   {
				                   return contains(command.consumer_ids);
			                   }) ||
			       std::any_of(controller.operations.begin(), controller.operations.end(),
			                   [&](const auto &operation)
			                   {
				                   return contains(operation.second);
			                   });
		};
		if (uses(affected) && !uses(source))
			recipients.push_back({ship, controller.console, controller.id});
	}
	return recipients;
}
auto Runtime::notify_officer_recovered(EntityId ship, std::size_t patient, const std::string &team) -> void
{
	const auto &state = entities.at(ship).get<PersonnelState>().officers.at(patient);
	const auto *duty = personnel_duty(ship, state.duty);
	if (!duty || team.empty())
		return;
	const auto &officer = definition(ship).personnel.officers.at(patient);
	std::vector<ControllerAddress> recipients;
	if (!duty->console.empty() && !duty->controller.empty())
		recipients.push_back({ship, duty->console, duty->controller});
	else if (!duty->team.empty())
		recipients = equipment_alert_recipients(ship, team, duty->team);
	const auto local = equipment_alert_recipients(ship, {}, team);
	for (const auto &recipient : recipients)
		if (std::find(local.begin(), local.end(), recipient) == local.end())
			knowledge.emit_equipment_alert(ship, team, recipient, "medical", officer.id, officer.name,
			                               ecs.get<Clock>().tick);
}

auto Runtime::notify_equipment_repaired(EntityId ship, const std::string &equipment, const std::string &team) -> void
{
	const auto fitted = instance(entities.at(ship), equipment);
	if (!fitted)
		return;
	for (const auto &recipient : equipment_alert_recipients(ship, team, equipment))
		knowledge.emit_equipment_alert(ship, team, recipient, "repair", equipment,
		                               std::string(operating_system_name(fitted->entity.get<Equipment>().kind)),
		                               ecs.get<Clock>().tick);
}

auto Runtime::restore_controller_coordination(EntityId ship, ConsoleControllerRuntime &controller, const Json &saved)
    -> void
{
	std::vector<KnowledgeId> peers;
	for (const auto &owner : entities.at(ship).get<EntityControls>().controllers)
		peers.push_back(knowledge.controller_id({ship, owner.definition.console, owner.definition.id}));
	phoenix::restore_controller_coordination(controller, saved, peers);
}

auto Runtime::officer_message(EntityId id, ConsoleControllerRuntime &controller, const OfficerRequest &request,
                              bool reply, bool execute) -> KnowledgeId
{
	const ConsoleAddress source{id, controller.definition.console};
	CoordinationInput message;
	message.sender_system = knowledge.controller_id({id, source.console, controller.definition.id});
	message.destination = {MessageDestinationKind::Controller, request.peer};
	message.origin = ControlOrigin::AI;
	message.payload.schema = knowledge.definition().schema("phoenix.coordination");
	message.payload.producer =
	    knowledge.message_producer(source, controller.definition.id, message.payload.schema, ControlOrigin::AI, true);
	message.payload.subject = id;
	message.payload.provenance = {message.sender_system, ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0,
	                              EvidenceKind::Intent};
	const auto field = [&](const char *name, KnowledgeValue value)
	{
		message.payload.fields[knowledge.definition().field(message.payload.schema, name)] = std::move(value);
	};
	field("operation", std::string(officer_operations.at(static_cast<std::size_t>(request.operation))));
	field("status", std::string(officer_statuses.at(static_cast<std::size_t>(request.status))));
	field("reason", request.reason);
	field("request", static_cast<std::int64_t>(reply ? request.id : 0));
	field("target", request.target);
	field("amount", request.amount);
	field("priority", request.priority);
	field("expires", static_cast<std::int64_t>(request.expires));
	field("mission", request.mission);
	field("capability", request.capability);
	field("request_revision", static_cast<std::int64_t>(request.request_revision));
	field("evidence_revision", static_cast<std::int64_t>(request.evidence_revision));
	field("requested_amount", request.requested_amount);
	if (reply && request.evidence_revision &&
	    (request.status == OfficerStatus::Completed || request.reason == "execution_started"))
	{
		message.payload.provenance.kind = EvidenceKind::Claim;
		message.payload.provenance.revision = request.evidence_revision;
	}

	message.presentation.title_key = "Officer coordination";
	const std::array<const char *, 6> needs{"I need additional power to carry out my assigned work.",
	                                        "I need additional cooling to carry out my assigned work.",
	                                        "Please manoeuvre so I can engage the assigned target.",
	                                        "Observed damage is preventing my work. Repairs are needed.",
	                                        "Please prepare up to {amount} departmental assets for mission {mission}.",
	                                        "Please obtain {capability} evidence for contact {target}."};
	const std::array<const char *, 6> accepted{
	    "I accept a power supply of {amount}; you requested {requested_amount}.",
	    "I accept a cooling allocation of {amount}; you requested {requested_amount}.",
	    "I will manoeuvre for the assigned target.",
	    "I will attend to the reported repair needs.",
	    "I will prepare {amount} departmental assets for mission {mission}.",
	    "I accept the request for {capability} evidence about contact {target}."};
	const std::array<const char *, 6> completed{
	    "The agreed power supply of {amount} is established; you requested {requested_amount}.",
	    "The agreed cooling allocation of {amount} is established; you requested {requested_amount}.",
	    "The requested manoeuvre is complete.",
	    "The reported repair work is complete.",
	    "The agreed departmental contribution for mission {mission} is prepared.",
	    "I have obtained {capability} evidence for contact {target}, observed at tick {evidence_revision}."};
	const auto operation = static_cast<std::size_t>(request.operation);
	message.presentation.body_key = needs.at(operation);
	if (reply)
	{
		switch (request.status)
		{
		case OfficerStatus::Accepted:
			message.presentation.body_key = accepted.at(operation);
			if (request.operation == OfficerOperation::Manifest && request.amount < request.requested_amount)
				message.presentation.body_key = "I can prepare {amount} of the {requested_amount} requested "
				                                "departmental assets for mission {mission}.";
			if (request.reason == "execution_started")
			{
				const std::array<const char *, 6> executing{
				    "I have set the agreed power output to {amount}; delivery is not yet confirmed.",
				    "I have set the agreed cooling allocation to {amount}; delivery is not yet confirmed.",
				    "I am moving toward the assigned target.",
				    "The team is travelling to or working on the reported repair assignment.",
				    "Departmental assets are being assigned for mission {mission}.",
				    "Acquisition of {capability} evidence about contact {target} is underway."};
				message.presentation.body_key = executing.at(operation);
			}
			break;
		case OfficerStatus::Completed:
			message.presentation.body_key = completed.at(operation);
			break;
		case OfficerStatus::Refused:
			message.presentation.body_key = "I cannot take on that work under my current orders and operating limits.";
			break;
		case OfficerStatus::Failed:
			message.presentation.body_key =
			    "I cannot carry out the agreed support. Check the current operating constraints.";
			break;
		default:
			message.presentation.body_key = "That support has ended.";
			break;
		}
	}
	message.presentation.parameters = {
	    {"operation", officer_operations.at(static_cast<std::size_t>(request.operation))},
	    {"status", officer_statuses.at(static_cast<std::size_t>(request.status))},
	    {"reason", request.reason}};
	message.presentation.parameters["target"] = std::to_string(request.target);
	message.presentation.parameters["amount"] = std::to_string(request.amount);
	message.presentation.parameters["mission"] = request.mission;
	message.presentation.parameters["capability"] = request.capability;
	if (request.operation == OfficerOperation::Scan)
	{
		const std::array<const char *, 5> labels{"basic identity", "contact arcs", "shield sectors",
		                                         "compartment condition", "findings"};
		message.presentation.parameters["capability"] = labels.at(officer_index(scan_capabilities, request.capability));
	}

	message.presentation.parameters["requested_amount"] = std::to_string(request.requested_amount);
	message.presentation.parameters["request_revision"] = std::to_string(request.request_revision);
	message.presentation.parameters["evidence_revision"] = std::to_string(request.evidence_revision);
	message.presentation.parameters["request"] = std::to_string(reply ? request.id : 0);
	message.presentation.urgency = static_cast<std::uint8_t>(std::min<std::int64_t>(request.priority, 2));
	const auto speaker = officer_speaker(id, source.console, controller.definition.id);
	message.presentation.speaker_name = speaker.first;
	message.presentation.speaker_portrait = speaker.second;
	message.delay = 1;
	message.expires_tick = std::max<KnowledgeTick>(request.expires, ecs.get<Clock>().tick + 20);
	return knowledge.admit_message(source, std::move(message), ecs.get<Clock>().tick, execute);
}

auto Runtime::process_controller_coordination(EntityId id, std::size_t index) -> void
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	const ControllerAddress address{id, controller.definition.console, controller.definition.id};
	const auto frozen = decision_frame.inboxes().find(address);
	const auto live = frozen == decision_frame.inboxes().end() ? knowledge.peek_controller(address)
	                                                           : std::vector<CoordinationDelivery>{};
	const auto &inbox = frozen == decision_frame.inboxes().end() ? live : frozen->second;
	std::vector<KnowledgeId> peers;
	if (!inbox.empty())
		for (const auto &owner : entities.at(id).get<EntityControls>().controllers)
			peers.push_back(knowledge.controller_id({id, owner.definition.console, owner.definition.id}));
	const OfficerProtocolPorts ports{[this, address](KnowledgeId message)
	                                 {
		                                 knowledge.acknowledge_controller(address, message);
	                                 },
	                                 [this, id, &controller](const OfficerRequest &request)
	                                 {
		                                 officer_message(id, controller, request, true);
	                                 }};
	process_officer_protocol(id, ecs.get<Clock>().tick, controller, inbox, knowledge.definition(), peers, ports);
	// A factual reporter visits every accepted promise, without selecting work or
	// writing commands. Admission proves execution from the frozen Console frame.
	std::vector<std::pair<KnowledgeId, std::int64_t>> started;
	for (const auto &request : controller.incoming_requests)
		if (request.status == OfficerStatus::Accepted && request.reason != "execution_started" &&
		    request.expires > ecs.get<Clock>().tick)
			started.emplace_back(request.id, request.amount);
	for (const auto &[request, amount] : started)
	{
		const std::array args{state_tree::Value::integer(request), state_tree::Value::string("accepted"),
		                      state_tree::Value::integer(amount), state_tree::Value::string("execution_started")};
		controller_coordination_action(id, index, "coordination.respond_to", args, true);
	}
}

auto Runtime::controller_coordination_facts(const ConsoleControllerRuntime &controller,
                                            const DecisionObservation &observed,
                                            std::vector<state_tree::Value> &values) const -> void
{
	ControllerCoordinationInputs inputs;
	// Fitted channel identities are immutable content, not a live equipment query.
	for (const auto &command : controller.definition.commands)
		for (std::size_t i = 0; i < command.consumers.size(); ++i)
		{
			const auto consumer = command.consumers[i];
			if (inputs.systems.size() <= consumer)
				inputs.systems.resize(consumer + 1);
			inputs.systems[consumer] = command.consumer_ids.at(i);
		}
	inputs.hold = observed.authority.stance == "hold";
	if (const auto slot = controller.brain->observation_layout->manoeuvre[1];
	    slot && values[*slot].type == state_tree::Type::Boolean)
		inputs.navigation_blocked = values[*slot].number != 0;
	phoenix::controller_coordination_facts(controller, observed, ecs.get<Clock>().tick, inputs, values);
}

auto Runtime::controller_coordination_action(EntityId id, std::size_t index, const std::string &name,
                                             std::span<const state_tree::Value> args, bool execute) -> bool
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (!controller_operation_owns(controller, "coordination", {}) ||
	    controller_operation_human(controller, "coordination"))
		return false;
	std::optional<ConsoleControllerRuntime> staged;
	if (!execute)
		staged = controller;
	auto &subject = staged ? *staged : controller;
	const ConsoleAddress address{id, controller.definition.console};
	std::optional<DecisionObservation> fallback;
	const OfficerActionPorts ports{[this, id](const std::string &console, const std::string &owner)
	                               {
		                               return knowledge.controller_id({id, console, owner});
	                               },
	                               [this, address, &fallback]() -> const DecisionObservation &
	                               {
		                               if (decision_frame.observations().contains(address))
			                               return decision_frame.observations().at(address);
		                               if (!fallback)
			                               fallback = capture_decision_observation(address);
		                               return *fallback;
	                               },
	                               [this, id, &subject, execute](const OfficerRequest &request, bool reply)
	                               {
		                               return officer_message(id, subject, request, reply, execute);
	                               }};
	return phoenix::officer_coordination_action(subject, ecs.get<Clock>().tick, name, args, ports);
}
} // namespace phoenix::world_detail
