module;
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <optional>

module phoenix.gameplay.docking;
import phoenix.gameplay.flight;
import phoenix.world.spatial;

namespace phoenix
{
DockApproachGuidance prepare_dock_approach(DockConnection &c, const DockPortDefinition &a, const DockPortDefinition &b,
                                           const DockApproachFacts &facts)
{
	const auto &own = facts.pose;
	const auto &other = facts.parent_pose;
	const auto maximum = facts.maximum_speed, acceleration = facts.deceleration;
	const bool inertial_engine = facts.inertial_engine, lateral_thrust = facts.lateral_thrust;
	DockApproachGuidance g;
	g.target = c.parent;
	g.navigate = true;
	const auto desired_heading = (other.heading + b.heading + 180000 - a.heading) % 360000;
	g.destination = dock_port_position({other.position, facts.parent_motion.velocity, other.heading}, b) -
	                rotate_dock_offset({a.x, a.y}, desired_heading);
	g.altitude = facts.parent_altitude + b.z - a.z;
	bool detour = false;
	// A port on the far side of a hull needs an approach around that hull.
	// Test the actual shapes, with a small navigation clearance, before choosing a side waypoint.
	const auto outward = fixed::direction((other.heading + b.heading) % 360000);
	constexpr std::int64_t clearance = 1000;
	bool port_corridor = false;
	if (inertial_engine && !lateral_thrust && a.heading == 0)
	{
		// A fore-port ship must finish turning before entering hull contact.
		// Otherwise its corners hit the receiver while it aligns at the port.
		const auto side = fixed::direction((other.heading + b.heading + 90000) % 360000);
		const auto offset = own.position - g.destination;
		const auto heading_error = std::abs((own.heading - desired_heading + 540000) % 360000 - 180000);
		port_corridor =
		    fixed::dot(offset, outward) >= 0 &&
		    std::abs(fixed::dot(offset, side) / fixed::unit) < std::min(a.capture_range, b.capture_range) / 2 &&
		    heading_error < 1000;
		if (!port_corridor)
		{
			const auto &hull = facts.collider;
			const auto turn_clearance = (hull.box ? hull.half_width + hull.half_length : hull.radius) + 2 * clearance;
			g.destination = g.destination + fixed::scale(outward, turn_clearance);
		}
	}
	auto approach_pose = own;
	// Test clearance up to an exterior approach gate. The final port contact
	// intentionally touches the hull and must not create a permanent detour.
	approach_pose.position = g.destination + fixed::scale(outward, 2 * clearance);
	approach_pose.heading = desired_heading;
	auto shape = facts.collider;
	if (shape.box)
	{
		shape.half_width += clearance;
		shape.half_length += clearance;
	}
	else
		shape.radius += clearance;
	const CollisionBody moving{approach_pose, Motion{}, shape}, obstacle{other, Motion{}, facts.parent_collider};
	const bool final_approach =
	    fixed::length(g.destination - own.position) <= 2 * clearance + std::min(a.capture_range, b.capture_range);
	if (!final_approach && sweep_contact_time(moving, obstacle, own.position, other.position, false))
	{
		const auto side = fixed::direction((other.heading + b.heading + 90000) % 360000);
		const auto offset = own.position - other.position;
		const auto sign = fixed::dot(offset, side) >= 0 ? 1 : -1;
		const auto &target_shape = facts.parent_collider;
		const auto radius =
		    (target_shape.box ? target_shape.half_width + target_shape.half_length : target_shape.radius) +
		    (shape.box ? shape.half_width + shape.half_length : shape.radius) + 3000;
		// Clear the side of the hull before advancing around its far end.
		// Holding the starting longitudinal offset forever cannot reach a far-side port.
		const auto side_distance = std::abs(fixed::dot(offset, side) / fixed::unit);
		const auto along = side_distance < radius * 3 / 4
		                       ? std::min<std::int64_t>(0, fixed::dot(offset, outward) / fixed::unit)
		                       : radius;
		g.destination = other.position + fixed::scale(side, sign * radius) + fixed::scale(outward, along);
		detour = true;
	}
	const auto delta = g.destination - own.position;
	const auto distance = fixed::length(delta);
	const auto parent_velocity = facts.parent_motion.velocity;
	const auto relative = facts.motion.velocity - parent_velocity;
	const auto approach_speed =
	    std::min({maximum / 4, distance / 8,
	              static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(2 * acceleration * distance)))});
	const auto desired_velocity = parent_velocity + fixed::scale(fixed::normal(delta), approach_speed);
	const auto guidance = pursuit_guidance(own, desired_velocity, maximum, desired_heading);
	g.heading = guidance.heading;
	g.speed = guidance.speed;
	if (inertial_engine && !lateral_thrust)
	{
		const auto bearing = [&](fixed::Vec direction)
		{
			return pursuit_guidance(own, direction, maximum, desired_heading).heading;
		};
		const auto angle = [](std::int64_t a, std::int64_t b)
		{
			return std::abs((a - b + 540000) % 360000 - 180000);
		};
		const auto tolerance = std::max<std::int64_t>(1, std::min(a.capture_speed, b.capture_speed) / 4);
		const auto remaining = fixed::length(relative);
		// Inside the capture-width corridor, keep the hull aligned and
		// close longitudinally; chasing tiny lateral error would restart the turn.
		const auto course = port_corridor ? desired_heading : bearing(delta);
		const bool at_port = !detour && distance < std::min(a.capture_range, b.capture_range) / 2;
		if (c.guidance_step == 0)
		{
			c.guidance_heading = remaining > tolerance ? bearing(relative) : at_port ? desired_heading : course;
			c.guidance_step = remaining > tolerance ? 1 : at_port ? 5 : 3;
		}
		g.heading = c.guidance_heading;
		const auto facing = fixed::direction(own.heading);
		// Hold the current forward component while passive damping removes drift during alignment.
		g.speed = std::clamp(fixed::dot(facts.motion.velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
		if (c.guidance_step == 1 && angle(own.heading, c.guidance_heading) < 1000)
			c.guidance_step = 2;
		if (c.guidance_step == 2)
		{
			g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
			if (remaining <= tolerance || std::abs(fixed::dot(relative, facing) / fixed::unit) <= tolerance)
				c.guidance_step = 0; // Re-latch any small residual lateral drift before another turn.
		}
		else if (c.guidance_step == 3 && angle(own.heading, c.guidance_heading) < 1000)
			c.guidance_step = 4;
		if (c.guidance_step == 4)
		{
			if (at_port || angle(course, c.guidance_heading) > 5000)
				c.guidance_step = 0;
			else
				g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit + approach_speed,
				                     std::int64_t{0}, maximum);
		}
		else if (c.guidance_step == 5)
		{
			g.heading = desired_heading;
			g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
			// Finish the latched turn before a changing approach gate can restart
			// navigation. Otherwise damping can leave the ship orbiting the gate.
			if (angle(own.heading, desired_heading) < 1000 && (!at_port || remaining > tolerance))
				c.guidance_step = 0;
		}
	}
	if (!(inertial_engine && !lateral_thrust) && !detour && distance < std::min(a.capture_range, b.capture_range) &&
	    fixed::length(relative) <= std::min(a.capture_speed, b.capture_speed) / 2)
	{
		g.heading = desired_heading;
		g.speed =
		    std::max<std::int64_t>(0, fixed::dot(parent_velocity, fixed::direction(desired_heading)) / fixed::unit);
	}
	return g;
}
} // namespace phoenix
