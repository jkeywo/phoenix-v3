module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

module phoenix.flight;

namespace phoenix
{
namespace
{
constexpr std::int64_t maximum_velocity = 1000000000, maximum_resource_rate = 1000000000000LL;
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
} // namespace
FlightDefinition compile_flight(const Json &spec, const std::string &path)
{
	if (!spec.is_object())
		throw std::runtime_error(path + ": expected flight definition object");
	FlightDefinition result;
	const auto kind = spec.value("kind", std::string("engine"));
	if (kind == "engine")
		result.kind = FlightKind::Engine;
	else if (kind == "steering")
		result.kind = FlightKind::Steering;
	else if (kind == "lateral")
		result.kind = FlightKind::Lateral;
	else if (kind == "boost")
		result.kind = FlightKind::Boost;
	else if (kind == "impulse")
		result.kind = FlightKind::Impulse;
	else
		throw std::runtime_error(path + ".kind: unsupported flight capability");
	const auto number = [&](const char *key, std::int64_t fallback = 0, std::int64_t minimum = 0,
	                        std::int64_t maximum = maximum_velocity)
	{
		return flight_integer(spec, key, fallback, minimum, maximum, path);
	};
	result.maximum_speed = number("max_speed_mm_s");
	result.reverse_speed = number("reverse_speed_mm_s", 0, 0, result.maximum_speed);
	result.acceleration = number("acceleration_mm_s2");
	result.turn_rate = number("turn_mdeg_s", 0, 0, 360000);
	result.lateral_speed = number("lateral_speed_mm_s", result.maximum_speed);
	result.lateral_acceleration =
	    number("lateral_acceleration_mm_s2", result.kind == FlightKind::Lateral ? result.acceleration : 0);
	result.assist_lateral = flight_boolean(spec, "assist_lateral", false, path);
	result.power_rate =
	    number("power_rate", number("energy_per_second", 0, 0, maximum_resource_rate), 0, maximum_resource_rate);
	result.heat_rate = number("heat_rate", 0, 0, maximum_resource_rate);
	result.match_stale_ticks = static_cast<std::uint64_t>(number("match_stale_ticks", 100, 1, 1000000));
	if (spec.contains("boost") || result.kind == FlightKind::Boost)
	{
		if (result.kind != FlightKind::Engine && result.kind != FlightKind::Boost)
			throw std::runtime_error(path + ".boost: mode requires an engine or boost actuator");
		const auto &mode = spec.contains("boost") ? spec.at("boost") : spec;
		if (!mode.is_object())
			throw std::runtime_error(path + ".boost: expected object");
		result.boost_available = true;
		result.boost_acceleration_multiplier =
		    flight_integer(mode, "acceleration_multiplier_permille", 1000, 1000, 100000, path + ".boost");
		result.boost_speed_multiplier =
		    flight_integer(mode, "speed_multiplier_permille", 1000, 1000, 100000, path + ".boost");
		result.boost_power_rate =
		    flight_integer(mode, "power_rate", result.kind == FlightKind::Boost ? result.power_rate : 0, 0,
		                   maximum_resource_rate, path + ".boost");
		result.boost_heat_rate = flight_integer(mode, "heat_rate", 0, 0, maximum_resource_rate, path + ".boost");
		if (result.kind == FlightKind::Boost)
		{
			result.power_rate = 0;
			result.heat_rate = 0;
		}
	}
	if (spec.contains("impulse") || result.kind == FlightKind::Impulse)
	{
		if (result.kind != FlightKind::Engine && result.kind != FlightKind::Impulse)
			throw std::runtime_error(path + ".impulse: mode requires an engine or impulse actuator");
		const auto &mode = spec.contains("impulse") ? spec.at("impulse") : spec;
		if (!mode.is_object())
			throw std::runtime_error(path + ".impulse: expected object");
		result.impulse_available = true;
		result.impulse_charge_ticks = flight_integer(mode, "charge_ticks", 20, 1, 1000000, path + ".impulse");
		result.impulse_acceleration_multiplier =
		    flight_integer(mode, "acceleration_multiplier_permille", 1000, 1000, 100000, path + ".impulse");
		result.impulse_speed_multiplier =
		    flight_integer(mode, "speed_multiplier_permille", 1000, 1000, 100000, path + ".impulse");
		result.impulse_steering_permille = flight_integer(mode, "steering_permille", 0, 0, 1000, path + ".impulse");
		result.impulse_power_rate =
		    flight_integer(mode, "power_rate", result.kind == FlightKind::Impulse ? result.power_rate : 0, 0,
		                   maximum_resource_rate, path + ".impulse");
		result.impulse_charge_power_rate = flight_integer(mode, "charge_power_rate", result.impulse_power_rate, 0,
		                                                  maximum_resource_rate, path + ".impulse");
		result.impulse_heat_rate = flight_integer(mode, "heat_rate", 0, 0, maximum_resource_rate, path + ".impulse");
		if (result.kind == FlightKind::Impulse)
		{
			result.power_rate = 0;
			result.heat_rate = 0;
		}
	}
	if ((result.kind == FlightKind::Engine || result.kind == FlightKind::Boost || result.kind == FlightKind::Impulse) &&
	    (!result.maximum_speed || !result.acceleration))
		throw std::runtime_error(path + ": forward flight requires positive speed and acceleration");
	if (result.kind == FlightKind::Steering && !result.turn_rate)
		throw std::runtime_error(path + ".turn_mdeg_s: positive steering rate required");
	if (result.kind == FlightKind::Lateral && (!result.lateral_speed || !result.lateral_acceleration))
		throw std::runtime_error(path + ": lateral flight requires positive lateral speed and acceleration");
	for (const auto multiplier : {result.boost_acceleration_multiplier, result.impulse_acceleration_multiplier})
		if (result.acceleration * multiplier / 1000 > maximum_velocity)
			throw std::runtime_error(path + ": multiplied acceleration exceeds fixed-point limit");
	for (const auto multiplier : {result.boost_speed_multiplier, result.impulse_speed_multiplier})
		if (result.maximum_speed * multiplier / 1000 > maximum_velocity)
			throw std::runtime_error(path + ": multiplied speed exceeds fixed-point limit");
	if (result.power_rate +
	            std::max({result.boost_power_rate, result.impulse_power_rate, result.impulse_charge_power_rate}) >
	        maximum_resource_rate ||
	    result.heat_rate + std::max(result.boost_heat_rate, result.impulse_heat_rate) > maximum_resource_rate)
		throw std::runtime_error(path + ": combined resource rate exceeds finite limit");
	result.signature = spec.dump();
	return result;
}
} // namespace phoenix
