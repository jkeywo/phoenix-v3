module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::officer_pending(const ConsoleControllerRuntime &controller) const -> const OfficerRequest *
{
	return pending_officer_request(controller, ecs.get<Clock>().tick);
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
	field("request_revision", static_cast<std::int64_t>(request.request_revision));
	field("evidence_revision", static_cast<std::int64_t>(request.evidence_revision));
	field("requested_amount", request.requested_amount);
	if (reply && request.status == OfficerStatus::Completed && request.evidence_revision)
	{
		message.payload.provenance.kind = EvidenceKind::Claim;
		message.payload.provenance.revision = request.evidence_revision;
	}

	message.presentation.title_key = "Officer coordination";
	const std::array<const char *, 5> needs{"I need additional power to carry out my assigned work.",
	                                        "I need additional cooling to carry out my assigned work.",
	                                        "Please manoeuvre so I can engage the assigned target.",
	                                        "Observed damage is preventing my work. Repairs are needed.",
	                                        "Please prepare up to {amount} departmental assets for mission {mission}."};
	const std::array<const char *, 5> accepted{
	    "I will provide the requested power.", "I will provide the requested cooling.",
	    "I will manoeuvre for the assigned target.", "I will attend to the reported repair needs.",
	    "I will prepare {amount} departmental assets for mission {mission}."};
	const std::array<const char *, 5> completed{
	    "The requested power supply is established.", "The requested cooling supply is established.",
	    "The requested manoeuvre is complete.", "The reported repair work is complete.",
	    "The agreed departmental contribution for mission {mission} is prepared."};
	const auto operation = static_cast<std::size_t>(request.operation);
	message.presentation.body_key = needs.at(operation);
	if (reply)
	{
		switch (request.status)
		{
		case OfficerStatus::Accepted:
			message.presentation.body_key =
			    request.amount < request.requested_amount && operation < 2
			        ? "I can provide some of the requested support, within the current operating limit."
			        : accepted.at(operation);
			if (request.operation == OfficerOperation::Manifest && request.amount < request.requested_amount)
				message.presentation.body_key = "I can prepare {amount} of the {requested_amount} requested "
				                                "departmental assets for mission {mission}.";
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
	const auto inbox = decision_frame.inboxes().contains(address) ? decision_frame.inboxes().at(address)
	                                                              : knowledge.peek_controller(address);
	std::vector<KnowledgeId> peers;
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
	if (controller.action.id == "officer_support" && controller.action.status == "running")
	{
		const auto *next = officer_pending(controller);
		for (auto &r : controller.incoming_requests)
			if (controller.action.invocation.starts_with("request/" + std::to_string(r.id) + "/") &&
			    (!next || next->id != r.id))
			{
				const auto observed = controller_observations(id, controller);
				cancel_controller_action(id, index, "commitment_ended", observed);
				if (r.status == OfficerStatus::Accepted ||
				    (r.status == OfficerStatus::Completed && officer_continuing_operation(r.operation)))
				{
					r.status = OfficerStatus::Failed;
					r.reason = "preempted";
					officer_message(id, controller, r, true);
				}
				break;
			}
	}
}

auto Runtime::controller_coordination_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                                            std::vector<state_tree::Value> &values) const -> void
{
	ControllerCoordinationInputs inputs;
	const auto *request = officer_pending(controller);
	if (request)
	{
		const auto entity = entities.at(observed.own.id);
		for (const auto fitted = fittings(entity); const auto &system : fitted->values)
			inputs.systems.push_back(system.id);
		if (request->operation == OfficerOperation::Manoeuvre &&
		    std::any_of(controller.definition.commands.begin(), controller.definition.commands.end(),
		                [](const auto &command)
		                {
			                return command.action == OperatingAction::FollowContact ||
			                       command.action == OperatingAction::Heading ||
			                       command.action == OperatingAction::Speed;
		                }) &&
		    std::any_of(observed.contacts.begin(), observed.contacts.end(),
		                [&](const auto &contact)
		                {
			                return contact.id == request->target && contact.disposition == "hostile";
		                }))
		{
			const auto &bridge = entity.get<BridgeBlackboard>();
			inputs.navigation_blocked = bridge.navigation.blocked;
			inputs.hold = observed.gameplay.value("stance", Json::object()).value("id", std::string{}) == "hold";
		}
	}
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
	std::optional<Observation> fallback;
	const OfficerActionPorts ports{[this, id](const std::string &console, const std::string &owner)
	                               {
		                               return knowledge.controller_id({id, console, owner});
	                               },
	                               [this, address, &fallback]() -> const Observation &
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
