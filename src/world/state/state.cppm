module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module phoenix.world.state;
export import phoenix.contracts;

export namespace phoenix::fixed
{
inline constexpr std::int64_t unit = 1000000000;
struct Vec
{
	std::int64_t x = 0, y = 0;
	inline Vec operator+(Vec b) const
	{
		return {x + b.x, y + b.y};
	}
	inline Vec operator-(Vec b) const
	{
		return {x - b.x, y - b.y};
	}
	inline Vec operator-() const
	{
		return {-x, -y};
	}
};
void to_json(Json &, const Vec &);
void from_json(const Json &, Vec &);
inline std::int64_t dot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y;
}
inline std::uint64_t sqrt(std::uint64_t value)
{
	std::uint64_t result = 0, bit = std::uint64_t{1} << 62;
	while (bit > value)
		bit >>= 2;
	while (bit)
	{
		if (value >= result + bit)
		{
			value -= result + bit;
			result = (result >> 1) + bit;
		}
		else
			result >>= 1;
		bit >>= 2;
	}
	return result;
}
inline std::int64_t length(Vec value)
{
	return static_cast<std::int64_t>(sqrt(static_cast<std::uint64_t>(dot(value, value))));
}
inline Vec scale(Vec direction, std::int64_t magnitude)
{
	return {direction.x * magnitude / unit, direction.y * magnitude / unit};
}
inline Vec normal(Vec value)
{
	auto size = length(value);
	return size ? Vec{value.x * unit / size, value.y * unit / size} : Vec{unit, 0};
}
// Multiply a bounded velocity component by a fraction without a 128-bit type.
// Ordinary ship values take the direct path; long division handles large valid
// authored velocities identically on MSVC and WebAssembly.
inline std::int64_t fraction_component(std::int64_t component, std::uint64_t numerator, std::uint64_t denominator)
{
	const auto magnitude = static_cast<std::uint64_t>(component < 0 ? -component : component);
	if (!magnitude)
		return 0;
	std::uint64_t result = 0;
	if (numerator <= std::numeric_limits<std::uint64_t>::max() / magnitude)
		result = magnitude * numerator / denominator;
	else
	{
		// Supported velocity bounds keep three times the denominator in uint64_t.
		std::uint64_t remainder = 0, bit = 1;
		while (bit <= magnitude / 2)
			bit <<= 1;
		for (; bit; bit >>= 1)
		{
			remainder = remainder * 2 + ((magnitude & bit) ? numerator : 0);
			result = result * 2 + remainder / denominator;
			remainder %= denominator;
		}
	}
	return component < 0 ? -static_cast<std::int64_t>(result) : static_cast<std::int64_t>(result);
}
// Closest point on the finite desired-velocity segment, including a zero target.
inline Vec velocity_projection(Vec velocity, Vec desired)
{
	const auto squared = dot(desired, desired), along = dot(velocity, desired);
	if (!squared || along <= 0)
		return {};
	if (along >= squared)
		return desired;
	return {fraction_component(desired.x, along, squared), fraction_component(desired.y, along, squared)};
}
inline Vec damp_velocity(Vec current, Vec desired, std::int64_t step)
{
	if (!step)
		return current;
	const auto target = velocity_projection(current, desired);
	const auto delta = target - current;
	const auto squared = dot(delta, delta);
	auto size = length(delta);
	if (size * size < squared)
		++size;
	if (size <= step)
		return target;
	auto result = current + Vec{delta.x * step / size, delta.y * step / size};
	// Component rounding must not turn passive damping into a speed increase.
	if (dot(result, result) > dot(current, current))
	{
		if (result.x * result.x > current.x * current.x)
			result.x = current.x;
		if (result.y * result.y > current.y * current.y)
			result.y = current.y;
	}
	// At sub-millimetre component steps, make one bounded lattice step that
	// reduces speed instead of discarding the budget forever.
	if (result.x == current.x && result.y == current.y)
	{
		if (target.x * target.x < current.x * current.x)
			result.x += delta.x < 0 ? -1 : 1;
		else if (target.y * target.y < current.y * current.y)
			result.y += delta.y < 0 ? -1 : 1;
	}
	return result;
}
// Integer CORDIC: no platform libm, floating point, or generated trig table.
// Heading 0 points along +X and increases counterclockwise in world space.
inline Vec direction(std::int64_t heading)
{
	heading %= 360000;
	if (heading < 0)
		heading += 360000;
	if (heading == 0)
		return {unit, 0};
	if (heading == 90000)
		return {0, unit};
	if (heading == 180000)
		return {-unit, 0};
	if (heading == 270000)
		return {0, -unit};
	if (heading > 180000)
		heading -= 360000;
	int sign = 1;
	if (heading > 90000)
	{
		heading -= 180000;
		sign = -1;
	}
	else if (heading < -90000)
	{
		heading += 180000;
		sign = -1;
	}
	constexpr int angles[] = {45000, 26565, 14036, 7125, 3576, 1790, 895, 448, 224, 112, 56, 28, 14, 7, 3, 2, 1};
	std::int64_t x = 607252935, y = 0;
	for (int i = 0; i < 17; ++i)
	{
		auto old = x;
		if (heading >= 0)
		{
			x -= y >> i;
			y += old >> i;
			heading -= angles[i];
		}
		else
		{
			x += y >> i;
			y -= old >> i;
			heading += angles[i];
		}
	}
	return {sign * x, sign * y};
}
inline std::int64_t bearing(Vec v)
{
	if (!v.x && !v.y)
		return 0;
	const auto x = std::abs(v.x), y = std::abs(v.y);
	std::int64_t lo = 0, hi = 90000;
	while (hi - lo > 1)
	{
		const auto m = (lo + hi) / 2;
		const auto d = direction(m);
		if (d.y * x < d.x * y)
			lo = m;
		else
			hi = m;
	}
	const auto a = (lo + hi) / 2;
	return v.x >= 0 ? (v.y >= 0 ? a : (360000 - a) % 360000) : (v.y >= 0 ? 180000 - a : 180000 + a);
}
} // namespace phoenix::fixed

export namespace phoenix
{
inline constexpr int simulation_tick_hz = 20;
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
inline void damp_motion(Motion &motion, fixed::Vec desired, std::int64_t deceleration, std::int64_t &remainder)
{
	remainder += deceleration;
	const auto step = remainder / simulation_tick_hz;
	remainder %= simulation_tick_hz;
	motion.velocity = fixed::damp_velocity(motion.velocity, desired, step);
}
struct VerticalMotion
{
	std::int64_t altitude = 0, velocity = 0, remainder = 0, acceleration_remainder = 0, desired = 0;
};
void to_json(Json &, const VerticalMotion &);
void from_json(const Json &, VerticalMotion &);
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
void to_json(Json &, const SavedBody &);
void from_json(const Json &, SavedBody &);
void to_json(Json &, const SavedOperation &);
void from_json(const Json &, SavedOperation &);
} // namespace phoenix
