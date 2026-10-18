#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace phoenix
{
// Presentation-only timers and random streams; independent of simulation and nebula flashes.
struct MenuAmbientClock
{
	double minimum = 1, maximum = 1, clock = 0, next = 0, age = -1;
	std::uint32_t random = 1;
	bool enabled = false;
	static std::array<double, 2> range(const nlohmann::json &data)
	{
		if (!data.is_array() || data.size() != 2)
			throw std::runtime_error("Invalid ambient menu interval");
		const double low = data.at(0), high = data.at(1);
		if (!std::isfinite(low) || !std::isfinite(high) || low <= 0 || high < low)
			throw std::runtime_error("Invalid ambient menu interval bounds");
		return {low, high};
	}
	static MenuAmbientClock parse(const nlohmann::json &data, std::uint32_t seed)
	{
		const auto bounds = range(data.at("interval_seconds"));
		MenuAmbientClock result;
		result.minimum = bounds[0];
		result.maximum = bounds[1];
		result.random = seed;
		return result;
	}
	double unit()
	{
		random = random * 1664525u + 1013904223u;
		return random / 4294967296.0;
	}
	double sample(double low, double high)
	{
		return low + (high - low) * unit();
	}
	bool update(double elapsed, bool active)
	{
		if (!active)
		{
			enabled = false;
			age = -1;
			return false;
		}
		if (!enabled)
		{
			enabled = true;
			clock = 0;
			next = sample(minimum, maximum);
			age = -1;
		}
		clock += std::max(0.0, elapsed);
		if (clock >= next)
		{
			age = 0;
			next = clock + sample(minimum, maximum);
			return true;
		}
		if (age >= 0)
			age += std::max(0.0, elapsed);
		return false;
	}
};
struct MenuAmbient
{
	MenuAmbientClock flicker, scutter;
	double flicker_duration = .28, crossing_duration = 5;
	std::array<double, 2> fast{4, 6}, slow{12, 18};
	bool rightward = true, fast_crossing = false;
	static MenuAmbient parse(const nlohmann::json &flicker_data, const nlohmann::json &scutter_data)
	{
		MenuAmbient result;
		result.flicker = MenuAmbientClock::parse(flicker_data, 0xc47);
		result.scutter = MenuAmbientClock::parse(scutter_data, 0x5c077e);
		result.flicker_duration = flicker_data.at("duration_seconds");
		result.fast = MenuAmbientClock::range(scutter_data.at("fast_crossing_seconds"));
		result.slow = MenuAmbientClock::range(scutter_data.at("slow_crossing_seconds"));
		if (!std::isfinite(result.flicker_duration) || result.flicker_duration <= 0 ||
		    result.flicker_duration >= result.flicker.minimum || result.fast[1] >= result.slow[0] ||
		    result.slow[1] >= result.scutter.minimum)
			throw std::runtime_error("Invalid ambient menu duration");
		return result;
	}
	void update(double elapsed, bool active)
	{
		flicker.update(elapsed, active);
		if (scutter.update(elapsed, active))
		{
			rightward = scutter.unit() > .5;
			fast_crossing = scutter.unit() > .5;
			const auto &speed = fast_crossing ? fast : slow;
			crossing_duration = scutter.sample(speed[0], speed[1]);
		}
	}
	float television_opacity() const
	{
		const double phase = flicker.age / flicker_duration;
		if (phase < 0 || phase >= .7)
			return 1;
		return phase < .17 ? .22f : phase < .35 ? .92f : phase < .5 ? .35f : .75f;
	}
	double crossing_progress() const
	{
		return scutter.age < 0 || scutter.age > crossing_duration ? -1 : scutter.age / crossing_duration;
	}
};
} // namespace phoenix
