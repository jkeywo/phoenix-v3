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
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{
void ConsoleSurface::State::prepare_navigation(Application &app, const ConsoleRegion &region, bool pregame)
{
	navigation_visible_ = false;
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			navigation_visible_ |=
			    widget.at("kind") == "navigation" && ui_widget_available(widget, model_.ui_availability());
	const auto objectives = model_.objectives();
	const auto report =
	    model_.observation() ? model_.observation()->gameplay.value("mission_report", Json(nullptr)) : Json(nullptr);
	const auto ftl =
	    model_.observation() && !pregame ? model_.observation()->gameplay.value("ftl", Json::array()) : Json::array();
	const bool navigator_panel = !ftl.empty() && model_.layout() && model_.layout()->at("widgets").empty() &&
	                             !model_.layout()->contains("layout");
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("kind") == "objectives")
				if (auto *target = document_->GetElementById("value/" + widget.at("id").get<std::string>()))
				{
					std::string text;
					for (const auto &objective : objectives)
						text += "<p>" +
						        Rml::StringUtilities::EncodeRml(localise(objective.value("title", "")) + ": " +
						                                        localise(objective.value("outcome", "").empty()
						                                                     ? objective.value("status", "")
						                                                     : objective.value("outcome", "")) +
						                                        ". " + localise(objective.value("description", ""))) +
						        "</p>";
					leaf_markup(target, text);
				}
	mission_outcome_.clear();
	for (const auto &objective : objectives)
		if (!objective.value("outcome", "").empty())
			mission_outcome_ = objective.at("outcome");
	if (!report.is_null())
		mission_outcome_ = "ftl";
	if (pregame)
		mission_outcome_.clear();
	if (!mission_outcome_.empty())
		navigation_visible_ = false;
	document_->SetClass("navigation-console", navigation_visible_);
	if ((navigation_visible_ || navigator_panel || !mission_outcome_.empty()) &&
	    (!(model_.layout() && model_.layout()->contains("layout")) || !mission_outcome_.empty()))
		property(document_->GetElementById("gameplay"), "display", "none");
	navigation_area_ = {0, 110, region.width, std::max(50.f, region.height - 250)};
	if (navigation_visible_ && model_.observation())
	{
		const auto &own = model_.observation()->own;
		double left = own.x, right = own.x, bottom = own.y, top = own.y;
		auto include = [&](double x, double y, double radius)
		{
			left = std::min(left, x - radius);
			right = std::max(right, x + radius);
			bottom = std::min(bottom, y - radius);
			top = std::max(top, y + radius);
		};
		for (const auto &contact : model_.observation()->contacts)
			include(contact.x, contact.y, 0);
		const auto w = model_.waypoint();
		if (w.value("active", false))
			include(w.at("x"), w.at("y"), 0);
		navigation_camera_.fit(left, bottom, right, top, navigation_area_.width, navigation_area_.height);
	}
	std::vector<std::pair<std::string, std::string>> actions;
	auto button = [&](std::string key, std::string label)
	{
		actions.emplace_back(key, localise(label));
		if (key.starts_with("on-screen/"))
			return on_screen_markup(key);
		return "<button id=\"" + id(key) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
	};
	std::string markup;
	if (!mission_outcome_.empty())
	{
		std::erase_if(app.screen_controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(id(""));
		              });
		std::erase_if(app.screen_inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(id(""));
		              });
		markup =
		    "<div class=\"mission-result\" style=\"width:" + std::to_string(region.width) +
		    "px;height:" + std::to_string(region.height) + "px;\"><img class=\"mission-art\" src=\"" +
		    asset_path("mission-terminal.png") +
		    "\"/><div class=\"mission-copy\" style=\"left:" + std::to_string(region.width * .07f) +
		    "px;top:" + std::to_string(region.height * .08f) + "px;width:" + std::to_string(region.width * .86f) +
		    "px;height:" + std::to_string(region.height * .8f) + "px;\"><h1>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("title").get<std::string>()
		                                                                  : "MISSION REPORT // FINAL") +
		    "</h1><h2>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("result_title").get<std::string>()
		             : mission_outcome_ == "ftl"                          ? "Departed via FTL"
		             : mission_outcome_ == "won"                          ? "You won"
		             : mission_outcome_ == "lost"                         ? "You lost"
		                                                                  : "Draw") +
		    "</h2><div class=\"terminal-rule\"></div><p class=\"terminal-status\">" + localise("Mission complete") +
		    "</p>";
		std::string report_text = localise(mission_outcome_ == "ftl" ? "Departed via FTL" : "Mission complete");
		if (!report.is_null() && report.value("version", 1) == 2)
			report_text += ". " + localise(report.at("title")) + ". " + localise(report.at("result_title"));
		const auto column_height = std::to_string(region.height * .57f) + "px";
		markup += "<div style=\"display:flex;width:" + std::to_string(region.width * .81f) +
		          "px;gap:24px;\"><div class=\"mission-report-main\" style=\"flex-shrink:0;width:" +
		          std::to_string(region.width * .53f) + "px;height:" + column_height + ";overflow:auto;\">";
		for (const auto &o : report.is_null() ? objectives : report.at("objectives"))
		{
			const auto status = o.value("status", "active");
			const auto line =
			    localise(o.value("title", "")) +
			    (report.is_null() ? ""
			                      : ": " + localise((status == "active" || status == "pending") ? "Unfinished"
			                                        : status == "completed"                     ? "Completed"
			                                        : status == "failed"                        ? "Failed"
			                                                                                    : "Cancelled"));
			markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
			report_text += ". " + line;
		}
		if (!report.is_null() && report.value("version", 1) == 2)
		{
			auto line = [&](std::string value)
			{
				markup += "<p>" + Rml::StringUtilities::EncodeRml(value) + "</p>";
				report_text += ". " + value;
			};

			line(localise(report.at("explanation")));
			for (const auto &a : report.value("assessments", Json::array()))
			{
				line(localise(a.at("title")) + ": " + localise(a.at("result_title")));
				line(localise(a.at("explanation")));
			}
			markup += "<h1>" + localise("Mission log") + "</h1>";
			for (const auto &entry : report.value("log", Json::array()))
			{
				const auto seconds = entry.at("tick").get<std::uint64_t>() / 20;
				line(std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
				     std::to_string(seconds % 60) + " — " + localise(entry.at("text")) +
				     (entry.value("name", "").empty() ? "" : ": " + localise(entry.at("name"))));
			}
			markup += "</div><div class=\"mission-report-side\" style=\"flex-shrink:0;width:" +
			          std::to_string(region.width * .25f) + "px;height:" + column_height +
			          ";overflow:auto;border-left:1px #51744c;padding-left:16px;\"><h1>" + localise("Ship and crew") +
			          "</h1>";
			for (const auto &part : report.value("ship_condition", Json::array()))
				line(localise(part.at("name")) + ": " + part.at("hp").dump() + "/" + part.at("maximum").dump());
			for (const auto &crew : report.value("crew", Json::array()))
				line(localise(crew.at("name")) + ": " + localise(crew.at("condition")));
			for (const auto &f : report.at("facts"))
			{
				std::string value = f.at("value").is_null()      ? localise("Unresolved")
				                    : f.at("value").is_string()  ? localise(f.at("value").get<std::string>())
				                    : f.at("value").is_boolean() ? localise(f.at("value").get<bool>() ? "Yes" : "No")
				                                                 : f.at("value").dump();
				line(localise(f.at("label")) + ": " + value);
			}
			for (const auto &p : report.at("personnel"))
				line(localise(p.at("name")) + ": " + localise(p.at("status")) +
				     (p.value("location", "").empty() ? "" : " — " + localise(p.at("location"))));
		}
		if (!report.is_null() && report.value("version", 1) == 1)
			for (const auto &name : report.at("left_behind"))
			{
				const auto line = localise("Personnel left behind") + ": " + localise(name);
				markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
				report_text += ". " + line;
			}
		markup += "</div></div>" + button("mission.leave", "Return to menu") + "</div></div>";
		app.screen_readouts.emplace_back(id("mission-result"), report_text);
	}
	else if (navigator_panel)
	{
		std::erase_if(app.screen_controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(id("game."));
		              });
		std::erase_if(app.screen_inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(id("game."));
		              });
		markup = "<div class=\"navigator-panel\" style=\"position:absolute;left:5%;top:120px;width:90%;height:" +
		         std::to_string(std::max(50.f, region.height - 160)) + "px;overflow:auto;\"><h1>" +
		         localise("FTL departure") + "</h1>";
		for (const auto &[key, text] : model_.gameplay_readouts())
			if (key.starts_with("game.readout/ftl"))
				markup += "<p>" + Rml::StringUtilities::EncodeRml(text) + "</p>";
		for (const auto &[key, label] : model_.gameplay_controls())
			if (key.starts_with("game.control/"))
			{
				actions.emplace_back(key, label);
				markup += "<button id=\"" + id(key) + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
		if (!ftl.at(0).at("ready").get<bool>())
			markup += "<button disabled=\"disabled\" aria-disabled=\"true\">" + localise("Leave mission") + "</button>";
		markup += "</div>";
	}
	else if (navigation_visible_)
	{
		markup = "<div class=\"navigation-canvas\" id=\"" + id("nav.map") +
		         "\" tabindex=\"0\" aria-label=\"Navigation map. Arrow keys select a "
		         "position; Page Up and Page Down zoom; Tab reaches the waypoint action.\" "
		         "style=\"position:absolute;left:0;top:0;width:100%;height:88%;\"></div><div "
		         "class=\"navigation-controls\" style=\"position:absolute;left:0;bottom:0;width:100%;height:12%;\">";
		if (navigation_selected_contact_ && model_.observation())
			for (const auto &contact : model_.observation()->contacts)
				if (contact.id == navigation_selected_contact_)
					markup += "<span>" +
					          Rml::StringUtilities::EncodeRml(localise(contact.definition) + " #" +
					                                          std::to_string(contact.id)) +
					          "</span>";
		if (navigation_selected_)
		{
			const auto waypoint = model_.waypoint();
			const bool selected_waypoint =
			    waypoint.value("active", false) &&
			    std::hypot(navigation_x_ - waypoint.value("x", 0.0), navigation_y_ - waypoint.value("y", 0.0)) <=
			        navigation_camera_.scale * 18;
			markup += button(selected_waypoint ? "nav.clear" : "nav.set",
			                 selected_waypoint ? "Clear waypoint" : "Set waypoint");
		}
		if (const auto *layout = model_.layout())
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("kind") == "navigation")
					markup += button("on-screen/" + widget.at("id").get<std::string>(), "On screen");
		markup += "</div>";
	}
	if (markup != navigation_markup_)
	{
		const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("navigation-layer")->SetInnerRML(markup);
		// Size the report after insertion; this layer has no intrinsic layout width.
		if (!mission_outcome_.empty())
			if (auto *copy = document_->QuerySelector(".mission-copy"))
			{
				auto *layer = document_->GetElementById("navigation-layer");
				layer->SetProperty("display", "block");
				layer->SetProperty("width", std::to_string(region.width) + "px");
				layer->SetProperty("height", std::to_string(region.height) + "px");
				auto *pane = copy->GetParentNode();
				pane->SetProperty("display", "block");
				pane->SetProperty("width", std::to_string(region.width) + "px");
				pane->SetProperty("height", std::to_string(region.height) + "px");
				copy->SetProperty("display", "block");
				copy->SetProperty("position", "absolute");
				copy->SetProperty("left", std::to_string(region.width * .07f) + "px");
				copy->SetProperty("top", std::to_string(region.height * .08f) + "px");
				copy->SetProperty("width", std::to_string(region.width * .86f) + "px");
				copy->SetProperty("height", std::to_string(region.height * .8f) + "px");
				for (int child = 0; child < copy->GetNumChildren(); ++child)
				{
					auto *line = copy->GetChild(child);
					if (line->GetTagName() == "h1" || line->GetTagName() == "h2" || line->GetTagName() == "p")
						line->SetProperty("width", std::to_string(region.width * .86f) + "px");
				}
				for (const auto &[selector, fraction] : std::vector<std::pair<std::string, float>>{
				         {".mission-report-main", .53f}, {".mission-report-side", .25f}})
					if (auto *column = copy->QuerySelector(selector))
						for (int child = 0; child < column->GetNumChildren(); ++child)
							column->GetChild(child)->SetProperty("width",
							                                     std::to_string(region.width * fraction - 24) + "px");
			}

		navigation_markup_ = markup;
		elements_.clear();
		if (!mission_outcome_.empty())
		{
			app.accessible_status = mission_outcome_ == "ftl"
			                            ? localise("Departed via FTL")
			                            : localise("Mission complete") + ": " + localise(mission_outcome_);
			if (auto *button = document_->GetElementById(id("mission.leave")))
				button->Focus();
		}
		else if (auto *previous = document_->GetElementById(focus))
			previous->Focus();
	}
	for (const auto &[key, label] : actions)
		if (key.starts_with("on-screen/"))
			if (auto *b = document_->GetElementById(id(key)))
			{
				const bool active = model_.is_on_screen(key.substr(10));
				app.screen_toggles[id(key)] = active;
				b->SetClass("on-screen-active", active);
				b->SetAttribute("aria-pressed", active ? "true" : "false");
			}

	context_->Update();
	navigation_buttons_.clear();
	for (const auto &[key, label] : actions)
	{
		app.screen_controls.emplace_back(id(key), label);
		place(app, region, id(key));
		if (auto *button = document_->GetElementById(id(key)))
		{
			const auto at = button->GetAbsoluteOffset(Rml::BoxArea::Border);
			navigation_buttons_.push_back({at.x, at.y, button->GetOffsetWidth(), button->GetOffsetHeight()});
		}
	}
	if (navigation_visible_)
	{
		app.screen_controls.emplace_back(id("nav.map"),
		                                 localise("Navigation map. Arrow keys select a position. Page Up and Page Down "
		                                          "zoom. Tab reaches the waypoint action."));
		place(app, region, id("nav.map"));
	}
}

bool ConsoleSurface::State::select_radar_contact(EntityId contact)
{
	const bool accepted = tactical_.select_contact(model_, contact);
	prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	return accepted;
}
void ConsoleSurface::State::prepare_system_popup(const ConsoleRegion &region, const std::vector<EngineeringCard> &cards)
{
	popup_controls_.clear();
	ConsoleModel *detail_model = nullptr;
	std::string detail_prefix;
	if (!model_.status_system().empty())
		detail_model = &model_;
	else if (!tactical_.console().empty() && !tactical_.model().status_system().empty())
	{
		detail_model = &tactical_.model();
		detail_prefix = "inline/";
	}
	else
		for (const auto &[console, source] : support_models_)
			if (!source->status_system().empty())
			{
				detail_model = source.get();
				detail_prefix = "support-console/" + console + "/";
				break;
			}
	const auto details = detail_model ? detail_model->system_details() : std::string{};
	std::string markup;
	std::vector<std::pair<std::string, std::string>> popup_readouts;
	std::vector<PowerSetting> popup_power;
	auto esc = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	auto add = [&](const std::string &action, const std::string &label)
	{
		const auto name = id((action.find("game.control/") != std::string::npos || action.starts_with("game.team") ||
		                      action.starts_with("game.compartment/"))
		                         ? "popup-control/" + action
		                         : action);
		popup_controls_.emplace_back(name, label);
		return "<button id=\"" + esc(name) + "\">" + esc(label) + "</button>";
	};
	auto readout = [&](const std::string &key, const std::string &text)
	{
		const auto name = id("popup/" + key);
		popup_readouts.emplace_back(name, text);
		return "<p id=\"" + esc(name) + "\"></p>";
	};
	if (!details.empty())
	{
		markup = "<h1>" + esc(localise("System details")) + "</h1>" + readout("summary", details) +
		         add("systems", localise("All system statuses")) + add("systems-close", localise("Close"));
		auto &source = *detail_model;
		const auto system = source.status_system();
		if (source.observation())
			for (const auto &state : source.observation()->systems)
				if (state.id == system)
				{
					for (const auto &[key, value] : state.telemetry)
						markup +=
						    readout(system + "/" + key, localise(engineering_name(key)) + ": " + std::to_string(value));
					for (const auto &[key, text] : source.gameplay_readouts())
						if ((state.kind == OperatingSystem::Ftl && key.starts_with("game.readout/ftl")) ||
						    (state.kind == OperatingSystem::Psychic && key.starts_with("psychic.")) ||
						    (state.kind == OperatingSystem::Team &&
						     (key.starts_with("team.") || key.starts_with("medical."))) ||
						    (state.kind == OperatingSystem::Shield && key.starts_with("shield.")))
							markup += readout(key, text);
				}
		if (source.observation())
			for (const auto &[name, label] : source.gameplay_controls())
				if (name.starts_with("game.control/"))
					for (const auto &control : source.observation()->gameplay.value("controls", Json::array()))
						if (descriptor_action(control.at("id")) == name && !control.value("momentary", false))
						{
							const auto consumers = command_consumers(*source.observation(), control);
							if (std::find(consumers.begin(), consumers.end(), system) != consumers.end())
								markup += add(detail_prefix + name, label);
						}
		if (&source == &model_ && source.observation())
			for (const auto &state : source.observation()->systems)
				if (state.id == system && state.kind == OperatingSystem::Team)
					for (const auto &[action, label] : model_.asset_away_controls("team:" + system))
						markup += add(action, label);
	}
	else if (systems_open_ && support_popup_ == "target")
	{
		markup = "<h1>" + esc(localise("Contact details")) + "</h1>";
		for (const auto &[key, text] : tactical_.target_readouts(model_, app_->session->catalog(), true))
			markup += readout(key, text);
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_ && (support_popup_ == "weapons" || support_popup_ == "teams"))
	{
		markup = "<h1>" + esc(localise(support_popup_ == "weapons" ? "Weapons" : "Teams")) + "</h1>";
		std::set<std::string> shown;
		for (auto &[source, prefix] : task_sources())
			if (source->observation())
			{
				const auto &view = *source->observation();
				if (support_popup_ == "teams")
					for (const auto &team : view.gameplay.value("teams", Json::array()))
						if (shown.insert(team.at("id")).second)
							markup += add(team.value("repair", false)
							                  ? "support-details/team/" + team.at("id").get<std::string>()
							                  : "support-details/medical",
							              localise(engineering_name(team.at("id"))) + " — " +
							                  localise(team.value("work", "Ready")));
				if (support_popup_ == "weapons")
					for (const auto &system : view.systems)
						if (system.kind == OperatingSystem::Weapon && !shown.contains(system.id))
							for (const auto &control : view.gameplay.value("controls", Json::array()))
								if (control.value("action", "") == "system_enabled" &&
								    control.value("value", -1) == (system.enabled ? 0 : 1))
								{
									const auto consumers = command_consumers(view, control);
									if (std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
									{
										shown.insert(system.id);
										markup += add(prefix + descriptor_action(control.at("id")),
										              localise(engineering_name(system.id)) + " — " +
										                  localise(system.enabled ? "On" : "Off"));
										break;
									}
								}
			}
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_ && support_popup_ == "power" && model_.observation())
	{
		markup = "<h1>" + esc(localise("Power details")) + "</h1><div class=\"engineering-board\">";
		const auto power = model_.observation()->gameplay.value("power", Json::object());
		for (const auto &row : power.value("reserves", Json::array()))
			markup += readout("reserve/" + row.at("id").get<std::string>(), localise(engineering_name(row.at("id"))) +
			                                                                    ": " + row.at("energy").dump() + " / " +
			                                                                    row.at("capacity").dump());
		for (const auto &row : power.value("groups", Json::array()))
			markup +=
			    readout("group/" + row.at("id").get<std::string>(),
			            localise(engineering_name(row.at("id"))) + ": " + std::to_string(row.value("allocation", 0LL)) +
			                " / s; " + row.at("energy").dump() + " / " + row.at("capacity").dump());
		std::set<std::string> detail_settings;
		for (const auto &original : power_settings(*model_.observation()))
		{
			auto setting = original;
			setting.id = "power/detail/" + setting.controller + "/" + setting.channel;
			if (!detail_settings.insert(setting.id).second)
				continue;
			power_controls_[setting.id] = setting;
			popup_power.push_back(setting);
			markup += "<label>" +
			          esc(localise(engineering_name(setting.subject)) + " — " +
			              localise(setting.coolant ? "Coolant" : "Power")) +
			          "</label><input type=\"range\" id=\"" + esc(id(setting.id)) +
			          "\" min=\"0\" max=\"100\" step=\"0.1\" value=\"0\"/>";
		}
		markup += "</div>" + add("systems-close", localise("Close"));
	}
	else if (systems_open_ && (support_popup_ == "compartments" || support_popup_ == "objectives") &&
	         model_.observation())
	{
		markup = "<h1>" + esc(localise(support_popup_ == "compartments" ? "Compartments" : "Objectives")) + "</h1>";
		if (support_popup_ == "compartments")
			for (const auto &part :
			     model_.observation()->gameplay.value("combat", Json::object()).value("compartments", Json::array()))
				markup += readout(part.at("id"), localise(engineering_name(part.at("id"))) + ": " +
				                                     part.at("hp").dump() + " / " + part.at("maximum").dump());
		else
			for (const auto &objective : model_.objectives())
				markup += readout(std::to_string(objective.value("id", 0ULL)),
				                  localise(objective.value("title", "")) + " — " +
				                      localise(objective.value("description", "")));
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_ && support_popup_.starts_with("team/") && model_.observation())
	{
		const auto selected = support_popup_.substr(5);
		const auto &view = *model_.observation();
		markup = "<h1>" + esc(localise(engineering_name(selected))) + "</h1>";
		for (const auto &team : view.gameplay.value("teams", Json::array()))
			if (team.at("id") == selected && team.value("repair", false))
			{
				markup += readout("team/activity",
				                  localise(team.value("work", "Idle")) + " | " + localise(team.at("condition")));
				const auto parts = view.gameplay.value("combat", Json::object()).value("compartments", Json::array());
				for (const auto &part : parts)
					if (part.at("index").get<std::int64_t>() == team.at("location").get<std::int64_t>() + 1)
						markup += readout("team/location",
						                  localise("Section") + ": " + localise(engineering_name(part.at("id"))));
				const bool present = !team.value("reserved", false) && team.value("travel", 0) == 0 &&
				                     team.at("condition") != "lost" && team.at("condition") != "detained";
				if (present)
				{
					bool any = false;
					for (const auto &fault :
					     view.gameplay.value("combat", Json::object()).value("faults", Json::array()))
						if (fault.at("compartment_index").get<std::int64_t>() ==
						    team.at("location").get<std::int64_t>() + 1)
						{
							any = true;
							markup += readout(
							    "fault/" + fault.at("system").get<std::string>() + "/" +
							        fault.at("symptom").get<std::string>(),
							    localise(engineering_name(fault.at("system"))) + ": " + localise(fault.at("symptom")) +
							        (fault.value("diagnosed", false) ? " — " + localise(fault.at("diagnosis")) : ""));
						}
					if (!any)
						markup += readout("team/damage", localise("No observed system damage here"));
				}
				else
					markup += readout("team/damage", localise("System damage details require a team in the section"));
				for (const auto &[action, label] : model_.asset_away_controls("team:" + selected))
					markup += add(action, label);
				if (team.value("assignment_authority", false) && !team.value("reserved", false))
					for (const auto &part : model_.team_map())
						if (part.value("dispatch", false) && part.value("eligible", false))
							markup += add("game.compartment/" + std::to_string(part.at("index").get<std::int64_t>()),
							              localise("Send to") + " " + localise(engineering_name(part.at("id"))));
				if (!team.value("reserved", false))
					for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
					{
						const auto consumers = command_consumers(view, descriptor);
						if (std::find(consumers.begin(), consumers.end(), selected) == consumers.end())
							continue;
						const auto action = descriptor.value("action", "");
						if (action == "repair_compartment")
						{
							const auto target = descriptor.at("value").get<std::int64_t>();
							if (target == 0)
								markup += add(descriptor_action(descriptor.at("id")), localise("Automatic deployment"));
							for (const auto &part : parts)
								if (part.at("index").get<std::int64_t>() == target)
									markup +=
									    add(descriptor_action(descriptor.at("id")),
									        localise("Send to") + " " + localise(engineering_name(part.at("id"))));
						}
						else if (action == "repair_system" && present)
						{
							const auto label = descriptor.value("label", action);
							bool observed = descriptor.at("value") == 0 || label == "Compartment hull";
							for (const auto &fault :
							     view.gameplay.value("combat", Json::object()).value("faults", Json::array()))
								observed |=
								    fault.at("system") == label && fault.at("compartment_index").get<std::int64_t>() ==
								                                       team.at("location").get<std::int64_t>() + 1;
							if (observed)
								markup +=
								    add(descriptor_action(descriptor.at("id")), localise(engineering_name(label)));
						}
					}
			}
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_ && support_popup_ == "medical" && model_.observation())
	{
		markup = "<h1>" + esc(localise("Medical and support teams")) + "</h1>";
		const auto teams = model_.observation()->gameplay.value("teams", Json::array());
		for (const auto &team : teams)
			if (!team.value("repair", false))
				markup += readout("team/" + team.at("id").get<std::string>(),
				                  localise(engineering_name(team.at("id"))) + " — " +
				                      localise(team.value("work", "Idle")) + " | " + localise(team.at("condition")));
		for (const auto &team : teams)
			if (!team.value("repair", false))
				for (const auto &[action, label] :
				     model_.asset_away_controls("team:" + team.at("id").get<std::string>()))
					markup += add(action, localise(engineering_name(team.at("id"))) + ": " + label);
		for (const auto &[key, text] : model_.gameplay_readouts())
			if (key.starts_with("medical."))
				markup += readout(key, text);
		for (const auto &[name, label] : model_.gameplay_controls())
		{
			bool allowed = name.starts_with("game.team-patient/");
			for (const auto &team : teams)
				if (!team.value("repair", false))
					allowed |= name.starts_with("game.team-order/" + team.at("id").get<std::string>() + "/") ||
					           name == "game.team-cancel/" + team.at("id").get<std::string>();
			if (allowed)
				markup += add(name, label);
		}
		for (const auto &part : model_.team_map())
			if (part.value("dispatch", false) && part.value("eligible", false))
				markup += add("game.compartment/" + std::to_string(part.at("index").get<std::int64_t>()),
				              localise("Send to") + " " + localise(engineering_name(part.at("id"))));
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_)
	{
		markup = "<h1>" + esc(localise("System statuses")) + "</h1>";
		std::set<std::string> shown, dedicated;
		for (const auto &card : cards)
			dedicated.insert(card.id);
		std::vector<std::pair<ConsoleModel *, std::string>> sources{{&model_, ""}};
		if (!tactical_.console().empty())
			sources.emplace_back(&tactical_.model(), "inline/");
		for (const auto &[console, source] : support_models_)
			sources.emplace_back(source.get(), "support-console/" + console + "/");
		for (const auto &[source, prefix] : sources)
			for (const auto &status : source->system_statuses())
				if (!dedicated.contains(status.system) && shown.insert(status.system).second)
					markup += "<p>" + add(prefix + "game.status/" + status.system, status.semantic_label) + "</p>";
		markup += add("systems-close", localise("Close"));
	}
	auto *dialog = document_->GetElementById("fault-dialog");
	if (markup != fault_dialog_signature_)
	{
		elements_.clear();
		properties_.clear();
		syncing_values_ = true;
		dialog->SetInnerRML(markup);
		syncing_values_ = false;
		fault_dialog_signature_ = markup;
	}
	property(dialog, "left", std::to_string(region.width * .1f) + "dp");
	property(dialog, "top", std::to_string(region.height * .15f) + "dp");
	property(dialog, "width", std::to_string(region.width * .8f) + "dp");
	property(dialog, "height", std::to_string(region.height * .65f) + "dp");
	property(dialog, "display", markup.empty() ? "none" : "block");
	context_->Update();
	if (!markup.empty())
	{
		const float width = std::max(44.f, region.width * .8f - 80.f);
		Rml::ElementList buttons;
		dialog->GetElementsByTagName(buttons, "button");
		for (auto *button : buttons)
		{
			property(button, "width", std::to_string(std::min(210.f, width)) + "dp");
			property(button, "min-height", "44dp");
		}
		for (int i = 0; i < dialog->GetNumChildren(); ++i)
			if (auto *child = dialog->GetChild(i); child->GetTagName() == "p" || child->GetTagName() == "h1")
				property(child, "width", std::to_string(width) + "dp");
	}
	if (!markup.empty())
	{
		std::erase_if(app_->screen_controls,
		              [&](const auto &entry)
		              {
			              return entry.first.starts_with(id(""));
		              });
		std::erase_if(app_->screen_inputs,
		              [&](const auto &entry)
		              {
			              return entry.id.starts_with(id(""));
		              });
	}
	syncing_values_ = true;
	for (const auto &setting : popup_power)
	{
		auto value = setting.value;
		if (auto pending = power_pending_.find(setting.id);
		    pending != power_pending_.end() && app_->session->ticks() < pending->second.until)
			value = pending->second.value;
		const auto percent = std::to_string(
		    std::clamp(100.0 * (value - setting.minimum) / std::max<std::int64_t>(1, setting.maximum - setting.minimum),
		               0.0, 100.0));
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id(setting.id))))
			if (field->GetValue() != percent)
				field->SetValue(percent);
		app_->screen_inputs.push_back({id(setting.id),
		                               localise(engineering_name(setting.subject)) + " " +
		                                   localise(setting.coolant ? "Coolant (%)" : "Power (%)"),
		                               percent, true});
	}
	syncing_values_ = false;
	for (const auto &[name, text] : popup_readouts)
	{
		if (auto *row = document_->GetElementById(name))
			leaf_markup(row, esc(text));
		app_->screen_readouts.emplace_back(name, text);
	}
	context_->Update();
	if (systems_open_ && support_popup_ == "power")
		for (const auto &[key, setting] : power_controls_)
			if (key.starts_with("power/detail/"))
				place(*app_, region, id(key));
	for (const auto &[name, label] : popup_controls_)
	{
		app_->screen_controls.emplace_back(name, label);
		place(*app_, region, name);
	}
}
void ConsoleSurface::State::prepare_radar_choices(const ConsoleRegion &region)
{
	if (!model_.layout() || !model_.observation())
		return;
	const auto &view = *model_.observation();
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("kind") == "radar")
		{
			auto *dial = document_->GetElementById("dial/" + widget.at("id").get<std::string>());
			if (!dial || !dial->IsVisible(true))
				continue;
			const auto chosen =
			    select_system(view.systems, OperatingSystem::Radar, model_.widget_system(widget.at("id")), false);
			for (const auto &sensor : view.systems)
				if (chosen && sensor.id == *chosen && sensor.maximum > 0)
				{
					if (!sensor.enabled)
					{
						if (dial->GetNumChildren())
						{
							elements_.clear();
							properties_.clear();
							dial->SetInnerRML("");
							radar_choices_signature_.clear();
						}
						return;
					}
					std::vector<std::pair<EntityId, Vector2>> points;
					std::map<EntityId, std::string> labels;
					const Vector2 centre{dial->GetOffsetWidth() / 2, dial->GetOffsetHeight() / 2};
					const float radius = std::min(centre.x, centre.y) * .66f;
					for (const auto &body : view.contacts)
						if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) !=
						        sensor.contact_ids.end() &&
						    radar_contact_in_range(view.own, body, sensor.maximum))
						{
							points.emplace_back(body.id,
							                    radar_contact_point(view.own, body, sensor.maximum, centre, radius));
							const auto &catalog = app_->session->catalog();
							labels[body.id] =
							    localise("Select target") + ": " +
							    localise(catalog.entities.contains(body.definition)
							                 ? catalog.entities.at(body.definition).value("name", body.definition)
							                 : body.definition) +
							    " #" + std::to_string(body.id);
						}
					for (const auto &body : view.bearing_contacts)
						if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) !=
						    sensor.contact_ids.end())
						{
							points.emplace_back(body.id, bearing_ring_point(centre, radius, body.bearing_mdeg));
							labels[body.id] = localise("Select bearing contact") + " #" + std::to_string(body.id);
						}
					std::string markup;
					for (const auto &[target, point] : points)
						markup += "<button class=\"radar-contact\" "
						          "style=\"decorator:none;background:transparent;border-width:0px\" id=\"" +
						          id("radar-contact/" + std::to_string(target)) + "\" aria-label=\"" +
						          Rml::StringUtilities::EncodeRml(labels[target]) + "\"/>";
					if (radar_choices_signature_ != markup || dial->GetNumChildren() != int(points.size()))
					{
						elements_.clear();
						properties_.clear();
						dial->SetInnerRML(markup);
						radar_choices_signature_ = markup;
					}
					for (const auto &[target, point] : points)
					{
						const auto name = id("radar-contact/" + std::to_string(target));
						auto *button = document_->GetElementById(name);
						const auto colour = radar_selection_colour(app_->session->catalog(),
						                                           widget.value("selection_source", "sensors"),
						                                           active_theme_.value("tokens", Json::object()));
						property(
						    button, "image-color",
						    "#" + std::string(TextFormat("%02x%02x%02x%02x", colour.r, colour.g, colour.b, colour.a)));
						property(button, "left", std::to_string(point.x - 12) + "px");
						property(button, "top", std::to_string(point.y - 12) + "px");
						app_->screen_controls.emplace_back(name, labels[target]);
					}
					context_->Update();
					for (const auto &[target, point] : points)
						place(*app_, region, id("radar-contact/" + std::to_string(target)));
					return;
				}
		}
}
void ConsoleSurface::State::refresh_support_panels(Application &app)
{
	if (!app.session)
	{
		support_root_.clear();
		support_models_.clear();
		return;
	}
	const auto snapshot = app.session->console_snapshot(route_);
	bool panel_layout = false;
	if (snapshot)
		for (const auto &spec : resolve_consoles(app.session->catalog(), snapshot->own.definition))
			if (spec.id == snapshot->console)
			{
				const auto &definition = app.session->catalog().consoles.at(spec.definition);
				const auto signature = std::to_string(app.session->generation()) + definition.dump();
				if (signature != support_layout_signature_)
				{
					support_layout_signature_ = signature;
					support_layout_requested_ = false;
					if (definition.contains("layout"))
					{
						const auto program = compile_ui_layout(app.session->catalog(), definition);
						std::function<void(const UiNode &)> visit = [&](const UiNode &node)
						{
							support_layout_requested_ |= node.component == "power" || node.component == "repair" ||
							                             node.component == "team-roster";
							for (const auto &child : node.children)
								visit(child);
						};
						visit(program.root);
					}
				}
				panel_layout = support_layout_requested_;
			}
	std::string root, comms, navigation;
	std::set<std::string> auxiliaries;
	if (snapshot && panel_layout)
		for (const auto &spec : resolve_consoles(app.session->catalog(), snapshot->own.definition))
		{
			if (spec.id == snapshot->console)
				continue;
			const auto found = std::find_if(snapshot->additional_consoles.begin(), snapshot->additional_consoles.end(),
			                                [&](const auto &extra)
			                                {
				                                return extra.console == spec.id;
			                                });
			if (found == snapshot->additional_consoles.end())
				continue;
			for (const auto &widget : app.session->catalog().consoles.at(spec.definition).at("widgets"))
			{
				if (widget.at("kind") == "conversations")
					comms = spec.id;
				if (widget.at("kind") == "navigation")
					navigation = spec.id;
			}
			auxiliaries.insert(spec.id);
		}
	if (snapshot && panel_layout)
		root = snapshot->console;
	if (root != support_root_)
	{
		support_page_ = "support";
		support_detail_ = false;
		support_popup_.clear();
		support_models_.clear();
	}
	support_root_ = root;
	support_comms_ = comms;
	support_navigation_ = navigation;
	if (!support_active())
		return;
	for (const auto &console : auxiliaries)
	{
		auto &source = support_models_[console];
		if (!source)
			source = std::make_unique<ConsoleModel>();
		source->refresh(app.session, route_);
		if (!source->observation() || source->observation()->console != console)
		{
			source->act("game.console/" + console);
			source->refresh(app.session, route_);
		}
	}
	std::erase_if(support_models_,
	              [&](const auto &entry)
	              {
		              return !auxiliaries.contains(entry.first);
	              });
}
void ConsoleSurface::State::prepare_engineering_component(const std::string &component, const ConsoleRegion &region,
                                                          bool pregame)
{
	auto *board = authored_component(component);
	if (settings_open_ || pregame || !model_.observation() || !authored_component_visible(component))
		return;
	const auto &view = *model_.observation();
	if (power_owner_.lock() != app_->session || power_generation_ != app_->session->generation() ||
	    power_identity_ != controller_identity())
	{
		power_pending_.clear();
		power_owner_ = app_->session;
		power_generation_ = app_->session->generation();
		power_identity_ = controller_identity();
	}
	if (component == "power")
		power_controls_.clear();
	else
		support_choices_.clear();
	std::vector<std::pair<std::string, std::string>> readouts, buttons;
	std::vector<std::pair<std::string, std::string>> selects;
	auto esc = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	auto readout = [&](const std::string &key, const std::string &text)
	{
		const auto name = id(key);
		readouts.emplace_back(name, text);
		return "<span id=\"" + esc(name) + "\"></span>";
	};
	auto button = [&](const std::string &key, const std::string &label)
	{
		buttons.emplace_back(id(key), label);
		return "<button id=\"" + esc(id(key)) + "\">" + esc(label) + "</button>";
	};
	const auto settings = power_settings(view);
	auto slider = [&](const PowerSetting *setting, const std::string &label)
	{
		if (!setting)
			return std::string{};
		power_controls_[setting->id] = *setting;
		return "<label for=\"" + esc(id(setting->id)) + "\">" + esc(label) + "</label><input id=\"" +
		       esc(id(setting->id)) + "\" type=\"range\" min=\"0\" max=\"100\" step=\"0.1\" value=\"0\"/>" +
		       readout("setting/" + setting->id, std::to_string(setting->value));
	};
	std::string markup = "<h2>" + esc(localise(component == "repair" ? "Repair" : "Power")) + "</h2>";
	if (component == "repair")
	{
		const auto combat = view.gameplay.value("combat", Json::object());
		const auto parts = combat.value("compartments", Json::array());
		const auto damaged = std::count_if(parts.begin(), parts.end(),
		                                   [](const Json &part)
		                                   {
			                                   return part.value("damaged", part.at("hp") < part.at("maximum"));
		                                   });
		markup += "<p>" +
		          readout("support/repair/summary", localise("Damaged sections") + ": " + std::to_string(damaged) +
		                                                " / " + std::to_string(parts.size())) +
		          "</p>";
		// High-level repair priorities remain directly on the board at assisted workloads.
		std::vector<Json> priorities;
		for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
			if (descriptor.value("action", "") == "intent_integer" && support_repair_control({}, descriptor))
				priorities.push_back(descriptor);
		if (!priorities.empty())
		{
			markup += "<label for=\"" + id("support-repair-priority") + "\">" + esc(localise("Repair priority")) +
			          "</label><select id=\"" + id("support-repair-priority") + "\">";
			std::string current;
			for (const auto &choice : priorities)
			{
				const auto key = std::to_string(choice.at("value").get<std::int64_t>());
				const auto label = localise(choice.value("label_key", choice.value("label", key)));
				support_choices_[key] = descriptor_action(choice.at("id"));
				bool selected = false;
				for (const auto &controller : view.gameplay.at("controllers"))
					if (controller.at("id") == choice.at("controller"))
						selected = controller.at("commands").at(choice.at("channel").get<std::string>()).at("value") ==
						           choice.at("value");
				markup +=
				    "<option value=\"" + key + "\"" + (selected ? " selected" : "") + ">" + esc(label) + "</option>";
				if (selected)
					current = label;
			}
			markup += "</select>";
			selects.emplace_back(id("support-repair-priority"), current);
		}
		markup += "<div class=\"repair-columns\"><div><h3>" + esc(localise("Sections")) + "</h3>";
		for (const auto &part : parts)
		{
			const auto key = part.at("id").get<std::string>();
			const auto condition =
			    part.at("hp").get<std::int64_t>() * 100 / std::max<std::int64_t>(1, part.at("maximum"));
			markup += "<p>" +
			          readout("support/repair/section/" + key,
			                  localise(engineering_name(key)) + " — " +
			                      localise(part.value("damaged", false) ? "Damaged" : "Operational") + " (" +
			                      std::to_string(condition) + "%)") +
			          "</p>";
		}
		markup += "</div><div><h3>" + esc(localise("Teams")) + "</h3>";
		for (const auto &team : view.gameplay.value("teams", Json::array()))
			if (team.value("repair", false))
			{
				const auto key = team.at("id").get<std::string>();
				auto location = [&](std::int64_t index)
				{
					for (const auto &part : parts)
						if (part.at("index").get<std::int64_t>() == index + 1)
							return localise(engineering_name(part.at("id")));
					return localise("Unknown");
				};
				const auto text =
				    localise(engineering_name(key)) + " — " + localise(team.value("work", "Idle")) +
				    (team.value("work_system", "").empty()
				         ? ""
				         : ": " + localise(engineering_name(team.at("work_system")))) +
				    " | " + location(team.at("location")) +
				    (team.value("work", "") == "Travelling" ? " → " + location(team.at("destination")) : "");
				const auto action = "support-details/team/" + key;
				buttons.emplace_back(id(action), text);
				markup += "<button id=\"" + esc(id(action)) + "\">" + readout("support/repair/team/" + key, text) +
				          "</button>";
			}
		markup += "</div></div>";
	}
	else
	{
		const auto power = view.gameplay.value("power", Json::object());
		std::int64_t energy = 0, capacity = 0, remaining = 0, coolant_capacity = 0;
		for (const auto &reserve : power.value("reserves", Json::array()))
		{
			energy += reserve.at("energy").get<std::int64_t>();
			capacity += reserve.at("capacity").get<std::int64_t>();
		}
		for (const auto &source : power.value("coolant", Json::array()))
		{
			remaining += source.at("remaining").get<std::int64_t>();
			coolant_capacity += source.at("capacity").get<std::int64_t>();
		}
		markup += "<div class=\"power-reserves\"><p>" +
		          readout("support/power/battery", localise("Battery reserves") + ": " + std::to_string(energy) +
		                                               " / " + std::to_string(capacity)) +
		          "</p><p>" +
		          readout("support/power/coolant", localise("Coolant reserves") + ": " + std::to_string(remaining) +
		                                               " / " + std::to_string(coolant_capacity)) +
		          "</p>";
		for (const auto &reactor : power.value("reactors", Json::array()))
		{
			const auto key = reactor.at("id").get<std::string>();
			const PowerSetting *setting = nullptr;
			for (const auto &candidate : settings)
				if (candidate.subject == key && !candidate.coolant)
					setting = &candidate;
			markup += "<div><h3>" + esc(localise(engineering_name(key))) + "</h3><p>" +
			          readout("support/power/reactor/" + key,
			                  localise("Output") + ": " + std::to_string(reactor.at("output").get<std::int64_t>()) +
			                      " / " + std::to_string(reactor.at("maximum").get<std::int64_t>())) +
			          "</p>" + slider(setting, localise("Reactor output (%)")) + "</div>";
		}
		markup += "</div>";
		const bool groups = !power.value("groups", Json::array()).empty();
		markup += "<h3>" + esc(localise(groups ? "Power groups" : "Systems")) + "</h3>";
		std::set<std::string> placed;
		auto rows = power.value(groups ? "groups" : "systems", Json::array());
		const float scale = text_scale_ / 100.f;
		const float room = board->GetClientHeight() / context_->GetDensityIndependentPixelRatio() - 170.f * scale;
		const auto limit =
		    static_cast<std::size_t>(std::max(2.f, std::floor(room / ((settings.empty() ? 55.f : 80.f) * scale))));
		std::size_t count = 0;
		for (const auto &row : rows)
		{
			if (groups && !row.value("parent", "").empty())
				continue;
			if (count++ >= limit)
				continue;
			const auto key = row.at("id").get<std::string>();
			const bool detail_only = groups && row.contains("group");
			auto *allocation = detail_only ? nullptr : power_setting_for(settings, row, false);
			auto *cooling = detail_only ? nullptr : power_setting_for(settings, row, true);
			const bool shared = allocation && !placed.insert(allocation->id).second;
			if (shared)
				allocation = nullptr;
			if (cooling && !placed.insert(cooling->id).second)
				cooling = nullptr;
			markup += "<div class=\"power-row\"><div class=\"power-name\">" + esc(localise(engineering_name(key))) +
			          "</div><div class=\"power-control\">" +
			          (shared ? "<span>" + esc(localise("Shared allocation")) + "</span>"
			                  : slider(allocation, localise("Power (%)"))) +
			          readout("support/power/allocation/" + key,
			                  localise("Power") + ": " + std::to_string(row.value("allocation", 0LL)) + " / s") +
			          "</div>";
			if (cooling)
				markup += "<div class=\"power-control\">" + slider(cooling, localise("Coolant (%)")) + "</div>";
			markup += "<div class=\"power-battery\">" +
			          readout("support/power/battery/" + key,
			                  localise("Battery") + ": " + std::to_string(row.at("energy").get<std::int64_t>()) +
			                      " / " + std::to_string(row.at("capacity").get<std::int64_t>())) +
			          "</div></div>";
		}
		markup += button("power-details", localise("Power details"));
	}
	if (markup != engineering_signatures_[component])
	{
		syncing_values_ = true;
		board->SetInnerRML(markup);
		syncing_values_ = false;
		elements_.clear();
		properties_.clear();
		engineering_signatures_[component] = markup;
	}
	for (const auto &[name, text] : readouts)
	{
		if (auto *element = document_->GetElementById(name))
			leaf_markup(element, esc(text));
		app_->screen_readouts.emplace_back(name, text);
	}
	for (const auto &[name, label] : buttons)
		app_->screen_controls.emplace_back(name, label);
	for (const auto &[name, value] : selects)
		app_->screen_inputs.push_back({name, localise("Repair priority"), value, false});
	syncing_values_ = true;
	for (const auto &[key, setting] : component == "power" ? power_controls_ : std::map<std::string, PowerSetting>{})
	{
		auto value = setting.value;
		if (auto pending = power_pending_.find(key); pending != power_pending_.end())
		{
			if (pending->second.value != setting.value && app_->session->ticks() < pending->second.until)
				value = pending->second.value;
			else
				power_pending_.erase(pending);
		}
		const auto percent =
		    std::clamp(100.0 * (value - setting.minimum) / std::max<std::int64_t>(1, setting.maximum - setting.minimum),
		               0.0, 100.0);
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id(key))))
			if (field->GetValue() != std::to_string(percent))
				field->SetValue(std::to_string(percent));
		app_->screen_inputs.push_back({id(key),
		                               localise(setting.coolant ? "Coolant (%)" : "Power (%)") + " — " +
		                                   localise(engineering_name(setting.subject)),
		                               std::to_string(percent), true});
	}
	syncing_values_ = false;
	context_->Update();
	for (const auto &[name, text] : readouts)
		place(*app_, region, name);
	for (const auto &[name, label] : buttons)
		place(*app_, region, name);
	for (const auto &[name, value] : selects)
		place(*app_, region, name);
	for (const auto &[key, setting] : component == "power" ? power_controls_ : std::map<std::string, PowerSetting>{})
		place(*app_, region, id(key));
}
std::vector<std::pair<ConsoleModel *, std::string>> ConsoleSurface::State::task_sources()
{
	std::vector<std::pair<ConsoleModel *, std::string>> sources{{&model_, ""}};
	for (auto &[console, source] : support_models_)
		sources.emplace_back(source.get(), "support-console/" + console + "/");
	if (!tactical_.console().empty())
		sources.emplace_back(&tactical_.model(), "inline/");
	return sources;
}
void ConsoleSurface::State::prepare_task_panels(const ConsoleRegion &region)
{
	task_actions_.clear();
	condition_choices_.clear();
	shield_sectors_.clear();
	for (const char *name : {"weapons", "objectives", "viewscreen-controls", "alert-controls", "shield-display",
	                         "ftl-controls", "compartment-health", "team-roster"})
	{
		if (!authored_component_visible(name))
			continue;
		auto *panel = authored_component(name);
		std::string markup;
		std::vector<std::pair<std::string, std::string>> buttons, readouts;
		std::map<std::string, bool> toggles;
		std::vector<std::tuple<std::string, std::string, bool>> weapon_choices;
		auto esc = [](const std::string &value)
		{
			return Rml::StringUtilities::EncodeRml(value);
		};
		auto line = [&](const std::string &key, const std::string &value)
		{
			const auto field = id(std::string(name) + "/" + key);
			readouts.emplace_back(field, value);
			return "<p id=\"" + field + "\">" + (std::string(name) == "shield-display" ? "" : esc(value)) + "</p>";
		};
		auto button = [&](const std::string &action, const std::string &label, const std::string &state = std::string{},
		                  bool enabled = true)
		{
			const auto key = "task/" + std::string(name) + "/" + std::to_string(buttons.size());
			if (enabled)
				task_actions_[key] = action;
			buttons.emplace_back(id(key), label);
			if (!state.empty())
				toggles[id(key)] = state == "on";
			return "<button id=\"" + esc(id(key)) + "\" class=\"task-button " + state + (enabled ? "" : " disabled") +
			       "\"" +
			       (state.empty() ? "" : " aria-pressed=\"" + std::string(state == "on" ? "true" : "false") + "\"") +
			       (enabled ? ">" : " disabled aria-disabled=\"true\" style=\"tab-index:none\">") +
			       (state.empty() ? "" : "<span class=\"task-state-icon\"></span>") + esc(label) + "</button>";
		};
		const std::string component = name;
		const auto logical_height = panel->GetClientHeight() / context_->GetDensityIndependentPixelRatio();
		const auto rows = static_cast<std::size_t>(
		    std::max(1.f, std::floor((logical_height - 110.f) / std::max(50.f, 70.f * text_scale_ / 100.f))));

		if (component == "weapons")
		{
			markup =
			    "<h2>" + localise(component == "weapons" ? "Weapons" : "Shields") + "</h2><div class=\"task-toggles\">";
			std::set<std::string> shown;
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &system : source->observation()->systems)
						if (system.kind ==
						        (component == "weapons" ? OperatingSystem::Weapon : OperatingSystem::Shield) &&
						    !shown.contains(system.id))
						{
							std::string action;
							for (const auto &descriptor :
							     source->observation()->gameplay.value("controls", Json::array()))
								if (descriptor.value("action", "") == "system_enabled" &&
								    descriptor.value("value", -1) == (system.enabled ? 0 : 1))
								{
									const auto consumers = command_consumers(*source->observation(), descriptor);
									if (std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
									{
										action = prefix + descriptor_action(descriptor.at("id"));
										break;
									}
								}
							if (component == "weapons" && action.empty())
								continue;
							shown.insert(system.id);
							const auto label = localise(engineering_name(system.id));
							weapon_choices.emplace_back(action, label, system.enabled);
						}

			const auto weapon_row_height = std::max(50.f, 70.f * text_scale_ / 100.f);
			auto weapon_rows =
			    static_cast<std::size_t>(std::max(1.f, std::floor((logical_height - 50.f) / weapon_row_height)));
			if (weapon_choices.size() > weapon_rows * 2)
				weapon_rows = rows; // Reserve the contextual-list button only when it is needed.
			for (std::size_t i = 0; i < std::min(weapon_choices.size(), weapon_rows * 2); ++i)
			{
				const auto &[action, label, enabled] = weapon_choices[i];
				markup += button(action, label, enabled ? "on" : "off");
			}
			markup += "</div>";
			if (weapon_choices.size() > weapon_rows * 2)
				markup += button("task-detail/weapons", localise("All weapons"));
		}
		else if (component == "shield-display")
		{
			markup = "<h2>" + localise("Shields") + "</h2><div class=\"shield-diagram\" id=\"" + id("shield-diagram") +
			         "\" style=\"height:" + std::string(text_scale_ > 100 ? "55%" : "65%") + "\"></div>";
			std::set<std::string> seen, controlled;
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
				{
					const auto &view = *source->observation();
					for (auto shield : view.gameplay.value("combat", Json::object()).value("shields", Json::array()))
						if (seen.insert(shield.at("component_id").get<std::string>()).second)
						{
							shield["enabled"] = true;
							for (const auto &system : view.systems)
								if (system.id == shield.value("id", ""))
									shield["enabled"] = system.enabled;
							const auto warmup_ticks = std::max(1LL, shield.value("warmup_ticks", 1LL));
							const auto warmup = std::clamp(shield.value("warmup", 0LL), 0LL, warmup_ticks);
							const auto capacity = shield.value("capacity", 0LL);
							const auto strength = shield.value("enabled", true)
							                          ? shield.value("strength", 0LL) * warmup / warmup_ticks
							                          : 0LL;
							shield["active_strength"] = strength;
							shield_sectors_.push_back(shield);
							markup += line("sector/" + shield.at("component_id").get<std::string>(),
							               localise(engineering_name(shield.value("id", ""))) + ": " +
							                   std::to_string(capacity ? strength * 100 / capacity : 0) + "%" +
							                   (!shield.at("enabled").get<bool>() ? " — " + localise("Off")
							                    : warmup == 0                     ? " — " + localise("Inactive")
							                                                      : ""));
						}
					for (const auto &system : view.systems)
						if (system.kind == OperatingSystem::Shield && !controlled.contains(system.id))
							for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
								if (descriptor.value("action", "") == "system_enabled" &&
								    descriptor.value("value", -1) == (system.enabled ? 0 : 1))
								{
									const auto consumers = command_consumers(view, descriptor);
									if (std::find(consumers.begin(), consumers.end(), system.id) == consumers.end())
										continue;
									controlled.insert(system.id);
									markup +=
									    button(prefix + descriptor_action(descriptor.at("id")),
									           localise(engineering_name(system.id)), system.enabled ? "on" : "off");
									break;
								}
				}
		}
		else if (component == "alert-controls")
		{
			markup = "<h2>" + localise("Condition") + "</h2>";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						if (descriptor.value("action", "") == "alert.set")
						{
							const auto key = descriptor.at("id").get<std::string>();
							const int number = key.size() > 11 && key.substr(key.size() - 11, 10) == "condition-" &&
							                           key.back() >= '1' && key.back() <= '5'
							                       ? key.back() - '0'
							                       : descriptor.value("value", 0) + 1;
							condition_choices_[number] = prefix + descriptor_action(key);
							if (descriptor.value("selected", false))
							{
								condition_selected_ = number;
							}
						}
			if (!condition_choices_.empty())
			{
				markup += "<input class=\"condition-slider\" id=\"" + id("support-condition") +
				          "\" type=\"range\" min=\"" + std::to_string(condition_choices_.begin()->first) + "\" max=\"" +
				          std::to_string(condition_choices_.rbegin()->first) +
				          "\" step=\"1\"/><div class=\"condition-ticks\">";
				for (const auto &[number, choice] : condition_choices_)
					markup += "<span>" + std::to_string(number) + "</span>";
				markup += "</div>";
			}
		}
		else if (component == "objectives")
		{
			markup = "<h2>" + localise("Objectives") + "</h2>";
			for (const auto &objective : model_.objectives())
			{
				const auto status = objective.value("status", Json{});
				static constexpr const char *statuses[]{"Pending", "Active", "Completed", "Failed", "Cancelled"};
				const auto label = status.is_number_integer()
				                       ? std::string(statuses[std::clamp(status.get<int>(), 0, 4)])
				                   : status.is_string() ? status.get<std::string>()
				                                        : std::string{};
				markup += line(std::to_string(objective.value("id", 0ULL)),
				               localise(objective.value("title", "")) + " — " + localise(label));
			}
		}
		else if (component == "team-roster" || component == "compartment-health")
		{
			markup = "<h2>" + localise(component == "team-roster" ? "Teams" : "Compartments") + "</h2>";
			if (model_.observation())
			{
				const auto &view = *model_.observation();
				if (component == "compartment-health")
				{
					std::size_t count = 0;
					auto parts = view.gameplay.value("combat", Json::object()).value("compartments", Json::array());
					std::stable_sort(parts.begin(), parts.end(),
					                 [](const auto &a, const auto &b)
					                 {
						                 return a.value("damaged", false) && !b.value("damaged", false);
					                 });
					const auto limit = parts.size() > 6 ? 4u : 6u;
					for (const auto &part : parts)
						if (count++ < limit)
							markup += "<div class=\"health-row\"><span>" +
							          esc(localise(engineering_name(part.at("id")))) + "</span><progress value=\"" +
							          part.at("hp").dump() + "\" max=\"" + part.at("maximum").dump() + "\"/></div>";
					if (count > limit)
						markup += button("task-detail/compartments", localise("All compartments"));
				}
				else
				{
					std::size_t count = 0;
					for (const auto &team : view.gameplay.value("teams", Json::array()))
						if (count++ < rows)
							markup += button(team.value("repair", false)
							                     ? "support-details/team/" + team.at("id").get<std::string>()
							                     : "support-details/medical",
							                 localise(engineering_name(team.at("id"))) + " — " +
							                     localise(team.value("work", "Ready")));
					if (count > rows)
						markup += button("task-detail/teams", localise("All teams"));
				}
			}
		}
		else if (component == "ftl-controls")
		{
			markup = "<h2>" + localise("FTL") + "</h2><div class=\"task-choices\">";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &ftl : source->observation()->gameplay.value("ftl", Json::array()))
					{
						const auto system = ftl.value("system", "");
						const bool preparing = ftl.value("enabled", false), ready = ftl.value("ready", false);
						std::string toggle, depart;
						for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						{
							if (descriptor.value("action", "") == "ftl.depart")
								depart = prefix + descriptor_action(descriptor.at("id"));
							if (descriptor.value("action", "") != "system_enabled" ||
							    descriptor.value("value", -1) != (preparing ? 0 : 1))
								continue;
							const auto consumers = command_consumers(*source->observation(), descriptor);
							if (std::find(consumers.begin(), consumers.end(), system) != consumers.end())
								toggle = prefix + descriptor_action(descriptor.at("id"));
						}
						if (!toggle.empty())
							markup += button(toggle, localise(preparing ? "Cancel FTL" : "Prepare FTL"));
						if (!depart.empty())
							markup += button(depart, localise("Depart"), "", ready);
						markup += line("status", localise(ready ? "Ready" : ftl.value("blocker", "Preparing")));
					}
			markup += "</div>";
		}
		else
		{
			markup = "<h2>" + localise("Viewscreen") + "</h2><div class=\"task-choices\">";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						if (descriptor.value("action", "") == "camera_mode" ||
						    descriptor.value("action", "") == "camera_direction")
							markup +=
							    button(prefix + descriptor_action(descriptor.at("id")),
							           localise(descriptor.value("label_key", descriptor.value("label", "Camera"))));
			markup += "</div>";
		}
		if (component == "objectives")
			markup += button("systems", localise("System status"));
		if (station_component_markup_[component] != markup)
		{
			const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			panel->SetInnerRML(markup);
			station_component_markup_[component] = markup;
			elements_.clear();
			if (auto *previous = document_->GetElementById(focus))
				previous->Focus();
		}
		if (component == "alert-controls" && !condition_choices_.empty())
		{
			auto selected = condition_selected_;
			if (condition_pending_)
			{
				if (condition_pending_->first != selected && app_->session->ticks() < condition_pending_->second)
					selected = condition_pending_->first;
				else
					condition_pending_.reset();
			}
			syncing_values_ = true;
			if (auto *field =
			        dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("support-condition"))))
				if (field->GetValue() != std::to_string(selected))
					field->SetValue(std::to_string(selected));
			syncing_values_ = false;
			app_->screen_inputs.push_back(
			    {id("support-condition"), localise("Condition"), std::to_string(selected), true});
			place(*app_, region, id("support-condition"));
		}
		context_->Update();
		for (const auto &[key, label] : buttons)
		{
			app_->screen_controls.emplace_back(key, label);
			place(*app_, region, key);
		}
		for (const auto &[key, value] : readouts)
		{
			if (component == "shield-display")
				if (auto *field = document_->GetElementById(key))
				{
					if (field->GetInnerRML() != esc(value))
						field->SetInnerRML(esc(value));
					if (auto *canvas = document_->GetElementById(id("shield-diagram")))
					{
						const auto offset = canvas->GetAbsoluteOffset(Rml::BoxArea::Border) -
						                    panel->GetAbsoluteOffset(Rml::BoxArea::Border);
						const float radius = std::min(canvas->GetClientWidth(), canvas->GetClientHeight()) * .43f;
						const float centre_x = offset.x + canvas->GetClientWidth() / 2,
						            centre_y = offset.y + canvas->GetClientHeight() / 2;
						bool right = true;
						for (const auto &sector : shield_sectors_)
							if (key == id("shield-display/sector/" + sector.at("component_id").get<std::string>()))
								right = std::cos(float(sector.value("bearing_mdeg", 0LL) -
								                       model_.observation()->own.heading) /
								                 1000 * DEG2RAD) >= 0;
						const float width = std::min(panel->GetClientWidth() * .25f,
						                             210.f * context_->GetDensityIndependentPixelRatio());
						property(field, "position", "absolute");
						property(field, "width", std::to_string(width) + "px");
						property(field, "left",
						         std::to_string(right ? centre_x + radius + 12 : centre_x - radius - width - 12) +
						             "px");
						property(field, "top", std::to_string(centre_y - field->GetOffsetHeight() / 2) + "px");
						property(field, "margin", "0px");
						property(field, "text-align", right ? "left" : "right");
					}
				}
			app_->screen_readouts.emplace_back(key, value);
			place(*app_, region, key);
		}
		for (const auto &[key, state] : toggles)
			app_->screen_toggles[key] = state;
	}
}
} // namespace phoenix
