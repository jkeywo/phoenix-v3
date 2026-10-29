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
import phoenix.content.ui;

import phoenix.application.preferences;
import phoenix.console_controls;
#include "model_internal.hpp"
namespace phoenix
{
using namespace console_detail;
UiAvailability console_ui_availability(const Observation &view)
{
	UiAvailability result;
	result.profile = view.gameplay.value("profile", std::string{});
	const auto stars = view.gameplay.value("profile_stars", Json::object());
	result.stars = stars.value(result.profile, 0);
	const auto controls = view.gameplay.value("controls", Json::array());
	for (const auto &control : controls)
	{
		result.actions.insert(control.value("action", std::string{}));
		const auto controller = control.value("controller", std::string{});
		const auto channel = control.value("channel", std::string{});
		if (!controller.empty() && !channel.empty())
		{
			result.controllers.insert(controller);
			result.bindings.insert("controller/" + controller + "/" + channel);
		}
		const auto system = control.value("system", std::string{});
		if (!system.empty() && std::any_of(view.systems.begin(), view.systems.end(),
		                                   [&](const auto &physical)
		                                   {
			                                   return physical.id == system;
		                                   }))
			result.systems.insert(system);
	}
	// Consumers identify the physical systems behind the permitted command channels.
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
		for (const auto &[channel, command] : controller.at("commands").items())
			if (result.bindings.contains("controller/" + controller.at("id").get<std::string>() + "/" + channel))
				for (const auto &consumer : command.at("consumers"))
					result.systems.insert(consumer.get<std::string>());
	for (const auto &craft : view.gameplay.value("craft", Json::array()))
		if (craft.value("manual", false))
			result.features.insert("craft");
	if (!view.gameplay.value("docking", Json::object()).value("ports", Json::array()).empty())
		result.features.insert("docking");
	const auto logistics = view.gameplay.value("logistics", Json::object());
	auto human = [&](const std::string &id)
	{
		for (const auto &controller : view.gameplay.value("controllers", Json::array()))
			if (controller.at("id") == id)
				return controller.value("human", false);
		return false;
	};
	if (logistics.value("manual", false))
	{
		bool available = human(logistics.value("controller", "")) || human(logistics.value("route_controller", ""));
		for (const auto &store : logistics.value("stores", Json::array()))
			available |= human(store.value("controller", ""));
		if (available &&
		    (!logistics.value("stores", Json::array()).empty() || !logistics.value("routes", Json::array()).empty()))
			result.features.insert("operations");
	}
	if (view.gameplay.value("personnel", Json::object()).value("manual", false) &&
	    !view.gameplay.value("personnel", Json::object()).value("officers", Json::array()).empty())
		result.features.insert("personnel");
	for (const auto &system : view.systems)
		if (system.kind == OperatingSystem::Dispatch && result.systems.contains(system.id))
			result.features.insert("away");
	for (const auto &mission : view.gameplay.value("away_missions", Json::array()))
		if (mission.value("dispatch", false))
			result.features.insert("away");
	for (const auto &team : view.gameplay.value("teams", Json::array()))
		if (team.value("repair", false) &&
		    !view.gameplay.value("combat", Json::object()).value("compartments", Json::array()).empty())
			result.features.insert("repair");
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
		for (const auto &[channel, command] : controller.at("commands").items())
			for (const auto &consumer : command.at("consumers"))
				for (const auto &system : view.systems)
					if (system.id == consumer.get<std::string>() &&
					    (system.kind == OperatingSystem::Generator || system.kind == OperatingSystem::Distribution ||
					     system.kind == OperatingSystem::Coolant || system.kind == OperatingSystem::Storage))
						result.features.insert("power");
	for (const auto &control : controls)
	{
		const auto action = control.value("action", "");
		if (action == "camera_mode" || action == "camera_direction")
			result.features.insert("camera");
		if (action == "alert.set")
			result.features.insert("alert");
	}
	for (const auto &system : view.systems)
	{
		if (system.kind == OperatingSystem::Weapon && result.systems.contains(system.id))
			result.features.insert("weapons");
		if (system.kind == OperatingSystem::Shield)
			result.features.insert("shields");
	}
	if (!view.gameplay.value("ftl", Json::array()).empty())
		result.features.insert("ftl");
	if (!view.gameplay.value("teams", Json::array()).empty())
		result.features.insert("teams");
	const auto conversations = view.gameplay.value("conversations", Json::object());
	for (const auto &thread : conversations.value("threads", Json::array()))
		if (const auto selected = conversations.value("selected", std::uint64_t{0});
		    selected && thread.value("id", std::uint64_t{0}) == selected)
		{
			if (!thread.value("incoming", Json::array()).empty())
				result.features.insert("conversation-incoming");
			if (!thread.value("choices", Json::array()).empty())
				result.features.insert("conversation-replies");
			if (thread.value("text_allowed", false))
				result.features.insert("conversation-compose");
		}
	for (const auto &extra : view.additional_consoles)
	{
		result.features.insert("console/" + extra.console);
		const auto granted = console_ui_availability(extra);
		for (const auto &feature : granted.features)
			if (feature == "weapons" || feature == "shields" || feature == "ftl" || feature == "camera" ||
			    feature == "alert")
				result.features.insert(feature);
	}
	return result;
}
} // namespace phoenix
