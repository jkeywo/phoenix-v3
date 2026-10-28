module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.console;

import phoenix.application.preferences;
import phoenix.console_controls;
#include "model_internal.hpp"
namespace phoenix
{
using namespace console_detail;
std::string ConsoleModel::widget_system(const std::string &id) const
{
	if (!observation_ || layout_.is_null())
		return {};
	for (const auto &widget : layout_.at("widgets"))
		if (widget.at("id") == id)
		{
			if (widget.contains("controller") && widget.contains("channel"))
				return "controller/" + widget.at("controller").get<std::string>() + "/" +
				       widget.at("channel").get<std::string>();
			const auto kind = widget.at("kind");
			const auto type = kind == "throttle"  ? OperatingSystem::Engine
			                  : kind == "heading" ? OperatingSystem::Steering
			                                      : OperatingSystem::Radar;
			return select_system(observation_->systems, type, widget.value("system", std::string{}), kind != "radar")
			    .value_or("");
		}
	return {};
}
std::int64_t ConsoleModel::system_maximum(const std::string &id) const
{
	if (const auto channel = controller_channel(id))
		return channel->value("maximum", std::int64_t{0});
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
				return system.maximum;
	return 0;
}
std::int64_t ConsoleModel::system_minimum(const std::string &id) const
{
	if (const auto channel = controller_channel(id))
		return channel->value("minimum", std::int64_t{0});
	if (!observation_)
		return 0;
	std::string target = id;
	if (target.empty())
		target = select_system(observation_->systems, OperatingSystem::Engine).value_or("");
	for (const auto &system : observation_->systems)
		if (system.id == target)
		{
			const auto found = system.telemetry.find("minimum_speed");
			if (found != system.telemetry.end())
				return std::clamp(found->second, -system.maximum, std::int64_t{0});
		}
	return 0;
}
std::int64_t ConsoleModel::system_request(const std::string &id, const std::string &action) const
{
	if (const auto channel = controller_channel(id))
	{
		const auto name = channel->value("action", std::string{});
		if (!action.empty() && action != name)
			return 0;
		if (const auto pending = pending_requests_.find({id, name}); pending != pending_requests_.end())
			return pending->second;
		return channel->value("value", std::int64_t{0});
	}
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
			{
				const auto channel =
				    action.empty() ? system.kind == OperatingSystem::Steering ? "heading_mdeg" : "speed_mm_s" : action;
				if (auto found = pending_requests_.find({id, channel}); found != pending_requests_.end())
					return found->second;
				const auto field = channel == "heading_mdeg" ? "desired_heading"
				                   : channel == "speed_mm_s" ? "desired_speed"
				                                             : channel;
				if (const auto found = system.telemetry.find(field); found != system.telemetry.end())
					return found->second;
				if ((channel == "heading_mdeg") == (system.kind == OperatingSystem::Steering))
					return system.requested;
				return 0;
			}
	return 0;
}
bool ConsoleModel::set_speed_fraction(double fraction, const std::string &system)
{
	if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	const auto binding = system.empty() ? default_binding("speed_mm_s") : system;
	const auto minimum = system_minimum(binding), maximum = binding.empty() ? maximum_ : system_maximum(binding);
	return observation_ &&
	       submit({observation_->own.id, "speed_mm_s",
	               static_cast<std::int64_t>(std::llround(minimum + (maximum - minimum) * fraction)), binding});
}
bool ConsoleModel::is_on_screen(const std::string &widget) const
{
	const auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	const auto value = session->public_board().value("display", Json(nullptr));
	return value.is_object() && value.value("console", "") == observation_->console &&
	       value.value("widget", "") == widget && value.at("own").at("id") == observation_->own.id;
}
bool ConsoleModel::on_screen(const std::string &widget, const std::vector<std::int64_t> &camera, bool update)
{
	const auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!observation_ || layout_.is_null())
		return false;
	bool supported = false;
	for (const auto &w : layout_.at("widgets"))
		if (w.at("id") == widget &&
		    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
			supported = true;
	if (!supported || (update && !is_on_screen(widget)))
		return false;
	OperatingCommand command{observation_->own.id, update ? "display.update" : "display.toggle", 0, widget};
	command.console = observation_->console;
	command.arguments = camera;
	if (update)
		command.generation = session->public_board().at("display").at("revision");
	const auto eligible = eligible_console_command(*observation_, layout_, session->public_board(), command);
	const bool accepted = eligible && session->dispatch_console_command(route_, eligible->command);
	if (!update)
		announcement_ = localise(accepted ? "Viewscreen request sent" : "The server refused that control request");
	return accepted;
}
bool ConsoleModel::set_waypoint(std::int64_t x, std::int64_t y, bool active)
{
	const auto session = owner_.lock();
	if (!session || !observation_ || !observation_->gameplay.contains("controllers"))
		return false;
	for (const auto &controller : observation_->gameplay.at("controllers"))
		if (controller.value("human", false))
			for (auto channel = controller.at("commands").begin(); channel != controller.at("commands").end();
			     ++channel)
				if (channel.value().at("action") == "waypoint")
				{
					OperatingCommand command{
					    observation_->own.id,       "waypoint", active ? 1 : 0, {}, controller.at("id"), channel.key(),
					    controller.at("generation")};
					command.arguments = {x, y};
					command.console = observation_->console;
					return operating_arguments_valid(OperatingAction::Waypoint, command.value, command.arguments) &&
					       session->dispatch_console_command(route_, command);
				}
	return false;
}
Json ConsoleModel::objectives() const
{
	std::map<std::uint64_t, Json> latest;
	if (observation_)
		for (const auto &record : observation_->gameplay.value("shared", Json::array()))
			if (record.value("schema", "") == "phoenix.objective")
			{
				const auto &fields = record.at("fields");
				latest[fields.at("id").get<std::uint64_t>()] = fields;
			}
	Json result = Json::array();
	for (const auto &[key, fields] : latest)
		result.push_back(fields);
	return result;
}
ShipCameraState ConsoleModel::camera() const
{
	return observation_ ? ship_camera_state(observation_->gameplay) : ShipCameraState{};
}
Json ConsoleModel::waypoint() const
{
	Json result = Json::object();
	if (observation_)
		for (const auto &record : observation_->gameplay.value("shared", Json::array()))
			if (record.value("schema", "") == "phoenix.waypoint")
				result = record.at("fields");
	return result;
}
bool ConsoleModel::set_heading_degrees(double degrees, const std::string &system)
{
	if (!std::isfinite(degrees) || degrees < 0 || degrees >= 360)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, "heading_mdeg",
	                               static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000,
	                               system.empty() ? default_binding("heading_mdeg") : system});
}
bool ConsoleModel::command(const std::string &action, std::int64_t value, const std::string &system)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, action, value, system});
}
// Resolve sibling commands by their physical consumers, never by channel names.
std::string ConsoleModel::manual_binding(const std::string &action, const std::string &widget_action) const
{
	return observation_ ? manual_console_binding(*observation_, layout_, action, widget_action) : std::string{};
}
bool ConsoleModel::manual_available() const
{
	return !manual_binding("longitudinal_thrust", "speed_mm_s").empty() &&
	       !manual_binding("manual_steering", "heading_mdeg").empty() &&
	       !manual_binding("assisted", "speed_mm_s").empty() && !manual_binding("assisted", "heading_mdeg").empty();
}
bool ConsoleModel::manual_active() const
{
	if (!manual_available())
		return false;
	for (const auto *anchor : {"speed_mm_s", "heading_mdeg"})
	{
		const auto channel = controller_channel(manual_binding("assisted", anchor));
		if (!channel || channel->value("value", 1) || channel->value("pending", true) ||
		    !channel->value("accepted", false))
			return false;
		// Navigation requests can re-enable physical assistance without rewriting the mode channel.
		for (const auto &consumer : channel->at("consumers"))
			for (const auto &system : observation_->systems)
				if (system.id == consumer.get<std::string>() && system.telemetry.contains("assisted") &&
				    system.telemetry.at("assisted"))
					return false;
	}
	return true;
}
bool ConsoleModel::set_manual(bool enabled)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!manual_available())
	{
		announcement_ = localise("Manual flight control unavailable");
		return false;
	}
	release_controls();
	std::vector<std::string> sent;
	for (const auto *anchor : {"speed_mm_s", "heading_mdeg"})
	{
		const auto binding = manual_binding("assisted", anchor);
		if (std::find(sent.begin(), sent.end(), binding) != sent.end())
			continue;
		if (!command("assisted", enabled ? 0 : 1, binding))
		{
			for (const auto &previous : sent)
				command("assisted", 1, previous);
			announcement_ = localise("Manual flight mode request refused");
			return false;
		}
		sent.push_back(binding);
	}
	return true;
}
bool ConsoleModel::set_manual_axes(int yaw, int thrust)
{
	auto session = owner_.lock();
	if (!session || yaw < -1000 || yaw > 1000 || thrust < -1000 || thrust > 1000)
		return false;
	refresh(session, route_);
	if (!manual_active())
		return false;
	for (const auto &[action, anchor, value] : std::vector<std::tuple<std::string, std::string, int>>{
	         {"longitudinal_thrust", "speed_mm_s", thrust}, {"manual_steering", "heading_mdeg", yaw}})
	{
		const auto binding = manual_binding(action, anchor);
		OperatingCommand request{observation_->own.id, action, value, binding};
		if (!submit(request))
		{
			release_controls();
			return false;
		}
		const auto section = controller_section(binding);
		request.controller = section->at("id");
		request.channel = binding.substr(binding.find('/', 11) + 1);
		request.generation = section->at("generation");
		request.system.clear();
		if (value)
			held_controls_["manual/" + action] = request;
		else
			held_controls_.erase("manual/" + action);
	}
	return true;
}
bool ConsoleModel::flight_button(const std::string &action, std::int64_t value, bool pressed)
{
	if (!observation_ || !observation_->gameplay.is_object())
		return false;
	bool accepted = false;
	if (action == "impulse" && value == -1)
		value = std::any_of(observation_->systems.begin(), observation_->systems.end(),
		                    [](const auto &system)
		                    {
			                    return system.telemetry.contains("impulse_phase") &&
			                           system.telemetry.at("impulse_phase") != 0;
		                    })
		            ? 0
		            : 1;
	const auto descriptors = observation_->gameplay.value("controls", Json::array());
	const bool coordinated = action == "impulse" && std::any_of(descriptors.begin(), descriptors.end(),
	                                                            [](const auto &d)
	                                                            {
		                                                            return d.value("action", "") == "flight.impulse";
	                                                            });
	for (const auto &descriptor : descriptors)
		if ((coordinated ? descriptor.value("action", "") == "flight.impulse"
		                 : descriptor.value("action", "") == action) &&
		    descriptor.value("value", std::int64_t{0}) == value)
		{
			const auto id = "game.control/" + console_encoded(descriptor.at("id").get<std::string>());
			accepted |= action == "impulse" ? (pressed && act(id)) : pressed ? hold_control(id) : release_control(id);
		}
	return accepted;
}
bool ConsoleModel::hold_control(const std::string &id)
{
	auto session = owner_.lock();
	if (!session || !id.starts_with("game.control/"))
		return false;
	refresh(session, route_);
	if (!observation_ || !observation_->gameplay.is_object())
		return false;
	const auto decoded = console_decoded(id.substr(13));
	if (!decoded)
		return false;
	const auto controls = observation_->gameplay.value("controls", Json::array());
	const Json *selected = nullptr;
	if (controls.is_array())
		for (const auto &descriptor : controls)
			if (console_descriptor(descriptor) && console_string(descriptor, "id") == *decoded)
			{
				if (selected)
					return false;
				selected = &descriptor;
			}
	if (!selected)
		return false;
	const auto action = console_string(*selected, "action");
	if (action != "longitudinal_thrust" && action != "lateral_thrust" && action != "manual_steering" &&
	    action != "boost")
		return false;
	const auto value = *console_integer(selected->at("value"));
	if (!value)
		return false;
	OperatingCommand request{observation_->own.id, action, value, console_string(*selected, "system")};
	if (!console_string(*selected, "controller").empty())
		request = decode_operating_command(*selected);
	if (request.system.empty() && request.controller.empty())
	{
		const auto selected_system = select_system(observation_->systems, operating_action(action)->system);
		if (!selected_system)
			return false;
		request.system = *selected_system;
	}
	const auto previous = held_controls_;
	held_controls_[id] = request;
	if (action == "lateral_thrust")
	{
		request.value = 0;
		for (const auto &[key, held] : held_controls_)
			if (held.action == action && held.system == request.system && held.controller == request.controller &&
			    held.channel == request.channel)
				request.value += held.value;
		request.value = std::clamp<std::int64_t>(request.value, -1000, 1000);
	}
	if (!submit(request))
	{
		held_controls_ = previous;
		return false;
	}
	return true;
}
bool ConsoleModel::release_control(const std::string &id)
{
	const auto found = held_controls_.find(id);
	if (found == held_controls_.end())
		return false;
	auto request = found->second;
	request.value = 0;
	held_controls_.erase(found);
	for (const auto &[key, held] : held_controls_)
		if (held.action == request.action && held.system == request.system && held.controller == request.controller &&
		    held.channel == request.channel)
			request.value = request.action == "lateral_thrust"
			                    ? std::clamp<std::int64_t>(request.value + held.value, -1000, 1000)
			                    : held.value;
	return submit(std::move(request));
}
void ConsoleModel::release_controls()
{
	const auto held = held_controls_;
	held_controls_.clear();
	for (const auto &[id, original] : held)
	{
		auto request = original;
		request.value = 0;
		submit(std::move(request));
	}
}
std::string ConsoleModel::default_binding(const std::string &action) const
{
	std::string result;
	if (layout_.is_null())
		return result;
	for (const auto &widget : layout_.at("widgets"))
		if ((widget.at("kind") == "throttle" && action == "speed_mm_s") ||
		    (widget.at("kind") == "heading" && action == "heading_mdeg"))
		{
			const auto binding = widget_system(widget.at("id"));
			if (!result.empty() && binding != result)
				return {};
			result = binding;
		}
	return result;
}
bool ConsoleModel::submit(OperatingCommand command)
{
	auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	command.console = observation_->console;
	const auto eligible = eligible_console_command(*observation_, layout_, session->public_board(), std::move(command));
	if (!eligible)
		return false;
	command = eligible->command;
	std::string projected_label;
	if (!eligible->descriptor.is_null())
	{
		const auto &descriptor = eligible->descriptor;
		projected_label = console_label(console_string(descriptor, "label_key"));
		if (projected_label.empty())
			projected_label = console_label(console_string(descriptor, "label"));
		if (descriptor.value("label_value", false))
			projected_label += " " + std::to_string(command.value);
		projected_label = console_execution_prefix(descriptor) + projected_label;
	}
	if (!session->dispatch_console_command(route_, command))
	{
		announcement_ = localise("The server refused that control request");
		return false;
	}
	if (command.action == "speed_mm_s")
		speed_ = command.value;
	else if (command.action == "heading_mdeg")
		heading_ = command.value;
	if (command.action == "speed_mm_s" || command.action == "heading_mdeg")
	{
		pending_at_ = session->ticks();
		const auto binding =
		    command.controller.empty() ? command.system : "controller/" + command.controller + "/" + command.channel;
		pending_requests_[{binding, command.action}] = command.value;
		announcement_ = localise("Requested speed") + " " + std::to_string(speed_ / 1000.0) + " " + localise("m/s") +
		                "; " + localise("heading") + " " + std::to_string(heading_ / 1000) + " " + localise("degrees") +
		                ".";
	}
	else
		announcement_ = localise("Request sent") + (projected_label.empty() ? std::string{} : ": " + projected_label);
	return true;
}
} // namespace phoenix
