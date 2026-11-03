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
	const auto &stars = borrowed_member(view.gameplay, "profile_stars");
	result.stars = stars.value(result.profile, 0);
	const auto &controls = borrowed_member(view.gameplay, "controls", true);
	const auto &controllers = controller_rows(view.gameplay);
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
	for (const auto &controller : controllers)
		for (const auto &[channel, command] : controller.at("commands").items())
			if (result.bindings.contains("controller/" + controller.at("id").get<std::string>() + "/" + channel))
				for (const auto &consumer : command.at("consumers"))
					result.systems.insert(consumer.get<std::string>());
	for (const auto &craft : borrowed_member(view.gameplay, "craft", true))
		if (craft.value("manual", false))
			result.features.insert("craft");
	if (!borrowed_member(borrowed_member(view.gameplay, "docking"), "ports", true).empty())
		result.features.insert("docking");
	const auto &logistics = borrowed_member(view.gameplay, "logistics");
	auto human = [&](const std::string &id)
	{
		for (const auto &controller : controllers)
			if (controller.at("id") == id)
				return controller.value("human", false);
		return false;
	};
	if (logistics.value("manual", false))
	{
		bool available = human(logistics.value("controller", "")) || human(logistics.value("route_controller", ""));
		for (const auto &store : borrowed_member(logistics, "stores", true))
			available |= human(store.value("controller", ""));
		if (available && (!borrowed_member(logistics, "stores", true).empty() ||
		                  !borrowed_member(logistics, "routes", true).empty()))
			result.features.insert("operations");
	}
	if (borrowed_member(view.gameplay, "personnel").value("manual", false) &&
	    !borrowed_member(borrowed_member(view.gameplay, "personnel"), "officers", true).empty())
		result.features.insert("personnel");
	for (const auto &system : view.systems)
		if (system.kind == OperatingSystem::Dispatch && result.systems.contains(system.id))
			result.features.insert("away");
	for (const auto &mission : borrowed_member(view.gameplay, "away_missions", true))
		if (mission.value("dispatch", false))
			result.features.insert("away");
	for (const auto &team : borrowed_member(view.gameplay, "teams", true))
		if (team.value("repair", false) &&
		    !borrowed_member(borrowed_member(view.gameplay, "combat"), "compartments", true).empty())
			result.features.insert("repair");
	for (const auto &controller : controllers)
		for (const auto &[channel, command] : controller.at("commands").items())
			for (const auto &consumer : command.at("consumers"))
				for (const auto &system : view.systems)
					if (system.id == consumer.get<std::string>() &&
					    (system.kind == OperatingSystem::Generator || system.kind == OperatingSystem::Distribution ||
					     system.kind == OperatingSystem::Coolant || system.kind == OperatingSystem::Storage))
						result.features.insert("power");
	if (!borrowed_member(view.gameplay, "power").empty())
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
	if (!borrowed_member(view.gameplay, "ftl", true).empty())
		result.features.insert("ftl");
	if (!borrowed_member(view.gameplay, "teams", true).empty())
		result.features.insert("teams");
	const auto &conversations = borrowed_member(view.gameplay, "conversations");
	for (const auto &thread : borrowed_member(conversations, "threads", true))
		if (const auto selected = conversations.value("selected", std::uint64_t{0});
		    selected && thread.value("id", std::uint64_t{0}) == selected)
		{
			if (!borrowed_member(thread, "incoming", true).empty())
				result.features.insert("conversation-incoming");
			if (!borrowed_member(thread, "choices", true).empty())
				result.features.insert("conversation-replies");
			if (thread.value("text_allowed", false))
				result.features.insert("conversation-compose");
		}
	for (const auto &extra : view.additional_consoles)
	{
		result.features.insert("console/" + extra.console);
		const auto granted = console_ui_availability(extra);
		if (granted.stars >= 1 && granted.stars <= 3)
			result.features.insert("console/" + extra.console + "/stars/" + std::to_string(granted.stars));
		for (const auto &feature : granted.features)
			if (feature == "weapons" || feature == "shields" || feature == "ftl" || feature == "camera" ||
			    feature == "alert")
				result.features.insert(feature);
	}
	return result;
}
} // namespace phoenix
