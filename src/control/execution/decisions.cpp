module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

module phoenix.control.execution;
import phoenix.gameplay.flight;

namespace phoenix
{
// Borrow the frozen combat projection. Json::value returns a deep copy, which
// multiplies compartment/weapon/shield data by the number of Controllers.
const Json &combat_observation(const Observation &observation)
{
	static const Json empty{{"compartments", Json::array()},
	                        {"faults", Json::array()},
	                        {"shields", Json::array()},
	                        {"weapons", Json::array()}};
	const auto found = observation.gameplay.find("combat");
	return found == observation.gameplay.end() ? empty : *found;
}
void controller_decision_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                               const ControllerDecisionContext &context, std::vector<state_tree::Value> &values)
{
	std::array<std::int64_t, 18> facts{};
	facts[2] = observed.own.heading;
	facts[6] = facts[7] = 1000;
	EntityId current = 0;
	std::int64_t repair = 0, repair_system = 0;
	for (std::size_t i = 0; i < controller.commands.size(); ++i)
	{
		const auto &spec = controller.definition.commands[i];
		if (spec.action == OperatingAction::WeaponTarget || spec.action == OperatingAction::DirectedTarget)
			current = controller.commands[i].value;
		if (spec.action == OperatingAction::RepairCompartment)
			repair = controller.commands[i].value;
		if (spec.action == OperatingAction::RepairSystem)
			repair_system = controller.commands[i].value;
		if (spec.action == OperatingAction::Speed)
		{
			facts[9] = spec.maximum / 2;
			facts[10] = spec.maximum / 5;
		}
	}
	const BodyView *target = nullptr;
	const auto eligible_threat = [&](const BodyView &contact)
	{
		return !context.escort ||
		       fixed::length(fixed::Vec{contact.x, contact.y} - context.escort_anchor) <= context.engagement_range;
	};
	std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
	for (const auto &contact : observed.contacts)
		if (contact.radar_icon == RadarIcon::ship && contact.disposition == "hostile" && eligible_threat(contact))
		{
			const auto distance = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
			if (!target || contact.id == current ||
			    (target->id != current && (distance < nearest || (distance == nearest && contact.id < target->id))))
			{
				target = &contact;
				nearest = distance;
			}
		}
	if (!target)
		for (const auto &b : observed.bearing_contacts)
			if (b.id == current)
			{
				facts[0] = b.id;
				facts[1] = -1;
				facts[2] = b.bearing_mdeg;
			}
	if (target)
	{
		facts[0] = target->id;
		facts[1] = nearest;
		facts[2] = fixed::bearing({target->x - observed.own.x, target->y - observed.own.y});
	}
	if (target)
	{
		facts[13] = target->id;
		std::int64_t maximum = 0;
		const auto acceleration = context.deceleration;
		for (const auto &command : controller.definition.commands)
			if (command.action == OperatingAction::Speed)
			{
				maximum = std::max(maximum, command.maximum);
			}
		const fixed::Vec delta{target->x - observed.own.x, target->y - observed.own.y};
		const auto remaining = std::max<std::int64_t>(0, fixed::length(delta) - 10000);
		const auto approach = std::min(
		    {maximum, remaining / 2,
		     static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(2 * acceleration * remaining)))});
		Pose own_pose;
		own_pose.position = {observed.own.x, observed.own.y};
		own_pose.heading = observed.own.heading;
		const auto guidance = pursuit_guidance(
		    own_pose, fixed::Vec{target->vx, target->vy} + fixed::scale(fixed::normal(delta), approach), maximum,
		    observed.own.heading);
		facts[15] = guidance.heading;
		facts[16] = guidance.speed;
		facts[17] = target->altitude + context.altitude_offset;
	}
	facts[14] = context.point_defence_target;
	const auto &combat = combat_observation(observed);
	// Equipment indices are static fitting metadata. Candidate damage and duties
	// come only from this Console's permitted snapshot and its accepted requests.
	std::set<std::int64_t> disabled_compartments;
	for (const auto &compartment : combat.at("compartments"))
		if (compartment.at("disabled").get<bool>())
			disabled_compartments.insert(compartment.at("index").get<std::int64_t>());
	const auto needed = [&](std::int64_t compartment)
	{
		return std::any_of(controller.incoming_requests.begin(), controller.incoming_requests.end(),
		                   [&](const auto &request)
		                   {
			                   return request.operation == OfficerOperation::Repair &&
			                          request.status == OfficerStatus::Accepted && request.target == compartment &&
			                          request.expires > context.tick;
		                   });
	};
	const auto priority_for = [&](std::int64_t compartment, bool disabled)
	{
		return disabled ? (needed(compartment) ? 3 : 2) : 1;
	};
	const auto better =
	    [](int priority, int best_priority, std::int64_t candidate, std::int64_t best, std::int64_t current)
	{
		return priority > best_priority || (priority == best_priority &&
		                                    (candidate == current || (best != current && (!best || candidate < best))));
	};
	int best_priority = -1, best_system_priority = -1;
	for (const auto &compartment : combat.at("compartments"))
	{
		if (compartment.at("hp") >= compartment.at("maximum"))
			continue;
		const auto candidate = compartment.at("index").get<std::int64_t>();
		const auto priority = priority_for(candidate, disabled_compartments.contains(candidate));
		if (better(priority, best_priority, candidate, facts[4], repair))
		{
			best_priority = priority;
			facts[4] = candidate;
		}
	}
	for (const auto &fault : combat.at("faults"))
	{
		if (fault.value("destroyed", false))
			continue;
		const auto candidate = fault.value("compartment_index", std::int64_t{0});
		if (!candidate)
			continue;
		const auto system_id = fault.at("system").get<std::string>();
		bool disabled = disabled_compartments.contains(candidate);
		for (const auto &system : observed.systems)
			if (system.id == system_id)
				disabled =
				    disabled || (system.telemetry.contains("enabled") && !system.telemetry.at("enabled")) ||
				    (system.telemetry.contains("structural_disabled") && system.telemetry.at("structural_disabled"));
		const auto priority = priority_for(candidate, disabled);
		if (better(priority, best_priority, candidate, facts[4], repair))
		{
			best_priority = priority;
			facts[4] = candidate;
		}
		if (const auto at = context.repair_system_indices.find(system_id); at != context.repair_system_indices.end())
		{
			const auto system = at->second;
			if (better(priority, best_system_priority, system, facts[5], repair_system))
			{
				best_system_priority = priority;
				facts[5] = system;
			}
		}
	}
	bool point_defence = false, intercepting = false;
	const auto owns = [&](const std::string &system)
	{
		return context.owned_systems.contains(system);
	};
	for (const auto &system : observed.systems)
		if (owns(system.id))
		{
			const auto &t = system.telemetry;
			if (t.contains("supply_limited") && t.at("supply_limited") && t.contains("reserved_power"))
				facts[11] += t.at("reserved_power");
			if (t.contains("thermal_stopped") && t.at("thermal_stopped"))
				facts[12] = 1;
		}
	for (const auto &shield : combat.at("shields"))
	{
		const auto capacity = shield.at("capacity").get<std::int64_t>();
		const auto fraction = capacity > 0 ? shield.at("strength").get<std::int64_t>() * 1000 / capacity : 0;
		facts[7] = std::min(facts[7], fraction);
		if (owns(shield.at("id")))
			facts[6] = fraction;
	}
	facts[8] = facts[6] - facts[7];
	for (const auto &weapon : combat.at("weapons"))
		if (owns(weapon.at("id")))
		{
			point_defence = weapon.value("point_defence", false);
			intercepting |= point_defence && context.intercepting_weapons.contains(weapon.at("id").get<std::string>());
			facts[3] = weapon.value("selected", std::int64_t{0});
			std::int64_t best = std::numeric_limits<std::int64_t>::max();
			for (const auto &option : weapon.at("ammunition_options"))
				if (option.at("available").get<bool>() && option.at("range_mm").get<std::int64_t>() >= facts[1] &&
				    option.at("range_mm").get<std::int64_t>() < best)
				{
					best = option.at("range_mm");
					facts[3] = option.at("index");
				}
		}
	if (intercepting)
		facts[0] = 0;
	for (std::size_t i = 0; i < facts.size(); ++i)
		values.at(controller.brain->decision_base + i) = state_tree::Value::integer(facts[i]);
	values.at(controller.brain->decision_base + facts.size()) = state_tree::Value::boolean(point_defence);
}

} // namespace phoenix
