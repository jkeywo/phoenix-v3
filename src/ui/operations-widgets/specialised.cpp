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
module phoenix.ui.operations_widgets;
import phoenix.ui.engineering_widgets;
import phoenix.application.preferences;
namespace phoenix
{
SpecialisedPanel OperationsWidgets::render_specialised(const std::string &component, ConsoleModel &model,
                                                       const Catalog &catalog, const std::string &prefix)
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
	if (component == "veil-controls")
	{
		markup = "<div class=\"instrument-title\"><h2>" + localise("Psychics") + "</h2>" +
		         result.button("task-detail/psychics", localise("Psychic details"), {}, "i") + "</div>";
		for (const auto &s : primary.systems)
			if (s.kind == OperatingSystem::Psychic)
			{
				const auto action = control(model, "", "system_enabled", s.id, s.enabled ? 0 : 1);
				markup += button(action, localise(s.enabled ? "Veil enabled" : "Veil disabled"), s.enabled);
				markup += text(localise(s.status.empty() ? (action.empty() ? "Automatic" : "Operational") : s.status));
			}
		const auto controls = model.gameplay_controls();
		for (const auto &descriptor : primary.gameplay.value("controls", Json::array()))
		{
			if (descriptor.value("action", "") == "system_enabled")
				continue;
			const auto action = descriptor_action(descriptor.at("id"));
			const auto label = std::find_if(controls.begin(), controls.end(),
			                                [&](const auto &row) { return row.first == action; });
			if (label != controls.end() && console_widget_part_command(catalog, primary, descriptor, "console.veil-controls"))
				markup += button(action, label->second, descriptor.value("selected", false));
		}
	}
	else if (component.starts_with("repair"))
	{
		const auto parts = primary.gameplay.value("combat", Json::object()).value("compartments", Json::array());
		const auto teams = primary.gameplay.value("teams", Json::array());
		const Json *selected = nullptr;
		const bool roster = component == "repair-teams" || component == "repair";
		const bool map = component == "repair-map" || component == "repair";
		if (roster && map)
			markup = "<div class=\"courier-repair\">";
		if (roster)
			markup += "<div class=\"" + std::string(map ? "courier-roster" : "repair-team-list") + "\"><h2>" +
			          localise("Teams") + "</h2>";
		// Selection is shared through the Console model.
		for (const auto &team : teams)
			if (team.at("id") == model.selected_team())
				selected = &team;
		if (roster)
			for (const auto &team : teams)
			{
				const auto identity = team.at("id").get<std::string>();
				std::string order;
				for (const auto &o : team.at("orders"))
					if (o.at("id") == "auto" || order.empty())
						order = o.at("id");
				markup += button(order.empty() ? "" : "game.team-order/" + identity + "/" + order,
				                 localise(engineering_name(identity)), identity == model.selected_team());
				markup +=
				    text(localise(team.value("work", "Idle")) + " / " + localise(team.value("condition", "healthy")));
			}
		if (roster)
			markup += "</div>";
		if (component == "repair-teams")
			return result;
		markup += map ? "<div class=\"" + std::string(roster ? "courier-map-area" : "repair-map-content") +
		                    "\"><div class=\"repair-map-title\"><h2>" + localise("Ship compartments") + "</h2>" +
		                    (selected && !roster
		                         ? result.button("game.teams-close", localise("Compartment health"), {}, localise("Health list"))
		                         : "") +
		                    "</div>"
		              : "<div><h2>" + localise("Team orders") + "</h2>";
		if (!selected && map)
		{
			for (const auto &part : parts)
			{
				const auto health = 100 * part.at("hp").get<std::int64_t>() /
				                    std::max<std::int64_t>(1, part.at("maximum").get<std::int64_t>());
				markup += text(localise(engineering_name(part.at("id"))) + " · " + std::to_string(health) + "%");
			}
		}
		else if (!selected)
			markup += text(localise("Select a team"));
		if (selected)
		{
			if (map)
			{
				std::set<std::string> decks;
				for (const auto &p : parts)
					decks.insert(p.at("deck"));
				for (const auto &deck : decks)
				{
					double minx = 1e12, miny = 1e12, maxx = -1e12, maxy = -1e12;
					for (const auto &p : parts)
						if (p.at("deck") == deck)
						{
							const double x = p.at("x_mm"), y = p.at("y_mm"), w = p.at("half_width_mm"),
							             h = p.at("half_length_mm");
							minx = std::min(minx, x - w);
							maxx = std::max(maxx, x + w);
							miny = std::min(miny, y - h);
							maxy = std::max(maxy, y + h);
						}
					if (decks.size() > 1)
						markup += "<h3>" + esc(localise(deck)) + "</h3>";
					markup += "<div class=\"courier-ship-map\">";
					const auto eligibility = model.team_map();
					for (const auto &p : parts)
						if (p.at("deck") == deck)
						{
							const auto index = p.at("index").get<std::int64_t>();
							const double x = p.at("x_mm"), y = p.at("y_mm"), w = p.at("half_width_mm"),
							             h = p.at("half_length_mm");
							const bool here = selected && selected->at("location").get<std::int64_t>() + 1 == index;
							const bool destination = selected && selected->at("destination").get<std::int64_t>() + 1 == index;
							bool allowed = false;
							for (const auto &e : eligibility)
								if (e.at("index") == index)
									allowed = selected && e.value("dispatch", false) && e.value("eligible", false) &&
									          selected->value("manual", false) && !selected->value("reserved", false);
							const auto key = "task/specialised/" + component + "/compartment/" + std::to_string(index);
							const auto label = localise(engineering_name(p.at("id"))) +
							                   (here ? " — " + localise(selected->value("work", "") == "Travelling"
							                                                ? "En route from here"
							                                                : "Team here")
							                    : destination ? " — " + localise("Destination")
							                                  : "");
							if (allowed)
							{
								task_actions_[key] = "game.compartment/" + std::to_string(index);
								buttons.emplace_back(id(key), label);
							}
							markup += "<button id=\"" + esc(id(key)) + "\" " + (allowed ? "" : "disabled ") +
							          "style=\"position:absolute;left:" +
							          std::to_string(100 * (maxy - y - h) / std::max(1.0, maxy - miny)) +
							          "%;top:" + std::to_string(100 * (maxx - x - w) / std::max(1.0, maxx - minx)) +
							          "%;width:" + std::to_string(200 * h / std::max(1.0, maxy - miny)) +
							          "%;height:" + std::to_string(200 * w / std::max(1.0, maxx - minx)) +
							          "%;\" class=\"" + (here || destination ? "selected" : "") + "\">" + esc(label) +
							          "</button>";
						}
					markup += "</div>";
				}
			}
			if (component == "repair-map")
			{
				markup += roster && map ? "</div></div>" : "</div>";
				return result;
			}
			if (selected->value("travel", 0) > 0 || selected->value("work", "") == "Travelling")
				markup += text(localise("Travelling — local details unavailable"));
			else if (selected->value("reserved", false) || selected->value("condition", "") == "lost" ||
			         selected->value("condition", "") == "detained")
				markup += text(localise("Team unavailable"));
			else
			{
				const auto location = selected->at("location").get<std::int64_t>() + 1;
				std::string compartment;
				for (const auto &p : parts)
					if (p.at("index") == location)
						compartment = p.at("id");
				markup += "<h3>" + esc(localise(engineering_name(compartment))) + "</h3>";
				if (selected->value("repair", false))
				{
					for (const auto &part : parts)
						if (part.at("index") == location)
							markup += text(localise("Hull") + ": " + part.at("hp").dump() + " / " +
							               part.at("maximum").dump());
					bool any = false;
					for (const auto &fault :
					     primary.gameplay.value("combat", Json::object()).value("faults", Json::array()))
						if (fault.at("compartment_index") == location)
						{
							any = true;
							markup +=
							    text(localise(engineering_name(fault.at("system"))) + ": " +
							         localise(fault.value("diagnosed", false) ? fault.value("diagnosis", std::string{})
							                                                  : fault.value("symptom", std::string{})));
						}
					if (!any)
						markup += text(localise("No observed system damage here"));
				}
				else
				{
					for (const auto &patient : selected->value("patients", Json::array()))
						if (patient.at("location") == selected->at("location"))
							markup += text(patient.value("name", patient.value("id", std::string{})) + ": " +
							               localise(patient.value("condition", std::string{})));
				}
				for (const auto &[action, label] : model.gameplay_controls())
					if ((stars >= 3 &&
					     action.starts_with("game.team-order/" + selected->at("id").get<std::string>() + "/")) ||
					    (action.starts_with("game.team-patient/") &&
					     std::any_of(selected->at("patients").begin(), selected->at("patients").end(),
					                 [&](const auto &patient)
					                 {
						                 return action == "game.team-patient/" +
						                                      patient.at("id").template get<std::string>() &&
						                        patient.at("location") == selected->at("location");
					                 })) ||
					    action == "game.team-cancel/" + selected->at("id").get<std::string>())
						markup += button(action, label);
			}
		}
		markup += roster && map ? "</div></div>" : "</div>";
	}
	return result;
}
} // namespace phoenix
