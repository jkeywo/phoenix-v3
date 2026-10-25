#pragma once
#include "body.hpp"

namespace phoenix
{
enum class FlightCommandKind : std::uint8_t
{
	Speed,
	Heading,
	Enabled,
	Assisted,
	Longitudinal,
	Lateral,
	Steering,
	Boost,
	Impulse,
	MatchSpeed,
	SelectContact,
	Destination,
	FollowContact
};
enum class ImpulsePhase : std::uint8_t
{
	Idle,
	Charging,
	Active
};
struct FlightContactObservation
{
	EntityId contact = 0;
	std::int64_t speed_mm_s = 0;
	std::uint64_t observed_tick = 0;
	std::uint64_t uncertainty = 0;
	fixed::Vec position, velocity;
	std::int64_t heading = 0;
};
struct FlightState
{
	bool enabled = true, assisted = true, boost = false, matching = false;
	std::int64_t desired_speed = 0, desired_heading = 0;
	int longitudinal = 0, lateral = 0, steering = 0;
	ImpulsePhase impulse = ImpulsePhase::Idle;
	std::int64_t charge_milliticks = 0;
	std::int64_t charge_remainder = 0;
	EntityId selected_contact = 0, matched_contact = 0;
	std::int64_t matched_speed = 0;
	std::uint64_t observed_tick = 0, uncertainty = 0;
	bool matched_observed = false, match_stale = false;
	bool following = false, follow_stale = false;
	bool navigating = false;
	fixed::Vec destination;
	std::int64_t follow_distance = 0, follow_speed_limit = 0;
	std::int64_t forward_remainder = 0, lateral_remainder = 0, turn_remainder = 0;
	std::int64_t damping_remainder = 0;
	// Derived each tick from admitted lateral actuators, never saved or commanded here.
	std::int64_t desired_lateral = 0;
	int actual_longitudinal = 0, actual_lateral = 0, actual_steering = 0;
	int steering_limit_permille = 1000;
	std::int64_t charge_spent = 0;
	// Numeric reason codes are translated by presentation: 0 none, 1 reserve,
	// 2 charging power, 3 operating power, 4 disabled, 5 collision, 6 manual.
	int impulse_reason = 0, effective_boost = 0;
	bool external_impulse = false;
};
struct FlightCommand
{
	FlightCommandKind kind;
	std::int64_t value;
	std::vector<std::int64_t> arguments;
};
struct FlightCommandResult
{
	bool accepted = false;
	std::string reason;
};
struct FlightDemand
{
	std::int64_t power_rate = 0, heat_rate = 0;
	int longitudinal = 0, lateral = 0, steering = 0;
	bool charging = false;
	// Prepared against the tick snapshot; execution may clip but never re-plan.
	std::int64_t target_speed = 0, acceleration = 0, target_lateral_speed = 0, lateral_acceleration = 0;
	std::int64_t heading_basis = 0, desired_heading = 0;
	std::int64_t turn_rate = 0;
	fixed::Vec desired_velocity;
	std::int64_t deceleration = 0;
};
struct FlightExecution
{
	fixed::Vec velocity_change;
	std::int64_t heading_change = 0, power_rate = 0, heat_rate = 0;
	bool supply_limited = false, match_stale = false;
	int forward_permille = 0, lateral_permille = 0, steering_permille = 0;
};
FlightState initial_flight(const FlightDefinition &, const Pose &, const Motion &);
FlightCommandResult apply_flight(const FlightDefinition &, FlightState &, FlightCommand, const Pose &, const Motion &);
// Call before quoting resources when a fresh permitted observation has arrived.
void observe_flight(const FlightDefinition &, FlightState &, std::uint64_t tick,
                    const std::optional<FlightContactObservation> &);
void guide_follow(const FlightDefinition &, FlightState &, const Pose &, const Motion &,
                  const std::optional<FlightContactObservation> &);
struct PursuitGuidance
{
	std::int64_t heading = 0, speed = 0;
};
// Shared velocity tracking; callers supply their positional policy and speed limit.
PursuitGuidance pursuit_guidance(const Pose &, const Motion &, fixed::Vec desired_velocity, std::int64_t speed_limit,
                                 std::int64_t fallback_heading);
FlightDemand flight_demand(const FlightDefinition &, const FlightState &, const Pose &, const Motion &);
FlightExecution step_flight(const FlightDefinition &, FlightState &, Pose &, Motion &, const FlightDemand &,
                            int paid_permille, int output_permille = 1000, int heat_permille = 1000);
// Handover/device loss clears held inputs without reverting settings or momentum.
void release_flight_inputs(FlightState &);
void cancel_impulse(FlightState &, int reason);
Json save_flight(const FlightDefinition &, const FlightState &);
FlightState load_flight(const FlightDefinition &, const Json &);
} // namespace phoenix
