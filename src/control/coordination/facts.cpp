module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

module phoenix.control.coordination_facts;
import phoenix.control.coordination;
import phoenix.control.execution;
import phoenix.world.state;

namespace phoenix
{
void controller_coordination_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                                   KnowledgeTick now, const ControllerCoordinationInputs &inputs,
                                   std::vector<state_tree::Value> &values)
{

	const auto at = controller.brain->coordination_base;
	const auto *r = pending_officer_request(controller, now);
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
					if (system.id == inputs.systems.at(consumer))
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
					bearing = fixed::bearing({contact.x - observed.own.x, contact.y - observed.own.y});
					done = range <= r->amount;
					blocked = blocked || inputs.navigation_blocked || inputs.hold;
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
						if (system.id == inputs.systems.at(consumer) && system.telemetry.contains("enabled") &&
						    !system.telemetry.at("enabled"))
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
} // namespace phoenix
