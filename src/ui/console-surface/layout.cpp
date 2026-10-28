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
import phoenix.ui.choice_specs;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
import phoenix.navigation;
#include "surface_state.hpp"
namespace phoenix
{
void ConsoleSurface::State::sync_console_browsing()
{
	const auto current = model_.observation()
	                         ? std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console
	                         : std::string{};
	if (current == browsing_console_)
		return;
	if (!browsing_console_.empty())
	{
		auto &saved = surface_browsing_[browsing_console_];
		saved.navigation = navigation_.browsing();
		saved.pages = authored_pages_;
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
			saved.draft = field->GetValue();
		for (const char *panel :
		     {"comms-history", "comms-list", "gameplay", "authored-body", "gameplay-readouts", "gameplay-messages"})
			if (auto *element = document_->GetElementById(panel))
				saved.scroll[panel] = element->GetScrollTop();
	}
	cancel_pointer();
	model_.release_controls();
	const auto saved = surface_browsing_.contains(current) ? surface_browsing_.at(current) : SurfaceBrowsing{};
	navigation_.restore_browsing(saved.navigation);
	authored_pages_ = saved.pages;
	restore_draft_ = saved.draft;
	restore_browsing_ = true;
	browsing_console_ = current;
}
void ConsoleSurface::State::restore_console_browsing()
{
	if (!restore_browsing_)
		return;
	if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
		field->SetValue(restore_draft_);
	if (surface_browsing_.contains(browsing_console_))
		for (const auto &[panel, scroll] : surface_browsing_.at(browsing_console_).scroll)
			if (auto *element = document_->GetElementById(panel))
				element->SetScrollTop(scroll);
	restore_browsing_ = false;
}
Rml::Element *ConsoleSurface::State::authored_component(const std::string &target)
{
	const std::map<std::string, std::string> panels = {{"settings", id("settings")},
	                                                   {"console-name", id("station-name")},
	                                                   {"tabs", "console-tabs"},
	                                                   {"weapons", "weapons-panel"},
	                                                   {"target-info", "gameplay-readouts"},
	                                                   {"power", "power-panel"},
	                                                   {"repair", "repair-panel"},
	                                                   {"operations", "operations-panel"},
	                                                   {"docking", "docking-panel"},
	                                                   {"craft", "craft-panel"},
	                                                   {"personnel", "personnel-panel"},
	                                                   {"away", "away-panel"},
	                                                   {"gameplay", "gameplay"},
	                                                   {"controls", "gameplay-controls"},
	                                                   {"readouts", "gameplay-readouts"},
	                                                   {"messages", "gameplay-messages"},
	                                                   {"engineering", "engineering-cards"},
	                                                   {"sensors", "sensor-panel"},
	                                                   {"conversations", "comms-layer"},
	                                                   {"conversation-list", "comms-list"},
	                                                   {"conversation-history", "comms-history"},
	                                                   {"conversation-replies", "comms-choices"},
	                                                   {"conversation-compose", "comms-compose"},
	                                                   {"conversation-header", "comms-header"},
	                                                   {"conversation-incoming", "comms-incoming"},
	                                                   {"navigation", "navigation-layer"},
	                                                   {"manual-flight", "manual-flight"}};
	if (!panels.contains(target) && model_.layout())
		for (const auto &widget : model_.layout()->at("widgets"))
			if (widget.at("id") == target)
			{
				if (widget.at("kind") == "conversations")
					return document_->GetElementById("comms-layer");
				if (widget.at("kind") == "navigation")
					return document_->GetElementById("navigation-layer");
			}
	for (const auto &component : console_ui_components())
		if (component.id == target && !panels.contains(target))
			return document_->GetElementById(target + "-panel");
	return document_->GetElementById(panels.contains(target) ? panels.at(target) : "widget/" + target);
}
bool ConsoleSurface::State::authored_component_visible(const std::string &target)
{
	auto *element = authored_component(target);
	if (!element)
		return false;
	for (const auto &placement : authored_resolved_.placements)
		if (placement.visible)
		{
			auto *panel = authored_component(placement.component);
			for (auto *ancestor = element; ancestor; ancestor = ancestor->GetParentNode())
				if (ancestor == panel)
					return true;
		}
	return false;
}
bool ConsoleSurface::State::authored_action(const std::string &action)
{
	if (action.starts_with("station-page/") || action.starts_with("console-page/"))
	{
		const auto requested = action.starts_with("station-page/") ? action.substr(13) : std::string{};
		for (const auto &page : station_pages_)
			if (action == page.action ||
			    (!requested.empty() && page.component == (requested == "logistics" ? "operations" : requested)))
			{
				if (!model_.act("game.console/" + page.console))
					return false;
				model_.refresh(app_->session, route_);
				sync_console_browsing();
				if (!page.group.empty())
					authored_pages_[page.group] = page.page;
				settings_open_ = false;
				support_detail_ = false;
				page_focus_pending_ = true;
				document_->SetClass("local-settings-open", false);
				document_->SetClass("station-page-open", false);
				return true;
			}
		return false;
	}
	if (settings_open_)
	{
		settings_open_ = false;
		document_->SetClass("local-settings-open", false);
		document_->SetClass("station-page-open", false);
	}
	if (action.starts_with("layout-page/"))
	{
		const auto page = action.substr(12);
		if (!authored_page_groups_.contains(page))
			return false;
		cancel_pointer();
		model_.release_controls();
		const auto group = authored_page_groups_.at(page);
		authored_pages_[group] = page;
		displayed_values_.clear();
		conversations_.invalidate_text();
		page_focus_pending_ = true;
		return true;
	}
	if (action.starts_with("game.console/"))
	{
		if (support_active())
		{
			support_detail_ = false;
			if (action.substr(13) == support_root_)
			{
				support_page_ = "support";
				model_.act("game.teams-close");
			}
		}
		cancel_pointer();
		model_.release_controls();

		const bool accepted = model_.act(action);
		if (accepted && support_active() && action.substr(13) == support_root_)
		{
			model_.refresh(app_->session, route_);
			model_.act("game.teams-close");
		}
		return accepted;
	}
	return false;
}
std::string ConsoleSurface::State::layout_node_id(const UiNode &node) const
{
	return "layout-node/" + node.id;
}
void ConsoleSurface::State::ensure_station_components()
{
	auto *bank = document_->GetElementById("component-bank");
	if (!bank)
	{
		auto created = document_->CreateElement("div");
		created->SetId("component-bank");
		created->SetProperty("display", "none");
		bank = document_->AppendChild(std::move(created));
	}
	for (const auto &spec : console_ui_components())
	{
		const auto &component = spec.id;
		if (!authored_component(component))
		{
			auto created = document_->CreateElement("div");
			created->SetId(component + "-panel");
			created->SetClass("station-panel", true);
			created->SetClass("engineering-board", component == "power" || component == "repair");
			bank->AppendChild(std::move(created));
		}
	}
}
bool ConsoleSurface::State::station_component_active(const std::string &component)
{
	return !settings_open_ && model_.ui_availability().features.contains(component) &&
	       authored_component_visible(component);
}
void ConsoleSurface::State::prepare_station_components(const ConsoleRegion &region, bool pregame)
{
	if (pregame || settings_open_)
		return;
	prepare_engineering_component("power", region, pregame);
	prepare_engineering_component("repair", region, pregame);
	prepare_task_panels(region);
	for (const char *component : {"operations", "docking", "craft", "personnel", "away"})
	{
		if (!station_component_active(component))
			continue;
		auto saved = std::move(local_settings_controls_);
		local_settings_controls_.clear();
		station_component_scope_ = component;
		std::string markup;
		std::vector<std::pair<std::string, std::string>> readouts;
		const auto panel_view = operations_.render(component, operations_context());
		markup = panel_view.markup;
		readouts = panel_view.readouts;
		station_component_scope_.clear();
		// Telemetry is updated in place so a live panel retains focus and scroll position.
		for (const auto &[key, text] : readouts)
		{
			const auto start = markup.find("id=\"" + key + "\"");
			if (start == std::string::npos)
				continue;
			const auto content = markup.find('>', start), end = markup.find("</", content);
			if (end != std::string::npos)
				markup.erase(content + 1, end - content - 1);
		}
		auto *panel = authored_component(component);
		if (station_component_markup_[component] != markup)
		{
			panel->SetInnerRML(markup);
			station_component_markup_[component] = markup;
			dom_.forget_elements();
		}
		for (const auto &[key, text] : readouts)
		{
			if (auto *element = document_->GetElementById(key))
				dom_.leaf_markup(element, Rml::StringUtilities::EncodeRml(text));
			app_->screen_readouts.emplace_back(key, text);
		}
		for (const auto &control : local_settings_controls_)
			app_->screen_controls.push_back(control);
		context_->Update();
		for (const auto &[key, text] : readouts)
			place(*app_, region, key);
		for (const auto &[key, label] : local_settings_controls_)
			place(*app_, region, key);
		local_settings_controls_ = std::move(saved);
	}
}
void ConsoleSurface::State::prepare_local_settings(Application &app, const ConsoleRegion &region)
{
	local_settings_controls_.clear();
	const auto content_width = std::max(180.f, region.width * .84f - 52.f);
	std::string markup =
	    "<div class=\"local-settings-card\"><div style=\"display:block;width:" + std::to_string(content_width) +
	    "dp;\"><h1>" + localise("Console settings") + "</h1><div>";
	for (const auto &[tab, label] : std::vector<std::pair<std::string, std::string>>{{"display", "Display"},
	                                                                                 {"language", "Language"},
	                                                                                 {"audio", "Audio"},
#ifndef __EMSCRIPTEN__
	                                                                                 {"controllers", "Controllers"},
#endif
	                                                                                 {"session", "Session"}})
		markup += settings_button("local-settings.tab/" + tab, label);
	markup += "</div>";
	if (!app.preference_error.empty())
		markup += "<p>" + Rml::StringUtilities::EncodeRml(app.preference_error) + "</p>";
	if (local_settings_tab_ == "display")
	{
		markup += "<p>" + localise("Text size") + ": " + std::to_string(text_scale_) + "%</p>";
		markup += settings_button("local-settings.smaller", "Smaller text");
		markup += settings_button("local-settings.larger", "Larger text");
	}
	else if (local_settings_tab_ == "language")
		markup += settings_button("local-settings.language", "Language: " + local_language_);
	else if (local_settings_tab_ == "audio")
	{
		for (const auto &[action, label] : screen_device_controls(app, route_, "audio"))
			markup += settings_button(action, label);
		for (const auto &[channel, label, value] :
		     std::vector<std::tuple<std::string, std::string, int>>{{"master", "Master volume", app.master_volume},
		                                                            {"music", "Music volume", app.music_volume},
		                                                            {"siren", "Siren volume", app.siren_volume},
		                                                            {"sfx", "SFX volume", app.sfx_volume}})
		{
			markup += "<p>" + localise(label) + ": " + std::to_string(value) + "%</p>";
			markup += settings_button("local-settings.audio/" + channel + "/down", label + " -");
			markup += settings_button("local-settings.audio/" + channel + "/up", label + " +");
		}
	}
	else if (local_settings_tab_ == "controllers")
	{
		for (const auto &[action, label] : screen_device_controls(app, route_, "controllers"))
			markup += settings_button(action, label);
	}
	else if (app.session->lobby().active && !app.session->lobby().launched)
	{
		if (!model_.layout() || !model_.layout()->value("command", false))
			for (const auto &seat : app.session->lobby().consoles)
				if (seat.local_screen == route_)
					for (const auto &choice : seat.profiles)
						markup += star_choice("complexity/" + choice, choice, seat.profile_stars, seat.profile, true);
	}
	else if (model_.observation())
	{
		const auto levels = model_.observation()->gameplay.value("profile_stars", std::map<std::string, int>{});
		const auto selected = model_.observation()->gameplay.value("profile", std::string{});
		if (levels.empty() && model_.observation()->gameplay.contains("profile"))
			markup += "<p>" + localise("Complexity") + ": " +
			          Rml::StringUtilities::EncodeRml(model_.observation()->gameplay.at("profile").get<std::string>()) +
			          "</p>";
		for (const auto &control : model_.observation()->gameplay.value("controls", Json::array()))
			if (control.value("action", "") == "away")
				markup += settings_button(descriptor_action(control.at("id")),
				                          control.value("value", 0) ? "Away - hand control to AI" : "Resume control");
			else if (control.value("action", "") == "preset")
				markup += star_choice(descriptor_action(control.at("id")),
				                      control.at("id").get<std::string>().substr(8), levels, selected, true);
	}
	markup += settings_button("local-settings.close", "Return to station") + "</div></div>";
	if (markup != local_settings_markup_)
	{
		const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		auto *settings = document_->GetElementById("local-settings");
		const auto scroll = settings->GetFirstChild() ? settings->GetFirstChild()->GetScrollTop() : 0.f;
		dom_.forget_elements();
		settings->SetInnerRML(markup);
		local_settings_markup_ = markup;
		if (auto *previous = document_->GetElementById(focus))
			previous->Focus();
		context_->Update();
		if (auto *card = settings->GetFirstChild())
			card->SetScrollTop(scroll);
	}
	context_->Update();
	if (std::none_of(local_settings_controls_.begin(), local_settings_controls_.end(),
	                 [&](const auto &control)
	                 {
		                 return context_->GetFocusElement() && context_->GetFocusElement()->GetId() == control.first;
	                 }))
		if (auto *first = document_->GetElementById(local_settings_controls_.front().first))
			first->Focus();
	for (const auto &[name, label] : local_settings_controls_)
	{
		app.screen_controls.emplace_back(name, localise(label));
		place(app, region, name);
	}
}
const UiResolved &ConsoleSurface::State::authored_geometry() const
{
	return preview_header_ ? authored_header_resolved_ : authored_resolved_;
}
void ConsoleSurface::State::prepare_current(Application &app, const ConsoleRegion &physical_region,
                                            const std::set<std::string> &pads)
{
	// Embedded Command uses the scale of its parent Viewscreen.
	const float display_scale = std::max(
	    1.f, std::min(physical_region.width * (route_ == 31 ? 4.f : 1.f) / 1280.f, physical_region.height / 720.f));
	context_->SetDensityIndependentPixelRatio(display_scale);
	const ConsoleRegion region{physical_region.console, physical_region.x, physical_region.y,
	                           physical_region.width / display_scale, physical_region.height / display_scale};
	app_ = &app;
	const auto preparation_started = GetTime();
	dom_.prune_markup();
	const auto previous_announcement = model_.announcement();
	model_.refresh(app.session, route_);
	throttles_.reconcile(model_, throttle_contacts_, GetTime());
	tactical_.refresh(model_, app.session, route_);
	refresh_support_panels(app);
	document_->SetClass("integrated-helm", !tactical_.console().empty());
	sync_console_browsing();
	app.frame_metrics["console/" + std::to_string(route_) + "/projection"] = GetTime() - preparation_started;
	const auto layout_started = GetTime();
	if (app.screen == Screen::settings)
	{
		cancel_pointer();
		return;
	}
	if (model_.announcement() != previous_announcement && !model_.announcement().empty())
		app.accessible_status = model_.announcement();
	if (owner_.lock() != app.session || (app.session && (generation_ != app.session->generation() ||
	                                                     content_packs_ != app.session->catalog().ordered_packs)))
	{
		surface_browsing_.clear();
		browsing_console_.clear();
		owner_ = app.session;
		content_packs_ =
		    app.session ? app.session->catalog().ordered_packs : std::vector<std::pair<std::string, std::string>>{};
		resolved_consoles_.clear();
		prepared_layout_ = nullptr;
		prepared_layout_revision_ = ~std::uint64_t{0};
		command_choices_.clear();
		command_language_.clear();
		command_markup_.clear();
		manual_profile_.clear();
		manual_console_.clear();
		local_language_ = app.language;
		generation_ = app.session ? app.session->generation() : 0;
		settings_open_ = false;
		document_->SetClass("local-settings-open", false);
		radar_textures_.clear();
		shield_ship_sprite_.reset();
		shield_ship_definition_.clear();
		radar_sweeps_.clear();
		signature_.clear();
		cancel_pointer();
		document_->Focus();
	}
	if (model_.observation())
	{
		const auto scope = std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		if (scope != manual_console_)
		{
			manual_console_ = scope;
			manual_profile_.clear();
		}
		const auto profile = model_.observation()->gameplay.value("profile", "");
		if (!profile.empty() && profile != "automatic")
			manual_profile_ = "profile." + profile;
	}
	context_->SetDimensions({static_cast<int>(physical_region.width), static_cast<int>(physical_region.height)});
	select_language(local_language_, app.pseudo_locale);
	struct RestoreLanguage
	{
		Application &app;
		~RestoreLanguage()
		{
			select_language(app.language, app.pseudo_locale);
		}
	} restore_language{app};
	dom_.property(
	    document_, "font-size",
	    std::to_string(text_scale_ *
	                   (active_theme_.contains("tokens") ? active_theme_.at("tokens").value("font_size", 20.f) : 20.f) /
	                   100.f) +
	        "dp");
	if (settings_open_)
	{
		prepare_local_settings(app, region);
		return;
	}
	const auto dispatch_map = model_.team_map();
	const bool team_dispatch = !dispatch_map.empty() && dispatch_map[0].value("dispatch", false);
	document_->SetClass("team-dispatch", team_dispatch);
	const bool pregame = app.session && app.session->lobby().active && !app.session->lobby().launched;
	const Json *layout = model_.layout();
	const bool command_console = layout && layout->value("command", false);
	document_->SetClass("command-console", command_console);
	document_->SetClass("narrow-command", command_console && region.width < 310.f);
	document_->SetClass("embedded-console", route_ == 31);
	const bool helm = layout && std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
	                                        [](const auto &widget)
	                                        {
		                                        return widget.at("kind") == "throttle";
	                                        });

	manual_toggle_ = !pregame && helm && layout && manual_preset(true) && manual_preset(false) &&
	                 std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
	                             [](const auto &w)
	                             {
		                             return w.at("kind") == "radar";
	                             });
	const bool has_camera =
	    layout && std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
	                          [](const auto &widget)
	                          {
		                          return widget.at("kind") == "camera_mode" || widget.at("kind") == "camera_direction";
	                          });
	document_->SetClass("helm", helm && !pregame);
	const auto cards_started = GetTime();
	auto cards = !pregame && (!helm || (layout && layout->contains("layout"))) && model_.observation()
	                 ? engineering_cards(*model_.observation())
	                 : std::vector<EngineeringCard>{};

	document_->SetClass("support-panels", support_primary());
	app.frame_metrics["console/" + std::to_string(route_) + "/card-model"] = GetTime() - cards_started;
	const bool engineering = !cards.empty();
	document_->SetClass("engineering", engineering);
	std::string title = "Console", description, briefing;
	std::string complexity;
	std::vector<std::string> complexity_choices;
	std::map<std::string, int> complexity_levels;
	bool ready = false;
	if (pregame)
	{
		const auto lobby = app.session->lobby();
		for (const auto &seat : lobby.consoles)
			if (seat.local_screen == route_)
			{
				title = seat.name;
				ready = seat.ready;
				complexity = seat.profile;
				complexity_choices = seat.profiles;
				complexity_levels = seat.profile_stars;
				for (const auto &member : lobby.members)
					if (member.id == seat.member && !profile_open_)
					{
						profile_name_ = member.name;
						profile_avatar_ = member.avatar;
						if (profile_avatar_.empty())
						{
							profile_avatar_ = read_file(
							    asset_path("../menu/avatar-" + std::to_string(GetRandomValue(1, 12)) + ".png"), 12288);
							app.session->set_local_player_profile(route_, profile_name_, profile_avatar_);
						}
					}
				for (const auto &slot : lobby.slots)
					if (slot.id == seat.slot)
					{
						briefing = slot.briefing;
						for (const auto &spec : console_specs(app, slot.ship))
							if (spec.id == seat.id)
								layout = &app.session->catalog().consoles.at(spec.definition);
					}
			}
	}
	if (layout)
	{
		title = layout->value("name", layout->value("id", title));
		description = layout->value(
		    "description", std::string("Operate the fitted systems and monitor this Console's permitted contacts."));
	}
	// Authored widgets still determine controls, placement and authority.
	if (prepared_layout_ != layout || prepared_layout_revision_ != model_.layout_revision())
	{
		prepared_layout_ = layout;
		prepared_layout_revision_ = model_.layout_revision();
		++layout_identity_;
		command_language_.clear();
	}
	std::string signature = std::to_string(layout_identity_) + (app.pseudo_locale ? "pseudo" : "normal") +
	                        (pregame ? ready ? "ready" : "waiting" : "running") + (profile_open_ ? "profile" : "") +
	                        (profile_open_ ? "" : profile_name_) + "/complexity/" + complexity +
	                        (manual_toggle_ ? "/manual" : "") +
	                        (operations_.away_attention(model_) ? "/away-attention" : "") + briefing;
	for (const auto &choice : complexity_choices)
		signature += "/" + choice;
	if (model_.observation())
		signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
	for (const auto &pad : pads)
		signature += "/pad/" + pad;
	if (signature != signature_)
	{
		restore_authored_components();
		signature_ = std::move(signature);
		placement_signature_.clear();
		dom_.forget_properties();
		dom_.forget_elements();
		gameplay_signature_.clear();
		gameplay_readout_signature_.clear();
		engineering_signature_.clear();
		displayed_values_.clear();
		conversations_.invalidate_text();
		controls_.clear();
		heading_contacts_.clear();
		std::string station_title = title;
		if (const auto snapshot = app.session->console_snapshot(route_))
			for (const auto &spec : console_specs(app, snapshot->own.definition))
				if (spec.id == snapshot->console)
					station_title = app.session->catalog().consoles.at(spec.definition).value("name", title);
		std::string toolbar = "<h1 id=\"" + id("station-name") + "\">" +
		                      Rml::StringUtilities::EncodeRml(localise(station_title)) + "</h1>";
		if ((toolbar_enabled_ || (layout && layout->contains("layout"))) && route_ != 31 && !pregame)
		{
			button("settings", "Settings");
			bool teams = false;
			if (model_.observation())
				for (const auto &t : model_.observation()->gameplay.value("teams", Json::array()))
					teams |= !t.at("orders").empty();
			if (!layout || !layout->contains("layout"))
				toolbar += "<div class=\"legacy-tools\" style=\"position:absolute;right:0;top:0;\">" +
				           button("systems", "Systems") +
				           (teams && !support_active() ? button("game.teams", "Teams") : std::string{}) + "</div>";
			toolbar += "<button class=\"settings-cog\" id=\"" + id("settings") + "\"><img src=\"" +
			           asset_path("../menu/settings.png") + "\" width=\"56\" height=\"56\"/></button>";
		}
		for (const auto &pad : toolbar_enabled_ && !pregame ? pads : std::set<std::string>{})
#ifdef __EMSCRIPTEN__
			toolbar += button("pad/" + pad, "Assign controller " + pad);
#else
			(void)pad;
#endif
		toolbar += "<p id=\"connection-status\"></p>";
		document_->GetElementById("toolbar")->SetInnerRML(toolbar);
		std::string widgets;
		bool has_radar = false;
		bool has_manual_toggle = false;
		int heading_count = 0;
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
			{
				if (widget.at("kind") == "radar")
					has_radar = true;
				if (widget.at("kind") == "heading")
					++heading_count;
			}
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
				auto percent = [&](const char *field)
				{
					return std::to_string(widget.at(field).get<double>() / 10) + "%";
				};
				widgets += "<div class=\"widget " + kind + "-widget\" id=\"widget/" +
				           Rml::StringUtilities::EncodeRml(key) + "\" style=\"left:" + percent("x") +
				           ";top:" + percent("y") + ";width:" + percent("width") + ";height:" + percent("height") +
				           "\"><h2>" + Rml::StringUtilities::EncodeRml(localise(widget.at("label"))) + "</h2>";
				if (const auto *choice = console_choice_spec(kind))
				{
					widgets += "<div class=\"camera-choices\" id=\"camera-panel/" +
					           Rml::StringUtilities::EncodeRml(key) + "\"><img class=\"camera-art\" src=\"" +
					           asset_path(std::string(choice->artwork)) + "\"/>";
					for (std::size_t value = 0; value < choice->options.size(); ++value)
					{
						const auto &option = choice->options[value];
						widgets += button("camera/" + key + "/" + std::to_string(value), std::string(option.label),
						                  std::string(option.visual));
					}
					widgets += "</div><p class=\"camera-status\" id=\"" +
					           Rml::StringUtilities::EncodeRml(id("camera-status/" + key)) + "\"/>";
				}
				if (kind == "throttle")
				{
					widgets += "<input class=\"throttle\" id=\"" + id("speed/" + key) +
					           "\" type=\"range\" orientation=\"vertical\" min=\"-100\" max=\"0\" step=\"0.1\" "
					           "value=\"0\"" +
					           (pregame ? " disabled" : "") + "/>";
					widgets += "<button class=\"all-stop\" id=\"" + id("all-stop/" + key) + "\">" +
					           Rml::StringUtilities::EncodeRml(localise("All stop")) + "</button>";
					controls_.emplace_back(id("all-stop/" + key), "All stop");
				}
				if (kind == "heading")
				{
					widgets +=
					    "<label for=\"" + id("heading/" + key) + "\">" +
					    Rml::StringUtilities::EncodeRml(localise("Desired heading (degrees)")) +
					    "</label><input class=\"heading-value\" id=\"" + id("heading/" + key) +
					    "\" type=\"text\" maxlength=\"7\" value=\"0\"" + (pregame ? " disabled" : "") + "/><p>" +
					    Rml::StringUtilities::EncodeRml(localise("Click or drag the radar ring. East 0; North 90.")) +
					    "</p>";
					if (!has_radar || heading_count > 1)
						widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>";
				}
				if (kind == "radar")
					widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>" +
					           button("game.contacts/" + key, "Read radar contacts") +
					           button("on-screen/" + key, "On screen") + "<button class=\"radar-systems\" id=\"" +
					           id("radar-systems/" + key) + "\">" +
					           Rml::StringUtilities::EncodeRml(localise("System status")) + "</button>";
				if (kind == "radar")
					controls_.emplace_back(id("radar-systems/" + key), "System status");
				if (kind == "radar" && manual_toggle_ && !has_manual_toggle)
				{
					has_manual_toggle = true;
					controls_.emplace_back(id("manual"), "Manual");
					widgets += "<button class=\"manual-toggle\" id=\"" + id("manual") +
					           "\"><img class=\"manual-art\" src=\"" + asset_path("manual-toggle.png") +
					           "\"/><span class=\"manual-label\">" +
					           Rml::StringUtilities::EncodeRml(localise("Manual")) +
					           "</span><span class=\"manual-lamp\" id=\"" + id("manual.lamp") +
					           "\"/><span class=\"manual-state\" id=\"" + id("manual.state") + "\"/></button>";
				}
				widgets += "<p class=\"" + std::string(kind == "throttle" ? "readout" : "heading-actual") +
				           "\" id=\"value/" + Rml::StringUtilities::EncodeRml(key) + "\"/></div>";
			}
		document_->GetElementById("widgets")->SetInnerRML(pregame ? "" : widgets);
		document_->GetElementById("toolbar")->SetProperty(
		    "display", pregame || (layout && layout->contains("layout")) ? "none" : "block");
		std::string lobby;
		if (pregame)
		{
			lobby = "<div class=\"ready-card\"><h1 id=\"" + id("lobby-title") + "\">" +
			        Rml::StringUtilities::EncodeRml(localise(title)) + "</h1><p>" +
			        Rml::StringUtilities::EncodeRml(localise(description)) + "</p><p>" +
			        Rml::StringUtilities::EncodeRml(localise(briefing)) + "</p><p>" +
			        Rml::StringUtilities::EncodeRml(
			            localise(ready ? "Ready. Waiting for the crew." : "Check your Console, then ready up.")) +
			        "</p>";
			if (!complexity_choices.empty() && complexity_levels.empty())
				lobby += "<p>" + localise("Complexity") + ": " + Rml::StringUtilities::EncodeRml(localise(complexity)) +
				         "</p>";
			for (const auto &choice : complexity_choices)
				lobby += star_choice("complexity/" + choice, choice, complexity_levels, complexity, false);
			lobby += button("ready", ready ? "Not Ready" : "Ready") + "<p id=\"countdown\"/></div>";
		}
		if (pregame)
		{
			lobby += button("settings", "Settings");
			button("profile", "Edit profile");
			const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0),
			            sh = terminal_.at("size").at(1);
			const auto &aperture = terminal_.at("screen");
			const float ax = aperture.at("x"), ay = aperture.at("y"), aw = aperture.at("width"),
			            ah = aperture.at("height");
			lobby += "<button id=\"" + id("profile") + "\" class=\"profile-tv\" style=\"width:" + std::to_string(tw) +
			         "dp;height:" + std::to_string(tw * sh / sw) + "dp\"><img src=\"" +
			         asset_path("../menu/crt-terminal.png") + "\" width=\"" + std::to_string(tw) +
			         "\"/><span style=\"left:" + std::to_string(ax / sw * 100) +
			         "%;top:" + std::to_string((ay + ah * .85f) / sh * 100) +
			         "%;width:" + std::to_string(aw / sw * 100) + "%;height:" + std::to_string(ah * .15f / sh * 100) +
			         "%;background:#101918;font-size:0.8em\">" + Rml::StringUtilities::EncodeRml(profile_name_) +
			         "</span></button>";
			if (profile_open_)
			{
				controls_.clear();
				lobby = "<div class=\"ready-card profile-card\"><h1>" +
				        Rml::StringUtilities::EncodeRml(localise("Profile")) + "</h1><label>" +
				        Rml::StringUtilities::EncodeRml(localise("Player name")) + "</label><input id=\"" +
				        id("profile-name") + "\" type=\"text\" maxlength=\"64\" value=\"" +
				        Rml::StringUtilities::EncodeRml(profile_name_) + "\"/><div>";
				for (int i = 1; i <= 12; ++i)
				{
					button("avatar/" + std::to_string(i), "Avatar " + std::to_string(i));
					lobby += "<button class=\"avatar-choice\" id=\"" + id("avatar/" + std::to_string(i)) +
					         "\"><img src=\"" + asset_path("../menu/avatar-" + std::to_string(i) + ".png") +
					         "\" width=\"48\" height=\"48\"/></button>";
				}
				lobby += "</div>" + button("profile-save", "Use this profile") + "</div>";
			}
		}
		document_->GetElementById("lobby")->SetInnerRML(lobby);
	}
	if (pregame && profile_open_)
		app.screen_inputs.push_back({id("profile-name"), localise("Player name"), profile_name_});
	if (pregame)
		app.screen_readouts.emplace_back(id("lobby-title"),
		                                 localise(title) + ". " + localise(description) + " " + localise(briefing));
	if (auto *status = document_->GetElementById("connection-status"))
	{
		const auto text = localise(pregame ? description : model_.announcement());
		if (displayed_values_["connection-status"] != text)
		{
			status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
			displayed_values_["connection-status"] = text;
		}
	}
	if (pregame)
		if (auto *countdown = document_->GetElementById("countdown"))
		{
			const auto left = app.session->lobby().countdown_ms;
			countdown->SetInnerRML(left < 0 ? ""
			                                : Rml::StringUtilities::EncodeRml(localise("Starting in")) + " " +
			                                      std::to_string((left + 999) / 1000));
		}
	app.frame_metrics["console/" + std::to_string(route_) + "/chrome"] = GetTime() - layout_started;
	const auto readouts_started = GetTime();
	auto gameplay_controls = pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_controls();
	std::erase_if(gameplay_controls,
	              [](const auto &control)
	              {
		              return control.first.starts_with("game.console/") || control.first.starts_with("game.status") ||
		                     control.first.starts_with("game.sensor-contact/") ||
		                     control.first.starts_with("game.combat-target/");
	              });
	if (!tactical_.console().empty())
	{
		gameplay_controls = tactical_.weapon_controls();
	}

	std::map<std::string, ConsoleSystemStatus> status_controls;
	const auto owned_systems =
	    model_.observation() ? station_owned_systems(*model_.observation()) : std::set<std::string>{};
	if (!pregame)
		for (const auto &status : model_.system_statuses())
			if (owned_systems.contains(status.system))
				status_controls.emplace("game.status/" + status.system, status);
	std::erase_if(gameplay_controls,
	              [&](const auto &control)
	              {
		              return control.first.starts_with("game.status/") && !status_controls.contains(control.first);
	              });

	enum class ControlDestination
	{
		general,
		manual_flight,
		settings,
		camera,
		dial,
		profile,
		message,
		stance
	};
	std::set<std::pair<std::string, std::string>> dial_bindings;
	if (helm && layout)
		for (const auto &widget : layout->at("widgets"))
		{
			const auto kind = widget.at("kind").get<std::string>();
			if (kind == "throttle" || kind == "heading")
				dial_bindings.emplace(kind == "throttle" ? "speed_mm_s" : "heading_mdeg",
				                      model_.widget_system(widget.at("id")));
		}
	const bool manual_available = model_.manual_available();
	const bool manual_active = manual_available && model_.manual_active();
	const auto destination = [&](const Json &descriptor)
	{
		const auto action = descriptor.value("action", "");
		if (manual_available &&
		    (action == "assisted" || action == "manual_steering" || action == "longitudinal_thrust" ||
		     (manual_active && (action == "speed_mm_s" || action == "heading_mdeg" || action == "destination" ||
		                        action == "follow_contact" || action == "match_speed"))))
			return ControlDestination::manual_flight;
		if (action == "away")
			return ControlDestination::settings;
		if (action == "camera_mode" || action == "camera_direction")
			return ControlDestination::camera;
		if (action == "preset")
			return ControlDestination::profile;
		if (action.starts_with("message."))
			return ControlDestination::message;
		if (action == "stance.set")
			return ControlDestination::stance;
		const auto target = descriptor.value("controller", std::string{}).empty()
		                        ? descriptor.value("system", std::string{})
		                        : "controller/" + descriptor.at("controller").get<std::string>() + "/" +
		                              descriptor.at("channel").get<std::string>();
		return dial_bindings.contains({action, target}) ? ControlDestination::dial : ControlDestination::general;
	};
	std::set<std::string> specialised_controls, stance_controls, sensor_actions, objective_controls;
	std::vector<StanceChoice> choices;
	if (!pregame && model_.observation() && model_.observation()->gameplay.contains("controls"))
		for (const auto &descriptor : model_.observation()->gameplay.at("controls"))
		{
			const auto action = descriptor_action(descriptor.at("id"));
			if (descriptor.value("action", "") == "sensor_target")
				sensor_actions.insert(action);
			if (descriptor.value("action", "") == "objective.priority")
				objective_controls.insert(action);
			switch (destination(descriptor))
			{
			case ControlDestination::general:
				break;
			case ControlDestination::message:
				if (engineering)
					specialised_controls.insert(action);
				break;
			case ControlDestination::stance:
				if (command_console)
				{
					stance_controls.insert(action);
					choices.push_back(
					    {action, descriptor.at("system"), descriptor.at("label"), descriptor.value("selected", false)});
				}
				break;
			default:
				specialised_controls.insert(action);
				break;
			}
		}
	std::erase_if(gameplay_controls,
	              [&](const auto &control)
	              {
		              return specialised_controls.contains(control.first);
	              });
	readout_values_ = pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_readouts();
	if (helm)
		readout_values_ = tactical_.target_readouts(model_, app_->session->catalog(), true);

	if (model_.observation())
		for (const auto &system : model_.observation()->systems)
			if (!owned_systems.contains(system.id))
			{
				const auto key = "game.readout/system-" + descriptor_action(system.id).substr(13);
				std::erase_if(readout_values_,
				              [&](const auto &readout)
				              {
					              return readout.first == key;
				              });
			}
	if (route_ == 31)
		std::erase_if(readout_values_,
		              [](const auto &row)
		              {
			              return row.first == "alert";
		              });
	if (engineering)
		std::erase_if(readout_values_,
		              [](const auto &readout)
		              {
			              return readout.first.starts_with("game.readout/system-");
		              });
	const auto &gameplay_readouts = readout_values_;
	dom_.leaf_markup(document_->GetElementById("sensor-title"),
	                 Rml::StringUtilities::EncodeRml(localise("Contact scan")));
	std::set<std::string> sensor_controls;
	for (const auto *section : {"sensor-controls", "sensor-readouts", "sensor-title"})
		dom_.property(document_->GetElementById(section), "width", std::to_string(region.width * .30f - 18) + "dp");
	std::string sensor_markup;
	for (const auto &[name, label] : gameplay_controls)
		if (sensor_actions.contains(name))
		{
			sensor_controls.insert(name);
			sensor_markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
			                 Rml::StringUtilities::EncodeRml(label) + "</button>";
		}
	if (sensor_markup != sensor_controls_signature_)
	{
		dom_.forget_elements();
		document_->GetElementById("sensor-controls")->SetInnerRML(sensor_markup);
		sensor_controls_signature_ = sensor_markup;
	}
	if (!(layout && layout->contains("layout")))
		dom_.property(document_->GetElementById("sensor-panel"), "display",
		              model_.sensor_contact() && !pregame &&
		                      (!sensor_controls.empty() || std::any_of(readout_values_.begin(), readout_values_.end(),
		                                                               [](const auto &r)
		                                                               {
			                                                               return r.first.starts_with(
			                                                                   "game.scan-finding/");
		                                                               }))
		                  ? "block"
		                  : "none");
	std::set<std::string> engineering_controls;
	dom_.property(document_->GetElementById("engineering-cards"), "width",
	              std::to_string(region.width * .92f - 24) + "dp");
	std::string engineering_markup;
	for (const auto &card : cards)
	{
		const int columns = region.width >= 900 ? 2 : 1;
		const float card_width = (region.width * .92f - 40) / columns - 12;
		engineering_markup += "<div class=\"engineering-card\" style=\"width:" + std::to_string(card_width) +
		                      "dp\"><h2>" + Rml::StringUtilities::EncodeRml(localise(card.title)) + "</h2><p id=\"" +
		                      id("resource/" + card.id) + "\"></p>";
		for (const auto &[name, value] : std::vector<std::pair<std::string, std::optional<int>>>{
		         {"heat", card.heat}, {"battery", card.battery}, {"condition", card.condition}})
			if (value)
				engineering_markup += "<div class=\"resource-meter\"><div class=\"resource-fill resource-" + name +
				                      "\" id=\"" + id("gauge/" + card.id + "/" + name) + "\"/></div>";
		for (const auto &[name, label] : card.controls)
		{
			engineering_controls.insert(name);
			for (auto &control : gameplay_controls)
				if (control.first == name)
					control.second = localise(card.title) + ": " + label;
			engineering_markup +=
			    "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
		}
		engineering_markup += "</div>";
	}
	if (engineering_markup != engineering_signature_)
	{
		dom_.forget_elements();
		document_->GetElementById("engineering-cards")->SetInnerRML(engineering_markup);
		engineering_signature_ = engineering_markup;
		dom_.forget_properties();
		for (const auto &card : cards)
			displayed_values_.erase("resource/" + card.id);
	}
	resource_readouts_.clear();
	for (const auto &card : cards)
	{
		const auto name = "resource/" + card.id;
		resource_readouts_.emplace_back(name, card.summary);
		update_readout(name, card.summary, false);
		app.screen_readouts.emplace_back(id(name), localise(card.title) + ". " + card.summary);
		for (const auto &[metric, value] : std::vector<std::pair<std::string, std::optional<int>>>{
		         {"heat", card.heat}, {"battery", card.battery}, {"condition", card.condition}})
			if (value)
				dom_.property(dom_.element(id("gauge/" + card.id + "/" + metric)), "width",
				              std::to_string(*value) + "%");
	}
	app.frame_metrics["console/" + std::to_string(route_) + "/readouts"] = GetTime() - readouts_started;
	const auto markup_started = GetTime();
	auto *gameplay = document_->GetElementById("gameplay");
	auto *widgets = document_->GetElementById("widgets");
	const auto messages = pregame ? std::vector<ConsoleMessageView>{} : model_.messages();
	const bool has_gameplay = !gameplay_controls.empty() || !gameplay_readouts.empty() || !messages.empty();
	const auto placement = signature_ + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) + "/" +
	                       std::to_string(text_scale_) + (has_gameplay ? "/gameplay" : "/empty");
	const bool placement_changed = placement_signature_ != placement;
	if (placement_changed && !(layout && layout->contains("layout")))
	{
		placement_signature_ = placement;
		gameplay->SetProperty("display", has_gameplay ? "block" : "none");
		widgets->SetProperty("right", "4%");
		widgets->SetProperty("bottom", "4%");
		gameplay->SetProperty("left", "4%");
		gameplay->SetProperty("right", "4%");
		gameplay->SetProperty("top", "125dp");
		gameplay->SetProperty("bottom", "4%");
		gameplay->RemoveProperty("width");
		gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 125)) + "dp");
		bool placed_gameplay = false;
		if (has_gameplay && layout && layout->contains("gameplay_panel") && layout->at("gameplay_panel").is_object())
		{
			const auto &panel = layout->at("gameplay_panel");
			if (panel.contains("x") && panel.contains("y") && panel.contains("width") && panel.contains("height") &&
			    panel.at("x").is_number() && panel.at("y").is_number() && panel.at("width").is_number() &&
			    panel.at("height").is_number())
			{
				const double x = panel.at("x"), y = panel.at("y"), width = panel.at("width"),
				             height = panel.at("height");
				if (std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) && x >= 0 &&
				    y >= 0 && width > 0 && height > 0 && x + width <= 1 && y + height <= 1)
				{
					const auto available_height = std::max(80.f, region.height - 125.f);
					gameplay->SetProperty("left", std::to_string(region.width * x) + "dp");
					gameplay->SetProperty("top", std::to_string(125 + available_height * y) + "dp");
					gameplay->SetProperty("width", std::to_string(region.width * width) + "dp");
					gameplay->SetProperty("height", std::to_string(available_height * height) + "dp");
					gameplay->RemoveProperty("right");
					gameplay->RemoveProperty("bottom");
					placed_gameplay = true;
				}
			}
		}
		if (has_gameplay && !command_console && !placed_gameplay && layout && !layout->at("widgets").empty())
		{
			if (region.width >= 900)
			{
				widgets->SetProperty("right", "42%");
				gameplay->SetProperty("left", "60%");
			}
			else
			{
				widgets->SetProperty("bottom", "45%");
				gameplay->SetProperty("top", "58%");
			}
		}
		// The compact helm preset has one throttle. Multiple independent controls
		// must retain their authored geometry instead of occupying the same slot.
		if (helm && !pregame && !placed_gameplay &&
		    std::count_if(layout->at("widgets").begin(), layout->at("widgets").end(),
		                  [](const auto &widget)
		                  {
			                  return widget.at("kind") == "throttle";
		                  }) == 1)
		{
			widgets->SetProperty("top", "4%");
			widgets->SetProperty("right", "4%");
			widgets->SetProperty("bottom", "4%");
			const float width = region.width * .92f, height = region.height * .92f;
			const float side = std::min(height, width * (has_camera ? .60f : .70f));
			for (const auto &widget : layout->at("widgets"))
			{
				auto *element = document_->GetElementById("widget/" + widget.at("id").get<std::string>());
				const std::string kind = widget.at("kind");
				if (kind == "radar")
				{
					element->SetProperty("left", std::to_string(has_camera ? width * .24f + (width * .61f - side) / 2
					                                                       : (width - side) / 2) +
					                                 "dp");
					element->SetProperty("top", std::to_string((height - side) / 2) + "dp");
					element->SetProperty("width", std::to_string(side) + "dp");
					element->SetProperty("height", std::to_string(side) + "dp");
				}
				else if (kind == "throttle")
				{
					element->SetProperty("left", "85%");
					element->SetProperty("top", "10%");
					element->SetProperty("width", "15%");
					element->SetProperty("height", "80%");
				}
				else if (const auto *choice = console_choice_spec(kind))
				{
					element->SetProperty("left", "0%");
					element->SetProperty("top", std::string(choice->housing_top));
					element->SetProperty("width", "22%");
					element->SetProperty("height", std::string(choice->housing_height));
				}
				else if (kind == "heading")
				{
					element->SetProperty("left", "0%");
					element->SetProperty("top", "85%");
					element->SetProperty("width", "15%");
					element->SetProperty("height", "15%");
				}
			}
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("top", has_camera ? "74%" : "18%");
			gameplay->SetProperty("width", has_camera ? "22%" : "12%");
			gameplay->SetProperty("height", has_camera ? "16%" : "60%");
			gameplay->RemoveProperty("right");
			gameplay->RemoveProperty("bottom");
		}
		if (engineering)
		{
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("top", "100dp");
			gameplay->SetProperty("width", std::to_string(region.width * .92f) + "dp");
			gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 100)) + "dp");
			gameplay->RemoveProperty("right");
			gameplay->RemoveProperty("bottom");
		}
		if (team_dispatch)
		{
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("top", "100dp");
			gameplay->SetProperty("width", std::to_string(region.width * .92f) + "dp");
			gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height - 140.f)) + "dp");
			gameplay->RemoveProperty("right");
			gameplay->RemoveProperty("bottom");
		}
		if (command_console)
		{
			const float left = route_ == 31 ? 16.f : 48.f;
			gameplay->SetProperty("left", std::to_string(left) + "dp");
			gameplay->SetProperty("top", "112dp");
			gameplay->SetProperty("width", std::to_string(region.width - left - 48.f) + "dp");
			gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height - 224.f)) + "dp");
			gameplay->RemoveProperty("right");
			gameplay->RemoveProperty("bottom");
		}
	}
	if (layout && layout->contains("layout"))
		placement_signature_ = placement;

	Json gameplay_identity = Json::array();
	for (const auto &[name, label] : gameplay_controls)
		if (!engineering_controls.contains(name))
			gameplay_identity.push_back(name);
	const auto command_language = local_language_ + (app.pseudo_locale ? "/pseudo" : "/normal");
	if (choices != command_choices_ || command_language != command_language_)
	{
		std::map<std::string, std::string> groups, titles;
		if (!choices.empty())
			for (const auto &seat : console_specs(app, model_.observation()->own.definition))
				titles[seat.id] = app.session->catalog().consoles.at(seat.definition).value("name", seat.id);
		for (const auto &choice : choices)
			groups[choice.target] +=
			    "<button class=\"stance-button " + std::string(choice.selected ? "selected" : "") + "\" id=\"" +
			    id(choice.action) + "\"><span class=\"stance-label\">" +
			    Rml::StringUtilities::EncodeRml(localise(choice.label)) + "</span><span class=\"stance-state\">" +
			    (choice.selected ? Rml::StringUtilities::EncodeRml(localise("Selected")) : std::string("&nbsp;")) +
			    "</span></button>";
		command_markup_.clear();
		for (const auto &[target, buttons] : groups)
			command_markup_ +=
			    "<div class=\"command-card\"><h2>" +
			    Rml::StringUtilities::EncodeRml(localise(titles.contains(target) ? titles.at(target) : target)) +
			    "</h2><div class=\"stance-row\">" + buttons + "</div></div>";
		command_choices_ = std::move(choices);
		command_language_ = command_language;
	}
	std::string objective_markup;
	if (model_.observation() && model_.observation()->gameplay.contains("objective_board"))
	{
		objective_markup = "<div class=\"command-card objective-card\"><h2>" +
		                   Rml::StringUtilities::EncodeRml(localise("Objectives")) + "</h2>";
		for (const auto &[name, text] : gameplay_readouts)
			if (name.starts_with("game.objective/"))
				objective_markup += "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
				                    Rml::StringUtilities::EncodeRml(text) + "</p>";
		for (const auto &[name, label] : gameplay_controls)
			if (objective_controls.contains(name))
				objective_markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
				                    Rml::StringUtilities::EncodeRml(label) + "</button>";
		objective_markup += "</div>";
	}
	const auto gameplay_signature = gameplay_identity.dump() + command_markup_ + objective_markup;
	if (gameplay_signature_ != gameplay_signature)
	{
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		dom_.forget_elements();
		for (const auto &held : held_clicks_)
			model_.release_control(held.substr(id("").size()));
		held_clicks_.clear();
		std::string markup = objective_markup + command_markup_ +
		                     (!tactical_.console().empty() ? "<h2>" + localise("Weapons") + "</h2>" : "");
		if (team_dispatch)
		{
			markup += "<h2>" + Rml::StringUtilities::EncodeRml(localise("Team dispatch")) + "</h2>";
			for (const auto &[name, label] : gameplay_controls)
				if (name.starts_with("game.team-order/") || name == "game.teams-close")
					markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
					          Rml::StringUtilities::EncodeRml(label) + "</button>";
		}
		if (model_.observation())
		{
			const auto compartments = model_.team_map();
			std::set<std::string> decks;
			for (const auto &c : compartments)
				decks.insert(c.at("deck").get<std::string>());
			for (const auto &deck : decks)
			{
				std::int64_t minx = 1000000000, miny = 1000000000, maxx = -1000000000, maxy = -1000000000;
				for (const auto &c : compartments)
					if (c.at("deck") == deck)
					{
						const auto x = c.at("x_mm").get<std::int64_t>(), y = c.at("y_mm").get<std::int64_t>(),
						           hw = c.at("half_width_mm").get<std::int64_t>(),
						           hl = c.at("half_length_mm").get<std::int64_t>();
						minx = std::min(minx, x - hw);
						maxx = std::max(maxx, x + hw);
						miny = std::min(miny, y - hl);
						maxy = std::max(maxy, y + hl);
					}
				markup += "<h2>" + Rml::StringUtilities::EncodeRml(deck) +
				          "</h2><div "
				          "style=\"display:block;position:relative;width:100%;height:300dp;min-height:300dp;clear:"
				          "both;overflow:hidden;\">";
				for (const auto &c : compartments)
					if (c.at("deck") == deck)
					{
						const auto index = c.at("index").get<std::int64_t>();
						const auto x = c.at("x_mm").get<double>(), y = c.at("y_mm").get<double>(),
						           hw = c.at("half_width_mm").get<double>(), hl = c.at("half_length_mm").get<double>();
						const auto name = "game.compartment/" + std::to_string(index);
						std::string label = engineering_name(c.at("id"));
						for (const auto &control : gameplay_controls)
							if (control.first == name)
								label = control.second;
						markup +=
						    "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) +
						    "\" style=\"position:absolute;left:" +
						    std::to_string(100 * (maxy - y - hl) / std::max<std::int64_t>(1, maxy - miny)) +
						    "%;top:" + std::to_string(100 * (maxx - x - hw) / std::max<std::int64_t>(1, maxx - minx)) +
						    "%;width:" + std::to_string(100 * 2 * hl / std::max<std::int64_t>(1, maxy - miny)) +
						    "%;height:" + std::to_string(100 * 2 * hw / std::max<std::int64_t>(1, maxx - minx)) +
						    "%;margin:0;min-width:0;min-height:0;box-sizing:border-box;background:" +
						    (c.at("disabled").get<bool>() ? std::string("#702c2c") : std::string("#164244")) + ";\">" +
						    Rml::StringUtilities::EncodeRml(label) + "</button>";
					}
				markup += "</div>";
			}
		}

		for (const auto &[name, label] : gameplay_controls)
			if (!stance_controls.contains(name) && !engineering_controls.contains(name) &&
			    !objective_controls.contains(name) && !name.starts_with("game.compartment/") &&
			    name != "game.status-close" &&
			    !(team_dispatch && (name.starts_with("game.team-order/") || name == "game.teams-close")) &&
			    !sensor_controls.contains(name))
			{
				const bool status = status_controls.contains(name);
				const auto &visible = status ? status_controls.at(name).compact_label : label;
				markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"" +
				          (status ? " class=\"status-indicator\"" : "") + ">" +
				          Rml::StringUtilities::EncodeRml(visible) + "</button>";
			}
		document_->GetElementById("gameplay-controls")->SetInnerRML(markup);
		if (auto *previous = document_->GetElementById(focused))
			previous->Focus();
		gameplay_signature_ = gameplay_signature;
		for (const auto &[name, label] : gameplay_controls)
			displayed_values_.erase("control/" + name);
	}
	for (const auto &[name, label] : gameplay_controls)
		if (!stance_controls.contains(name) && !engineering_controls.contains(name) &&
		    displayed_values_["control/" + name] != label)
		{
			if (auto *element = dom_.element(id(name)))
				element->SetInnerRML(Rml::StringUtilities::EncodeRml(
				    status_controls.contains(name) ? status_controls.at(name).compact_label : label));
			displayed_values_["control/" + name] = label;
		}
	for (const auto &[name, status] : status_controls)
		if (auto *light = dom_.element(id(name)))
			dom_.property(light, "border-left", status.attention() ? "10dp #f08060" : "10dp #83be90");
	if (model_.observation())
		for (const auto &c : model_.team_map())
		{
			if (auto *compartment =
			        dom_.element(id("game.compartment/" + std::to_string(c.at("index").get<std::int64_t>()))))
				dom_.property(compartment, "background-color",
				              c.at("disabled").get<bool>()                                         ? "#702c2c"
				              : model_.selected_compartment() == c.at("index").get<std::int64_t>() ? "#486b56"
				                                                                                   : "#164244");
		}
	gameplay_identity = Json::array();
	for (const auto &[name, text] : gameplay_readouts)
		gameplay_identity.push_back(name);
	const auto dock_control =
	    helm ? model_.target_docking_control(tactical_.console().empty() ? model_.sensor_contact()
	                                                                     : tactical_.model().sensor_contact())
	         : std::optional<ConsoleDockAction>{};
	const auto readout_signature =
	    gameplay_identity.dump() + (dock_control ? dock_control->id + dock_control->label : "");
	if (gameplay_readout_signature_ != readout_signature)
	{
		dom_.forget_elements();
		std::string markup, sensor_readouts;
		for (const auto &[name, text] : gameplay_readouts)
		{
			if (name.starts_with("game.objective/"))
				continue;
			(name.starts_with("game.scan-") ? sensor_readouts : markup) +=
			    "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"" +
			    (name == "target/range" || name == "target/speed" ? " class=\"target-motion\"" : "") + "></p>";
			displayed_values_.erase(name);
		}
		if (dock_control)
			markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(dock_control->id)) + "\">" +
			          Rml::StringUtilities::EncodeRml(dock_control->label) + "</button>";
		document_->GetElementById("gameplay-readouts")->SetInnerRML(markup);
		document_->GetElementById("sensor-readouts")->SetInnerRML(sensor_readouts);
		gameplay_readout_signature_ = readout_signature;
	}
	message_ui::refresh(*document_, *document_->GetElementById("gameplay-messages"), messages,
	                    app.session ? &app.session->catalog() : nullptr, app.screen_readouts, id(""));
	conversations_.refresh(model_, conversation_frame(app, region));
	for (const auto &[name, text] : gameplay_readouts)
	{
		update_readout(name, text, placement_changed || name.starts_with("game.scan-"));
		if (name.starts_with("game.readout/system-"))
			if (auto *readout = dom_.element(id(name)))
			{
				dom_.property(readout, "border-left",
				              text.find("[!]") != std::string::npos ? "8dp #f08060" : "8dp #83be90");
				dom_.property(readout, "padding-left", "8dp");
			}

		app.screen_readouts.emplace_back(id(name), text);
	}
	app.frame_metrics["console/" + std::to_string(route_) + "/markup"] = GetTime() - markup_started;
	const auto navigation_started = GetTime();
	navigation_.prepare(model_, navigation_frame(app, region, pregame));
	prepare_manual(app, region, pregame);
	app.frame_metrics["console/" + std::to_string(route_) + "/navigation"] = GetTime() - navigation_started;
	if (!pregame)
	{
		const auto authored_started = GetTime();
		prepare_authored_layout(layout, region);
		conversations_.fit(model_, conversation_frame(app, region));
		finish_support_panels(region);
		app.frame_metrics["console/" + std::to_string(route_) + "/authored-layout"] = GetTime() - authored_started;
	}
	prepare_station_components(region, pregame);
	if (layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
		{
			const bool available = ui_widget_available(widget, model_.ui_availability());
			if (!layout->contains("layout") || !available)
				dom_.property(document_->GetElementById("widget/" + widget.at("id").get<std::string>()), "display",
				              available ? "block" : "none");
		}
	restore_console_browsing();
	{
		const auto update_started = GetTime();
		context_->Update();
		app.frame_metrics["console/" + std::to_string(route_) + "/before-widgets"] = GetTime() - update_started;
	}
	heading_dial_.reset();
	heading_dials_.clear();
	dial_systems_.clear();
	throttle_tracks_.clear();
	std::string shared_heading;
	int heading_count = 0;
	if (layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("kind") == "heading" && ui_widget_available(widget, model_.ui_availability()))
			{
				shared_heading = model_.widget_system(widget.at("id").get<std::string>());
				++heading_count;
			}
	if (layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
		{
			const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
			if (auto *element = document_->GetElementById("widget/" + key); !element || !element->IsVisible(true))
				continue;
			if (kind == "radar" || kind == "heading")
				if (auto *dial = document_->GetElementById("dial/" + key))
				{
					const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
					const float radius = std::max(1.f, std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f);
					const auto system = kind == "heading"    ? model_.widget_system(key)
					                    : heading_count == 1 ? shared_heading
					                                         : std::string{};
					if (!system.empty())
					{
						heading_dials_[key] = {at.x + dial->GetOffsetWidth() / 2, at.y + dial->GetOffsetHeight() / 2,
						                       radius};
						dial_systems_[key] = system;
						if (!heading_dial_)
							heading_dial_ = heading_dials_.at(key);
					}
				}
			if (kind == "throttle")
			{
				const auto track = throttle_rect(key);
				const auto &markers = hud_metadata_.at("helm-throttle-track").at("markers");
				const float top = markers.at("travel_max")[1].get<float>() /
				                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
				            bottom = markers.at("travel_min")[1].get<float>() /
				                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
				auto *slider = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("speed/" + key)));
				dom_.property(slider, "left",
				              std::to_string(track.x - widget_offset(key).x - track.width * .1f) + "px");
				dom_.property(slider, "top",
				              std::to_string(track.y - widget_offset(key).y + track.height * top - track.width * .2f) +
				                  "px");
				dom_.property(slider, "width", std::to_string(track.width * 1.2f) + "px");
				dom_.property(slider, "height",
				              std::to_string(track.height * (bottom - top) + track.width * .4f) + "px");
				const double percent = 100 * throttles_.fraction(model_, key);
				throttle_tracks_[key] = {track.x, track.y + track.height * top, track.width,
				                         track.height * (bottom - top)};
				// Programmatic projection updates must never enqueue a fresh player command.
				syncing_values_ = true;
				if (slider->GetValue() != std::to_string(-percent))
					slider->SetValue(std::to_string(-percent));
				syncing_values_ = false;
				Rml::ElementList bars;
				slider->GetElementsByTagName(bars, "sliderbar");
				for (auto *bar : bars)
					dom_.property(bar, "height", std::to_string(track.width * .4f) + "px");
				if (!pregame && !model_.manual_active())
					app.screen_inputs.push_back(
					    {id("speed/" + key), "Desired speed (%)", std::to_string(percent), true});
			}
			if (kind == "heading" && !pregame && !model_.manual_active())
			{
				auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("heading/" + key)));
				const auto requested = std::string(
				    TextFormat("%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0));
				if (context_->GetFocusElement() != field && field->GetValue() != requested)
				{
					syncing_values_ = true;
					field->SetValue(requested);
					syncing_values_ = false;
				}
				app.screen_inputs.push_back(
				    {id("heading/" + key), "Desired heading (degrees)",
				     std::string(
				         TextFormat("%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0)),
				     false});
			}
			if (const auto *choice = console_choice_spec(kind))
			{
				const auto selected = choice->selected(model_.camera());
				auto *housing = document_->GetElementById("widget/" + key);
				const float width =
				    std::max(1.f, std::min(housing->GetOffsetWidth() - 16 * display_scale,
				                           (housing->GetOffsetHeight() - 50 * display_scale) * choice->aspect));
				const float height = width / choice->aspect;
				if (auto *panel = document_->GetElementById("camera-panel/" + key))
				{
					dom_.property(panel, "left", std::to_string((housing->GetOffsetWidth() - width) / 2) + "px");
					dom_.property(panel, "width", std::to_string(width) + "px");
					dom_.property(panel, "height", std::to_string(height) + "px");
				}
				if (auto *status = document_->GetElementById(id("camera-status/" + key)))
				{
					dom_.property(status, "top", std::to_string(height + 30 * display_scale) + "px");
					update_readout("camera-status/" + key, localise(std::string(choice->options[selected].label)),
					               true);
				}
				for (std::size_t i = 0; i < choice->options.size(); ++i)
					if (auto *button = document_->GetElementById(id("camera/" + key + "/" + std::to_string(i))))
					{
						const auto &option = choice->options[i];
						button->SetClass("selected", i == static_cast<std::size_t>(selected));
						dom_.property(button, "left", std::to_string(option.left) + "%");
						dom_.property(button, "top", std::to_string(option.top) + "%");
						dom_.property(button, "width", std::to_string(option.width) + "%");
						dom_.property(button, "height", std::to_string(option.height) + "%");
					}
			}
			std::string value =
			    kind == "throttle"
			        ? "Requested " + std::string(TextFormat("%.1f", throttles_.request(model_, key) / 1000.0)) + " m/s"
			    : kind == "heading"
			        ? "Actual " + std::to_string(model_.observation() ? model_.observation()->own.heading / 1000 : 0) +
			              " degrees"
			        : "";
			value = localise(value);
			if (auto *readout = document_->GetElementById("value/" + key);
			    readout && displayed_values_["value/" + key] != value)
			{
				dom_.leaf_markup(readout, Rml::StringUtilities::EncodeRml(value));
				displayed_values_["value/" + key] = value;
			}
		}
	sharing_pods_.clear();
	if (helm && !pregame && layout && !layout->contains("layout"))
		for (const auto &w : layout->at("widgets"))
			if (w.at("kind") == "radar")
			{
				const auto key = w.at("id").get<std::string>();
				auto *dial = document_->GetElementById("dial/" + key);
				auto *control = document_->GetElementById(id("on-screen/" + key));
				if (!dial || !control)
					continue;
				const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
				const auto parent = widget_offset(key);
				const float r = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
				const float cx = at.x + dial->GetOffsetWidth() / 2, cy = at.y + dial->GetOffsetHeight() / 2;
				const float pod_width =
				    std::clamp(r * .70f, 196.f * display_scale, 240.f * display_scale) * text_scale_ / 100.f;
				const float pod_height = 76.f * display_scale * text_scale_ / 100.f;
				const Rectangle pod{
				    std::min(cx + r * .34f, physical_region.width - pod_width - 24.f * display_scale),
				    std::min(cy + r * 1.02f, physical_region.height - pod_height - 24.f * display_scale), pod_width,
				    pod_height};
				sharing_pods_[key] = pod;
				dom_.property(control, "left", std::to_string(pod.x - parent.x) + "px");
				dom_.property(control, "top", std::to_string(pod.y - parent.y) + "px");
				dom_.property(control, "width", std::to_string(pod.width) + "px");
				dom_.property(control, "height", std::to_string(pod.height) + "px");
			}
	{
		const auto update_started = GetTime();
		context_->Update();
		app.frame_metrics["console/" + std::to_string(route_) + "/after-widgets"] = GetTime() - update_started;
	}
	const auto semantics_started = GetTime();
	for (const auto &[name, label] : controls_)
	{
		if (name.starts_with(id("all-stop/")) && model_.manual_active())
			continue;
		if (name == id("manual"))
		{
			const bool manual = model_.observation()->gameplay.value("profile", "") != "automatic";
			app.screen_toggles[name] = manual;
			if (auto *toggle = dom_.element(name))
			{
				toggle->SetClass("manual-on", manual);
				toggle->SetAttribute("aria-pressed", manual ? "true" : "false");
				// Keep the single switch inside the radar bezel at smaller viewport sizes.
				const float width = std::clamp(toggle->GetParentNode()->GetOffsetWidth() * .34f, 160.f * display_scale,
				                               220.f * display_scale) *
				                    text_scale_ / 100.f;
				dom_.property(toggle, "width", std::to_string(width) + "px");
				dom_.property(toggle, "height", std::to_string(width / 2.68f) + "px");
			}
			update_readout("manual.lamp", manual ? "+" : "-", true);
			update_readout("manual.state", localise(manual ? "On" : "Off"), true);
			app.screen_controls.emplace_back(name, localise("Manual") + ": " + localise(manual ? "On" : "Off"));
		}
		else if (name.starts_with(id("on-screen/")))
		{
			const auto widget = name.substr(id("on-screen/").size());
			const bool active = model_.is_on_screen(widget);
			app.screen_toggles[name] = active;
			if (auto *b = document_->GetElementById(name))
			{
				b->SetClass("on-screen", true);
				if (layout && layout->contains("layout"))
				{
					dom_.property(b, "pointer-events", "auto");
					dom_.property(b, "z-index", "10");
				}

				b->SetClass("on-screen-active", active);
				b->SetAttribute("aria-pressed", active ? "true" : "false");
			}
			app.screen_controls.emplace_back(name, localise("On screen") + ": " + localise(active ? "On" : "Off"));
		}
		else
		{
			const auto *choice = dom_.element(name);
			const bool selected =
			    name.find("/camera/") != std::string::npos && choice && choice->IsClassSet("selected");
			app.screen_controls.emplace_back(name, selected ? localise(label) + ". " + localise("Selected")
			                                                : localise(label));
		}
		if (name == id("manual"))
			context_->Update();
		place(app, region, name);
	}
	for (const auto &[name, label] : gameplay_controls)
	{
		app.screen_controls.emplace_back(id(name), label);
		if (stance_controls.contains(name))
		{
			auto *choice = document_->GetElementById(id(name));
			app.screen_toggles[id(name)] = choice->IsClassSet("selected");
			choice->SetAttribute("aria-pressed", choice->IsClassSet("selected") ? "true" : "false");
		}
		place(app, region, id(name));
	}
	for (const auto &[name, text] : gameplay_readouts)
		place(app, region, id(name));
	if (dock_control)
	{
		app.screen_controls.emplace_back(id(dock_control->id), dock_control->label);
		place(app, region, id(dock_control->id));
	}
	for (const auto &card : cards)
		place(app, region, id("resource/" + card.id));
	for (const auto &field : app.screen_inputs)
		if (field.id.starts_with(id("")))
			place(app, region, field.id);
	if (pregame)
		place(app, region, id("lobby-title"));
	if (!pregame && model_.observation())
	{
		const auto publication = app.session->public_board().value("display", Json(nullptr));
		const auto source = publication.is_object()
		                        ? publication.value("console", "") + "/" + publication.value("widget", "")
		                        : std::string{};
		if (source != display_announcement_)
		{
			display_announcement_ = source;
			app.accessible_status =
			    source.empty() ? localise("Normal viewscreen") : localise("On screen") + ": " + source;
		}
		if (publication.is_object() && publication.at("kind") == "navigation" &&
		    model_.is_on_screen(publication.at("widget")) && display_update_tick_ != app.session->ticks())
		{
			const auto camera = display_camera(publication.at("widget"));
			if (Json(camera) != publication.at("camera"))
			{
				model_.on_screen(publication.at("widget"), camera, true);
				display_update_tick_ = app.session->ticks();
			}
		}
	}
	if (layout && !pregame)
	{
		auto hidden = [&](const std::string &name)
		{
			if (!name.starts_with(id("")))
				return false;
			const auto *e = dom_.element(name);
			return !e || !e->IsVisible(true);
		};
		std::erase_if(app.screen_controls,
		              [&](const auto &entry)
		              {
			              return hidden(entry.first);
		              });
		std::erase_if(app.screen_readouts,
		              [&](const auto &entry)
		              {
			              return hidden(entry.first);
		              });
		std::erase_if(app.screen_inputs,
		              [&](const auto &entry)
		              {
			              return hidden(entry.id);
		              });
		if (layout->contains("layout"))
		{
			if (auto *canvas = document_->GetElementById(id("nav.map")); canvas && canvas->IsVisible(true))
			{
				const auto at = canvas->GetAbsoluteOffset(Rml::BoxArea::Border);
				navigation_.layout_area({at.x, at.y, canvas->GetOffsetWidth(), canvas->GetOffsetHeight()});
			}
			else
				navigation_.hide();
			if (auto *stick = document_->GetElementById("manual-stick"); stick && stick->IsVisible(true))
			{
				const auto at = stick->GetAbsoluteOffset(Rml::BoxArea::Border);
				joystick_area_ = {at.x, at.y, stick->GetOffsetWidth(), stick->GetOffsetHeight()};
			}
			else
				joystick_area_ = {};
		}
	}
	app.frame_metrics["console/" + std::to_string(route_) + "/placements"] = GetTime() - semantics_started;
	if (!pregame && !settings_open_)
	{
		prepare_radar_choices(region);
		prepare_system_popup(region, cards);
	}
	app.frame_metrics["console/" + std::to_string(route_) + "/layout"] = GetTime() - layout_started;
}
} // namespace phoenix
