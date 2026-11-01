module;
#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.control.coordination;

namespace phoenix
{

std::optional<KnowledgeTick> scan_completion_evidence(const Observation &observed, const OfficerRequest &request)
{
	const auto sensors = observed.gameplay.find("sensors");
	if (sensors == observed.gameplay.end())
		return {};
	for (const auto &contact : sensors->at("contacts"))
		if (contact.at("id") == request.target && contact.at("findings").contains(request.capability))
		{
			const auto &sample = contact.at("findings").at(request.capability);
			const auto tick = sample.at("observed").get<KnowledgeTick>();
			if (!sample.at("data").is_null() && tick >= request.request_revision && tick <= observed.observed_tick)
				return tick;
		}
	return {};
}
bool officer_continuing_operation(OfficerOperation operation)
{
	return operation == OfficerOperation::Power || operation == OfficerOperation::Cooling ||
	       operation == OfficerOperation::Manoeuvre;
}
bool officer_supports(const ConsoleControllerRuntime &controller, OfficerOperation operation)
{
	if (operation == OfficerOperation::Repair && controller.definition.operations.contains("team_assignment") &&
	    controller.brain &&
	    std::any_of(controller.brain->objective_bindings.begin(), controller.brain->objective_bindings.end(),
	                [](const auto &binding)
	                {
		                return binding.behavior == "repair" &&
		                       (binding.candidate_source == "repair" || binding.candidate_source == "requests");
	                }))
		return true;
	if (operation == OfficerOperation::Scan)
		return controller.brain &&
		       std::any_of(controller.brain->objective_bindings.begin(), controller.brain->objective_bindings.end(),
		                   [](const auto &binding)
		                   {
			                   return binding.candidate_source == "scans";
		                   }) &&
		       std::any_of(controller.definition.commands.begin(), controller.definition.commands.end(),
		                   [](const auto &command)
		                   {
			                   return command.action == OperatingAction::SensorTarget;
		                   });
	if (operation == OfficerOperation::Manifest)
		return controller.definition.operations.contains("contribution") && controller.brain &&
		       std::any_of(controller.brain->objective_bindings.begin(), controller.brain->objective_bindings.end(),
		                   [](const auto &binding)
		                   {
			                   return binding.candidate_source == "requests" && binding.behavior == "away";
		                   });
	return std::any_of(
	    controller.definition.commands.begin(), controller.definition.commands.end(),
	    [&](const auto &command)
	    {
		    return (operation == OfficerOperation::Power && command.action == OperatingAction::GeneratorOutput) ||
		           (operation == OfficerOperation::Cooling && command.action == OperatingAction::CoolingAllocation) ||
		           (operation == OfficerOperation::Repair && command.action == OperatingAction::RepairCompartment) ||
		           (operation == OfficerOperation::Manoeuvre &&
		            (command.action == OperatingAction::FollowContact || command.action == OperatingAction::Heading ||
		             command.action == OperatingAction::Speed));
	    });
}
const OfficerRequest *pending_officer_request(const ConsoleControllerRuntime &controller, KnowledgeTick now)
{
	const OfficerRequest *best = nullptr;
	const auto committed = [](const OfficerRequest &r)
	{
		return r.status == OfficerStatus::Accepted || r.status == OfficerStatus::Completed;
	};
	for (const auto &r : controller.incoming_requests)
		if ((r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
		     (r.status == OfficerStatus::Completed && officer_continuing_operation(r.operation))) &&
		    r.expires > now && officer_supports(controller, r.operation))
			if (!best || r.priority > best->priority ||
			    (r.priority == best->priority && committed(r) && !committed(*best)) ||
			    (r.priority == best->priority && committed(r) == committed(*best) && r.id < best->id))
				best = &r;
	return best;
}
Json save_controller_coordination(const ConsoleControllerRuntime &controller)
{
	Json result = Json::array();
	for (const auto *list : {&controller.incoming_requests, &controller.outgoing_requests})
	{
		Json rows = Json::array();
		for (const auto &r : *list)
			rows.push_back({{"id", r.id},
			                {"peer", r.peer},
			                {"operation", static_cast<int>(r.operation)},
			                {"status", static_cast<int>(r.status)},
			                {"target", r.target},
			                {"amount", r.amount},
			                {"requested_amount", r.requested_amount},
			                {"reply_tick", r.reply_tick},
			                {"retry_ticks", r.retry_ticks},
			                {"priority", r.priority},
			                {"expires", r.expires},
			                {"reason", r.reason},
			                {"mission", r.mission},
			                {"capability", r.capability},
			                {"request_revision", r.request_revision},
			                {"evidence_revision", r.evidence_revision}});
		result.push_back(rows);
	}
	result.push_back(controller.coordination_receipts);
	return result;
}
void restore_controller_coordination(ConsoleControllerRuntime &controller, const Json &saved,
                                     std::span<const KnowledgeId> known_peers)
{
	if (!saved.is_array() || saved.size() != 3)
		throw std::runtime_error("Invalid officer continuation");
	std::size_t index = 0;
	for (auto *list : {&controller.incoming_requests, &controller.outgoing_requests})
	{
		const auto &rows = saved.at(index++);
		if (!rows.is_array() || rows.size() > 128)
			throw std::runtime_error("Unbounded officer continuation");
		std::set<KnowledgeId> ids;
		list->clear();
		for (const auto &row : rows)
		{
			OfficerRequest r;
			r.id = row.at("id");
			r.peer = row.at("peer");
			r.target = row.at("target");
			r.amount = row.at("amount");
			r.requested_amount = row.at("requested_amount");
			r.reply_tick = row.at("reply_tick");
			r.retry_ticks = row.at("retry_ticks");
			r.priority = row.at("priority");
			r.expires = row.at("expires");
			r.reason = row.at("reason");
			r.mission = row.value("mission", std::string{});
			r.capability = row.value("capability", std::string{});
			r.request_revision = row.value("request_revision", 0ULL);
			r.evidence_revision = row.value("evidence_revision", 0ULL);
			const int operation = row.at("operation"), status = row.at("status");
			if (!r.id || !r.peer || !ids.insert(r.id).second || operation < 0 ||
			    operation >= static_cast<int>(officer_operations.size()) || status < 0 || status >= 6 || r.amount < 0 ||
			    r.amount > 1000000000 || r.requested_amount < 0 || r.requested_amount > 1000000000 || r.priority < 0 ||
			    r.priority > 2 || r.expires < 0 || r.reply_tick < 0 || r.retry_ticks < 0 || r.retry_ticks > 72000 ||
			    r.reason.size() > 4096)
				throw std::runtime_error("Invalid officer request");
			if (std::find(known_peers.begin(), known_peers.end(), r.peer) == known_peers.end())
				throw std::runtime_error("Saved request has an unknown peer");

			if (r.mission.size() > 128 || r.request_revision > 9007199254740991ULL ||
			    r.evidence_revision > 9007199254740991ULL ||
			    (operation == static_cast<int>(OfficerOperation::Manifest) &&
			     (r.mission.empty() || !r.request_revision || r.amount > r.requested_amount ||
			      (status == static_cast<int>(OfficerStatus::Completed) &&
			       (!r.amount || r.evidence_revision < r.request_revision)))))
				throw std::runtime_error("Invalid mission request scope");
			if (r.capability.size() > 128 ||
			    (operation == static_cast<int>(OfficerOperation::Scan) &&
			     (std::find(scan_capabilities.begin(), scan_capabilities.end(), r.capability) ==
			          scan_capabilities.end() ||
			      r.target <= 0 || r.target > 2000000000 || !r.request_revision ||
			      (status == static_cast<int>(OfficerStatus::Completed) && r.evidence_revision < r.request_revision))))
				throw std::runtime_error("Invalid scan request scope");
			r.operation = static_cast<OfficerOperation>(operation);
			r.status = static_cast<OfficerStatus>(status);
			list->push_back(r);
		}
	}
	const auto &receipts = saved.at(2);
	if (!receipts.is_array() || receipts.size() > 65536)
		throw std::runtime_error("Unbounded officer protocol receipts");
	controller.coordination_receipts.clear();
	for (const auto &receipt : receipts)
		if (!receipt.is_number_unsigned() || !receipt.get<KnowledgeId>() ||
		    !controller.coordination_receipts.insert(receipt.get<KnowledgeId>()).second)
			throw std::runtime_error("Invalid officer protocol receipt");
}

} // namespace phoenix
