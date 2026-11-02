module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.protocol;
import phoenix.control.coordination;
import phoenix.control.decisions;

namespace phoenix
{
namespace
{
bool execution_observed(const ConsoleControllerRuntime &controller, const DecisionObservation &observed,
                        const OfficerRequest &request)
{
	if (observed.observed_tick < request.reply_tick)
		return false;
	if (request.operation == OfficerOperation::Power || request.operation == OfficerOperation::Cooling)
	{
		const bool power = request.operation == OfficerOperation::Power;
		bool supported = false;
		for (const auto &command : controller.definition.commands)
			if (command.action == (power ? OperatingAction::GeneratorOutput : OperatingAction::CoolingAllocation))
			{
				supported = true;
				if (command.consumer_ids.empty())
					return false;
				for (const auto &id : command.consumer_ids)
				{
					bool found = false;
					for (const auto &system : observed.systems)
						if (system.id == id)
						{
							const auto &t = system.telemetry;
							const auto field =
							    power ? TelemetryField::generator_output : TelemetryField::cooling_allocation;
							found = t.contains(field) && t.at(field) == request.amount &&
							        (!t.contains(TelemetryField::enabled) || t.at(TelemetryField::enabled)) &&
							        (!t.contains(TelemetryField::thermal_stopped) ||
							         !t.at(TelemetryField::thermal_stopped));
						}
					if (!found)
						return false;
				}
			}
		return supported;
	}
	if (request.operation == OfficerOperation::Manoeuvre)
	{
		for (const auto &contact : observed.contacts)
			if (contact.id == request.target && contact.disposition == "hostile" &&
			    fixed::length({observed.own.vx, observed.own.vy}) > 0)
			{
				const auto to = fixed::Vec{contact.x - observed.own.x, contact.y - observed.own.y};
				return fixed::dot(to, {observed.own.vx - contact.vx, observed.own.vy - contact.vy}) > 0;
			}
	}
	if (request.operation == OfficerOperation::Repair)
		for (const auto &team : observed.teams)
			if (team.repair && team.available && controller_operation_owns(controller, "team_assignment", team.id) &&
			    !controller_operation_human(controller, "team_assignment") &&
			    ((team.destination + 1 == request.target && team.travel > 0) ||
			     (team.location + 1 == request.target && team.repair_work_active)))
				return true;
	if (request.operation == OfficerOperation::Manifest)
		for (const auto &mission : observed.away_missions)
			if (mission.id == request.mission && mission.target == request.target && mission.department &&
			    mission.department->assigned > 0)
				return true;
	if (request.operation == OfficerOperation::Scan)
		for (const auto &scanner : observed.sensors.scanners)
			if (scanner.target == request.target && scanner.powered && scanner.blocker.empty())
				for (const auto &progress : scanner.progress)
					if (progress.capability == request.capability && progress.ticks > 0)
						return true;
	return false;
}
} // namespace
void process_officer_protocol(EntityId id, KnowledgeTick now, ConsoleControllerRuntime &controller,
                              std::span<const CoordinationDelivery> inbox, const CompiledKnowledge &definitions,
                              std::span<const KnowledgeId> known_peers, const OfficerProtocolPorts &ports)
{
	const auto schema = definitions.schema("phoenix.coordination");
	const auto protocol_message = [&](const CoordinationDelivery &delivery)
	{
		const auto &message = delivery.message.value;
		return message.payload.schema == schema && !message.evidence_report &&
		       message.payload.provenance.source == message.sender_system &&
		       message.payload.provenance.kind != EvidenceKind::Fact;
	};
	std::set<KnowledgeId> pending_protocol;
	for (const auto &delivery : inbox)
		if (protocol_message(delivery))
			pending_protocol.insert(delivery.message.id);
	std::erase_if(controller.coordination_receipts,
	              [&](const auto message)
	              {
		              return !pending_protocol.contains(message);
	              });
	bool protocol_prefix = true;
	for (const auto &delivery : inbox)
	{
		const auto &m = delivery.message;
		if (!protocol_message(delivery))
		{
			protocol_prefix = false;
			continue;
		}
		if (controller.coordination_receipts.contains(m.id))
		{
			if (protocol_prefix)
			{
				ports.acknowledge(m.id);
				controller.coordination_receipts.erase(m.id);
			}
			continue;
		}
		const auto &fields = m.value.payload.fields;
		const auto value = [&](const char *name) -> const KnowledgeValue &
		{
			return fields.at(definitions.field(schema, name));
		};
		OfficerRequest r;
		std::int64_t correlation = 0;
		bool valid = false;
		try
		{
			r.operation = static_cast<OfficerOperation>(
			    officer_index(officer_operations, std::get<std::string>(value("operation"))));
			r.status =
			    static_cast<OfficerStatus>(officer_index(officer_statuses, std::get<std::string>(value("status"))));
			r.peer = m.value.sender_system;
			r.id = m.id;
			r.target = std::get<std::int64_t>(value("target"));
			r.amount = std::get<std::int64_t>(value("amount"));
			r.requested_amount = r.amount;
			r.reply_tick = now;
			r.priority = std::get<std::int64_t>(value("priority"));
			r.expires = std::get<std::int64_t>(value("expires"));
			r.reason = std::get<std::string>(value("reason"));
			const auto optional = [&](const char *name) -> const KnowledgeValue *
			{
				const auto at = fields.find(definitions.field(schema, name));
				return at == fields.end() ? nullptr : &at->second;
			};
			if (const auto capability = optional("capability"))
				r.capability = std::get<std::string>(*capability);
			if (const auto mission = optional("mission"))
				r.mission = std::get<std::string>(*mission);
			if (const auto revision = optional("request_revision"))
				r.request_revision = std::get<std::int64_t>(*revision);
			if (const auto revision = optional("evidence_revision"))
				r.evidence_revision = std::get<std::int64_t>(*revision);
			if (const auto amount = optional("requested_amount"))
				r.requested_amount = std::get<std::int64_t>(*amount);
			if (r.operation == OfficerOperation::Manifest &&
			    (r.mission.empty() || r.mission.size() > 128 || !r.request_revision ||
			     r.request_revision > 9007199254740991ULL || r.evidence_revision > 9007199254740991ULL ||
			     r.requested_amount < r.amount || r.requested_amount > 1000000000 ||
			     (r.status == OfficerStatus::Completed && (!r.amount || r.evidence_revision < r.request_revision))))
				throw std::runtime_error("Invalid mission coordination scope");
			if (r.operation == OfficerOperation::Scan)
			{
				(void)officer_index(scan_capabilities, r.capability);
				if (r.target <= 0 || r.target > 2000000000 || !r.request_revision || r.request_revision > now ||
				    r.evidence_revision > now ||
				    (r.status == OfficerStatus::Completed && r.evidence_revision < r.request_revision))
					throw std::runtime_error("Invalid scan coordination scope");
			}
			if (r.requested_amount < 0 || r.requested_amount > 1000000000 || r.mission.size() > 128 ||
			    r.request_revision > 9007199254740991ULL || r.evidence_revision > 9007199254740991ULL)
				throw std::runtime_error("Invalid request metadata");
			if (r.reason == "execution_started" &&
			    (r.status != OfficerStatus::Accepted || !r.evidence_revision || r.evidence_revision > now ||
			     r.evidence_revision < r.request_revision || m.value.payload.provenance.kind != EvidenceKind::Claim))
				throw std::runtime_error("Execution report requires a dated claim");
			correlation = std::get<std::int64_t>(value("request"));
			if (!r.expires)
				r.expires = m.value.expires_tick;
			if (r.priority < 0 || r.priority > 2 || r.amount < 0 || r.amount > 1000000000 || r.expires <= now ||
			    r.expires > now + 72000 || m.value.payload.subject != id ||
			    std::find(known_peers.begin(), known_peers.end(), r.peer) == known_peers.end())
				throw std::runtime_error("Invalid or expired request");
			valid = true;
		}
		catch (const std::exception &)
		{
			// Reject malformed fields, but do not swallow failures from reply ports.
			valid = false;
		}
		if (valid)
		{
			if (correlation == 0 && r.status == OfficerStatus::Requested)
			{
				if (std::none_of(controller.incoming_requests.begin(), controller.incoming_requests.end(),
				                 [&](const auto &v)
				                 {
					                 return v.id == r.id;
				                 }) &&
				    controller.incoming_requests.size() < 128)
				{
					if (!officer_supports(controller, r.operation))
					{
						r.status = OfficerStatus::Refused;
						r.reason = "unsupported_operation";
						ports.reply(r);
					}
					controller.incoming_requests.push_back(r);
				}
			}
			else
			{
				auto &requests =
				    r.status == OfficerStatus::Cancelled ? controller.incoming_requests : controller.outgoing_requests;
				for (auto &original : requests)
					if (original.id == static_cast<KnowledgeId>(correlation) && original.peer == r.peer &&
					    original.operation == r.operation && original.target == r.target &&
					    original.mission == r.mission && original.capability == r.capability &&
					    (r.operation != OfficerOperation::Scan || original.request_revision == r.request_revision) &&
					    (r.operation != OfficerOperation::Manifest ||
					     (original.request_revision == r.request_revision &&
					      original.requested_amount == r.requested_amount)) &&
					    (original.status == OfficerStatus::Requested || original.status == OfficerStatus::Accepted ||
					     original.status == OfficerStatus::Completed))
					{
						if (original.status == OfficerStatus::Completed && r.status == OfficerStatus::Accepted)
							continue;
						if (r.reason == "execution_started" && (r.evidence_revision < original.request_revision ||
						                                        r.evidence_revision < original.evidence_revision))
							continue;
						original.status = r.status;
						if (!(m.value.origin == ControlOrigin::Human && r.status == OfficerStatus::Accepted &&
						      r.reason == "accept_requested_limit"))
							original.amount = r.amount;
						original.reason = r.reason;
						original.reply_tick = now;
						original.evidence_revision = r.evidence_revision;
					}
			}
		}
		if (protocol_prefix)
			ports.acknowledge(m.id);
		else
		{
			if (controller.coordination_receipts.size() >= 65536)
				throw std::runtime_error("Unbounded officer protocol receipts");
			controller.coordination_receipts.insert(m.id);
		}
	}
	for (auto *list : {&controller.incoming_requests, &controller.outgoing_requests})
	{
		for (auto &r : *list)
			if (r.expires <= now &&
			    (r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
			     (r.status == OfficerStatus::Completed && officer_continuing_operation(r.operation))))
			{
				r.status = OfficerStatus::Failed;
				r.reason = "expired";
				r.reply_tick = now;
			}
		std::erase_if(*list,
		              [&](const auto &r)
		              {
			              return std::max(r.expires + 20, r.reply_tick + r.retry_ticks) < now;
		              });
	}
}
bool officer_coordination_action(ConsoleControllerRuntime &controller, KnowledgeTick now, const std::string &name,
                                 std::span<const state_tree::Value> args, const OfficerActionPorts &ports)
{

	const bool manifest = name == "coordination.request_manifest" || name == "coordination.maintain_manifest";
	const bool scan = name == "coordination.request_scan";
	if (name == "coordination.request" || name == "coordination.maintain" || manifest || scan)
	{
		const bool maintain = name == "coordination.maintain" || name == "coordination.maintain_manifest";
		OfficerRequest r;
		std::vector<state_tree::Value> canonical;
		if (scan)
		{
			r.capability = args[3].text;
			if (std::find(scan_capabilities.begin(), scan_capabilities.end(), r.capability) == scan_capabilities.end())
				return false;
			const auto &observed = ports.observation();
			const auto target = args[2].number;
			const bool known =
			    target > 0 && (std::any_of(observed.contacts.begin(), observed.contacts.end(),
			                               [&](const auto &c)
			                               {
				                               return c.id == target;
			                               }) ||
			                   std::any_of(observed.bearing_contacts.begin(), observed.bearing_contacts.end(),
			                               [&](const auto &c)
			                               {
				                               return c.id == target;
			                               }));
			if (!known)
				return false;
			r.request_revision = std::max<KnowledgeTick>(1, now);
			canonical = {state_tree::Value::string("scan"), args[0], args[1], args[2],
			             state_tree::Value::integer(1),     args[4], args[5]};
			args = canonical;
		}
		if (manifest)
		{
			r.mission = args[2].text;
			const auto &observed = ports.observation();
			const AwayMissionDecision *mission = nullptr;
			for (const auto &row : observed.away_missions)
				if (row.id == r.mission &&
				    (row.phase == "available" || row.phase == "open" || row.phase == "recovered") &&
				    (controller_operation_owns(controller, "mission", row.dispatch_system) ||
				     controller_operation_owns(controller, "mission_send", row.dispatch_system)))
					mission = &row;
			if (!mission || !args[3].number)
				return false;
			r.request_revision = mission->revision;
			canonical = {state_tree::Value::string("manifest"),
			             args[0],
			             args[1],
			             state_tree::Value::integer(mission->target),
			             args[3],
			             args[4],
			             args[5]};
			if (maintain)
				canonical.push_back(args[6]);
			args = canonical;
		}
		r.operation = static_cast<OfficerOperation>(officer_index(officer_operations, args[0].text));
		try
		{
			r.peer = ports.peer(args[1].text, args[2].text);
		}
		catch (const std::exception &)
		{
			return false;
		}
		if (!r.peer || (r.operation == OfficerOperation::Manifest && !manifest) ||
		    (r.operation == OfficerOperation::Scan && !scan))
			return false;
		r.target = args[3].number;
		r.amount = args[4].number;
		r.requested_amount = r.amount;
		r.reply_tick = now;
		r.retry_ticks = maintain ? args[7].number : 0;
		r.priority = args[5].number;
		if (r.amount < 0 || r.amount > 1000000000 || r.priority < 0 || r.priority > 2 || args[6].number < 1 ||
		    args[6].number > 72000 || (maintain && (args[7].number < 1 || args[7].number > 72000)))
			return false;
		if (r.operation == OfficerOperation::Manoeuvre || r.operation == OfficerOperation::Repair)
		{
			const auto &observed = ports.observation();
			bool permitted = false;
			if (r.operation == OfficerOperation::Manoeuvre)
				permitted = std::any_of(observed.contacts.begin(), observed.contacts.end(),
				                        [&](const auto &contact)
				                        {
					                        return contact.id == r.target && contact.disposition == "hostile";
				                        });
			else
				for (const auto &compartment : observed.combat.compartments)
					if (compartment.index == r.target && compartment.hp < compartment.maximum)
						permitted = true;
			if (r.operation == OfficerOperation::Repair)
			{
				for (const auto &need : observed.repair_needs)
					if (need.compartment_index == r.target)
						permitted = true;
				for (const auto &fault : observed.combat.faults)
					if (fault.compartment_index == r.target && !fault.destroyed)
						permitted = true;
			}
			if (!permitted)
				return false;
		}
		r.expires = now + args[6].number;
		bool replaced = false;
		for (auto &old : controller.outgoing_requests)
			if (old.peer == r.peer && old.operation == r.operation)
			{
				const bool same = old.target == r.target && old.mission == r.mission &&
				                  old.capability == r.capability && old.requested_amount == r.requested_amount &&
				                  old.priority == r.priority;
				const bool active =
				    old.expires > now &&
				    (old.status == OfficerStatus::Requested || old.status == OfficerStatus::Accepted ||
				     (old.status == OfficerStatus::Completed && officer_continuing_operation(old.operation)));
				if (same && active)
					return true;
				if (active)
				{
					replaced = true;
					old.status = OfficerStatus::Cancelled;
					old.reason = "request_replaced";
					old.reply_tick = now;
					ports.send(old, true);
				}
			}
		if (maintain && !replaced)
			for (auto old = controller.outgoing_requests.rbegin(); old != controller.outgoing_requests.rend(); ++old)
				if (old->peer == r.peer && old->operation == r.operation)
				{
					if (old->target == r.target && old->mission == r.mission &&
					    old->requested_amount == r.requested_amount && old->priority == r.priority &&
					    now < old->reply_tick + args[7].number)
						return true;
					break;
				}
		if (controller.outgoing_requests.size() >= 128)
			return false;
		r.id = ports.send(r, false);
		controller.outgoing_requests.push_back(r);
		return true;
	}
	if (name == "coordination.withdraw" || name == "coordination.withdraw_from")
	{
		const auto op = static_cast<OfficerOperation>(officer_index(officer_operations, args[0].text));
		const auto peer = name == "coordination.withdraw_from" ? ports.peer(args[1].text, args[2].text) : 0;
		for (auto &r : controller.outgoing_requests)
			if (r.operation == op && (!peer || r.peer == peer) &&
			    (r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
			     (r.status == OfficerStatus::Completed && officer_continuing_operation(r.operation))))
			{
				r.status = OfficerStatus::Cancelled;
				r.reason = "no_longer_required";
				r.reply_tick = now;
				ports.send(r, true);
			}
		return true;
	}
	const auto *pending = pending_officer_request(controller, now);
	if (name == "coordination.respond_to")
	{
		pending = nullptr;
		for (const auto &request : controller.incoming_requests)
			if (request.id == static_cast<KnowledgeId>(args[0].number) && request.expires > now &&
			    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted) &&
			    officer_supports(controller, request.operation))
				pending = &request;
		args = args.subspan(1);
	}
	if (!pending)
		return false;
	const auto request_id = pending->id;
	const auto status = static_cast<OfficerStatus>(officer_index(officer_statuses, args[0].text));
	if (status == OfficerStatus::Requested || status == OfficerStatus::Cancelled || args[1].number < 0 ||
	    args[1].number > 1000000000)
		return false;
	for (auto &r : controller.incoming_requests)
		if (r.id == request_id)
		{
			std::uint64_t evidence_revision = 0;
			if (args[2].text == "execution_started")
			{
				if (status != OfficerStatus::Accepted || r.status != OfficerStatus::Accepted ||
				    args[1].number != r.amount || !execution_observed(controller, ports.observation(), r))
					return false;
				evidence_revision = ports.observation().observed_tick;
			}
			if ((r.operation == OfficerOperation::Power || r.operation == OfficerOperation::Cooling) &&
			    status == OfficerStatus::Completed)
			{
				if (r.status != OfficerStatus::Accepted || args[1].number != r.amount)
					return false;
				const auto proof = supply_completion_evidence(controller, ports.observation(), r, args[1].number);
				if (!proof)
					return false;
				evidence_revision = *proof;
			}
			if (r.operation == OfficerOperation::Manoeuvre && status == OfficerStatus::Completed)
			{
				if (r.status != OfficerStatus::Accepted || args[1].number != r.amount)
					return false;
				const auto &observed = ports.observation();
				bool proven = false;
				for (const auto &contact : observed.contacts)
					if (contact.id == r.target && contact.disposition == "hostile" &&
					    fixed::length({contact.x - observed.own.x, contact.y - observed.own.y}) <= r.amount)
						proven = true;
				if (!proven)
					return false;
				evidence_revision = observed.observed_tick;
			}
			if (r.operation == OfficerOperation::Manifest)
			{
				if (args[1].number > r.requested_amount ||
				    ((status == OfficerStatus::Accepted || status == OfficerStatus::Completed) &&
				     args[1].number == 0) ||
				    (status == OfficerStatus::Completed && r.status == OfficerStatus::Accepted &&
				     args[1].number > r.amount))
					return false;
				if (status == OfficerStatus::Completed)
				{
					bool proven = false;
					const auto &observed = ports.observation();
					for (const auto &row : observed.away_missions)
						if (row.id == r.mission && row.target == r.target && row.department)
						{
							const auto &department = *row.department;
							proven = department.ready && department.assigned >= args[1].number;
							evidence_revision = row.revision;
						}
					if (!proven)
						return false;
				}
			}
			if (r.operation == OfficerOperation::Scan && status == OfficerStatus::Completed)
			{
				if (r.status != OfficerStatus::Accepted)
					return false;
				const auto proof = scan_completion_evidence(ports.observation(), r);
				if (!proof || args[1].number != 1)
					return false;
				evidence_revision = *proof;
			}
			if (r.operation == OfficerOperation::Repair && status == OfficerStatus::Completed)
			{
				if (r.status != OfficerStatus::Accepted)
					return false;
				const auto &observed = ports.observation();
				const auto &combat = observed.combat;
				bool proven = false;
				if (combat.present)
				{
					for (const auto &room : combat.compartments)
						if (room.index == r.target)
						{
							const bool local = room.inspected;
							const bool reported = room.reported_inspection && room.inspection_tick >= r.reply_tick &&
							                      room.inspection_fault_count == 0 &&
							                      room.inspection_hp >= room.inspection_maximum;
							proven = (local || reported) && room.hp >= room.maximum;
							if (reported && !local)
								evidence_revision = room.inspection_revision;
						}
					for (const auto &fault : combat.faults)
						if (fault.compartment_index == r.target)
							proven = false;
				}
				if (!proven)
					return false;
				if (!evidence_revision)
					evidence_revision = observed.observed_tick;
			}

			if ((r.operation == OfficerOperation::Power || r.operation == OfficerOperation::Cooling) &&
			    status == OfficerStatus::Accepted)
			{
				if (args[1].number > r.requested_amount)
					return false;
				// Admission enacts the policy's explicit choice of an exclusive local supply commitment.
				// It does not rank requests or change any physical command on their behalf.
				if (r.status == OfficerStatus::Requested)
					for (auto &other : controller.incoming_requests)
						if (other.id != r.id && other.operation == r.operation && other.expires > now &&
						    (other.status == OfficerStatus::Accepted || other.status == OfficerStatus::Completed))
						{
							other.status = OfficerStatus::Failed;
							other.reason = "preempted";
							other.reply_tick = now;
							ports.send(other, true);
						}
			}
			if (r.status == status && r.amount == args[1].number && r.reason == args[2].text)
				return true;
			r.status = status;
			r.amount = args[1].number;
			r.reason = args[2].text;
			r.reply_tick = now;
			r.evidence_revision = evidence_revision;
			ports.send(r, true);
			return true;
		}
	return false;
}
} // namespace phoenix
