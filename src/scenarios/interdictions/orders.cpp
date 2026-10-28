module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.scenario.interdictions;
namespace phoenix
{
EncounterMotionDemand encounter_motion_demand(const Pose &pose, const EncounterMovement &movement)
{
	const auto delta = movement.destination - pose.position;
	const auto distance = fixed::length(delta);
	const auto heading = distance > movement.stop_radius ? fixed::bearing(delta) : pose.heading;
	const auto difference = std::abs((heading - pose.heading + 540000) % 360000 - 180000);
	return {heading, difference < 5000 && distance > movement.stop_radius
	                     ? std::min<std::int64_t>(movement.cruise_speed, (distance - movement.stop_radius) / 8)
	                     : 0};
}
bool encounter_manages_action(OperatingAction action)
{
	return action == OperatingAction::Speed || action == OperatingAction::Heading ||
	       action == OperatingAction::Destination || action == OperatingAction::FollowContact ||
	       action == OperatingAction::MatchSpeed || action == OperatingAction::WeaponTarget;
}
InterdictionShipOrder InterdictionBook::ship_order(std::size_t index, const InterdictionOrderFacts &facts,
                                                   const InterdictionShipFacts &ship) const
{
	const auto &s = states_.at(index);
	const auto &d = definitions_.at(index);
	const auto delta = facts.player_position - ship.position;
	const bool attack = ship.permitted_player_contact && s.posture == "hostile" &&
	                    fixed::length(delta) <= d.engagement && (ship.ship != s.boarder || s.blocked || s.captured);
	InterdictionShipOrder order;
	order.ship = ship.ship;
	order.weapon_target = attack ? facts.player : 0;
	order.hold_if_docked = ship.ship == s.boarder;
	order.attack = attack;
	return order;
}
EncounterMovement InterdictionBook::ship_movement(std::size_t index, std::size_t fleet_index,
                                                  const InterdictionOrderFacts &facts,
                                                  const InterdictionShipFacts &ship,
                                                  const InterdictionShipOrder &order) const
{
	const auto &d = definitions_.at(index);
	if (order.attack)
		return {*ship.permitted_player_contact, d.combat_standoff, d.cruise_speed};
	const auto &variant =
	    d.variants.at(d.variants.contains(facts.player_definition) ? facts.player_definition : "default");
	const auto offset = fixed::direction(variant[fleet_index].formation_bearing);
	return {facts.target_position +
	            fixed::Vec{offset.x * d.formation / fixed::unit, offset.y * d.formation / fixed::unit},
	        40000};
}
std::optional<InterdictionDepartureOrder> InterdictionBook::departure_order(std::size_t index,
                                                                            const InterdictionOrderFacts &facts) const
{
	const auto &s = states_.at(index);
	const auto &d = definitions_.at(index);
	if (!s.captured || !facts.target_captured || !participant(index, facts.target_owner) || !facts.target_available)
		return {};
	InterdictionDepartureOrder order;
	order.prize = d.target;
	order.group.insert(d.target);
	for (auto id : facts.live_ships)
		order.group.insert(id);
	order.leader = s.boarder ? s.boarder : d.target;
	order.ready = order.group.contains(order.leader);
	if (order.ready)
	{
		const auto away = facts.target_position - facts.player_position;
		order.movement = {facts.target_position + away, 10000};
	}
	return order;
}
} // namespace phoenix
