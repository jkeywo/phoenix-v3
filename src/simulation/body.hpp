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
	int forward_output = 0;
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
struct FittedRuntime
{
	std::string id;
	OperatingSystem kind;
	std::optional<Engine> engine;
	std::optional<Steering> steering;
	std::optional<Radar> radar;
	std::optional<Controller> controller;
	bool held = false;
	bool advanced = false;
	bool enabled = true;
	std::int64_t maximum = 0;
};
// Numeric settings/state compile once at load. Processing uses Flecs queries;
// strings are used only by command, observation and persistence boundaries.
struct FittedSystems
{
	std::vector<FittedRuntime> values;
};
// Transient numeric collision workspace; authoritative state remains in ECS.
struct CollisionBody : Pose, Motion, Collider
{
};
void collide(CollisionBody &a, CollisionBody &b);
// Sweep the actual circle/rotated-box shapes over relative translation. On a
// hit, stop both at first contact and apply the ordinary collision response.
std::optional<std::int64_t> sweep_contact_time(const CollisionBody &a, const CollisionBody &b, fixed::Vec start_a,
                                               fixed::Vec start_b);
bool sweep_collide(CollisionBody &a, CollisionBody &b, fixed::Vec start_a, fixed::Vec start_b);
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
