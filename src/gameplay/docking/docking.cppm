module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

export module phoenix.gameplay.docking;
export import phoenix.world.state;
export import phoenix.content.logistics.types;

export namespace phoenix
{
struct DockPortState
{
	bool accepting = false, supply = false;
	std::set<EntityId> permitted;
};
struct DockingState
{
	std::vector<DockPortState> ports;
	std::uint64_t revision = 1;
};
struct DockConnection
{
	EntityId parent = 0;
	std::size_t port = 0, remote_port = 0;
	std::string phase = "approach", controller;
	std::int64_t progress = 0, relative_heading = 0, relative_z = 0;
	fixed::Vec relative;
	bool assisted = false;
	std::uint64_t generation = 0;
	std::map<std::string, std::uint64_t> generations;
	int guidance_step = 0;
	std::int64_t guidance_heading = 0;
};
void to_json(Json &, const DockPortState &);
void from_json(const Json &, DockPortState &);
void to_json(Json &, const DockingState &);
void from_json(const Json &, DockingState &);
void to_json(Json &, const DockConnection &);
void from_json(const Json &, DockConnection &);

struct DockKinematics
{
	fixed::Vec position, velocity;
	std::int64_t heading = 0, altitude = 0, vertical_velocity = 0;
};
struct DockStepFacts
{
	bool permitted = false, in_envelope = false, ports_working = false, crossing_busy = false;
	std::int64_t parent_heading = 0;
};
enum class DockProgress
{
	Unchanged,
	Captured,
	Mated,
	Released
};
struct DockApproachFacts
{
	Pose pose;
	Motion motion;
	Collider collider;
	Pose parent_pose;
	Motion parent_motion;
	Collider parent_collider;
	std::int64_t parent_altitude = 0, maximum_speed = 10000, deceleration = 1000;
	bool inertial_engine = false, lateral_thrust = false;
};
struct DockApproachGuidance
{
	fixed::Vec destination;
	std::int64_t altitude = 0, speed = 0, heading = 0;
	EntityId target = 0;
	bool navigate = false;
};
DockApproachGuidance prepare_dock_approach(DockConnection &, const DockPortDefinition &, const DockPortDefinition &,
                                           const DockApproachFacts &);
// Continuation between the pre-propulsion capture and post-propulsion phase.
// Only assembly roots are recorded; applying mass compensation never changes
// the captured baseline or reorders roots.
class DockPropulsionContinuation
{
public:
	void begin();
	void capture(EntityId root, fixed::Vec velocity);
	const std::map<EntityId, fixed::Vec> &starts() const;
	fixed::Vec apply(EntityId root, fixed::Vec velocity, std::int64_t bare_mass, std::int64_t assembly_mass) const;

private:
	std::map<EntityId, fixed::Vec> starts_;
};
fixed::Vec capture_dock_momentum(fixed::Vec root, fixed::Vec incoming, std::int64_t incoming_mass,
                                 std::int64_t assembly_mass);
fixed::Vec rotate_dock_offset(fixed::Vec, std::int64_t heading);
fixed::Vec dock_port_position(const DockKinematics &, const DockPortDefinition &);
bool dock_capture_envelope(const DockKinematics &, const DockPortDefinition &, const DockKinematics &,
                           const DockPortDefinition &);
DockProgress advance_dock_connection(DockConnection &, const DockPortDefinition &, const DockPortDefinition &,
                                     DockStepFacts);
void synchronise_docked_body(const DockConnection &, const Pose &parent_pose, const Motion &parent_motion,
                             std::int64_t parent_altitude, Pose &, Motion &, VerticalMotion *);
} // namespace phoenix
