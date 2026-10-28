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
import phoenix.control.execution;

namespace phoenix
{
void process_officer_protocol(EntityId id, KnowledgeTick now, ConsoleControllerRuntime &controller,
                              std::span<const CoordinationDelivery> inbox, const CompiledKnowledge &definitions,
                              std::span<const KnowledgeId> known_peers, const OfficerProtocolPorts &ports)
{
	const auto schema = definitions.schema("phoenix.coordination");
	std::set<KnowledgeId> pending_protocol;
	for (const auto &delivery : inbox)
		if (delivery.message.value.payload.schema == schema)
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
		if (m.value.payload.schema != schema)
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
					    (original.status == OfficerStatus::Requested || original.status == OfficerStatus::Accepted ||
					     original.status == OfficerStatus::Completed))
					{
						original.status = r.status;
						if (!(m.value.origin == ControlOrigin::Human && r.status == OfficerStatus::Accepted &&
						      r.reason == "accept_requested_limit"))
							original.amount = r.amount;
						original.reason = r.reason;
						original.reply_tick = now;
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
			if (r.expires <= now && (r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
			                         (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair)))
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

	if (name == "coordination.request" || name == "coordination.maintain")
	{
		const bool maintain = name == "coordination.maintain";
		OfficerRequest r;
		r.operation = static_cast<OfficerOperation>(officer_index(officer_operations, args[0].text));
		r.peer = ports.peer(args[1].text, args[2].text);
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
				for (const auto &compartment : combat_observation(observed).at("compartments"))
					if (compartment.at("index") == r.target && compartment.at("hp") < compartment.at("maximum"))
						permitted = true;
			if (r.operation == OfficerOperation::Repair)
				for (const auto &fault : combat_observation(observed).at("faults"))
					if (fault.value("compartment_index", std::int64_t{0}) == r.target &&
					    !fault.value("destroyed", false))
						permitted = true;
			if (!permitted)
				return false;
		}
		r.expires = now + args[6].number;
		bool replaced = false;
		for (auto &old : controller.outgoing_requests)
			if (old.peer == r.peer && old.operation == r.operation)
			{
				const bool same =
				    old.target == r.target && old.requested_amount == r.requested_amount && old.priority == r.priority;
				const bool active =
				    old.expires > now &&
				    (old.status == OfficerStatus::Requested || old.status == OfficerStatus::Accepted ||
				     (old.status == OfficerStatus::Completed && old.operation != OfficerOperation::Repair));
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
					if (old->target == r.target && old->requested_amount == r.requested_amount &&
					    old->priority == r.priority && now < old->reply_tick + args[7].number)
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
			     (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair)))
			{
				r.status = OfficerStatus::Cancelled;
				r.reason = "no_longer_required";
				r.reply_tick = now;
				ports.send(r, true);
			}
		return true;
	}
	const auto *pending = pending_officer_request(controller, now);
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
			if (r.status == status && r.amount == args[1].number && r.reason == args[2].text)
				return true;
			r.status = status;
			r.amount = args[1].number;
			r.reason = args[2].text;
			r.reply_tick = now;
			ports.send(r, true);
			return true;
		}
	return false;
}
} // namespace phoenix
