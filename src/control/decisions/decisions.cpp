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

module phoenix.control.decisions;
import phoenix.gameplay.flight;
import phoenix.gameplay.ordnance.contacts;

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
	std::set<EntityId> accepted_contacts;
	std::int64_t repair = 0, repair_system = 0;
	for (std::size_t i = 0; i < controller.commands.size(); ++i)
	{
		const auto &spec = controller.definition.commands[i];
		if ((spec.action == OperatingAction::WeaponTarget || spec.action == OperatingAction::DirectedTarget ||
		     spec.action == OperatingAction::SelectContact || spec.action == OperatingAction::FollowContact) &&
		    controller.commands[i].accepted && controller.commands[i].value > 0)
			accepted_contacts.insert(static_cast<EntityId>(controller.commands[i].value));
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
	current = accepted_contacts.size() == 1 ? *accepted_contacts.begin() : 0;
	const BodyView *target = nullptr;
	const auto eligible_threat = [&](const BodyView &contact)
	{
		return !context.escort ||
		       fixed::length(fixed::Vec{contact.x, contact.y} - context.escort_anchor) <= context.engagement_range;
	};
	std::int64_t target_distance = 0;
	for (const auto &contact : observed.contacts)
		if (contact.radar_icon == RadarIcon::ship && contact.disposition == "hostile" && eligible_threat(contact) &&
		    contact.id == current)
		{
			const auto distance = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
			target = &contact;
			target_distance = distance;
			break;
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
		facts[1] = target_distance;
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
	const auto &combat = combat_observation(observed);
	const bool observes_point_defence = std::any_of(combat.at("weapons").begin(), combat.at("weapons").end(),
	                                                [](const auto &weapon)
	                                                {
		                                                return weapon.value("point_defence", false);
	                                                });
	std::vector<PointDefenceCandidate> missile_tracks;
	if (observes_point_defence)
		for (const auto &contact : observed.contacts)
			if (contact.id >= 7000000000000000ULL && contact.id < 8000000000000000ULL &&
			    contact.disposition != "friendly")
				missile_tracks.push_back(
				    {contact.id, {contact.x, contact.y}, {contact.vx, contact.vy}, contact.altitude});
	// Compatibility target facts describe an admitted binding; policy enumerates
	// and scores observed trajectories instead of receiving a native ranked target.
	for (const auto &track : missile_tracks)
		if (track.id == current)
			facts[14] = current;
	// Legacy scalar facts describe accepted bindings only. Destination policy
	// enumerates known assignments through the shared objective evaluator.
	facts[4] = repair;
	facts[5] = repair_system;
	bool assigns_repairs = std::any_of(controller.definition.commands.begin(), controller.definition.commands.end(),
	                                   [](const auto &command)
	                                   {
		                                   return command.action == OperatingAction::RepairCompartment;
	                                   });
	for (const auto &team : observed.gameplay.value("teams", Json::array()))
		assigns_repairs |= team.value("repair", false) &&
		                   controller_operation_owns(controller, "team_assignment", team.at("id").get<std::string>());
	if (!assigns_repairs)
	{
		// A single reported location is a factual request parameter. Ambiguous
		// departmental needs require separate authored candidates, not a native choice.
		std::set<std::int64_t> locations;
		for (const auto &need : observed.gameplay.value("repair_needs", Json::array()))
			if (context.owned_systems.contains(need.at("system").get<std::string>()))
				locations.insert(need.at("compartment_index").get<std::int64_t>());
		if (locations.size() == 1)
			facts[4] = *locations.begin();
	}
	bool repair_has_work = false;
	for (const auto &candidate : controller_repair_candidates(controller, observed, context.tick))
		repair_has_work |=
		    candidate.at("repair_candidate.eligible").number && !candidate.at("repair_candidate.home").number;
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
		}
	if (intercepting)
		facts[0] = facts[14];
	for (std::size_t i = 0; i < facts.size(); ++i)
		values.at(controller.brain->decision_base + i) = state_tree::Value::integer(facts[i]);
	values.at(controller.brain->decision_base + facts.size()) = state_tree::Value::boolean(point_defence);
	values.at(controller.brain->decision_base + facts.size() + 1) = state_tree::Value::boolean(repair_has_work);
	if (controller.brain->report_observations)
		controller_report_facts(controller, observed, context, values);
}

} // namespace phoenix
