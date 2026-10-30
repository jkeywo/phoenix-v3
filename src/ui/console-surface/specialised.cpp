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
module phoenix.ui.console_surface;
import phoenix.content.assets;
import phoenix.content.controllers;
import phoenix.content.controllers.types;
import phoenix.content.ui;
import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.graphics.scene;
import phoenix.ui.theme;
import phoenix.ui.engineering_widgets;
import phoenix.ui.information_widgets;
import phoenix.ui.conversation_panel;
import phoenix.ui.surface_host;
import phoenix.ui.navigation_panel;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{
bool ConsoleSurface::State::prepare_specialised_component(const std::string &component, const ConsoleRegion &region)
{
	if (settings_open_ || !model_.layout() || !model_.layout()->value("strict_widgets", false) ||
	    !model_.observation() || !authored_component_visible(component))
		return false;
	if (component != "weapons" && component != "missiles" && component != "flight-status" &&
	    component != "lateral-left" && component != "lateral-right" && component != "veil-controls" &&
	    component != "repair" && component != "target-info")
		return false;
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	std::vector<std::pair<std::string, std::string>> readouts, buttons;
	std::string markup;
	auto text = [&](const std::string &value)
	{
		const auto key = id("specialised/" + component + "/readout/" + std::to_string(readouts.size()));
		readouts.emplace_back(key, value);
		return "<p id=\"" + esc(key) + "\"></p>";
	};
	auto button = [&](const std::string &action, const std::string &label, std::optional<bool> selected = {},
	                  const std::string &visible = {})
	{
		if (action.empty())
			return "<span>" + esc(label) + "</span>";
		const auto key = "task/specialised/" + component + "/" + std::to_string(buttons.size());
		task_actions_[key] = action;
		if (selected)
			app_->screen_toggles[id(key)] = *selected;
		buttons.emplace_back(id(key), label);
		return "<button id=\"" + esc(id(key)) + "\" class=\"" + (selected.value_or(false) ? "selected" : "") + "\">" +
		       esc(visible.empty() ? label : visible) + "</button>";
	};
	const auto &primary = *model_.observation();
	const auto profile = primary.gameplay.value("profile", std::string{});
	const int stars = primary.gameplay.value("profile_stars", Json::object()).value(profile, 0);
	auto control = [&](ConsoleModel &source, const std::string &prefix, const std::string &action,
	                   const std::string &equipment, std::int64_t value)
	{
		if (source.observation())
			for (const auto &d : source.observation()->gameplay.value("controls", Json::array()))
			{
				if (d.value("action", std::string{}) != action || d.value("value", std::int64_t{-99999}) != value ||
				    !d.value("enabled", true))
					continue;
				const auto consumers = command_consumers(*source.observation(), d);
				if (equipment.empty() || std::find(consumers.begin(), consumers.end(), equipment) != consumers.end())
					return prefix + descriptor_action(d.at("id"));
			}
		return std::string{};
	};
	if (component == "target-info")
	{
		for (const auto &[key, value] : tactical_.target_readouts(model_, app_->session->catalog(), false))
			markup += text(value);
		const auto target = tactical_.console().empty() ? model_.sensor_contact() : tactical_.model().sensor_contact();
		if (target && !model_.manual_active() &&
		    std::any_of(primary.contacts.begin(), primary.contacts.end(),
		                [&](const auto &contact)
		                {
			                return contact.id == target;
		                }))
		{
			markup += button("contact-navigation/follow_contact", localise("Follow"));
			markup += button("contact-navigation/match_speed", localise("Match speed"));
		}
		if (const auto dock = model_.target_docking_control(target))
			markup += button(dock->id, dock->label);
	}
	else if (component == "flight-status")
	{
		const bool narrow = document_->IsClassSet("courier-narrow");
		markup += text(localise(model_.manual_active() ? "Manual flight" : "Assisted flight"));
		const auto speed = std::string(TextFormat(
		    "%.1f", std::hypot(static_cast<double>(primary.own.vx), static_cast<double>(primary.own.vy)) / 1000.0));
		std::string requested;
		if (narrow)
			for (const auto &widget : model_.layout()->at("widgets"))
				if (widget.at("kind") == "throttle")
					requested = localise("Requested") + " " +
					            std::string(TextFormat(
					                "%.1f", model_.system_request(model_.widget_system(widget.at("id")), "speed_mm_s") /
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
	else if (component == "weapons" || component == "missiles")
	{
		markup =
		    "<h2>" + localise(component == "weapons" ? "Weapons" : "Missiles") + "</h2><div class=\"courier-weapons\">";
		std::set<std::string> shown, salvos;
		auto sources = task_sources();
		std::reverse(sources.begin(), sources.end());
		for (auto &[source, prefix] : sources)
			if (source->observation())
			{
				const auto &view = *source->observation();
				const auto combat = view.gameplay.value("combat", Json::object());
				const auto widgets = console_control_widgets(app_->session->catalog(), view);
				auto main_data = [&](const std::string &equipment, const std::string &binding)
				{
					for (const auto &widget : widgets)
						for (const auto &datum : widget.data)
							if (datum.equipment == equipment && datum.binding == binding &&
							    datum.destination == WidgetDataDestination::Main)
								return true;
					return false;
				};
				for (const auto &w : combat.value("weapons", Json::array()))
				{
					const auto identity = w.at("id").get<std::string>();
					const bool missile = w.value("load_ticks", std::int64_t{}) > 0 && !w.value("point_defence", false);
					if (missile != (component == "missiles") || !shown.insert(identity).second)
						continue;
					const auto system = std::find_if(view.systems.begin(), view.systems.end(),
					                                 [&](const auto &s)
					                                 {
						                                 return s.id == identity;
					                                 });
					if (system == view.systems.end())
						continue;
					const ControlWidgetView *details = nullptr;
					for (const auto &widget : widgets)
						if (std::any_of(widget.data.begin(), widget.data.end(),
						                [&](const auto &datum)
						                {
							                return datum.equipment == identity;
						                }) &&
						    (!details || widget.data.size() > details->data.size()))
							details = &widget;
					markup += "<div class=\"courier-weapon\"><div class=\"weapon-heading\">";
					markup += "<div class=\"weapon-switch\">";
					const auto toggle = control(*source, prefix, "system_enabled", identity, system->enabled ? 0 : 1);
					markup +=
					    missile ? "<h3>" + esc(localise(engineering_name(identity))) + "</h3>"
					            : button(toggle,
					                     localise(w.value("point_defence", false) ? "PDC" : engineering_name(identity)),
					                     system->enabled);
					markup += "</div>";
					if (details)
						markup += "<div class=\"weapon-details\">" +
						          button("control-details/" + view.console + "/" + details->id,
						                 localise("Details") + ": " + localise(engineering_name(identity)), {}, "i") +
						          "</div>";
					markup += "</div>";
					if (toggle.empty() && !missile)
						markup += text(localise("Automatic"));
					if (main_data(identity, "system.status"))
						markup += text(localise(system->status.empty() ? (system->enabled ? "Operational" : "Off")
						                                               : system->status));
					const auto mags = w.value("magazines", std::vector<std::string>{});
					const auto selected = w.value("selected", std::size_t{});
					if (!mags.empty() && selected < mags.size())
					{
						const auto magazine = std::find_if(view.systems.begin(), view.systems.end(),
						                                   [&](const auto &s)
						                                   {
							                                   return s.id == mags[selected];
						                                   });
						const auto ammo = magazine == view.systems.end()         ? 0
						                  : magazine->telemetry.contains("ammo") ? magazine->telemetry.at("ammo")
						                                                         : 0;
						std::string type = localise(engineering_name(mags[selected]));
						for (const auto &mag : combat.value("magazines", Json::array()))
							if (mag.at("id") == mags[selected])
							{
								const auto label = mag.value("resource_labels", Json::object())
								                       .value(mag.value("selected_resource", std::string{}), type);
								if (label != mag.value("selected_resource", std::string{}))
									type = localise(label);
							}
						if (missile)
						{
							if (main_data(mags[selected], "system.ammo"))
								markup += text(type + " — " + localise("Reserve") + ": " + std::to_string(ammo));
							const bool show_type = main_data(identity, "weapon.loaded_resource"),
							           show_quantity = main_data(identity, "weapon.loaded_quantity");
							if (show_type || show_quantity)
							{
								const auto loaded = w.value("loaded", std::int64_t{-1});
								std::string loaded_type = loaded >= 0 && static_cast<std::size_t>(loaded) < mags.size()
								                              ? localise(engineering_name(mags[loaded]))
								                              : localise("Empty");
								if (loaded >= 0 && static_cast<std::size_t>(loaded) < mags.size())
									for (const auto &mag : combat.value("magazines", Json::array()))
										if (mag.at("id") == mags[loaded])
										{
											const auto resource = w.value("loaded_resource", std::string{});
											const auto label = mag.value("resource_labels", Json::object())
											                       .value(resource, loaded_type);
											if (label != resource)
												loaded_type = localise(label);
										}
								markup += text(localise("Loaded") + ": " + (show_type ? loaded_type : "") +
								               (show_type && show_quantity ? " / " : "") +
								               (show_quantity ? w.at("loaded_quantity").dump() : ""));
							}
							markup += text(localise(w.value("unloading", 0) > 0         ? "Unloading"
							                        : w.value("loading", 0) > 0         ? "Loading"
							                        : w.value("loaded_quantity", 0) > 0 ? "Loaded"
							                                                            : "Empty"));
							if (stars >= 2)
								for (std::size_t i = 0; i < mags.size(); ++i)
									markup += button(control(*source, prefix, "magazine_select", identity, i),
									                 localise(engineering_name(mags[i])), selected == i);
						}
						else if (main_data(mags[selected], "system.ammo"))
							markup += text(localise("Ammo") + ": " + std::to_string(ammo));
					}
					if (missile)
						for (const auto &owner : view.gameplay.value("controllers", Json::array()))
							for (const auto &[channel, state] : owner.at("commands").items())
								if (state.at("action") == "weapon_fire" && state.value("request_id", std::uint64_t{}) &&
								    !state.value("pending", false) && !state.value("accepted", false) &&
								    std::find(state.at("consumers").begin(), state.at("consumers").end(),
								              Json(identity)) != state.at("consumers").end())
									markup += text(localise("Not fired: tube not ready or target unavailable"));
					if (!missile && main_data(identity, "system.stored_energy") &&
					    system->telemetry.contains("stored_energy"))
					{
						const auto stored = system->telemetry.at("stored_energy");
						const auto capacity =
						    system->telemetry.contains("energy_capacity") ? system->telemetry.at("energy_capacity") : 0;
						markup += text(
						    localise("Battery") + ": " +
						    (capacity > 0 ? std::to_string(stored * 100 / capacity) + "%" : std::to_string(stored)));
					}
					if (stars >= 3)
					{
						const auto target = source->sensor_contact();
						if (target)
							markup +=
							    button(control(*source, prefix, "weapon_target", identity, target), localise("Target"));
						if (w.value("point_defence", false))
							for (int i = 0; i < 3; ++i)
								markup += button(control(*source, prefix, "weapon_mode", identity, i),
								                 localise(i == 0   ? "Hold fire"
								                          : i == 1 ? "Intercept"
								                                   : "Selected target"),
								                 w.value("mode", 0) == i);
					}
					std::set<std::pair<std::string, std::string>> extra;
					for (const auto &widget : widgets)
						for (const auto &datum : widget.data)
							if (datum.equipment == identity)
							{
								if (datum.destination == WidgetDataDestination::Main &&
								    datum.binding != "system.status" &&
								    (missile || datum.binding != "system.stored_energy") &&
								    (!missile || (datum.binding != "weapon.loaded_quantity" &&
								                  datum.binding != "weapon.loaded_resource")) &&
								    extra.emplace(datum.equipment, datum.binding).second)
									markup += text(datum.label + ": " + datum.text);
							}
					markup += "</div>";
				}
				if (component == "missiles")
					for (const auto &d : view.gameplay.value("controls", Json::array()))
						if (d.value("action", "") == "weapon.salvo" &&
						    salvos.insert(view.console + "/" + d.value("system", "")).second)
							markup += button(prefix + descriptor_action(d.at("id")), localise("Fire"));
			}
		markup += "</div>";
	}
	else if (component == "veil-controls")
	{
		markup = "<h2>" + localise("Veil") + "</h2>";
		for (const auto &s : primary.systems)
			if (s.kind == OperatingSystem::Psychic)
			{
				const auto action = control(model_, "", "system_enabled", s.id, s.enabled ? 0 : 1);
				markup += button(action, localise(s.enabled ? "Veil enabled" : "Veil disabled"), s.enabled);
				markup += text(localise(s.status.empty() ? (action.empty() ? "Automatic" : "Operational") : s.status));
				if (stars >= 3)
					markup += button("game.status/" + s.id, localise("Details"));
			}
	}
	else if (component == "repair")
	{
		const auto parts = primary.gameplay.value("combat", Json::object()).value("compartments", Json::array());
		const auto teams = primary.gameplay.value("teams", Json::array());
		const Json *selected = nullptr;
		markup = "<div class=\"courier-repair\"><div class=\"courier-roster\"><h2>" + localise("Teams") + "</h2>";
		for (const auto &team : teams)
		{
			const auto identity = team.at("id").get<std::string>();
			if (identity == model_.selected_team())
				selected = &team;
			std::string order;
			for (const auto &o : team.at("orders"))
				if (o.at("id") == "auto" || order.empty())
					order = o.at("id");
			markup += button(order.empty() ? "" : "game.team-order/" + identity + "/" + order,
			                 localise(engineering_name(identity)), identity == model_.selected_team());
			markup += text(localise(team.value("work", "Idle")) + " / " + localise(team.value("condition", "healthy")));
		}
		markup += "</div><div class=\"courier-map-area\"><h2>" + localise("Ship compartments") + "</h2>";
		if (!selected)
			markup += text(localise("Select a team"));
		else
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
				markup += "<h3>" + esc(localise(deck)) + "</h3><div class=\"courier-ship-map\">";
				const auto eligibility = model_.team_map();
				for (const auto &p : parts)
					if (p.at("deck") == deck)
					{
						const auto index = p.at("index").get<std::int64_t>();
						const double x = p.at("x_mm"), y = p.at("y_mm"), w = p.at("half_width_mm"),
						             h = p.at("half_length_mm");
						const bool here = selected->at("location").get<std::int64_t>() + 1 == index;
						const bool destination = selected->at("destination").get<std::int64_t>() + 1 == index;
						bool allowed = false;
						for (const auto &e : eligibility)
							if (e.at("index") == index)
								allowed = e.value("dispatch", false) && e.value("eligible", false) &&
								          selected->value("manual", false) && !selected->value("reserved", false);
						const auto key = "task/specialised/repair/" + std::to_string(buttons.size());
						const auto label =
						    localise(engineering_name(p.at("id"))) +
						    (here ? " — " + localise(selected->value("work", "") == "Travelling" ? "En route from here"
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
						          "%;height:" + std::to_string(200 * w / std::max(1.0, maxx - minx)) + "%;\" class=\"" +
						          (here ? "selected" : "") + "\">" + esc(label) + "</button>";
					}
				markup += "</div>";
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
				for (const auto &[action, label] : model_.gameplay_controls())
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
		markup += "</div></div>";
	}
	auto *panel = authored_component(component);
	panel->SetClass("compact-weapons",
	                component == "weapons" && panel->GetClientWidth() / context_->GetDensityIndependentPixelRatio() <
	                                              220 * text_scale_ / 100.f);
	if (station_component_markup_[component] != markup)
	{
		panel->SetInnerRML(markup);
		station_component_markup_[component] = markup;
		dom_.forget_elements();
	}
	for (const auto &[key, value] : readouts)
	{
		if (auto *field = document_->GetElementById(key))
			dom_.leaf_markup(field, esc(value));
		app_->screen_readouts.emplace_back(key, value);
	}
	context_->Update();
	for (const auto &[key, value] : readouts)
		place(*app_, region, key);
	for (const auto &[key, label] : buttons)
	{
		app_->screen_controls.emplace_back(key, label);
		place(*app_, region, key);
	}
	return true;
}
} // namespace phoenix
