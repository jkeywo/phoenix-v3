module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.flight_widgets;
import phoenix.ui.engineering_widgets;
import phoenix.application.preferences;
namespace phoenix
{
SpecialisedPanel render_flight_component(const std::string &component, ConsoleModel &model, const std::string &prefix,
                                         bool narrow)
{

	SpecialisedPanel result{prefix, component};
	auto &markup = result.markup;
	auto &buttons = result.buttons;
	auto &task_actions_ = result.actions;
	const auto id = [&](const std::string &name)
	{
		return result.id(name);
	};
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	const auto text = [&](const std::string &s, const std::string &compact = std::string{})
	{
		return result.text(s, compact);
	};
	const auto button = [&](const std::string &action, const std::string &label, std::optional<bool> selected = {},
	                        const std::string &visible = std::string{}, bool enabled = true)
	{
		return result.button(action, label, selected, visible, enabled);
	};
	const auto &primary = *model.observation();
	const auto profile = primary.gameplay.value("profile", std::string{});
	const int stars = primary.gameplay.value("profile_stars", Json::object()).value(profile, 0);
	const auto control = [&](ConsoleModel &source, const std::string &source_prefix, const std::string &action,
	                         const std::string &equipment, std::int64_t value)
	{
		return source.observation()
		           ? permitted_control_action(*source.observation(), source_prefix, action, equipment, value)
		           : std::string{};
	};
	if (component == "flight-actions")
	{
		std::string key;
		for (const auto &widget : model.layout()->at("widgets"))
			if (widget.at("kind") == "throttle")
			{
				key = widget.at("id").get<std::string>();
				break;
			}
		int phase = 0;
		for (const auto &system : primary.systems)
			if (system.kind == OperatingSystem::Impulse && system.telemetry.contains("impulse_phase"))
				phase = std::max(phase, static_cast<int>(system.telemetry.at("impulse_phase")));
		markup = "<div class=\"flight-actions\">";
		for (const auto &[action, label, visible] : std::vector<std::tuple<std::string, std::string, std::string>>{
		         {"flight-impulse/" + key,
		          localise(phase == 1   ? "Cancel impulse: charging"
		                   : phase == 2 ? "Cancel impulse: active"
		                                : "Impulse"),
		          localise(phase == 1   ? "Charging"
		                   : phase == 2 ? "Active"
		                                : "Impulse")},
		         {"flight-stop/" + key, localise("All stop"), localise("All stop")}})
		{
			buttons.emplace_back(id(action), label);
			markup += "<button id=\"" + esc(id(action)) + "\">" + esc(visible) + "</button>";
		}
		markup += "</div>";
	}
	else if (component == "flight-status")
	{

		markup += text(localise(model.manual_active() ? "Manual flight" : "Assisted flight"));
		const auto speed = std::string(TextFormat(
		    "%.1f", std::hypot(static_cast<double>(primary.own.vx), static_cast<double>(primary.own.vy)) / 1000.0));
		std::string requested;
		if (narrow)
			for (const auto &widget : model.layout()->at("widgets"))
				if (widget.at("kind") == "throttle")
					requested = localise("Requested") + " " +
					            std::string(TextFormat(
					                "%.1f", model.system_request(model.widget_system(widget.at("id")), "speed_mm_s") /
					                            1000.0)) +
					            " / ";
		markup += text(requested + localise("Actual speed") + " " + speed + " " + localise("m/s"));
		std::string conditions;
		for (const auto &s : primary.systems)
			if (s.kind == OperatingSystem::Engine || s.kind == OperatingSystem::Steering)
			{
				const auto status = localise(engineering_name(s.id)) + ": " +
				                    localise(s.status.empty() ? (s.enabled ? "Operational" : "Unavailable") : s.status);
				if (narrow)
					conditions += (conditions.empty() ? "" : " / ") + status;
				else
					markup += text(status);
			}
		if (narrow)
			markup += text(conditions);
	}
	else if (component == "lateral-left" || component == "lateral-right")
	{
		const auto value = component == "lateral-left" ? -1000 : 1000;
		for (const auto &d : primary.gameplay.value("controls", Json::array()))
			if (d.value("action", "") == "lateral_thrust" && d.value("value", 0) == value)
			{
				// The existing held-control router handles press, simultaneous directions and neutral release.
				const auto action = descriptor_action(d.at("id"));
				const auto key = id(action);
				const auto label = localise(value < 0 ? "Left thrust" : "Right thrust");
				markup += "<button id=\"" + esc(key) + "\">" + esc(label) + "</button>";
				buttons.emplace_back(key, label);
			}
	}
	return result;
}
} // namespace phoenix
