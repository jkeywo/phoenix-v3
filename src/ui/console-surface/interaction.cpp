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
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.navigation;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{

bool ConsoleSurface::State::support_action(const std::string &action)
{
	if (action.starts_with("support-console/"))
	{
		const auto split = action.find('/', 16);
		if (split == std::string::npos)
			return false;
		const auto source = support_models_.find(action.substr(16, split - 16));
		return source != support_models_.end() && source->second->act(action.substr(split + 1));
	}
	if (!support_active() && !station_component_active("power") && !station_component_active("repair"))
		return false;
	if (action == "support-page/repair")
		return authored_action("station-page/repair");
	if (action.starts_with("support-page/") || action == "support-close-details")
	{
		const auto page = action == "support-close-details" ? std::string("support") : action.substr(13);
		if (page != "support" && page != "repair")
			return false;
		if (!model_.act("game.console/" + support_root_))
			return false;
		model_.refresh(app_->session, route_);
		cancel_pointer();
		support_page_ = page;
		support_detail_ = false;
		if (page == "support")
			model_.act("game.teams-close");
		settings_open_ = false;
		document_->SetClass("local-settings-open", false);
		document_->SetClass("station-page-open", false);
		return true;
	}
	if (action == "support-navigation" && !support_navigation_.empty())
	{
		support_detail_ = model_.act("game.console/" + support_navigation_);
		return support_detail_;
	}
	if (action == "support-details/medical" && station_component_active("power"))
	{
		model_.act("game.teams-close");
		bool found = false;
		for (const auto &team : model_.observation()->gameplay.value("teams", Json::array()))
			if (!team.value("repair", false) && !team.at("orders").empty())
			{
				found = true;
				if (team.value("manual", false) && !team.value("reserved", false))
				{
					model_.act("game.team-order/" + team.at("id").get<std::string>() + "/" +
					           team.at("orders")[0].at("id").get<std::string>());
					break;
				}
			}
		if (!found)
			return false;
		model_.act("game.status-close");
		support_popup_ = "medical";
		systems_open_ = true;
		return true;
	}
	if (action.starts_with("support-details/"))
	{
		const auto detail = action.substr(16);
		if ((!authored_component_visible("repair") && !authored_component_visible("team-roster")) ||
		    !detail.starts_with("team/"))
			return false;
		const auto teams = model_.observation()->gameplay.value("teams", Json::array());
		if (std::none_of(teams.begin(), teams.end(),
		                 [&](const Json &team)
		                 {
			                 return team.at("id") == detail.substr(5) && team.value("repair", false);
		                 }))
			return false;
		for (const auto &team : teams)
			if (team.at("id") == detail.substr(5) && team.value("assignment_authority", false) &&
			    !team.value("reserved", false))
				model_.act("game.team-order/" + team.at("id").get<std::string>() + "/auto");
		model_.act("game.status-close");
		support_popup_ = detail;
		systems_open_ = true;
		return true;
	}
	return false;
}
bool ConsoleSurface::State::support_value(const std::string &name, const std::string &text)
{
	if (settings_open_)
		return false;
	if (name == id("support-repair-priority") && authored_component_visible("repair"))
	{
		const auto choice = support_choices_.find(text);
		return choice != support_choices_.end() && model_.act(choice->second);
	}
	if (systems_open_ && (support_popup_ == "help" || support_popup_.starts_with("control/")))
		return false;
	if (!name.starts_with(id("power/")))
		return false;
	const auto found = power_controls_.find(name.substr(id("").size()));
	if (found == power_controls_.end())
		return false;
	if (found->second.component.empty() ? !authored_component_visible("power") && support_popup_ != "power"
	                                    : !authored_component_visible(found->second.component))
		return false;
	double percent = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), percent);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(percent) || percent < 0 ||
	    percent > 100)
		return false;
	const auto setting = found->second;
	const auto value = setting.minimum + std::llround((setting.maximum - setting.minimum) * percent / 100);
	ConsoleModel *target = setting.console.empty() ? &model_ : nullptr;
	if (!target)
		for (const auto &[source, prefix] : task_sources())
			if (source->observation() && source->observation()->console == setting.console)
				target = source;
	if (!target || !target->command(setting.action, value, "controller/" + setting.controller + "/" + setting.channel))
		return false;
	power_pending_[setting.id] = {value, app_->session->ticks() + 40};
	return true;
}
bool ConsoleSurface::State::action(const std::string &name)
{
	const auto prefix = id("");
	if (!app_ || !name.starts_with(prefix))
		return false;
	prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	if (station_loading_)
		return false;
	const auto action =
	    name.substr(prefix.size()).starts_with("radar-systems/") ? std::string("systems") : name.substr(prefix.size());
	if (systems_open_ && (support_popup_ == "help" || support_popup_.starts_with("control/")) &&
	    action != "systems-close")
		return false;
	if (const auto handled = popup_action(action))
		return *handled;
	if (const auto operation = control_operation_actions_.find(action); operation != control_operation_actions_.end())
	{
		const auto binding = operation->second;
		if (!settings_open_)
			for (const auto &[source, prefix] : task_sources())
				if (source->observation() && source->observation()->console == binding.console)
				{
					if (binding.renderer == "teams" && binding.action.starts_with("game."))
						return source->act(binding.action);
					if (binding.action.starts_with("asset-away/"))
						return source->asset_away_action(binding.action);
					return operations_.action(operations_context(source, binding.controller, binding.renderer),
					                          binding.action);
				}
		return false;
	}

	if (action.starts_with("inline/"))
	{
		const bool accepted = !tactical_.console().empty() && tactical_.model().act(action.substr(7));
		return accepted;
	}
	if (action.starts_with("contact-dock/"))
	{
		if (settings_open_ || !authored_component_visible("readouts"))
			return false;
		const auto target = tactical_.console().empty() ? model_.sensor_contact() : tactical_.model().sensor_contact();
		cancel_pointer();
		model_.release_controls();
		return model_.target_docking_action(target, action);
	}
	if (action.starts_with("radar-contact/"))
		return select_radar_contact(std::stoull(action.substr(14)));
	if (action.starts_with("layout-page/") || action.starts_with("game.console/") ||
	    action.starts_with("station-page/") || action.starts_with("console-page/"))
		return authored_action(action);
	if (action.starts_with("asset-away/"))
	{
		if (settings_open_ || (!station_component_active("personnel") && !systems_open_))
			return false;
		return model_.asset_away_action(action);
	}
	if (app_ && app_->session && operations_.action(operations_context(), action))
		return true;
	if (app_ && !settings_open_ && (route_ != 31 || app_->screen != Screen::settings) &&
	    conversations_.action(model_, dom_, id(""), action))
		return true;
	if (app_ && navigation_.action(model_, dom_, id(""), app_->return_home, action))
		return true;
	if ((app_->screen != Screen::console && app_->screen != Screen::viewscreen &&
	     app_->screen != Screen::server_lobby && app_->screen != Screen::settings) ||
	    owner_.lock() != app_->session || (app_->session && generation_ != app_->session->generation()))
		return false;
	if (const auto handled = manual_flight_action(action))
		return *handled;
	if (action.starts_with("on-screen/"))
		return !settings_open_ && model_.on_screen(action.substr(10), display_camera(action.substr(10)));
	if (action == "settings" || action == "local-settings.close")
		return open_local_settings(action);
	if (action == "operations")
		return authored_action("station-page/logistics");
	if (const auto handled = local_settings_action(name, action))
		return *handled;
	if (const auto handled = lobby_profile_action(action))
		return *handled;

	if (action == "manual")
	{
		if (!manual_toggle_ || !game_active(*app_) || (route_ == 31 && app_->screen == Screen::settings) ||
		    !model_.observation())
			return false;
		const bool manual = model_.observation()->gameplay.value("profile", "") != "automatic";
		const auto preset = manual_preset(manual);
		if (!preset)
			return false;
		const bool accepted = model_.act(descriptor_action(*preset));
		app_->accessible_status = model_.announcement();
		return accepted;
	}

	if (const auto handled = camera_navigation_action(action))
		return *handled;

	if (action == "release")
	{
#ifdef __EMSCRIPTEN__
		return app_->session && app_->session->release_console(route_);
#else
		return false;
#endif
	}
	if (action.starts_with("claim/"))
	{
#ifndef __EMSCRIPTEN__
		if (game_active(*app_))
			return false;
#endif
		for (const auto &seat : app_->session->console_seats())
			if (action == "claim/" + std::to_string(seat.address.ship) + "/" + seat.address.console)
				return app_->session->claim_console(route_, seat.address);
		return false;
	}
	if (action.starts_with("pad/"))
	{
#ifdef __EMSCRIPTEN__
		app_->gamepad_assignments[action.substr(4)] = route_;
#else
		return assign_screen_gamepad(*app_, route_, action.substr(4));
#endif
		return true;
	}
	if (model_.observation())
		for (const auto &control : model_.observation()->gameplay.value("controls", Json::array()))
			if (control.value("action", "") == "preset" && descriptor_action(control.at("id")) == action)
				return false;
	const bool accepted = model_.act(action);
	app_->accessible_status = model_.announcement();
	return accepted;
}
std::optional<bool> ConsoleSurface::State::popup_action(const std::string &action)
{
	if (action.starts_with("control-details/") && model_.observation() && app_->session)
	{
		const auto reference = action.substr(16);
		const auto scope = reference.find('/');
		const auto console = scope == std::string::npos ? model_.observation()->console : reference.substr(0, scope);
		const auto instance = scope == std::string::npos ? reference : reference.substr(scope + 1);
		bool found = false;
		for (const auto &[source, prefix] : task_sources())
			if (source->observation() && source->observation()->console == console)
				for (const auto &widget : console_control_widgets(app_->session->catalog(), *source->observation()))
					found |= widget.id == instance;
		if (!found)
			return false;
		cancel_pointer();
		for (const auto &[source, prefix] : task_sources())
			source->release_controls();
		control_detail_return_focus_ = id(action);
		support_popup_ = "control/" + console + "/" + instance;
		systems_open_ = true;
		return true;
	}
	if (action == "help")
	{
		if (!game_active(*app_) || settings_open_ || console_help_systems(help_level_, help_page_).empty())
			return false;
		cancel_pointer();
		model_.release_controls();
		systems_open_ = true;
		support_popup_ = "help";
		help_open_identity_ = help_identity_;
		return true;
	}
	if (action == "power-details" || action == "task-detail/compartments" || action == "task-detail/objectives" ||
	    action == "task-detail/weapons" || action == "task-detail/teams" || action == "task-detail/target")
	{
		support_popup_ = action == "power-details" ? "power" : action.substr(12);
		systems_open_ = true;
		return true;
	}
	if (action.starts_with("task/"))
	{
		const auto found = task_actions_.find(action);
		return found != task_actions_.end() && this->action(id(found->second));
	}
	if (action.starts_with("popup-control/"))
	{
		const bool accepted = this->action(id(action.substr(14)));
		return accepted;
	}
	if (action.starts_with("support-") && support_action(action))
		return true;
	if (action.starts_with("all-stop/"))
		return all_stop(action.substr(9));
	if (action == "systems" || action == "systems-close")
	{
		const bool help = support_popup_ == "help";
		const bool control_detail = support_popup_.starts_with("control/");
		model_.act("game.status-close");
		tactical_.model().act("game.status-close");
		systems_open_ = action == "systems";
		support_popup_.clear();
		for (auto &[console, source] : support_models_)
			source->act("game.status-close");
		if (help)
		{
			dom_.property(document_->GetElementById("fault-dialog"), "display", "none");
			dom_.property(document_->GetElementById("help-shade"), "display", "none");
			if (auto *button = document_->GetElementById(id("help")))
				button->Focus();
		}
		if (control_detail)
		{
			dom_.property(document_->GetElementById("fault-dialog"), "display", "none");
			if (auto *button = document_->GetElementById(control_detail_return_focus_))
				button->Focus();
			control_detail_return_focus_.clear();
		}
		return true;
	}
	return std::nullopt;
}
std::optional<bool> ConsoleSurface::State::manual_flight_action(const std::string &action)
{
	if (action == "manual-mode")
	{
		if (settings_open_ || (route_ == 31 && app_->screen == Screen::settings))
			return false;
		cancel_pointer();
		const bool accepted = model_.set_manual(!model_.manual_active());
		app_->accessible_status = model_.announcement();
		return accepted;
	}
	if (action == "manual-left" || action == "manual-right" || action == "manual-forward" || action == "manual-reverse")
	{
		if (!model_.manual_active() || settings_open_ || (route_ == 31 && app_->screen == Screen::settings))
			return false;
		manual_input_.sample("accessible", {action == "manual-left"    ? 1000
		                                    : action == "manual-right" ? -1000
		                                                               : 0,
		                                    action == "manual-forward"   ? 1000
		                                    : action == "manual-reverse" ? -1000
		                                                                 : 0});
		manual_pulse_tick_ = app_->session->ticks();
		flush_manual();
		return true;
	}
	if (action == "manual-neutral")
	{
		cancel_pointer();
		return true;
	}
	return std::nullopt;
}
bool ConsoleSurface::State::open_local_settings(const std::string &action)
{
	if (action == "settings" && route_ == 31)
	{
		app_->settings_console = route_;
		return invoke_navigation(*app_, "settings");
	}
	if (action == "settings" || action == "local-settings.close")
	{
		if (!app_->session)
			return false;
		settings_open_ = action == "settings" && !settings_open_;
		if (!settings_open_ && app_->pairing_screen == screen_identity(*app_, route_))
			app_->pairing_screen.clear();
		if (settings_open_)
			local_settings_tab_ = "display";
		cancel_pointer();
		model_.release_controls();
		document_->SetClass("local-settings-open", settings_open_);
		if (!settings_open_)
			if (auto *cog = document_->GetElementById(id("settings")))
				cog->Focus();
		return true;
	}
	return false;
}

std::optional<bool> ConsoleSurface::State::local_settings_action(const std::string &name, const std::string &action)
{
	if (!settings_open_)
		return std::nullopt;
	if (std::none_of(local_settings_controls_.begin(), local_settings_controls_.end(),
	                 [&](const auto &control)
	                 {
		                 return control.first == name;
	                 }))
		return false;
	if (action.starts_with("local-settings.tab/"))
	{
		if (action.substr(19) != "display" && action.substr(19) != "language" && action.substr(19) != "audio" &&
		    action.substr(19) != "session" && action.substr(19) != "controllers")
			return false;
		local_settings_tab_ = action.substr(19);
		if (local_settings_tab_ != "controllers" && app_->pairing_screen == screen_identity(*app_, route_))
			app_->pairing_screen.clear();
	}
	else if (action.starts_with("device."))
		return screen_device_action(*app_, route_, action);
	else if (action == "local-settings.smaller")
		text_scale_ = std::max(80, text_scale_ - 10);
	else if (action == "local-settings.larger")
		text_scale_ = std::min(150, text_scale_ + 10);
	else if (action == "local-settings.language")
	{
		const auto languages = installed_languages();
		auto it = std::find(languages.begin(), languages.end(), local_language_);
		local_language_ = it == languages.end() || ++it == languages.end() ? languages.front() : *it;
		signature_.clear();
	}
	else if (action.starts_with("local-settings.audio/"))
	{
		const auto channel = action.substr(21, action.find('/', 21) - 21);
		auto &value = channel == "master"  ? app_->master_volume
		              : channel == "music" ? app_->music_volume
		              : channel == "siren" ? app_->siren_volume
		                                   : app_->sfx_volume;
		value = std::clamp(value + (action.ends_with("/up") ? 5 : -5), 0, 100);
		remember_preferences(*app_);
	}
	else if (!action.starts_with("complexity/"))
		return model_.act(action);
	else
		return select_lobby_complexity(action.substr(11));
	return true;
}

std::optional<bool> ConsoleSurface::State::lobby_profile_action(const std::string &action)
{
	if (action == "profile" || action == "profile-save" || action.starts_with("avatar/"))
	{
		if (!app_->session->lobby().active || app_->session->lobby().launched)
			return false;
		if (action == "profile")
			profile_open_ = !profile_open_;
		else if (action == "profile-save")
		{
			if (!app_->session->set_local_player_profile(route_, profile_name_, profile_avatar_))
				return false;
			profile_open_ = false;
		}
		else
		{
			int choice = 0;
			const auto value = action.substr(7);
			if (std::from_chars(value.data(), value.data() + value.size(), choice).ec != std::errc{} || choice < 1 ||
			    choice > 12)
				return false;
			profile_avatar_ = read_file(asset_path("../menu/avatar-" + std::to_string(choice) + ".png"), 12288);
		}
		signature_.clear();
		return true;
	}
	if (action.starts_with("complexity/"))
		return select_lobby_complexity(action.substr(11));
	if (action == "ready")
	{
		const auto lobby = app_->session->lobby();
		if (!lobby.active || lobby.launched)
			return false;
		for (const auto &seat : lobby.consoles)
			if (seat.local_screen == route_)
			{
				LobbyCommand command{seat.ready ? LobbyAction::unready : LobbyAction::ready};
				command.screen = route_;
				return app_->session->lobby_command(command);
			}
		return false;
	}
	return std::nullopt;
}

std::optional<bool> ConsoleSurface::State::camera_navigation_action(const std::string &action)
{
	if ((action.starts_with("camera/") || action == "game.camera" || action == "game.camera-back") &&
	    (!game_active(*app_) || (route_ == 31 && app_->screen == Screen::settings)))
		return false;
	if (action == "game.manual")
	{
		if (!manual_toggle_ || !game_active(*app_) || (route_ == 31 && app_->screen == Screen::settings))
			return false;
		if (auto *toggle = dom_.element(id("manual")))
			return toggle->Focus();
		return false;
	}
	if (action == "game.camera")
	{
		for (const auto &[name, label] : controls_)
			if (name.find("/camera/") != std::string::npos)
				if (auto *choice = dom_.element(name))
				{
					choice->Focus();
					return true;
				}
		return false;
	}
	if (auto *focused = context_->GetFocusElement();
	    focused && (focused->GetId().find("/camera/") != std::string::npos || focused->GetId() == id("manual")))
	{
		if (action == "game.camera-back")
		{
			focused->Blur();
			return true;
		}
		if (action == "game.contacts")
		{
			context_->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
			context_->ProcessKeyUp(Rml::Input::KI_RETURN, 0);
			return true;
		}
		if (action == "game.left" || action == "game.right" || action == "game.faster" || action == "game.slower")
		{
			std::vector<std::string> choices;
			for (const auto &[name, label] : controls_)
				if (name.find("/camera/") != std::string::npos || name == id("manual"))
					choices.push_back(name);
			const auto current = std::find(choices.begin(), choices.end(), focused->GetId());
			if (current == choices.end() || choices.empty())
				return false;
			const int step = action == "game.left" || action == "game.faster" ? -1 : 1;
			const auto index = (static_cast<int>(current - choices.begin()) + static_cast<int>(choices.size()) + step) %
			                   choices.size();
			if (auto *choice = dom_.element(choices[index]))
				choice->Focus();
			return true;
		}
	}
	if (action.starts_with("camera/"))
	{
		if (owner_.lock() != app_->session || generation_ != app_->session->generation())
			return false;
		const auto split = action.rfind('/');
		if (split <= 7 || split + 2 != action.size() || action.back() < '0' || action.back() > '7')
			return false;
		const auto key = action.substr(7, split - 7);
		const auto *layout = model_.layout();
		if (!layout)
			return false;
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("id") == key &&
			    (widget.at("kind") == "camera_mode" || widget.at("kind") == "camera_direction"))
				return model_.command(widget.at("kind"), action.back() - '0');
		return false;
	}
	return std::nullopt;
}

bool ConsoleSurface::State::controller_hold(const std::string &name, bool pressed)
{
	if (!name.starts_with(id("")))
		return false;
	auto key = name.substr(id("").size());
	if (key.starts_with("task/"))
	{
		const auto mapped = task_actions_.find(key);
		if (mapped == task_actions_.end())
			return false;
		key = mapped->second;
	}
	for (const auto &[source, prefix] : task_sources())
		if (key.starts_with(prefix + "game.control/"))
			return pressed ? !settings_open_ && !systems_open_ && source->hold_control(key.substr(prefix.size()))
			               : source->release_control(key.substr(prefix.size()));
	return false;
}
std::string ConsoleSurface::State::controller_identity() const
{
	if (!model_.observation())
		return {};
	std::string identity = std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
	for (const auto &controller : model_.observation()->gameplay.value("controllers", Json::array()))
		identity += "/" + controller.at("id").get<std::string>() + "/" + controller.at("generation").dump() + "/" +
		            controller.at("human").dump();
	return identity;
}
bool ConsoleSurface::State::controller_flight() const
{
	return !settings_open_ && model_.layout() &&
	       std::any_of(model_.layout()->at("widgets").begin(), model_.layout()->at("widgets").end(),
	                   [](const auto &w)
	                   {
		                   return w.at("kind") == "throttle";
	                   });
}
Rml::Element *ConsoleSurface::State::controller_element(const std::string &name)
{
	return document_->GetElementById(name);
}
void ConsoleSurface::State::controller_cancel()
{
	cancel_pointer();
	model_.release_controls();
}
bool ConsoleSurface::State::controller_back()
{
	if (systems_open_ && (support_popup_ == "help" || support_popup_.starts_with("control/")))
		return action(id("systems-close"));
	if (settings_open_)
		return action(id("local-settings.close"));
	if (profile_open_)
		return action(id("profile"));
	return false;
}
void ConsoleSurface::State::ProcessEvent(Rml::Event &event)
{
	if (syncing_values_)
		return;
	render_dirty_ = true;
	if (event.GetType() != "mousemove" && event.GetType() != "mouseover" && event.GetType() != "mouseout")
		prepare_dirty_ = render_urgent_ = true;
	const auto name = event.GetTargetElement()->GetId();
	if (systems_open_ && support_popup_ == "help")
	{
		if (event.GetType() == "keydown")
		{
			const auto key = event.GetParameter<int>("key_identifier", 0);
			if (key == Rml::Input::KI_ESCAPE)
				action(id("systems-close"));
			else if (key == Rml::Input::KI_TAB)
				document_->GetElementById(id("systems-close"))->Focus();
			else if (key == Rml::Input::KI_DOWN || key == Rml::Input::KI_UP || key == Rml::Input::KI_NEXT ||
			         key == Rml::Input::KI_PRIOR || key == Rml::Input::KI_HOME || key == Rml::Input::KI_END)
			{
				auto *dialog = document_->GetElementById("fault-dialog");
				const float delta = key == Rml::Input::KI_DOWN    ? 44.f
				                    : key == Rml::Input::KI_UP    ? -44.f
				                    : key == Rml::Input::KI_PRIOR ? -dialog->GetClientHeight()
				                                                  : dialog->GetClientHeight();
				dialog->SetScrollTop(key == Rml::Input::KI_HOME  ? 0.f
				                     : key == Rml::Input::KI_END ? dialog->GetScrollHeight()
				                                                 : dialog->GetScrollTop() + delta);
			}
			else
				return;
			event.StopPropagation();
			return;
		}
		if (event.GetType() == "focus" && name != id("systems-close"))
			document_->GetElementById(id("systems-close"))->Focus();
		if (event.GetType() != "click")
			return;
	}
	if (event.GetType() == "keydown" && name == id("nav.map") && navigation_.visible())
	{
		if (!navigation_.key(event.GetParameter<int>("key_identifier", 0), dom_, id("")))
			return;
		event.StopPropagation();
		return;
	}
	if (event.GetType() == "mousescroll" && navigation_.visible() && !settings_open_)
	{
		const auto x = event.GetParameter<float>("mouse_x", 0), y = event.GetParameter<float>("mouse_y", 0);
		navigation_.wheel(x, y, event.GetParameter<float>("wheel_delta", 0));
		return;
	}
	if (event.GetType() == "mousedown" || event.GetType() == "touchstart")
	{
		if (controller_hold(name, true))
			held_clicks_.insert(name);
		return;
	}
	if (event.GetType() == "mouseup" || event.GetType() == "mouseout" || event.GetType() == "touchend" ||
	    event.GetType() == "touchcancel")
	{
		controller_hold(name, false);
		if (event.GetType() == "mouseout" || event.GetType() == "touchcancel")
			held_clicks_.erase(name);
		return;
	}
	if (event.GetType() == "click" && held_clicks_.erase(name))
		return;
	if (event.GetType() == "change")
	{
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(event.GetTargetElement()))
		{
			if (name.starts_with(id("speed/")))
			{
				double value = 0;
				const auto text = field->GetValue();
				if (std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{})
					set_value(name, std::to_string(-value));
			}
			else
				set_value(name, field->GetValue());
		}
		return;
	}
	if (event.GetType() != "click")
		return;
	if (app_ && app_->screen == Screen::server_lobby && name == id("ready"))
		invoke_navigation(*app_, name);
	else
	{
		if (app_ && app_->button_click)
		{
			app_->sound_route = route_;
			app_->button_click();
		}
		action(name);
	}
}
void ConsoleSurface::State::focus(const std::string &name)
{
	prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	if (auto *element = dom_.element(name))
	{
		element->ScrollIntoView({Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
		                         Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
		element->Focus(true);
	}
}
bool ConsoleSurface::State::set_value(const std::string &name, const std::string &text)
{
	if (name.starts_with(id("")))
		prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	if (name == id("comms/draft") && text.size() <= 4096)
	{
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(name)))
		{
			if (field->GetValue() != text)
				field->SetValue(text);
			return true;
		}
		return false;
	}
	if (name == id("nav.x") || name == id("nav.y"))
	{
		std::int64_t value = 0;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < -1000000000 ||
		    value > 1000000000)
			return false;
		navigation_.coordinate(name == id("nav.x"), static_cast<double>(value));
		return true;
	}
	if (settings_open_ || systems_open_ || !model_.status_system().empty() ||
	    (!tactical_.console().empty() && !tactical_.model().status_system().empty()))
		return false;
	if (app_ && profile_open_ && name == id("profile-name"))
	{
		profile_name_ = text;
		return true;
	}
	if (!app_ || !name.starts_with(id("")) || !game_active(*app_) || owner_.lock() != app_->session ||
	    generation_ != app_->session->generation())
		return false;
	double value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
		return false;
	const auto action = name.substr(id("").size());
	if (action == "support-condition")
	{
		const int number = int(std::lround(value));
		if (value != number || !condition_choices_.contains(number))
			return false;
		if (!this->action(id(condition_choices_.at(number))))
			return false;
		condition_pending_ = std::pair{number, app_->session->ticks() + 40};
		return true;
	}
	if (action.starts_with("power/") || action == "support-repair-priority")
		return support_value(name, text);
	if (model_.manual_active() && (action.starts_with("speed/") || action.starts_with("heading/")))
		return false;
	bool accepted = false;
	if (action.starts_with("speed/"))
		accepted = model_.set_speed_fraction(value / 100.0, model_.widget_system(action.substr(6)));
	else if (action.starts_with("heading/"))
		accepted = model_.set_heading_degrees(value, model_.widget_system(action.substr(8)));
	if (accepted)
	{
		app_->accessible_status = model_.announcement();
		if (action.starts_with("heading/"))
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(name));
			    field && field->GetValue() != text)
				field->SetValue(text);
	}
	return accepted;
}
void ConsoleSurface::State::flush_manual()
{
	if (!app_ || !app_->session || !model_.manual_active() || settings_open_)
		return;
	const auto axes = manual_input_.axes();
	if (axes == manual_sent_)
		return;
	const auto tick = app_->session->ticks();
	const bool releasing = (axes.yaw == 0 && manual_sent_.yaw != 0) || (axes.thrust == 0 && manual_sent_.thrust != 0);
	if (!releasing && axes != FlightAxes{} && manual_sent_tick_ == tick)
		return;
	if (model_.set_manual_axes(axes.yaw, axes.thrust))
	{
		manual_sent_ = axes;
		manual_sent_tick_ = tick;
	}
}
void ConsoleSurface::State::cancel_gamepad(const std::string &device)
{
	manual_input_.cancel_source("pad/" + device);
	model_.release_controls();
	flush_manual();
}
void ConsoleSurface::State::gamepad(const std::string &device, const std::string &control, float value)
{
	if (control == "flight.impulse")
	{
		if (value > 0 && !settings_open_ && !systems_open_)
			model_.flight_button("impulse", -1, true);
		return;
	}
	if (control == "flight.boost" || control == "flight.port" || control == "flight.starboard")
	{
		model_.flight_button(control == "flight.boost" ? "boost" : "lateral_thrust",
		                     control == "flight.port"        ? -1000
		                     : control == "flight.starboard" ? 1000
		                                                     : 1,
		                     value > 0 && !settings_open_ && !systems_open_);
		return;
	}
	auto &axes = pad_axes_[device];
	(control == "flight.yaw" ? axes.yaw : axes.thrust) = static_cast<int>(std::lround(value * 1000));
	manual_input_.sample("pad/" + device, axes);
	if (!model_.manual_active() || settings_open_ || systems_open_)
		manual_input_.cancel();
	// Both stick axes are sampled before the frame submits their combined value.
}
void ConsoleSurface::State::manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused)
{
	if (keyboard_axes.yaw || keyboard_axes.thrust)
		prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	const auto *keyboard_focus = context_->GetFocusElement();
	const bool keyboard_enabled = keyboard_selected && focused && !settings_open_ && !systems_open_ &&
	                              (!keyboard_focus || keyboard_focus->GetTagName() != "input");
	const std::array<int, 3> keys{KEY_Q, KEY_E, KEY_LEFT_SHIFT};
	if (keyboard_enabled && IsKeyPressed(KEY_I))
		model_.flight_button("impulse", -1, true);
	for (std::size_t i = 0; i < keys.size(); ++i)
	{
		const bool pressed = keyboard_enabled && (keyboard_flight_[i] ? IsKeyDown(keys[i]) : IsKeyPressed(keys[i]));
		if (pressed != keyboard_flight_[i])
			model_.flight_button(i == 2 ? "boost" : "lateral_thrust", i == 0 ? -1000 : i == 1 ? 1000 : 1, pressed);
		keyboard_flight_[i] = pressed;
	}
	if (!focused || settings_open_ || systems_open_)
		model_.release_controls();
	manual_input_.sample("keyboard", keyboard_axes);
	if (!keyboard_selected)
		manual_input_.cancel_source("keyboard");
	const auto *focus = context_->GetFocusElement();
	if (focus && focus->GetTagName() == "input")
		manual_input_.cancel_source("keyboard");
	const auto identity = controller_identity();
	const bool active = model_.manual_active();
	if (identity != manual_identity_ || active != manual_was_active_ || !active || settings_open_ || systems_open_ ||
	    !focused)
	{
		if (identity != manual_identity_ || !focused || settings_open_ || systems_open_)
			model_.release_controls();
		manual_input_.cancel();
		if (manual_was_active_ || manual_sent_ != FlightAxes{})
			model_.release_controls();
		manual_sent_ = {};
		joystick_contact_.reset();
		manual_pulse_tick_.reset();
	}
	manual_identity_ = identity;
	manual_was_active_ = active;
	if (manual_pulse_tick_ && app_->session->ticks() > *manual_pulse_tick_)
	{
		manual_input_.sample("accessible", {});
		manual_pulse_tick_.reset();
		flush_manual();
	}
	flush_manual();
}
void ConsoleSurface::State::prepare_manual(Application &app, const ConsoleRegion &region, bool pregame)
{
	auto *panel = document_->GetElementById("manual-flight");
	const bool available = !pregame && model_.manual_available();
	if (!(model_.layout() && model_.layout()->contains("layout")))
		dom_.property(panel, "display", available ? "block" : "none");
	const bool active = available && model_.manual_active();
	document_->SetClass("manual-flight", active);
	if (!available)
		return;
	std::vector<std::pair<std::string, std::string>> actions{{"manual-mode", "Manual flight"}};
	if (active)
		for (const auto &[name, label] :
		     std::vector<std::pair<std::string, std::string>>{{"manual-left", "Yaw left (pulse)"},
		                                                      {"manual-right", "Yaw right (pulse)"},
		                                                      {"manual-forward", "Forward thrust (pulse)"},
		                                                      {"manual-reverse", "Reverse thrust (pulse)"},
		                                                      {"manual-neutral", "Release flight controls"}})
			actions.emplace_back(name, label);
	std::string markup = "<button class=\"flight-mode\" id=\"" + id("manual-mode") + "\">" +
	                     Rml::StringUtilities::EncodeRml(localise(active ? "Manual flight" : "Assisted flight")) +
	                     "</button>";
	markup += "<p>" +
	          Rml::StringUtilities::EncodeRml(
	              localise("Q/E lateral thrust; Shift boost. Gamepad shoulders: lateral; west button: boost.")) +
	          "</p>";
	if (active)
	{
		markup += "<div id=\"manual-stick\"><div id=\"manual-knob\"/></div><p id=\"manual-values\"/><p>" +
		          Rml::StringUtilities::EncodeRml(
		              localise("W/S speed; A/D yaw. Left stick: yaw and speed. Release to brake.")) +
		          "</p>";
		for (std::size_t i = 1; i < actions.size(); ++i)
			markup += "<button id=\"" + id(actions[i].first) + "\">" +
			          Rml::StringUtilities::EncodeRml(localise(actions[i].second)) + "</button>";
	}

	if (manual_markup_ != markup)
	{
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		dom_.forget_properties();
		panel->SetInnerRML(markup);
		manual_markup_ = markup;
		if (auto *previous = document_->GetElementById(focused))
			previous->Focus();
	}
	context_->Update();
	for (const auto &[name, label] : actions)
	{
		app.screen_controls.emplace_back(id(name), localise(label));
		place(app, region, id(name));
	}
	app.screen_toggles[id("manual-mode")] = active;
	document_->GetElementById(id("manual-mode"))->SetAttribute("aria-pressed", active ? "true" : "false");
	joystick_area_ = {};
	if (active)
	{
		auto *stick = document_->GetElementById("manual-stick");
		const auto at = stick->GetAbsoluteOffset(Rml::BoxArea::Border);
		joystick_area_ = {at.x, at.y, stick->GetOffsetWidth(), stick->GetOffsetHeight()};
		const auto axes = manual_input_.axes();
		auto *knob = document_->GetElementById("manual-knob");
		dom_.property(knob, "left", std::to_string(50 - axes.yaw * .04f) + "%");
		dom_.property(knob, "top", std::to_string(50 - axes.thrust * .04f) + "%");
		const auto text = localise("Yaw") + " " + std::to_string(axes.yaw / 10) + "%; " + localise("Thrust") + " " +
		                  std::to_string(axes.thrust / 10) + "%";
		dom_.leaf_markup(document_->GetElementById("manual-values"), Rml::StringUtilities::EncodeRml(text));
		app.screen_readouts.emplace_back(id("manual-values"), text);
	}
}
std::optional<bool> ConsoleSurface::State::select_radar_at(float x, float y)
{
	if (!model_.layout() || !model_.observation())
		return std::nullopt;
	const auto &view = *model_.observation();
	for (const auto &widget : model_.layout()->at("widgets"))
	{
		if (widget.at("kind") != "radar")
			continue;
		const auto key = widget.at("id").get<std::string>();
		auto *dial = document_->GetElementById("dial/" + key);
		if (!dial)
			continue;
		const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
		const Vector2 centre{at.x + dial->GetOffsetWidth() / 2, at.y + dial->GetOffsetHeight() / 2};
		const auto radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .33f;
		const auto radar = select_system(view.systems, OperatingSystem::Radar, model_.widget_system(key), false);
		if (!radar)
			continue;
		for (const auto &sensor : view.systems)
		{
			if (sensor.id != *radar || !sensor.enabled)
				continue;
			EntityId nearest = 0;
			float distance = 18 * context_->GetDensityIndependentPixelRatio();
			for (const auto &body : view.contacts)
			{
				if (sensor.maximum <= 0 ||
				    std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) ==
				        sensor.contact_ids.end() ||
				    !radar_contact_in_range(view.own, body, sensor.maximum))
					continue;
				const auto point = radar_contact_point(view.own, body, sensor.maximum, centre, radius);
				const Vector2 body_centre{centre.x + float(double(body.x - view.own.x) / sensor.maximum) * radius,
				                          centre.y - float(double(body.y - view.own.y) / sensor.maximum) * radius};
				const auto size = float(double(body.radius) / sensor.maximum) * radius;
				const float delta = size > 12
				                        ? std::min(std::hypot(x - point.x, y - point.y),
				                                   std::abs(std::hypot(x - body_centre.x, y - body_centre.y) - size))
				                        : std::hypot(x - point.x, y - point.y);
				if (delta < distance)
				{
					distance = delta;
					nearest = body.id;
				}
			}
			if (nearest)
				return select_radar_contact(nearest);
			for (const auto &bearing : view.bearing_contacts)
			{
				if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), bearing.id) ==
				    sensor.contact_ids.end())
					continue;
				if (CheckCollisionPointCircle({x, y}, bearing_ring_point(centre, radius, bearing.bearing_mdeg), 14))
					return select_radar_contact(bearing.id);
			}
		}
	}
	return std::nullopt;
}
bool ConsoleSurface::State::pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
{
	render_dirty_ = true;
	if (kind != DeviceEventKind::Move || heading_contacts_.contains(contact) || throttle_contacts_.contains(contact) ||
	    navigation_.captures(contact) || joystick_contact_ == contact)
		prepare_dirty_ = true;
	if (settings_open_ || systems_open_ || !model_.status_system().empty() ||
	    (!tactical_.console().empty() && !tactical_.model().status_system().empty()))
		return false;
	if (kind == DeviceEventKind::Down)
		for (auto *hit = context_->GetElementAtPoint({x, y}); hit; hit = hit->GetParentNode())
			if (hit->GetTagName() == "button")
				return false;
	if (!app_ || !game_active(*app_) || owner_.lock() != app_->session || generation_ != app_->session->generation())
		return false;
	if (const auto held = sharing_contacts_.find(contact); held != sharing_contacts_.end())
	{
		const auto key = held->second;
		if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
		{
			sharing_contacts_.erase(held);
			if (kind == DeviceEventKind::Up && sharing_hit(key, x, y))
				action(id("on-screen/" + key));
		}
		return true;
	}
	if (kind == DeviceEventKind::Down)
		for (const auto &[key, dial] : sharing_pods_)
			if (sharing_hit(key, x, y))
			{
				sharing_contacts_[contact] = key;
				if (auto *b = document_->GetElementById(id("on-screen/" + key)))
					b->Focus();
				return true;
			}
	if (kind == DeviceEventKind::Down && model_.layout())
		for (const auto &w : model_.layout()->at("widgets"))
			if (w.at("kind") == "radar" && !sharing_pods_.contains(w.at("id").get<std::string>()))
				if (auto *b = document_->GetElementById(id("on-screen/" + w.at("id").get<std::string>())))
				{
					const auto at = b->GetAbsoluteOffset(Rml::BoxArea::Border);
					if (CheckCollisionPointRec({x, y}, {at.x, at.y, b->GetOffsetWidth(), b->GetOffsetHeight()}))
						return false;
				}
	if (kind == DeviceEventKind::Down)
		if (const auto selected = select_radar_at(x, y))
			return *selected;
	if (const auto result = navigation_.pointer(model_, contact, x, y, kind); result.handled)
	{
		if (result.changed)
			prepare_dirty_ = render_dirty_ = true;
		return true;
	}
	if (model_.manual_active())
	{
		if (kind == DeviceEventKind::Down && !joystick_contact_ && CheckCollisionPointRec({x, y}, joystick_area_))
			joystick_contact_ = contact;
		if (joystick_contact_ == contact)
		{
			if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			{
				manual_input_.sample("joystick", {});
				joystick_contact_.reset();
				flush_manual();
			}
			else
			{
				const auto axis = [](float value)
				{
					return static_cast<int>(std::lround(std::clamp(value, -1.f, 1.f) * 1000));
				};
				manual_input_.sample(
				    "joystick",
				    {axis((joystick_area_.x + joystick_area_.width / 2 - x) / (joystick_area_.width / 2)),
				     axis((joystick_area_.y + joystick_area_.height / 2 - y) / (joystick_area_.height / 2))});
				flush_manual();
			}
			return true;
		}
		return false;
	}
	if (kind == DeviceEventKind::Down)
		for (const auto &[key, track] : throttle_tracks_)
			if (CheckCollisionPointRec({x, y}, {track.x - track.width * .2f, track.y - track.width * .2f,
			                                    track.width * 1.4f, track.height + track.width * .4f}))
			{
				if (std::any_of(throttle_contacts_.begin(), throttle_contacts_.end(),
				                [&](const auto &held)
				                {
					                return held.second == key && held.first != contact;
				                }))
					return true;
				throttle_contacts_[contact] = key;
				break;
			}
	if (const auto found = throttle_contacts_.find(contact); found != throttle_contacts_.end())
	{
		const auto key = found->second;
		if (kind != DeviceEventKind::Cancel && throttle_tracks_.contains(key))
		{
			const auto track = throttle_tracks_.at(key);
			const double fraction =
			    throttles_.detent(model_, key, std::clamp(double(track.y + track.height - y) / track.height, 0.0, 1.0));
			const auto system = model_.widget_system(key);
			throttles_.track(model_, key, fraction, GetTime());
			const auto requested = throttles_.request(model_, key);
			if (throttles_.differs(key, requested) && (kind == DeviceEventKind::Down || kind == DeviceEventKind::Up ||
			                                           GetTime() - throttle_sent_at_[key] >= .05))
				if (model_.set_speed_fraction(fraction, system))
				{
					throttle_sent_at_[key] = GetTime();
					throttles_.sent(key, requested);
				}
		}
		if (kind == DeviceEventKind::Cancel)
			throttles_.erase(key);
		if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			throttle_contacts_.erase(contact);
		return true;
	}
	if (kind == DeviceEventKind::Down)
	{
		for (const auto &[key, dial] : heading_dials_)
		{
			const auto [cx, cy, radius] = dial;
			const auto length = std::hypot(x - cx, cy - y);
			if (length >= radius * .69f && length <= radius)
			{
				heading_contacts_[contact] = key;
				break;
			}
		}
		if (!heading_contacts_.contains(contact))
			return false;
	}
	if (!heading_contacts_.contains(contact))
		return false;
	const auto key = heading_contacts_.at(contact);
	const auto [cx, cy, radius] = heading_dials_.at(key);
	const float dx = x - cx, dy = cy - y, length = std::hypot(dx, dy);
	if (kind != DeviceEventKind::Cancel && length > 1)
	{
		const double degrees = std::fmod(std::atan2(dy, dx) * 180.0 / 3.141592653589793 + 360.0, 360.0);
		const auto &system = dial_systems_.at(key);
		const auto requested = static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000;
		if (requested != model_.system_request(system, "heading_mdeg") &&
		    (kind == DeviceEventKind::Down || kind == DeviceEventKind::Up || GetTime() - heading_sent_at_[key] >= .05))
		{
			if (model_.set_heading_degrees(degrees, system))
				heading_sent_at_[key] = GetTime();
			app_->accessible_status = model_.announcement();
		}
	}
	if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
		heading_contacts_.erase(contact);
	return true;
}
void ConsoleSurface::State::cancel_pointer()
{
	manual_input_.cancel();
	manual_sent_ = {};
	joystick_contact_.reset();
	manual_pulse_tick_.reset();
	navigation_.cancel();
	heading_contacts_.clear();
	sharing_contacts_.clear();
	throttle_contacts_.clear();
	throttles_.clear();
	model_.release_controls();
	held_clicks_.clear();
}
} // namespace phoenix
