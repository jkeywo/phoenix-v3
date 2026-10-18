#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace phoenix
{
// Cosmetic, private clock and PRNG: never consume simulation time or random state.
struct MenuFlash
{
	double interval_min = 20, interval_max = 30, ramp = .1, short_hold = .1, gap = .3, long_hold = .3;
	float intensity = .9f;
	double clock = 0, next = 0, started = -1000;
	std::uint32_t random = 0x5eed;
	bool enabled = false;
	static MenuFlash parse(const nlohmann::json &data)
	{
		MenuFlash result;
		result.interval_min = data.at("interval_seconds").at(0);
		result.interval_max = data.at("interval_seconds").at(1);
		result.ramp = data.at("ramp_seconds");
		result.short_hold = data.at("short_hold_seconds");
		result.long_hold = data.at("long_hold_seconds");
		result.gap = data.at("gap_seconds");
		result.intensity = data.at("intensity");
		for (double value : {result.interval_min, result.interval_max, result.ramp, result.short_hold, result.long_hold,
		                     result.gap, static_cast<double>(result.intensity)})
			if (!std::isfinite(value) || value <= 0)
				throw std::runtime_error("Invalid menu flash timing");
		if (result.interval_max < result.interval_min || result.interval_min <= result.duration() ||
		    result.intensity > 1)
			throw std::runtime_error("Invalid menu flash interval or intensity");
		return result;
	}
	double duration() const
	{
		return 4 * ramp + short_hold + gap + long_hold;
	}
	double interval()
	{
		random = random * 1664525u + 1013904223u;
		return interval_min + (interval_max - interval_min) * (random / 4294967296.0);
	}
	float envelope(double age) const
	{
		auto pulse = [&](double time, double hold)
		{
			return time < 0 ? 0.0 : std::clamp(std::min(time / ramp, (2 * ramp + hold - time) / ramp), 0.0, 1.0);
		};
		return static_cast<float>(
		    std::max(pulse(age, short_hold), pulse(age - 2 * ramp - short_hold - gap, long_hold)));
	}
	float update(double elapsed, bool active)
	{
		if (!active)
		{
			enabled = false;
			return 0;
		}
		if (!enabled)
		{
			enabled = true;
			clock = 0;
			next = interval();
			started = -1000;
		}
		clock += std::max(0.0, elapsed);
		if (clock >= next)
		{
			started = clock;
			next = clock + interval();
		}
		return intensity * envelope(clock - started);
	}
};
} // namespace phoenix
