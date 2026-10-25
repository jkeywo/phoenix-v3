#pragma once
#include "math.hpp"
namespace phoenix
{
struct StableId
{
	EntityId id = 0;
};
struct Definition
{
	std::string name;
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
};
struct Pose
{
	fixed::Vec position;
	std::int64_t heading = 0;
	std::int64_t altitude = 0;
	std::int64_t previous_altitude = 0;
};
struct Motion
{
	fixed::Vec velocity, remainder;
	std::int64_t vertical_velocity = 0;
};
struct VerticalMotion
{
	std::int64_t altitude = 0, velocity = 0, remainder = 0, acceleration_remainder = 0, desired = 0;
};
struct Collider
{
	std::int64_t mass = 0, radius = 0, half_width = 0, half_length = 0;
	bool box = false;
	std::int64_t half_height = 5000;
	bool spatial = false;
};
struct Movable
{
};
struct Claimed
{
};
// Quadratic launch response, evaluated in fixed point for native/WASM parity.
// Twice the nominal rate at rest; a small floor reaches the requested cap.
// Braking keeps the authored constant rate.
inline std::int64_t launch_acceleration(std::int64_t nominal, std::int64_t speed, std::int64_t maximum)
{
	if (!nominal)
		return 0;
	const auto fraction = std::clamp<std::int64_t>(speed * 1000 / std::max<std::int64_t>(1, maximum), 0, 1000);
	return std::max<std::int64_t>(1,
	                              nominal * std::max<std::int64_t>(50, 2000 - 2 * fraction * fraction / 1000) / 1000);
}
struct Engine
{
	std::int64_t max_speed = 0, acceleration = 0, speed = 0, remainder = 0;
	bool enabled = true;
	int forward_output = 0;
	std::int64_t deceleration = 0, damping_remainder = 0;
};
struct Steering
{
	std::int64_t rate = 0, heading = 0, remainder = 0;
};
struct Radar
{
	std::int64_t range = 0;
	bool enabled = true;
};
struct CompiledEquipment
{
	std::string id;
	OperatingSystem kind;
	std::optional<Engine> engine;
	std::optional<Steering> steering;
	std::optional<Radar> radar;
	bool advanced = false;
	bool enabled = true;
	std::int64_t maximum = 0;
};
// Load-time templates only; runtime state lives on equipment child entities.
struct CompiledFittings
{
	std::vector<CompiledEquipment> values;
};
// Transient numeric collision workspace; authoritative state remains in ECS.
struct CollisionBody : Pose, Motion, Collider
{
};
void collide(CollisionBody &a, CollisionBody &b);
// Sweep the actual circle/rotated-box shapes over relative translation. On a
// hit, stop both at first contact and apply the ordinary collision response.
std::optional<std::int64_t> sweep_contact_time(const CollisionBody &a, const CollisionBody &b, fixed::Vec start_a,
                                               fixed::Vec start_b, bool spatial = false);
bool sweep_collide(CollisionBody &a, CollisionBody &b, fixed::Vec start_a, fixed::Vec start_b, bool spatial = false);
// Schema-1 boundary records, never queried by simulation systems.
struct SavedBody : Pose, Motion
{
	EntityId id = 0;
	std::string definition;
};
struct SavedOperation
{
	std::int64_t speed = 0, heading = 0, acceleration_remainder = 0, turn_remainder = 0, damping_remainder = 0;
	bool engine_enabled = true, radar_enabled = true;
};
} // namespace phoenix
