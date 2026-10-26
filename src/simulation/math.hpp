#pragma once
#include <cstdlib>
#include <cstdint>
#include <limits>
namespace phoenix::fixed
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
