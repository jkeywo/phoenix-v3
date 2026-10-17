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
};
struct Pose
{
	fixed::Vec position;
	std::int64_t heading = 0;
};
struct Motion
{
	fixed::Vec velocity, remainder;
};
struct Collider
{
	std::int64_t mass = 0, radius = 0, half_width = 0, half_length = 0;
	bool box = false;
};
struct Movable
{
};
struct Claimed
{
};
struct Engine
{
	std::int64_t max_speed = 0, acceleration = 0, speed = 0, remainder = 0;
	bool enabled = true;
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
struct Controller
{
	std::size_t program = 0;
	TreeCursor cursor;
};
struct SystemControllers
{
	std::array<std::optional<Controller>, 3> values;
};
struct ControlClaims
{
	SystemMask held = 0;
};
// Transient numeric collision workspace; authoritative state remains in ECS.
struct CollisionBody : Pose, Motion, Collider
{
};
void collide(CollisionBody &a, CollisionBody &b);
// Schema-1 boundary records, never queried by simulation systems.
struct SavedBody : Pose, Motion
{
	EntityId id = 0;
	std::string definition;
};
struct SavedOperation
{
	std::int64_t speed = 0, heading = 0, acceleration_remainder = 0, turn_remainder = 0;
	bool engine_enabled = true, radar_enabled = true;
	TreeState tree;
};
struct LegacyOperation
{
	SavedOperation value;
};
} // namespace phoenix
