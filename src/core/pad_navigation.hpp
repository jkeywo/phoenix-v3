#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace phoenix
{
// Presentation input, independent of device APIs and simulation time.
struct PadNavigationEvent
{
	std::string action;
	bool pressed;
};
class PadNavigation
{
	struct Held
	{
		bool down = false;
		double repeat = 0;
	};
	std::map<std::string, Held> held_;
	std::map<std::string, float> axes_;
	std::vector<PadNavigationEvent> events_;
	bool blocked_ = true;
	void button(const std::string &key, bool down, double now)
	{
		auto &held = held_[key];
		if (down == held.down)
			return;
		held = {down, now + .4};
		events_.push_back({key, down});
	}

public:
	void cancel()
	{
		for (auto &[key, held] : held_)
			if (held.down)
			{
				events_.push_back({key, false});
				held.down = false;
			}
		blocked_ = true;
	}
	void sample(const std::string &control, float value, double now)
	{
		if (!std::isfinite(value))
			return;
		axes_[control] = value;
		if (blocked_)
		{
			if (!axes_.contains("flight.yaw") || !axes_.contains("flight.thrust") ||
			    std::any_of(axes_.begin(), axes_.end(),
			                [](const auto &p)
			                {
				                return std::abs(p.second) > .15f;
			                }))
				return;
			blocked_ = false;
		}
		if (control == "flight.yaw" || control == "flight.thrust")
		{
			const auto positive = control == "flight.yaw" ? "left" : "up";
			const auto negative = control == "flight.yaw" ? "right" : "down";
			const auto positive_button = control == "flight.yaw" ? "game.left" : "game.faster";
			const auto negative_button = control == "flight.yaw" ? "game.right" : "game.slower";
			button(positive, value >= (held_[positive].down ? .15f : .25f) || axes_[positive_button] > .5f, now);
			button(negative, value <= -(held_[negative].down ? .15f : .25f) || axes_[negative_button] > .5f, now);
			return;
		}
		const std::map<std::string, std::string> names{{"game.left", "left"},         {"game.right", "right"},
		                                               {"game.faster", "up"},         {"game.slower", "down"},
		                                               {"game.contacts", "activate"}, {"game.camera-back", "cancel"},
		                                               {"game.manual", "mode"},       {"flight.port", "previous"},
		                                               {"flight.starboard", "next"},  {"flight.impulse", "settings"}};
		if (const auto it = names.find(control); it != names.end())
		{
			const auto &key = it->second;
			const float threshold = held_[key].down ? .15f : .25f;
			const bool analog = key == "left"    ? axes_["flight.yaw"] >= threshold
			                    : key == "right" ? axes_["flight.yaw"] <= -threshold
			                    : key == "up"    ? axes_["flight.thrust"] >= threshold
			                                     : key == "down" && axes_["flight.thrust"] <= -threshold;
			button(key, value > .5f || analog, now);
		}
	}
	std::vector<PadNavigationEvent> drain(double now)
	{
		for (auto &[key, held] : held_)
			if (held.down && now >= held.repeat &&
			    (key == "left" || key == "right" || key == "up" || key == "down" || key == "next" || key == "previous"))
			{
				events_.push_back({key, true});
				held.repeat = now + .1;
			}
		std::vector<PadNavigationEvent> result;
		result.swap(events_);
		return result;
	}
};
} // namespace phoenix
