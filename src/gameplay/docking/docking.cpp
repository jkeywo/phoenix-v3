module;
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

module phoenix.gameplay.docking;

namespace phoenix
{
void to_json(Json &json, const DockPortState &value)
{
	json["accepting"] = value.accepting;
	json["supply"] = value.supply;
	json["permitted"] = value.permitted;
}
void from_json(const Json &json, DockPortState &value)
{
	json.at("accepting").get_to(value.accepting);
	json.at("supply").get_to(value.supply);
	json.at("permitted").get_to(value.permitted);
}
void to_json(Json &json, const DockingState &value)
{
	json["ports"] = value.ports;
	json["revision"] = value.revision;
}
void from_json(const Json &json, DockingState &value)
{
	json.at("ports").get_to(value.ports);
	json.at("revision").get_to(value.revision);
}
void to_json(Json &json, const DockConnection &value)
{
	json["parent"] = value.parent;
	json["port"] = value.port;
	json["remote_port"] = value.remote_port;
	json["phase"] = value.phase;
	json["controller"] = value.controller;
	json["progress"] = value.progress;
	json["relative_heading"] = value.relative_heading;
	json["relative_z"] = value.relative_z;
	json["relative"] = value.relative;
	json["assisted"] = value.assisted;
	json["generation"] = value.generation;
	json["generations"] = value.generations;
	json["guidance_step"] = value.guidance_step;
	json["guidance_heading"] = value.guidance_heading;
}
void from_json(const Json &json, DockConnection &value)
{
	json.at("parent").get_to(value.parent);
	json.at("port").get_to(value.port);
	json.at("remote_port").get_to(value.remote_port);
	json.at("phase").get_to(value.phase);
	json.at("controller").get_to(value.controller);
	json.at("progress").get_to(value.progress);
	json.at("relative_heading").get_to(value.relative_heading);
	json.at("relative_z").get_to(value.relative_z);
	json.at("relative").get_to(value.relative);
	json.at("assisted").get_to(value.assisted);
	json.at("generation").get_to(value.generation);
	json.at("generations").get_to(value.generations);
	json.at("guidance_step").get_to(value.guidance_step);
	json.at("guidance_heading").get_to(value.guidance_heading);
}

void DockPropulsionContinuation::begin()
{
	starts_.clear();
}
void DockPropulsionContinuation::capture(EntityId root, fixed::Vec velocity)
{
	starts_.try_emplace(root, velocity);
}
const std::map<EntityId, fixed::Vec> &DockPropulsionContinuation::starts() const
{
	return starts_;
}
fixed::Vec DockPropulsionContinuation::apply(EntityId root, fixed::Vec velocity, std::int64_t bare_mass,
                                             std::int64_t assembly_mass) const
{
	if (!assembly_mass || !bare_mass)
		return velocity;
	const auto before = starts_.at(root);
	return {before.x + (velocity.x - before.x) * bare_mass / assembly_mass,
	        before.y + (velocity.y - before.y) * bare_mass / assembly_mass};
}
fixed::Vec capture_dock_momentum(fixed::Vec root, fixed::Vec incoming, std::int64_t incoming_mass,
                                 std::int64_t assembly_mass)
{
	if (assembly_mass <= 0 || incoming_mass <= 0)
		return root;
	return {root.x + (incoming.x - root.x) * incoming_mass / assembly_mass,
	        root.y + (incoming.y - root.y) * incoming_mass / assembly_mass};
}
fixed::Vec rotate_dock_offset(fixed::Vec value, std::int64_t heading)
{
	const auto direction = fixed::direction(heading);
	return {value.x * direction.x / fixed::unit - value.y * direction.y / fixed::unit,
	        value.x * direction.y / fixed::unit + value.y * direction.x / fixed::unit};
}
fixed::Vec dock_port_position(const DockKinematics &body, const DockPortDefinition &port)
{
	return body.position + rotate_dock_offset({port.x, port.y}, body.heading);
}
bool dock_capture_envelope(const DockKinematics &own, const DockPortDefinition &port, const DockKinematics &parent,
                           const DockPortDefinition &remote)
{
	const auto z = own.altitude + port.z - parent.altitude - remote.z;
	const auto delta = dock_port_position(own, port) - dock_port_position(parent, remote);
	const auto speed = own.velocity - parent.velocity;
	const auto vertical_speed = own.vertical_velocity - parent.vertical_velocity;
	const auto alignment =
	    std::abs((own.heading + port.heading - parent.heading - remote.heading + 360000) % 360000 - 180000);
	const auto range = std::min(port.capture_range, remote.capture_range);
	const auto limit = std::min(port.capture_speed, remote.capture_speed);
	return fixed::dot(delta, delta) + z * z <= range * range &&
	       fixed::dot(speed, speed) + vertical_speed * vertical_speed <= limit * limit &&
	       alignment <= std::min(port.alignment, remote.alignment);
}
DockProgress advance_dock_connection(DockConnection &connection, const DockPortDefinition &port,
                                     const DockPortDefinition &remote, DockStepFacts facts)
{
	if (connection.phase == "approach")
	{
		if (!facts.permitted)
			return DockProgress::Released;
		if (!facts.in_envelope)
			return DockProgress::Unchanged;
		const auto heading = (facts.parent_heading + remote.heading + 180000 - port.heading) % 360000;
		connection.relative_heading = (heading - facts.parent_heading + 360000) % 360000;
		connection.relative =
		    fixed::Vec{remote.x, remote.y} - rotate_dock_offset({port.x, port.y}, connection.relative_heading);
		connection.relative_z = remote.z - port.z;
		connection.phase = "mating";
		connection.progress = 0;
		connection.assisted = false;
		return DockProgress::Captured;
	}
	if (connection.phase == "mating")
	{
		if (facts.ports_working && ++connection.progress >= std::max(port.mating_ticks, remote.mating_ticks))
		{
			connection.phase = "docked";
			connection.progress = 0;
			return DockProgress::Mated;
		}
	}
	else if (connection.phase == "separating" && !facts.crossing_busy &&
	         ++connection.progress >= std::max(port.separation_ticks, remote.separation_ticks))
		return DockProgress::Released;
	return DockProgress::Unchanged;
}
void synchronise_docked_body(const DockConnection &connection, const Pose &parent_pose, const Motion &parent_motion,
                             std::int64_t parent_altitude, Pose &pose, Motion &motion, VerticalMotion *vertical)
{
	pose.position = parent_pose.position + rotate_dock_offset(connection.relative, parent_pose.heading);
	pose.heading = (parent_pose.heading + connection.relative_heading) % 360000;
	pose.altitude = parent_altitude + connection.relative_z;
	motion.remainder = parent_motion.remainder;
	motion.velocity = parent_motion.velocity;
	motion.vertical_velocity = parent_motion.vertical_velocity;
	if (vertical)
	{
		vertical->altitude = pose.altitude;
		vertical->velocity = parent_motion.vertical_velocity;
	}
}
} // namespace phoenix
