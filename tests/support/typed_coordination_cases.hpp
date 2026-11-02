#pragma once

// Synthetic snapshots exercise admission independently of presentation encoding.
inline void typed_coordination_evidence_cases()
{
	using state_tree::Value;
	ConsoleControllerRuntime controller;
	controller.brain.emplace();
	controller.brain->request_support[static_cast<std::size_t>(OfficerOperation::Power)] = true;
	controller.brain->request_support[static_cast<std::size_t>(OfficerOperation::Scan)] = true;
	decltype(controller.definition.commands)::value_type output;
	output.action = OperatingAction::GeneratorOutput;
	output.minimum = 0;
	output.maximum = 1000;
	output.consumer_ids = {"generator"};
	controller.definition.commands.push_back(output);
	controller.human_commands.push_back(false);
	OfficerRequest request;
	request.id = 9;
	request.operation = OfficerOperation::Power;
	request.status = OfficerStatus::Accepted;
	request.amount = request.requested_amount = 100;
	request.request_revision = request.reply_tick = 5;
	request.expires = 100;
	controller.incoming_requests.push_back(request);
	DecisionObservation observed;
	observed.observed_tick = 10;
	DecisionSystemView generator;
	generator.id = "generator";
	generator.telemetry[TelemetryField::enabled] = 1;
	generator.telemetry[TelemetryField::generator_output] = 100;
	observed.systems.push_back(generator);
	std::size_t replies = 0;
	const OfficerActionPorts ports{[](const std::string &, const std::string &) -> KnowledgeId
	                               {
		                               return 7;
	                               },
	                               [&]() -> const DecisionObservation &
	                               {
		                               return observed;
	                               },
	                               [&](const OfficerRequest &, bool) -> KnowledgeId
	                               {
		                               ++replies;
		                               return 8;
	                               }};
	const std::array complete_power{Value::integer(9), Value::string("completed"), Value::integer(100),
	                                Value::string("physical_evidence")};
	const auto respond = [&](const auto &arguments)
	{
		return officer_coordination_action(controller, 10, "coordination.respond_to", arguments, ports);
	};
	require(!respond(complete_power) && replies == 0,
	        "typed accepted generator intent cannot establish physical completion without an output sample");
	observed.systems[0].telemetry[TelemetryField::actual_output] = 100;
	observed.observed_tick = 4;
	require(!respond(complete_power) && replies == 0,
	        "typed supply evidence older than the request cannot complete it");
	observed.observed_tick = 10;
	observed.systems[0].telemetry[TelemetryField::enabled] = 0;
	require(!respond(complete_power) && replies == 0,
	        "disabled supply cannot claim completion from a retained physical sample");
	observed.systems[0].telemetry[TelemetryField::enabled] = 1;
	controller.incoming_requests[0].status = OfficerStatus::Requested;
	require(!respond(complete_power) && replies == 0,
	        "physical supply evidence does not replace prior request acceptance");
	controller.incoming_requests[0].status = OfficerStatus::Accepted;
	require(respond(complete_power) && replies == 1 && controller.incoming_requests[0].evidence_revision == 10,
	        "typed supply completion preserves the observed physical revision");

	request.id = 10;
	request.operation = OfficerOperation::Scan;
	request.target = 22;
	request.amount = request.requested_amount = 1;
	request.capability = "compartment_condition";
	request.request_revision = 7;
	controller.incoming_requests = {request};
	const std::array complete_scan{Value::integer(10), Value::string("completed"), Value::integer(1),
	                               Value::string("scan_evidence")};
	SensorScannerDecision scanner;
	scanner.target = 22;
	scanner.powered = true;
	scanner.progress.push_back({"compartment_condition", 8, 8});
	observed.sensors.scanners.push_back(scanner);
	require(!respond(complete_scan), "scanner target and progress are not a finding receipt");
	SensorContactDecision contact;
	contact.id = 23;
	SensorFindingDecision finding;
	finding.observed = 8;
	finding.data_present = true;
	contact.findings.emplace(request.capability, finding);
	observed.sensors.contacts.push_back(contact);
	require(!respond(complete_scan), "typed evidence from another target cannot satisfy a scan request");
	observed.sensors.contacts[0].id = 22;
	auto &sample = observed.sensors.contacts[0].findings.at(request.capability);
	sample.data_present = false;
	require(!respond(complete_scan), "withdrawn finding data cannot establish scan completion");
	sample.data_present = true;
	sample.observed = 6;
	require(!respond(complete_scan), "finding evidence before the request cannot establish scan completion");
	sample.observed = 11;
	require(!respond(complete_scan), "future finding evidence cannot establish scan completion");
	sample.observed = 8;
	controller.incoming_requests[0].status = OfficerStatus::Requested;
	require(!respond(complete_scan), "scan completion requires prior acceptance even with matching evidence");
	controller.incoming_requests[0].status = OfficerStatus::Accepted;
	require(respond(complete_scan) && replies == 2 && controller.incoming_requests[0].evidence_revision == 8,
	        "typed scan completion retains its finding timestamp rather than refreshing it to the frame tick");
}
