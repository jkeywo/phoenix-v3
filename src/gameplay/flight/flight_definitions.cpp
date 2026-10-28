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
		return json_integer(spec, key, fallback, minimum, maximum, path);
	};
	result.maximum_speed = number("max_speed_mm_s");
	result.reverse_speed = number("reverse_speed_mm_s", 0, 0, result.maximum_speed);
	result.acceleration = number("acceleration_mm_s2");
	result.deceleration = number("deceleration_mm_s2", result.acceleration);
	result.turn_rate = number("turn_mdeg_s", 0, 0, 360000);
	result.lateral_speed = number("lateral_speed_mm_s", result.maximum_speed);
	result.lateral_acceleration =
	    number("lateral_acceleration_mm_s2", result.kind == FlightKind::Lateral ? result.acceleration : 0);
	result.assist_lateral = json_boolean(spec, "assist_lateral", false, path);
	const auto direction = spec.value("lateral_direction", std::string("both"));
	if (direction != "both" && direction != "port" && direction != "starboard")
		throw std::runtime_error(path + ".lateral_direction: expected port, starboard or both");
	result.lateral_direction = direction == "port" ? -1 : direction == "starboard" ? 1 : 0;
	if (spec.contains("targets"))
	{
		const auto &targets = spec.at("targets");
		if (!targets.is_array() || targets.empty() || targets.size() > 256 ||
		    (result.kind != FlightKind::Boost && result.kind != FlightKind::Impulse))
			throw std::runtime_error(path + ".targets: expected bounded modifier targets");
		for (const auto &target : targets)
		{
			FlightModifierTarget binding;
			binding.system = target.at("system").get<std::string>();
			const auto property = target.at("property").get<std::string>();
			if (property != "max_speed_mm_s" && property != "acceleration_mm_s2" && property != "turn_mdeg_s")
				throw std::runtime_error(path + ".targets.property: unsupported property " + property);
			binding.property = property == "max_speed_mm_s"       ? FlightProperty::Speed
			                   : property == "acceleration_mm_s2" ? FlightProperty::Acceleration
			                                                      : FlightProperty::Turn;
			binding.multiplier = json_integer(target, "multiplier_permille", 1000, 1000, 100000, path + ".targets");
			result.targets.push_back(std::move(binding));
		}
	}
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
		    json_integer(mode, "acceleration_multiplier_permille", 1000, 1000, 100000, path + ".boost");
		result.boost_speed_multiplier =
		    json_integer(mode, "speed_multiplier_permille", 1000, 1000, 100000, path + ".boost");
		result.boost_power_rate =
		    json_integer(mode, "power_rate", result.kind == FlightKind::Boost ? result.power_rate : 0, 0,
		                 maximum_resource_rate, path + ".boost");
		result.boost_heat_rate = json_integer(mode, "heat_rate", 0, 0, maximum_resource_rate, path + ".boost");
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
		result.impulse_charge_ticks = json_integer(mode, "charge_ticks", 20, 1, 1000000, path + ".impulse");
		result.impulse_acceleration_multiplier =
		    json_integer(mode, "acceleration_multiplier_permille", 1000, 1000, 100000, path + ".impulse");
		result.impulse_speed_multiplier =
		    json_integer(mode, "speed_multiplier_permille", 1000, 1000, 100000, path + ".impulse");
		result.impulse_steering_permille = json_integer(mode, "steering_permille", 0, 0, 1000, path + ".impulse");
		result.impulse_power_rate =
		    json_integer(mode, "power_rate", result.kind == FlightKind::Impulse ? result.power_rate : 0, 0,
		                 maximum_resource_rate, path + ".impulse");
		result.impulse_charge_power_rate = json_integer(mode, "charge_power_rate", result.impulse_power_rate, 0,
		                                                maximum_resource_rate, path + ".impulse");
		result.impulse_heat_rate = json_integer(mode, "heat_rate", 0, 0, maximum_resource_rate, path + ".impulse");
		result.impulse_charge_energy = json_integer(mode, "charge_energy", 0, 0, maximum_resource_rate, path);
		result.impulse_minimum_reserve = json_integer(mode, "minimum_reserve", result.impulse_charge_energy,
		                                              result.impulse_charge_energy, maximum_resource_rate, path);
		if (!result.targets.empty() && !result.impulse_charge_energy)
			throw std::runtime_error(path + ".charge_energy: targeted impulse requires positive total charging energy");
		if (result.kind == FlightKind::Impulse)
		{
			result.power_rate = 0;
			result.heat_rate = 0;
		}
	}
	if ((result.kind == FlightKind::Engine ||
	     ((result.kind == FlightKind::Boost || result.kind == FlightKind::Impulse) && result.targets.empty())) &&
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
void resolve_flight_modifiers(std::vector<FlightDefinition> &definitions, const std::vector<std::string> &names)
{
	for (auto &source : definitions)
		for (auto &binding : source.targets)
		{
			const auto found = std::find(names.begin(), names.end(), binding.system);
			if (found == names.end())
				throw std::runtime_error("flight.targets: missing System " + binding.system);
			binding.index = static_cast<std::size_t>(found - names.begin());
			const auto &target = definitions.at(binding.index);
			if (source.kind == FlightKind::Impulse && binding.property == FlightProperty::Turn)
				throw std::runtime_error("flight.targets: impulse targets propulsion; steering uses steering_permille");
			if (target.kind == FlightKind::Boost || target.kind == FlightKind::Impulse ||
			    (binding.property == FlightProperty::Turn ? !target.turn_rate : target.kind != FlightKind::Engine))
				throw std::runtime_error("flight.targets: incompatible or recursive target " + binding.system);
			const auto base = binding.property == FlightProperty::Speed          ? target.maximum_speed
			                  : binding.property == FlightProperty::Acceleration ? target.acceleration
			                                                                     : target.turn_rate;
			const auto limit = binding.property == FlightProperty::Turn ? 360000 : maximum_velocity;
			if (base * binding.multiplier / 1000 > limit)
				throw std::runtime_error("flight.targets: multiplied property exceeds limit for " + binding.system);
		}
}
} // namespace phoenix
