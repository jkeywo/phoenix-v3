static constexpr std::array<const char *, 4> officer_operations{"power", "cooling", "manoeuvre", "repair"};
static constexpr std::array<const char *, 6> officer_statuses{"requested", "accepted", "refused",
                                                              "completed", "failed",   "cancelled"};
template <std::size_t N>
static std::size_t officer_index(const std::array<const char *, N> &names, const std::string &name)
{
	for (std::size_t i = 0; i < N; ++i)
		if (name == names[i])
			return i;
	throw std::runtime_error("Unknown officer protocol value: " + name);
}
bool officer_supports(const ConsoleControllerRuntime &controller, OfficerOperation operation) const
{
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
const OfficerRequest *officer_pending(const ConsoleControllerRuntime &controller) const
{
	const OfficerRequest *best = nullptr;
	const auto committed = [](const OfficerRequest &r)
	{
		return r.status == OfficerStatus::Accepted || r.status == OfficerStatus::Completed;
	};
	for (const auto &r : controller.incoming_requests)
		if ((r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
		     (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair)) &&
		    r.expires > ecs.get<Clock>().tick && officer_supports(controller, r.operation))
			if (!best || r.priority > best->priority ||
			    (r.priority == best->priority && committed(r) && !committed(*best)) ||
			    (r.priority == best->priority && committed(r) == committed(*best) && r.id < best->id))
				best = &r;
	return best;
}
KnowledgeId officer_message(EntityId id, ConsoleControllerRuntime &controller, const OfficerRequest &request,
                            bool reply)
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
	message.presentation.title_key = "Officer coordination";
	message.presentation.body_key =
	    "{operation}: {status}; target {target}, amount {amount}, request {request}. {reason}";
	message.presentation.parameters = {
	    {"operation", officer_operations.at(static_cast<std::size_t>(request.operation))},
	    {"status", officer_statuses.at(static_cast<std::size_t>(request.status))},
	    {"reason", request.reason}};
	message.presentation.parameters["target"] = std::to_string(request.target);
	message.presentation.parameters["amount"] = std::to_string(request.amount);
	message.presentation.parameters["request"] = std::to_string(reply ? request.id : 0);
	message.presentation.urgency = static_cast<std::uint8_t>(std::min<std::int64_t>(request.priority, 2));
	const auto speaker = officer_speaker(id, source.console, controller.definition.id);
	message.presentation.speaker_name = speaker.first;
	message.presentation.speaker_portrait = speaker.second;
	message.delay = 1;
	message.expires_tick = std::max<KnowledgeTick>(request.expires, ecs.get<Clock>().tick + 20);
	return knowledge.admit_message(source, std::move(message), ecs.get<Clock>().tick);
}
void process_controller_coordination(EntityId id, std::size_t index)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	const ControllerAddress address{id, controller.definition.console, controller.definition.id};
	const auto schema = knowledge.definition().schema("phoenix.coordination");
	const auto inbox =
	    controller_inboxes.contains(address) ? controller_inboxes.at(address) : knowledge.peek_controller(address);
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
				knowledge.acknowledge_controller(address, m.id);
				controller.coordination_receipts.erase(m.id);
			}
			continue;
		}
		const auto &fields = m.value.payload.fields;
		const auto value = [&](const char *name) -> const KnowledgeValue &
		{
			return fields.at(knowledge.definition().field(schema, name));
		};
		try
		{
			OfficerRequest r;
			r.operation = static_cast<OfficerOperation>(
			    officer_index(officer_operations, std::get<std::string>(value("operation"))));
			r.status =
			    static_cast<OfficerStatus>(officer_index(officer_statuses, std::get<std::string>(value("status"))));
			r.peer = m.value.sender_system;
			r.id = m.id;
			r.target = std::get<std::int64_t>(value("target"));
			r.amount = std::get<std::int64_t>(value("amount"));
			r.requested_amount = r.amount;
			r.reply_tick = ecs.get<Clock>().tick;
			r.priority = std::get<std::int64_t>(value("priority"));
			r.expires = std::get<std::int64_t>(value("expires"));
			r.reason = std::get<std::string>(value("reason"));
			const auto correlation = std::get<std::int64_t>(value("request"));
			if (!r.expires)
				r.expires = m.value.expires_tick;
			if (r.priority < 0 || r.priority > 2 || r.amount < 0 || r.amount > 1000000000 ||
			    r.expires <= ecs.get<Clock>().tick || r.expires > ecs.get<Clock>().tick + 72000 ||
			    m.value.payload.subject != id ||
			    std::none_of(entities.at(id).get<EntityControls>().controllers.begin(),
			                 entities.at(id).get<EntityControls>().controllers.end(),
			                 [&](const auto &owner)
			                 {
				                 return knowledge.controller_id({id, owner.definition.console, owner.definition.id}) ==
				                        r.peer;
			                 }))
				throw std::runtime_error("Invalid or expired request");
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
						officer_message(id, controller, r, true);
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
						original.reply_tick = ecs.get<Clock>().tick;
					}
			}
		}
		catch (const std::exception &)
		{ /* Malformed protocol data is not a command. */
		}
		if (protocol_prefix)
			knowledge.acknowledge_controller(address, m.id);
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
			if (r.expires <= ecs.get<Clock>().tick &&
			    (r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
			     (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair)))
			{
				r.status = OfficerStatus::Failed;
				r.reason = "expired";
				r.reply_tick = ecs.get<Clock>().tick;
			}
		std::erase_if(*list,
		              [&](const auto &r)
		              {
			              return std::max(r.expires + 20, r.reply_tick + r.retry_ticks) < ecs.get<Clock>().tick;
		              });
	}
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
				    (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair))
				{
					r.status = OfficerStatus::Failed;
					r.reason = "preempted";
					officer_message(id, controller, r, true);
				}
				break;
			}
	}
}
void controller_coordination_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                                   std::vector<state_tree::Value> &values) const
{
	const auto at = controller.brain->coordination_base;
	const auto *r = officer_pending(controller);
	values.at(at) = state_tree::Value::boolean(r != nullptr);
	for (std::size_t i = 10; i <= 12; ++i)
		values.at(at + i) = state_tree::Value::boolean(false);
	for (std::size_t i = 13; i <= 15; ++i)
		values.at(at + i) = state_tree::Value::integer(0);
	for (std::size_t i = 0; i < 4; ++i)
	{
		values.at(at + 16 + i * 2) = state_tree::Value::string("none");
		values.at(at + 17 + i * 2) = state_tree::Value::integer(0);
		for (const auto &reply : controller.outgoing_requests)
			if (static_cast<std::size_t>(reply.operation) == i)
			{
				values.at(at + 16 + i * 2) =
				    state_tree::Value::string(officer_statuses.at(static_cast<std::size_t>(reply.status)));
				values.at(at + 17 + i * 2) = state_tree::Value::integer(reply.amount);
			}
	}
	if (!r)
		return;
	values.at(at + 1) = state_tree::Value::string(officer_operations.at(static_cast<std::size_t>(r->operation)));
	values.at(at + 2) = state_tree::Value::string(officer_statuses.at(static_cast<std::size_t>(r->status)));
	values.at(at + 3) = state_tree::Value::string(r->reason);
	values.at(at + 4) = state_tree::Value::string("request/" + std::to_string(r->id));
	const std::array<std::int64_t, 5> numbers{static_cast<std::int64_t>(r->id), r->target, r->amount, r->priority,
	                                          static_cast<std::int64_t>(r->expires)};
	for (std::size_t i = 0; i < numbers.size(); ++i)
		values.at(at + 5 + i) = state_tree::Value::integer(numbers[i]);
	bool feasible = false, done = false, blocked = false;
	std::int64_t limit = r->requested_amount, bearing = observed.own.heading, range = 0;
	if (r->operation == OfficerOperation::Power || r->operation == OfficerOperation::Cooling)
		for (const auto &read : controller.brain->command_reads)
			if (controller.brain->program.registry.observations.at(read.destination).name == "summary.level" &&
			    values.at(read.destination).type == state_tree::Type::Integer)
				limit = std::min(limit, values.at(read.destination).number);
	const auto &entity = entities.at(observed.own.id);
	const bool supply = r->operation == OfficerOperation::Power || r->operation == OfficerOperation::Cooling;
	std::int64_t supply_minimum = 0, supply_maximum = 1000000000;
	if (supply)
	{
		done = true;
		for (const auto &command : controller.definition.commands)
			if ((r->operation == OfficerOperation::Power && command.action == OperatingAction::GeneratorOutput) ||
			    (r->operation == OfficerOperation::Cooling && command.action == OperatingAction::CoolingAllocation))
			{
				supply_minimum = std::max(supply_minimum, command.minimum);
				supply_maximum = std::min(supply_maximum, command.maximum);
			}
		if (supply_minimum <= supply_maximum)
			limit = std::clamp(limit, supply_minimum, supply_maximum);
	}
	for (const auto &command : controller.definition.commands)
	{
		const bool supports =
		    (r->operation == OfficerOperation::Power && command.action == OperatingAction::GeneratorOutput) ||
		    (r->operation == OfficerOperation::Cooling && command.action == OperatingAction::CoolingAllocation);
		if (supports)
		{
			for (const auto consumer : command.consumers)
			{
				bool seen = false;
				for (const auto &system : observed.systems)
					if (system.id == (*fittings(entity)).values[consumer].id)
					{
						const auto &t = system.telemetry;
						seen = true;
						feasible = true;
						blocked = blocked || (t.contains("enabled") && !t.at("enabled")) ||
						          (t.contains("thermal_stopped") && t.at("thermal_stopped"));
						const auto field =
						    r->operation == OfficerOperation::Power ? "actual_output" : "actual_coolant_used";
						done = done && t.contains(field) &&
						       (r->operation == OfficerOperation::Power ? t.at(field) == limit : t.at(field) >= limit);
					}
				if (!seen)
				{
					blocked = true;
					done = false;
				}
			}
		}
		if (r->operation == OfficerOperation::Manoeuvre &&
		    (command.action == OperatingAction::FollowContact || command.action == OperatingAction::Heading ||
		     command.action == OperatingAction::Speed))
			for (const auto &contact : observed.contacts)
				if (contact.id == r->target && contact.disposition == "hostile")
				{
					feasible = true;
					range = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
					bearing = combat_bearing({contact.x - observed.own.x, contact.y - observed.own.y});
					done = range <= r->amount;
					const auto &bridge = entity.get<BridgeBlackboard>();
					blocked = blocked || bridge.navigation.blocked;
					if (bridge.stances.contains(controller.definition.console))
						blocked = blocked || definition(observed.own.id)
						                             .console_bridge.at(controller.definition.console)
						                             .stances.at(bridge.stances.at(controller.definition.console))
						                             .id == "hold";
				}
		if (r->operation == OfficerOperation::Repair && command.action == OperatingAction::RepairCompartment)
			for (const auto &compartment : combat_observation(observed).at("compartments"))
				if (compartment.at("index") == r->target)
				{
					feasible = true;
					done = compartment.at("hp") >= compartment.at("maximum");
				}
	}
	if (r->operation == OfficerOperation::Repair || r->operation == OfficerOperation::Manoeuvre)
		for (const auto &command : controller.definition.commands)
			if ((r->operation == OfficerOperation::Repair && command.action == OperatingAction::RepairCompartment) ||
			    (r->operation == OfficerOperation::Manoeuvre &&
			     (command.action == OperatingAction::FollowContact || command.action == OperatingAction::Speed ||
			      command.action == OperatingAction::Heading)))
				for (const auto consumer : command.consumers)
					for (const auto &system : observed.systems)
						if (system.id == (*fittings(entity)).values[consumer].id &&
						    system.telemetry.contains("enabled") && !system.telemetry.at("enabled"))
							blocked = true;
	if (r->operation == OfficerOperation::Repair)
		for (const auto &fault : combat_observation(observed).at("faults"))
			if (fault.value("compartment_index", std::int64_t{0}) == r->target)
			{
				done = false;
				if (fault.value("destroyed", false))
					blocked = true;
			}
	const auto &stance = values.at(controller.brain->own[ControllerOwnField::Stance]);
	if (supply && (supply_minimum > supply_maximum || r->requested_amount < supply_minimum))
	{
		feasible = false;
		blocked = true;
		done = false;
	}
	if (stance.type == state_tree::Type::Text && !stance.text.empty() &&
	    stance.text != controller.brain->routine_stance)
		blocked = true;
	if (r->operation == OfficerOperation::Manoeuvre)
		for (std::size_t i = 0; i < controller.brain->program.registry.observations.size(); ++i)
			if (controller.brain->program.registry.observations[i].name == "objective.status" &&
			    values[i].type == state_tree::Type::Text && values[i].text == "active")
				blocked = true;
	values.at(at + 10) = state_tree::Value::boolean(feasible);
	values.at(at + 11) = state_tree::Value::boolean(done);
	values.at(at + 12) = state_tree::Value::boolean(blocked);
	values.at(at + 13) = state_tree::Value::integer(bearing);
	values.at(at + 14) = state_tree::Value::integer(range);
	values.at(at + 15) = state_tree::Value::integer(limit);
}
bool controller_coordination_action(EntityId id, std::size_t index, const std::string &name,
                                    std::span<const state_tree::Value> args)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (name == "coordination.request" || name == "coordination.maintain")
	{
		const bool maintain = name == "coordination.maintain";
		OfficerRequest r;
		r.operation = static_cast<OfficerOperation>(officer_index(officer_operations, args[0].text));
		r.peer = knowledge.controller_id({id, args[1].text, args[2].text});
		r.target = args[3].number;
		r.amount = args[4].number;
		r.requested_amount = r.amount;
		r.reply_tick = ecs.get<Clock>().tick;
		r.retry_ticks = maintain ? args[7].number : 0;
		r.priority = args[5].number;
		if (r.amount < 0 || r.amount > 1000000000 || r.priority < 0 || r.priority > 2 || args[6].number < 1 ||
		    args[6].number > 72000 || (maintain && (args[7].number < 1 || args[7].number > 72000)))
			return false;
		if (r.operation == OfficerOperation::Manoeuvre || r.operation == OfficerOperation::Repair)
		{
			const ConsoleAddress address{id, controller.definition.console};
			const auto fallback = observation_snapshots.contains(address) ? std::optional<Observation>{}
			                                                              : capture_decision_observation(address);
			const auto &observed = fallback ? *fallback : observation_snapshots.at(address);
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
		r.expires = ecs.get<Clock>().tick + args[6].number;
		bool replaced = false;
		for (auto &old : controller.outgoing_requests)
			if (old.peer == r.peer && old.operation == r.operation)
			{
				const bool same =
				    old.target == r.target && old.requested_amount == r.requested_amount && old.priority == r.priority;
				const bool active =
				    old.expires > ecs.get<Clock>().tick &&
				    (old.status == OfficerStatus::Requested || old.status == OfficerStatus::Accepted ||
				     (old.status == OfficerStatus::Completed && old.operation != OfficerOperation::Repair));
				if (same && active)
					return true;
				if (active)
				{
					replaced = true;
					old.status = OfficerStatus::Cancelled;
					old.reason = "request_replaced";
					old.reply_tick = ecs.get<Clock>().tick;
					officer_message(id, controller, old, true);
				}
			}
		if (maintain && !replaced)
			for (auto old = controller.outgoing_requests.rbegin(); old != controller.outgoing_requests.rend(); ++old)
				if (old->peer == r.peer && old->operation == r.operation)
				{
					if (old->target == r.target && old->requested_amount == r.requested_amount &&
					    old->priority == r.priority && ecs.get<Clock>().tick < old->reply_tick + args[7].number)
						return true;
					break;
				}
		if (controller.outgoing_requests.size() >= 128)
			return false;
		r.id = officer_message(id, controller, r, false);
		controller.outgoing_requests.push_back(r);
		return true;
	}
	if (name == "coordination.withdraw" || name == "coordination.withdraw_from")
	{
		const auto op = static_cast<OfficerOperation>(officer_index(officer_operations, args[0].text));
		const auto peer =
		    name == "coordination.withdraw_from" ? knowledge.controller_id({id, args[1].text, args[2].text}) : 0;
		for (auto &r : controller.outgoing_requests)
			if (r.operation == op && (!peer || r.peer == peer) &&
			    (r.status == OfficerStatus::Requested || r.status == OfficerStatus::Accepted ||
			     (r.status == OfficerStatus::Completed && r.operation != OfficerOperation::Repair)))
			{
				r.status = OfficerStatus::Cancelled;
				r.reason = "no_longer_required";
				r.reply_tick = ecs.get<Clock>().tick;
				officer_message(id, controller, r, true);
			}
		return true;
	}
	const auto *pending = officer_pending(controller);
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
			r.reply_tick = ecs.get<Clock>().tick;
			officer_message(id, controller, r, true);
			return true;
		}
	return false;
}
Json save_controller_coordination(const ConsoleControllerRuntime &controller) const
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
			                {"reason", r.reason}});
		result.push_back(rows);
	}
	result.push_back(controller.coordination_receipts);
	return result;
}
void restore_controller_coordination(EntityId ship, ConsoleControllerRuntime &controller, const Json &saved)
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
			const int operation = row.at("operation"), status = row.at("status");
			if (!r.id || !r.peer || !ids.insert(r.id).second || operation < 0 || operation >= 4 || status < 0 ||
			    status >= 6 || r.amount < 0 || r.amount > 1000000000 || r.requested_amount < 0 ||
			    r.requested_amount > 1000000000 || r.priority < 0 || r.priority > 2 || r.expires < 0 ||
			    r.reply_tick < 0 || r.retry_ticks < 0 || r.retry_ticks > 72000 || r.reason.size() > 4096)
				throw std::runtime_error("Invalid officer request");
			const auto &owners = entities.at(ship).get<EntityControls>().controllers;
			if (std::none_of(owners.begin(), owners.end(),
			                 [&](const auto &owner)
			                 {
				                 return knowledge.controller_id(
				                            {ship, owner.definition.console, owner.definition.id}) == r.peer;
			                 }))
				throw std::runtime_error("Saved request has an unknown peer");
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
