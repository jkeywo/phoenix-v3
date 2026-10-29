module;
#include "native_headers.hpp"

module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::refresh_navigation_blackboard(EntityId id) -> void
{
	const auto &ship = entities.at(id);
	auto &evidence = ship.get_mut<BridgeBlackboard>().navigation;
	evidence = {};
	evidence.observed_tick = ecs.get<Clock>().tick;
	Observation observation;
	observation.own = view(id, ship.get<Definition>(), ship.get<Pose>(), ship.get<Motion>());
	// Close-in lookout uses physical world geometry, independently of sensor feeds.
	for (const auto &contact : contact_rows)
		if (contact.id != id)
			observation.contacts.push_back(view(contact.id, contact.entity.get<Definition>(),
			                                    contact.entity.get<Pose>(), contact.entity.get<Motion>()));
	for (const auto &equipment : ship.get<EquipmentIndex>().values)
	{
		const auto &fit = equipment.get<Equipment>();
		if (fit.kind != OperatingSystem::Lateral)
			continue;
		SystemView system;
		system.id = fit.id;
		system.telemetry = telemetry(id, fit.ordinal);
		system.enabled = system.telemetry.at("enabled") != 0;
		observation.systems.push_back(std::move(system));
	}
	bool blocked = false;
	const auto forward = fixed::direction(observation.own.heading);
	const auto speed = fixed::length({observation.own.vx, observation.own.vy});
	const auto &own_shape = definitions.at(observation.own.definition).collider;
	const auto own_radius = own_shape.box ? own_shape.half_width + own_shape.half_length : own_shape.radius;
	for (const auto &contact : observation.contacts)
	{
		const auto &shape = definitions.at(contact.definition).collider;
		const auto radius = own_radius + (shape.box ? shape.half_width + shape.half_length : shape.radius) + 3000;
		const auto dx = contact.x - observation.own.x, dy = contact.y - observation.own.y;
		const auto ahead = (dx * forward.x + dy * forward.y) / fixed::unit;
		const auto side = (dx * forward.y - dy * forward.x) / fixed::unit;
		blocked |= ahead >= -radius && ahead <= radius + std::max<std::int64_t>(12000, speed * 4) && side >= -radius &&
		           side <= radius;
	}
	evidence.blocked = blocked;
	evidence.left_heading = (observation.own.heading + 30000) % 360000;
	// Assess candidate manoeuvres from the ship's close-in lookout.
	// This produces evidence for the authored Controller tree, never commands.
	std::int64_t port_acceleration = 0, starboard_acceleration = 0, port_cap = 0, starboard_cap = 0;
	std::set<std::string> delegated_lateral;
	for (const auto &owner : ship.get<EntityControls>().controllers)
		for (std::size_t i = 0; i < owner.definition.commands.size(); ++i)
		{
			const auto &channel = owner.definition.commands[i];
			if (channel.action == OperatingAction::Lateral && !controller_command_human(owner, i))
				for (auto consumer : channel.consumers)
					delegated_lateral.insert(ship.get<EquipmentIndex>().values[consumer].get<Equipment>().id);
		}
	for (const auto &system : observation.systems)
	{
		if (!delegated_lateral.contains(system.id) || !system.enabled ||
		    !system.telemetry.contains("lateral_acceleration"))
			continue;
		const auto &t = system.telemetry;
		if (t.contains("thermal_stopped") && t.at("thermal_stopped"))
			continue;
		if (t.contains("stored_energy") && !t.at("stored_energy") &&
		    (!t.contains("incoming_power") || !t.at("incoming_power")))
			continue;
		const auto paid = t.contains("supply_permille") ? t.at("supply_permille") : 1000;
		const auto acceleration = t.at("lateral_acceleration") * paid / 1000;
		const auto direction = t.at("lateral_direction");
		if (direction <= 0)
		{
			port_acceleration += acceleration;
			port_cap = port_cap ? std::min(port_cap, t.at("lateral_maximum_speed")) : t.at("lateral_maximum_speed");
		}
		if (direction >= 0)
		{
			starboard_acceleration += acceleration;
			starboard_cap =
			    starboard_cap ? std::min(starboard_cap, t.at("lateral_maximum_speed")) : t.at("lateral_maximum_speed");
		}
	}
	const auto side_direction = fixed::direction((observation.own.heading + 90000) % 360000);
	const fixed::Vec velocity{observation.own.vx, observation.own.vy};
	const auto side_speed = fixed::dot(velocity, side_direction) / fixed::unit;
	int lateral_order = 0;
	bool lateral_safe = false;
	if (blocked)
	{
		std::int64_t best_clearance = -1;
		for (const int sign : {-1, 1})
		{
			const auto acceleration = sign < 0 ? port_acceleration : starboard_acceleration;
			const auto lateral_cap = sign < 0 ? port_cap : starboard_cap;
			if (!acceleration || !lateral_cap)
				continue;
			bool safe = true;
			std::int64_t clearance = 1000000000;
			fixed::Vec previous{};
			auto lateral_velocity = side_speed;
			std::int64_t displacement = 0;
			// Eight half-second segments include relative contact motion; test
			// closest points along each segment rather than just its endpoints.
			for (std::int64_t step = 1; step <= 8; ++step)
			{
				const auto next_velocity =
				    sign > 0 ? std::min(lateral_velocity + acceleration / 2, std::max(lateral_velocity, lateral_cap))
				             : std::max(lateral_velocity - acceleration / 2, std::min(lateral_velocity, -lateral_cap));
				displacement += (lateral_velocity + next_velocity) / 4 - side_speed / 2;
				lateral_velocity = next_velocity;
				const auto point = fixed::Vec{velocity.x * step / 2, velocity.y * step / 2} +
				                   fixed::scale(side_direction, displacement);
				for (const auto &contact : observation.contacts)
				{
					const auto &shape = definitions.at(contact.definition).collider;
					const auto radius =
					    own_radius + (shape.box ? shape.half_width + shape.half_length : shape.radius) + 1000;
					const fixed::Vec start{previous.x - (contact.x - observation.own.x + contact.vx * (step - 1) / 2),
					                       previous.y - (contact.y - observation.own.y + contact.vy * (step - 1) / 2)};
					const fixed::Vec end{point.x - (contact.x - observation.own.x + contact.vx * step / 2),
					                     point.y - (contact.y - observation.own.y + contact.vy * step / 2)};
					const auto segment = end - start;
					const auto length = fixed::length(segment);
					const auto along = length ? std::clamp<std::int64_t>(
					                                -fixed::dot(start, fixed::normal(segment)) / fixed::unit, 0, length)
					                          : 0;
					const auto closest = start + fixed::scale(fixed::normal(segment), along);
					const auto margin = fixed::length(closest) - radius;
					clearance = std::min(clearance, margin);
					safe &= margin > 0;
				}
				previous = point;
			}
			if (safe && clearance > best_clearance)
			{
				best_clearance = clearance;
				lateral_order = sign * 1000;
				lateral_safe = true;
			}
		}
	}
	evidence.lateral_thrust = lateral_order;
	evidence.lateral_safe = lateral_safe;
}
} // namespace phoenix::world_detail
