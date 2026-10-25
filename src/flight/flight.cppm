module;
#include <cstdint>
#include <string>
#include <vector>

export module phoenix.flight;
export import phoenix.data;

export namespace phoenix
{
enum class FlightKind : std::uint8_t
{
	Engine,
	Steering,
	Lateral,
	Boost,
	Impulse
};
enum class FlightProperty : std::uint8_t
{
	Speed,
	Acceleration,
	Turn
};
struct FlightModifierTarget
{
	std::string system;
	FlightProperty property = FlightProperty::Speed;
	std::int64_t multiplier = 1000;
	std::size_t index = 0;
};
struct FlightDefinition
{
	FlightKind kind = FlightKind::Engine;
	std::int64_t maximum_speed = 0, reverse_speed = 0, acceleration = 0, turn_rate = 0;
	std::int64_t deceleration = 0;
	std::int64_t lateral_speed = 0, lateral_acceleration = 0;
	std::int64_t power_rate = 0, heat_rate = 0;
	bool boost_available = false, impulse_available = false, assist_lateral = false;
	std::int64_t boost_acceleration_multiplier = 1000, boost_speed_multiplier = 1000;
	std::int64_t boost_power_rate = 0, boost_heat_rate = 0;
	std::int64_t impulse_charge_ticks = 1, impulse_acceleration_multiplier = 1000, impulse_speed_multiplier = 1000;
	std::int64_t impulse_steering_permille = 0, impulse_charge_power_rate = 0, impulse_power_rate = 0,
	             impulse_heat_rate = 0;
	std::uint64_t match_stale_ticks = 100;
	std::string signature;
	// Negative is port, positive starboard; zero permits both directions.
	int lateral_direction = 0;
	std::int64_t impulse_charge_energy = 0, impulse_minimum_reserve = 0;
	std::vector<FlightModifierTarget> targets;
};
FlightDefinition compile_flight(const Json &spec, const std::string &path = "flight");
void resolve_flight_modifiers(std::vector<FlightDefinition> &, const std::vector<std::string> &);
} // namespace phoenix
