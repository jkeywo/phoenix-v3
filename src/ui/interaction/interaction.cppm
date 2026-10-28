module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <tuple>
#include <utility>
#include <vector>
#include <string>

export module phoenix.ui.interaction;

export namespace phoenix
{
struct LoadingProgress
{
	bool active = false;
	std::size_t completed = 0, total = 0;
	std::string phase, title, error;
};
// Presentation metadata shared by visual navigation and platform accessibility.
// These records carry permitted UI state; they own no Application or session.
struct TextControl
{
	std::string id, label, value;
	bool percentage = false;
};
struct ControlPlacement
{
	float x = 0, y = 0, width = 0, height = 0;
	bool focused = false, enabled = true;
};
struct SemanticControl
{
	std::string id;
	std::string label;
	ControlPlacement placement;
	bool editable = false;
	std::string value;
	bool percentage = false;
	bool read_only = false;
	bool tab = false, selected = false;
	bool toggle = false, pressed = false;
	bool numeric = false;
	double minimum = 0, maximum = 100, step = 1;
};
} // namespace phoenix

export namespace phoenix
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

export namespace phoenix
{
bool same_semantic_controls(const std::vector<SemanticControl> &, const std::vector<SemanticControl> &);
}

export namespace phoenix
{
// Presentation-only camera gestures. World positions remain under the fingers
// while the midpoint moves and the pinch span changes, including at zoom limits.
struct MapGesture
{
	struct Point
	{
		float x, y;
	};
	std::map<std::uint64_t, Point> contacts;
	template <class Pointer>
	void event(const Pointer &event, bool inside, float centre_x, float centre_y, float minimum, float maximum,
	           float &pan_x, float &pan_y, float &zoom)
	{
		if (event.phase == Pointer::down)
		{
			if (inside && contacts.size() < 2)
				contacts[event.id] = {event.x, event.y};
			return;
		}
		auto found = contacts.find(event.id);
		if (found == contacts.end())
			return;
		if (event.phase == Pointer::up || event.phase == Pointer::cancel)
		{
			contacts.erase(found);
			return;
		}
		auto midpoint = [&]()
		{
			Point p{};
			for (const auto &[id, point] : contacts)
			{
				p.x += point.x;
				p.y += point.y;
			}
			p.x /= contacts.size();
			p.y /= contacts.size();
			return p;
		};
		auto span = [&]()
		{
			if (contacts.size() != 2)
				return 0.f;
			const auto a = contacts.begin()->second, b = std::next(contacts.begin())->second;
			return std::hypot(a.x - b.x, a.y - b.y);
		};
		const auto before = midpoint();
		const auto old_span = span();
		found->second = {event.x, event.y};
		const auto after = midpoint();
		const auto new_zoom = old_span > 1 ? std::clamp(zoom * span() / old_span, minimum, maximum) : zoom;
		const auto ratio = new_zoom / zoom;
		pan_x = after.x - centre_x - (before.x - centre_x - pan_x) * ratio;
		pan_y = after.y - centre_y - (before.y - centre_y - pan_y) * ratio;
		zoom = new_zoom;
	}
};

} // namespace phoenix
