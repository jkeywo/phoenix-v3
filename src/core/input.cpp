module;
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>

module phoenix.input;

namespace phoenix
{
bool InputRouter::contains(ConsoleId id) const
{
	return std::any_of(regions_.begin(), regions_.end(),
	                   [id](const auto &region)
	                   {
		                   return region.console == id;
	                   });
}
bool InputRouter::set_regions(std::vector<ConsoleRegion> regions)
{
	std::set<ConsoleId> ids;
	for (std::size_t i = 0; i < regions.size(); ++i)
	{
		const auto &a = regions[i];
		if (!a.console || !ids.insert(a.console).second || !std::isfinite(a.x) || !std::isfinite(a.y) ||
		    !std::isfinite(a.width) || !std::isfinite(a.height) || a.width <= 0 || a.height <= 0)
			return false;
		for (std::size_t j = 0; j < i; ++j)
		{
			const auto &b = regions[j];
			if (a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y)
				return false;
		}
	}
	// Removing an assigned console requires releasing its devices first, so
	// callers can deliver neutral events to the old console before destruction.
	for (const auto &[device, id] : gamepads_)
		if (!ids.contains(id))
			return false;
	for (const auto &[contact, id] : captures_)
		if (!ids.contains(id))
			return false;
	regions_ = std::move(regions);
	if (keyboard_ && !contains(*keyboard_))
		keyboard_.reset();
	return true;
}
std::optional<PointerInput> InputRouter::pointer_down(std::uint64_t contact, float x, float y)
{
	if (captures_.contains(contact) || !std::isfinite(x) || !std::isfinite(y))
		return {};
	for (const auto &region : regions_)
		if (x >= region.x && y >= region.y && x < region.x + region.width && y < region.y + region.height)
		{
			captures_[contact] = region.console;
			return PointerInput{region.console, contact, x - region.x, y - region.y, false};
		}
	return {};
}
std::optional<PointerInput> InputRouter::pointer(std::uint64_t contact, float x, float y, bool release)
{
	auto found = captures_.find(contact);
	if (found == captures_.end() || !std::isfinite(x) || !std::isfinite(y))
		return {};
	ConsoleId console = found->second;
	auto region = std::find_if(regions_.begin(), regions_.end(),
	                           [console](const auto &r)
	                           {
		                           return r.console == console;
	                           });
	if (release)
		captures_.erase(found);
	return PointerInput{console, contact, x - region->x, y - region->y, release};
}
std::optional<PointerInput> InputRouter::pointer_move(std::uint64_t contact, float x, float y)
{
	return pointer(contact, x, y, false);
}
std::optional<PointerInput> InputRouter::pointer_up(std::uint64_t contact, float x, float y)
{
	return pointer(contact, x, y, true);
}
void ManualFlightInput::sample(const std::string &source, FlightAxes axes)
{
	axes.yaw = std::clamp(axes.yaw, -1000, 1000);
	axes.thrust = std::clamp(axes.thrust, -1000, 1000);
	auto &state = sources_[source];
	state.axes = axes;
	if (axes == FlightAxes{})
	{
		state.blocked = false;
		if (owner_ == source)
		{
			owner_.clear();
			axes_ = {};
		}
		return;
	}
	if (state.blocked)
		return;
	if (!owner_.empty() && owner_ != source)
	{
		state.blocked = true;
		return;
	}
	owner_ = source;
	axes_ = axes;
}
void ManualFlightInput::cancel_source(const std::string &source)
{
	auto &state = sources_[source];
	state.blocked = state.axes != FlightAxes{};
	if (owner_ == source)
	{
		owner_.clear();
		axes_ = {};
	}
}
void ManualFlightInput::cancel()
{
	for (auto &[name, source] : sources_)
		source.blocked = source.axes != FlightAxes{};
	owner_.clear();
	axes_ = {};
}
void InputRouter::neutralise(const std::string &device)
{
	auto assigned = gamepads_.find(device);
	if (assigned == gamepads_.end())
		return;
	for (const auto &[control, value] : held_[device])
		if (value != 0)
			pending_.push_back({assigned->second, control, 0, device, true});
	held_.erase(device);
}
void InputRouter::assign_gamepad(const std::string &device, std::optional<ConsoleId> console)
{
	if (console && !contains(*console))
		return;
	auto old = gamepads_.find(device);
	if (old != gamepads_.end() && console && old->second == *console)
		return;
	neutralise(device);
	flight_start_.erase(device);
	flight_ready_.erase(device);
	if (console)
		gamepads_[device] = *console;
	else
		gamepads_.erase(device);
}
void InputRouter::gamepad(const std::string &device, const std::string &control, float value)
{
	auto found = gamepads_.find(device);
	if (found == gamepads_.end() || !std::isfinite(value))
		return;
	value = std::clamp(value, -1.0f, 1.0f);
	if ((control == "flight.yaw" || control == "flight.thrust") && !flight_ready_[device])
	{
		auto &start = flight_start_[device];
		start[control] = value;
		if (start.size() < 2 || std::abs(start["flight.yaw"]) > .15f || std::abs(start["flight.thrust"]) > .15f)
			return;
		flight_ready_[device] = true;
		pending_.push_back({found->second, "flight.yaw", 0, device});
		pending_.push_back({found->second, "flight.thrust", 0, device});
	}

	if (control == "flight.yaw" || control == "flight.thrust")
		value = std::abs(value) <= .15f ? 0.f : std::copysign((std::abs(value) - .15f) / .85f, value);
	else if (std::abs(value) < .15f)
		value = 0;
	if (held_[device][control] == value)
		return;
	held_[device][control] = value;
	pending_.push_back({found->second, control, value, device});
}
void InputRouter::disconnect_gamepad(const std::string &device)
{
	assign_gamepad(device, {});
}
bool InputRouter::select_keyboard(ConsoleId console)
{
	if (!contains(console))
		return false;
	keyboard_ = console;
	return true;
}
std::vector<ConsoleInput> InputRouter::drain_gamepad_inputs()
{
	std::vector<ConsoleInput> result;
	result.swap(pending_);
	return result;
}
} // namespace phoenix
