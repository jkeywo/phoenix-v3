module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.simulation;
import phoenix.content;
#include "flight.hpp"

namespace phoenix
{
namespace
{
constexpr std::int64_t flight_hz = 20, maximum_velocity = 1000000000;
std::int64_t flight_integer(const Json &source, const char *key, std::int64_t fallback, std::int64_t minimum,
                            std::int64_t maximum, const std::string &path)
{
	if (!source.contains(key))
		return fallback;
	if (!source.at(key).is_number_integer())
		throw std::runtime_error(path + "." + key + ": expected integer");
	const auto value = source.at(key).get<std::int64_t>();
	if (value < minimum || value > maximum)
		throw std::runtime_error(path + "." + key + ": outside supported finite range");
	return value;
}
bool flight_boolean(const Json &source, const char *key, bool fallback, const std::string &path)
{
	if (!source.contains(key))
		return fallback;
	if (!source.at(key).is_boolean())
		throw std::runtime_error(path + "." + key + ": expected boolean");
	return source.at(key).get<bool>();
}
std::int64_t forward_speed(const Pose &pose, const Motion &motion)
{
	return fixed::dot(motion.velocity, fixed::direction(pose.heading)) / fixed::unit;
}
std::int64_t lateral_speed(const Pose &pose, const Motion &motion)
{
	return fixed::dot(motion.velocity, fixed::direction((pose.heading + 90000) % 360000)) / fixed::unit;
}
std::int64_t tick_delta(std::int64_t rate, int effort, int paid, int output, std::int64_t &remainder)
{
	constexpr auto divisor = flight_hz * 1000000000LL;
	const auto input = rate * effort;
	const auto multiplier = std::int64_t{paid} * output;
	// Split before multiplying: valid authored acceleration and maximum
	// overdrive would otherwise overflow a signed 64-bit intermediate.
	const auto total = input % divisor * multiplier + remainder;
	remainder = total % divisor;
	return input / divisor * multiplier + total / divisor;
}
int correction(std::int64_t difference, std::int64_t acceleration)
{
	if (!difference || !acceleration)
		return 0;
	const auto magnitude =
	    std::min<std::int64_t>(1000, (std::abs(difference) * flight_hz * 1000 + acceleration - 1) / acceleration);
	return static_cast<int>(difference < 0 ? -magnitude : magnitude);
}
std::int64_t flight_scaled_rate(std::int64_t rate, int permille)
{
	// Conservatively round rates up so a prolonged sub-unit input cannot obtain
	// actuation while permanently truncating its authored energy/heat debit.
	return rate && permille ? (rate * permille + 999) / 1000 : 0;
}
std::int64_t heading_difference(std::int64_t desired, std::int64_t actual)
{
	return (desired - actual + 540000) % 360000 - 180000;
}
struct FlightTargets
{
	std::int64_t speed = 0, acceleration = 0, lateral_speed = 0, lateral_acceleration = 0;
	int forward = 0, lateral = 0, steering = 0;
	std::int64_t power = 0, heat = 0;
	bool charging = false;
};
FlightTargets flight_targets(const FlightDefinition &def, const FlightState &state, const Pose &pose,
                             const Motion &motion)
{
	FlightTargets result;
	if (!state.enabled)
		return result;
	const bool primary = def.kind == FlightKind::Engine;
	const bool impulse = state.impulse == ImpulsePhase::Active;
	const bool boost = state.boost && state.impulse == ImpulsePhase::Idle;
	const auto current = forward_speed(pose, motion);
	if (primary || def.kind == FlightKind::Boost || def.kind == FlightKind::Impulse)
	{
		result.acceleration = def.acceleration;
		std::int64_t cap = def.maximum_speed;
		if (impulse)
		{
			cap = cap * def.impulse_speed_multiplier / 1000;
			result.acceleration = result.acceleration * def.impulse_acceleration_multiplier / 1000;
		}
		else if (boost)
		{
			cap = cap * def.boost_speed_multiplier / 1000;
			result.acceleration = result.acceleration * def.boost_acceleration_multiplier / 1000;
		}
		if (impulse || boost)
		{
			result.speed = cap;
			result.forward = current < cap ? 1000 : 0;
		}
		else if (primary)
		{
			if (state.assisted)
			{
				result.speed = state.desired_speed;
				result.forward = correction(result.speed - current, result.acceleration);
			}
			else
			{
				result.speed = state.longitudinal < 0 ? -def.reverse_speed : cap;
				result.forward = state.longitudinal;
				if ((result.forward > 0 && current >= cap) || (result.forward < 0 && current <= -def.reverse_speed))
					result.forward = 0;
			}
		}
	}
	if (def.kind == FlightKind::Lateral || (primary && def.lateral_acceleration > 0))
	{
		result.lateral_acceleration = def.lateral_acceleration;
		const auto current_lateral = lateral_speed(pose, motion);
		if (state.assisted && def.assist_lateral)
		{
			result.lateral_speed = 0;
			result.lateral = correction(-current_lateral, result.lateral_acceleration);
		}
		else
		{
			result.lateral_speed = state.lateral < 0 ? -def.lateral_speed : def.lateral_speed;
			result.lateral = state.lateral;
			if ((result.lateral > 0 && current_lateral >= def.lateral_speed) ||
			    (result.lateral < 0 && current_lateral <= -def.lateral_speed))
				result.lateral = 0;
		}
	}
	if (def.kind == FlightKind::Steering || (primary && def.turn_rate > 0))
	{
		result.steering = state.assisted
		                      ? correction(heading_difference(state.desired_heading, pose.heading), def.turn_rate)
		                      : state.steering;
		if (impulse)
			result.steering = result.steering * static_cast<int>(def.impulse_steering_permille) / 1000;
		result.steering = result.steering * state.steering_limit_permille / 1000;
	}
	const auto effort = std::max({std::abs(result.forward), std::abs(result.lateral), std::abs(result.steering)});
	result.power = flight_scaled_rate(def.power_rate, effort);
	result.heat = flight_scaled_rate(def.heat_rate, effort);
	if (boost && result.forward)
	{
		result.power += def.boost_power_rate;
		result.heat += def.boost_heat_rate;
	}
	if (impulse && result.forward)
	{
		result.power += def.impulse_power_rate;
		result.heat += def.impulse_heat_rate;
	}
	if (state.impulse == ImpulsePhase::Charging)
	{
		result.charging = true;
		result.power += def.impulse_charge_power_rate;
		result.heat += def.impulse_heat_rate;
	}
	return result;
}
std::int64_t cap_delta(std::int64_t delta, std::int64_t remaining)
{
	if (delta > 0)
		return remaining > 0 ? std::min(delta, remaining) : 0;
	if (delta < 0)
		return remaining < 0 ? std::max(delta, remaining) : 0;
	return 0;
}
void validate_motion(const Pose &pose, const Motion &motion)
{
	if (pose.heading < 0 || pose.heading >= 360000 || motion.velocity.x < -maximum_velocity ||
	    motion.velocity.x > maximum_velocity || motion.velocity.y < -maximum_velocity ||
	    motion.velocity.y > maximum_velocity)
		throw std::runtime_error("flight: pose or velocity outside supported fixed-point range");
}
} // namespace

FlightState initial_flight(const FlightDefinition &def, const Pose &pose, const Motion &motion)
{
	validate_motion(pose, motion);
	FlightState result;
	result.desired_speed = std::clamp(forward_speed(pose, motion), -def.reverse_speed, def.maximum_speed);
	result.desired_heading = pose.heading;
	return result;
}
FlightCommandResult apply_flight(const FlightDefinition &def, FlightState &state, FlightCommand command,
                                 const Pose &pose, const Motion &motion)
{
	validate_motion(pose, motion);
	auto candidate = state;
	const auto flag = command.value == 0 || command.value == 1;
	switch (command.kind)
	{
	case FlightCommandKind::Speed:
		if (def.kind != FlightKind::Engine || command.value < -def.reverse_speed || command.value > def.maximum_speed)
			return {false, "flight.invalid-speed"};
		candidate.desired_speed = command.value;
		candidate.matching = false;
		candidate.match_stale = false;
		break;
	case FlightCommandKind::Heading:
		if ((def.kind != FlightKind::Steering && !def.turn_rate) || command.value < 0 || command.value >= 360000)
			return {false, "flight.invalid-heading"};
		candidate.desired_heading = command.value;
		break;
	case FlightCommandKind::Enabled:
		if (!flag)
			return {false, "flight.invalid-boolean"};
		candidate.enabled = command.value != 0;
		if (!candidate.enabled)
			release_flight_inputs(candidate);
		break;
	case FlightCommandKind::Assisted:
		if (!flag)
			return {false, "flight.invalid-boolean"};
		if (candidate.assisted != (command.value != 0))
		{
			candidate.assisted = command.value != 0;
			release_flight_inputs(candidate);
			candidate.matching = false;
			candidate.match_stale = false;
			if (candidate.assisted)
			{
				candidate.desired_speed =
				    std::clamp(forward_speed(pose, motion), -def.reverse_speed, def.maximum_speed);
				candidate.desired_heading = pose.heading;
			}
		}
		break;
	case FlightCommandKind::Longitudinal:
		if (def.kind != FlightKind::Engine || candidate.assisted || command.value < -1000 || command.value > 1000)
			return {false, "flight.invalid-manual-axis"};
		candidate.longitudinal = static_cast<int>(command.value);
		candidate.matching = false;
		candidate.match_stale = false;
		break;
	case FlightCommandKind::Lateral:
		if (!def.lateral_acceleration || command.value < -1000 || command.value > 1000)
			return {false, "flight.invalid-lateral-axis"};
		candidate.lateral = static_cast<int>(command.value);
		break;
	case FlightCommandKind::Steering:
		if (!def.turn_rate || candidate.assisted || command.value < -1000 || command.value > 1000)
			return {false, "flight.invalid-steering-axis"};
		candidate.steering = static_cast<int>(command.value);
		break;
	case FlightCommandKind::Boost:
		if (!flag || !def.boost_available || (command.value && candidate.impulse != ImpulsePhase::Idle))
			return {false, "flight.boost-unavailable"};
		candidate.boost = command.value != 0;
		break;
	case FlightCommandKind::Impulse:
		if (!flag || !def.impulse_available)
			return {false, "flight.impulse-unavailable"};
		if (!command.value)
		{
			candidate.impulse = ImpulsePhase::Idle;
			candidate.charge_milliticks = 0;
			candidate.charge_remainder = 0;
		}
		else if (candidate.impulse == ImpulsePhase::Idle)
		{
			candidate.impulse = ImpulsePhase::Charging;
			candidate.charge_milliticks = 0;
			candidate.charge_remainder = 0;
			candidate.boost = false;
		}
		break;
	case FlightCommandKind::MatchSpeed:
		if (!flag || def.kind != FlightKind::Engine || !candidate.assisted ||
		    (command.value && !candidate.selected_contact))
			return {false, "flight.match-unavailable"};
		candidate.matching = command.value != 0;
		if (candidate.matching && candidate.matched_contact != candidate.selected_contact)
		{
			candidate.matched_observed = false;
			candidate.matched_contact = candidate.selected_contact;
		}
		candidate.match_stale = false;
		if (candidate.matching && candidate.matched_observed)
			candidate.desired_speed = candidate.matched_speed;
		break;
	case FlightCommandKind::SelectContact:
		if (command.value < 0)
			return {false, "flight.invalid-contact"};
		if (candidate.selected_contact != static_cast<EntityId>(command.value))
		{
			candidate.matching = false;
			candidate.match_stale = false;
		}
		candidate.selected_contact = static_cast<EntityId>(command.value);
		break;
	default:
		return {false, "flight.unsupported-command"};
	}
	state = candidate;
	return {true, {}};
}
void release_flight_inputs(FlightState &state)
{
	state.longitudinal = state.lateral = state.steering = 0;
	state.boost = false;
	state.actual_longitudinal = state.actual_lateral = state.actual_steering = 0;
}
void observe_flight(const FlightDefinition &def, FlightState &state, std::uint64_t tick,
                    const std::optional<FlightContactObservation> &observation)
{
	if (!state.matching)
	{
		state.match_stale = false;
		return;
	}
	if (observation && observation->contact == state.matched_contact &&
	    observation->contact == state.selected_contact && observation->observed_tick <= tick &&
	    observation->speed_mm_s >= 0 && observation->speed_mm_s <= maximum_velocity &&
	    (!state.matched_observed || observation->observed_tick > state.observed_tick))
	{
		state.matched_speed = std::min(observation->speed_mm_s, def.maximum_speed);
		state.desired_speed = state.matched_speed;
		state.observed_tick = observation->observed_tick;
		state.uncertainty = observation->uncertainty;
		state.matched_observed = true;
	}
	state.match_stale =
	    !state.matched_observed || tick < state.observed_tick || tick - state.observed_tick > def.match_stale_ticks;
}
FlightDemand flight_demand(const FlightDefinition &def, const FlightState &state, const Pose &pose,
                           const Motion &motion)
{
	validate_motion(pose, motion);
	const auto target = flight_targets(def, state, pose, motion);
	return {
	    target.power,    target.heat,          target.forward,      target.lateral,       target.steering,
	    target.charging, target.speed,         target.acceleration, target.lateral_speed, target.lateral_acceleration,
	    pose.heading,    state.desired_heading};
}
FlightExecution step_flight(const FlightDefinition &def, FlightState &state, Pose &pose, Motion &motion,
                            const FlightDemand &demand, int paid_permille, int output_permille, int heat_permille)
{
	if (paid_permille < 0 || paid_permille > 1000 || output_permille < 1000 || output_permille > 10000 ||
	    heat_permille < 1000 || heat_permille > 10000)
		throw std::runtime_error("flight: invalid resource grant fraction");
	validate_motion(pose, motion);
	auto candidate = state;
	auto new_pose = pose;
	auto new_motion = motion;
	FlightTargets target;
	target.power = demand.power_rate;
	target.heat = demand.heat_rate;
	target.forward = demand.longitudinal;
	target.lateral = demand.lateral;
	target.steering = demand.steering;
	target.charging = demand.charging;
	target.speed = demand.target_speed;
	target.acceleration = demand.acceleration;
	target.lateral_speed = demand.target_lateral_speed;
	target.lateral_acceleration = demand.lateral_acceleration;
	auto basis = pose;
	basis.heading = demand.heading_basis;
	FlightExecution result;
	const auto start = motion.velocity;
	const auto start_heading = pose.heading;
	const auto forward = fixed::direction(basis.heading), lateral = fixed::direction((basis.heading + 90000) % 360000);
	if (target.forward)
	{
		auto change = tick_delta(target.acceleration, target.forward, paid_permille, output_permille,
		                         candidate.forward_remainder);
		change = cap_delta(change, target.speed - forward_speed(basis, motion));
		new_motion.velocity = new_motion.velocity + fixed::scale(forward, change);
	}
	if (target.lateral)
	{
		auto change = tick_delta(target.lateral_acceleration, target.lateral, paid_permille, output_permille,
		                         candidate.lateral_remainder);
		change = cap_delta(change, target.lateral_speed - lateral_speed(basis, motion));
		new_motion.velocity = new_motion.velocity + fixed::scale(lateral, change);
	}
	if (target.steering)
	{
		auto change =
		    tick_delta(def.turn_rate, target.steering, paid_permille, output_permille, candidate.turn_remainder);
		if (candidate.assisted)
			change = cap_delta(change, heading_difference(demand.desired_heading, pose.heading));
		new_pose.heading = (pose.heading + change + 360000) % 360000;
	}
	new_motion.velocity.x = std::clamp(new_motion.velocity.x, -maximum_velocity, maximum_velocity);
	new_motion.velocity.y = std::clamp(new_motion.velocity.y, -maximum_velocity, maximum_velocity);
	if (target.charging && paid_permille > 0)
	{
		const auto progress = std::int64_t{paid_permille} * output_permille + candidate.charge_remainder;
		candidate.charge_remainder = progress % 1000;
		candidate.charge_milliticks =
		    std::min(def.impulse_charge_ticks * 1000, candidate.charge_milliticks + progress / 1000);
		if (candidate.charge_milliticks == def.impulse_charge_ticks * 1000)
		{
			candidate.impulse = ImpulsePhase::Active;
			candidate.charge_remainder = 0;
		}
	}
	candidate.actual_longitudinal = target.forward * paid_permille / 1000;
	candidate.actual_lateral = target.lateral * paid_permille / 1000;
	candidate.actual_steering = target.steering * paid_permille / 1000;
	result.velocity_change = new_motion.velocity - start;
	result.heading_change = heading_difference(new_pose.heading, start_heading);
	result.power_rate =
	    std::min(resource_limit, flight_scaled_rate(flight_scaled_rate(target.power, output_permille), paid_permille));
	result.heat_rate =
	    std::min(resource_limit,
	             flight_scaled_rate(flight_scaled_rate(flight_scaled_rate(target.heat, output_permille), heat_permille),
	                                paid_permille));
	result.supply_limited = paid_permille < 1000 &&
	                        (target.power || target.forward || target.lateral || target.steering || target.charging);
	result.match_stale = candidate.match_stale;
	result.forward_permille = candidate.actual_longitudinal;
	result.lateral_permille = candidate.actual_lateral;
	result.steering_permille = candidate.actual_steering;
	state = candidate;
	pose = new_pose;
	motion = new_motion;
	return result;
}
Json save_flight(const FlightDefinition &def, const FlightState &state)
{
	return {{"schema", 3},
	        {"definition", def.signature},
	        {"enabled", state.enabled},
	        {"assisted", state.assisted},
	        {"boost", state.boost},
	        {"matching", state.matching},
	        {"desired_speed", state.desired_speed},
	        {"desired_heading", state.desired_heading},
	        {"longitudinal", state.longitudinal},
	        {"lateral", state.lateral},
	        {"steering", state.steering},
	        {"impulse", static_cast<unsigned>(state.impulse)},
	        {"charge_milliticks", state.charge_milliticks},
	        {"charge_remainder", state.charge_remainder},
	        {"selected_contact", state.selected_contact},
	        {"matched_contact", state.matched_contact},
	        {"matched_speed", state.matched_speed},
	        {"observed_tick", state.observed_tick},
	        {"uncertainty", state.uncertainty},
	        {"matched_observed", state.matched_observed},
	        {"match_stale", state.match_stale},
	        {"forward_remainder", state.forward_remainder},
	        {"lateral_remainder", state.lateral_remainder},
	        {"turn_remainder", state.turn_remainder},
	        {"actual_longitudinal", state.actual_longitudinal},
	        {"actual_lateral", state.actual_lateral},
	        {"actual_steering", state.actual_steering},
	        {"steering_limit_permille", state.steering_limit_permille}};
}
FlightState load_flight(const FlightDefinition &def, const Json &saved)
{
	if (!saved.is_object() || (saved.value("schema", 0) != 2 && saved.value("schema", 0) != 3) ||
	    saved.value("definition", std::string{}) != def.signature)
		throw std::runtime_error("flight save: incompatible definition or schema");
	FlightState result;
	const auto number = [&](const char *key, std::int64_t minimum, std::int64_t maximum)
	{
		if (!saved.contains(key))
			throw std::runtime_error(std::string("flight save: missing ") + key);
		return flight_integer(saved, key, 0, minimum, maximum, "flight save");
	};
	const auto flag = [&](const char *key)
	{
		if (!saved.contains(key))
			throw std::runtime_error(std::string("flight save: missing ") + key);
		return flight_boolean(saved, key, false, "flight save");
	};
	result.enabled = flag("enabled");
	result.assisted = flag("assisted");
	result.boost = flag("boost");
	result.matching = flag("matching");
	result.desired_speed = number("desired_speed", -def.reverse_speed, def.maximum_speed);
	result.desired_heading = number("desired_heading", 0, 359999);
	result.longitudinal = static_cast<int>(number("longitudinal", -1000, 1000));
	result.lateral = static_cast<int>(number("lateral", -1000, 1000));
	result.steering = static_cast<int>(number("steering", -1000, 1000));
	result.impulse = static_cast<ImpulsePhase>(number("impulse", 0, 2));
	result.charge_milliticks = number("charge_milliticks", 0, def.impulse_charge_ticks * 1000);
	const bool legacy_remainders = saved.at("schema") == 2;
	result.charge_remainder = legacy_remainders ? 0 : number("charge_remainder", 0, 999);
	result.selected_contact =
	    static_cast<EntityId>(number("selected_contact", 0, std::numeric_limits<std::int64_t>::max()));
	result.matched_contact =
	    static_cast<EntityId>(number("matched_contact", 0, std::numeric_limits<std::int64_t>::max()));
	result.matched_speed = number("matched_speed", 0, def.maximum_speed);
	result.observed_tick =
	    static_cast<std::uint64_t>(number("observed_tick", 0, std::numeric_limits<std::int64_t>::max()));
	result.uncertainty = static_cast<std::uint64_t>(number("uncertainty", 0, std::numeric_limits<std::int64_t>::max()));
	result.matched_observed = flag("matched_observed");
	result.match_stale = flag("match_stale");
	const auto remainder_limit = legacy_remainders ? 19999999LL : 19999999999LL;
	const auto remainder_scale = legacy_remainders ? 1000 : 1;
	result.forward_remainder = number("forward_remainder", -remainder_limit, remainder_limit) * remainder_scale;
	result.lateral_remainder = number("lateral_remainder", -remainder_limit, remainder_limit) * remainder_scale;
	result.turn_remainder = number("turn_remainder", -remainder_limit, remainder_limit) * remainder_scale;
	result.actual_longitudinal = static_cast<int>(number("actual_longitudinal", -1000, 1000));
	result.actual_lateral = static_cast<int>(number("actual_lateral", -1000, 1000));
	result.actual_steering = static_cast<int>(number("actual_steering", -1000, 1000));
	result.steering_limit_permille = static_cast<int>(number("steering_limit_permille", 0, 1000));
	if ((result.boost && (!def.boost_available || result.impulse != ImpulsePhase::Idle)) ||
	    (result.impulse != ImpulsePhase::Idle && !def.impulse_available) ||
	    (result.impulse == ImpulsePhase::Idle && result.charge_milliticks) ||
	    (result.impulse != ImpulsePhase::Charging && result.charge_remainder) ||
	    (result.impulse == ImpulsePhase::Active && result.charge_milliticks != def.impulse_charge_ticks * 1000) ||
	    (result.impulse == ImpulsePhase::Charging && result.charge_milliticks >= def.impulse_charge_ticks * 1000) ||
	    (!result.enabled && (result.boost || result.longitudinal || result.lateral || result.steering)) ||
	    (result.assisted && (result.longitudinal || result.steering)) || (!result.matching && result.match_stale) ||
	    (result.matched_observed && !result.matched_contact) ||
	    (result.matching && (def.kind != FlightKind::Engine || !result.assisted || !result.selected_contact ||
	                         result.matched_contact != result.selected_contact ||
	                         (result.matched_observed && result.desired_speed != result.matched_speed))))
		throw std::runtime_error("flight save: inconsistent mode continuation");
	return result;
}
} // namespace phoenix
