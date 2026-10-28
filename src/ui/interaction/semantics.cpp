module;
#include <array>
#include <cstdint>
#include <unordered_map>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <vector>

module phoenix.navigation;

import phoenix.application.preferences;

namespace phoenix
{

std::vector<SemanticControl> navigation_controls(const Application &app, bool include_consoles)
{
	std::vector<SemanticControl> controls;
	for (const char *id : {"home", "host", "join", "view", "edit", "locale", "settings", "ai.config", "lobby"})
	{
		if (app.console_surface_active)
			break;
		if (game_active(app) && app.screen == Screen::settings)
			break;
		if (game_active(app) && std::string(id) != "settings" && std::string(id) != "ai.config")
			continue;
		if (app.native_menu)
		{
			if (app.screen == Screen::home || app.screen == Screen::server_lobby ||
			    app.screen == Screen::client_lobby || app.screen == Screen::settings || app.packs_popup)
				break;
			if (std::string(id) != "settings" && std::string(id) != "ai.config")
				continue;
		}
		if (std::string(id) == "lobby" && !app.native_menu)
			continue;
		if (std::string(id) == "host" && !app.can_host)
			continue;
		if (std::string(id) == "edit" && !app.can_edit)
			continue;
		if (std::string(id) == "settings" && !app.can_host && !app.native_menu && !game_active(app))
			continue;
		if (std::string(id) == "ai.config" && !ai_configuration_available(app))
			continue;
		controls.push_back(
		    {id, translate(std::string(id) == "ai.config" ? "AI configuration" : id, app.pseudo_locale)});
	}
	for (const auto &[id, label] : app.screen_controls)
		if (include_consoles || !id.starts_with("console/"))
			controls.push_back({id, translate(label, app.pseudo_locale)});
	for (const auto &[id, label] : app.screen_readouts)
		if (include_consoles || !id.starts_with("console/"))
			controls.push_back({id, label, {}, false, {}, false, true});
	for (const auto &input : app.screen_inputs)
		if (include_consoles || !input.id.starts_with("console/"))
			controls.push_back(
			    {input.id, translate(input.label, app.pseudo_locale), {}, true, input.value, input.percentage});
	for (auto &control : controls)
	{
		control.numeric = control.percentage || control.id.find("/heading/") != std::string::npos ||
		                  control.id.ends_with("/nav.x") || control.id.ends_with("/nav.y");
		if (control.id.find("/heading/") != std::string::npos)
			control.maximum = 360;
		if (control.id.ends_with("/nav.x") || control.id.ends_with("/nav.y"))
		{
			control.minimum = -1e9;
			control.maximum = 1e9;
		}
		if (const auto found = app.screen_toggles.find(control.id); found != app.screen_toggles.end())
		{
			control.toggle = true;
			control.pressed = found->second;
		}
		if (control.id.find("/layout-page/") != std::string::npos ||
		    control.id.find("/game.console/") != std::string::npos ||
		    control.id.find("/station-page/") != std::string::npos)
		{
			control.tab = true;
			control.selected = app.screen_toggles.contains(control.id) && app.screen_toggles.at(control.id);
		}
		if (control.id.starts_with("settings.tab/"))
		{
			control.tab = true;
			control.selected = control.id.substr(13) == app.settings_tab;
		}
		if (auto found = app.control_placements.find(control.id); found != app.control_placements.end())
			control.placement = found->second;
	}
	return controls;
}
bool invoke_navigation(Application &app, const std::string &id)
{
	app.sound_route = 0;
	if (id.starts_with("console/"))
	{
		const auto end = id.find('/', 8);
		if (end != std::string::npos)
			std::from_chars(id.data() + 8, id.data() + end, app.sound_route);
	}
	if (id.starts_with("device.") && app.screen == Screen::settings)
	{
		for (const auto &[control, label] : screen_device_controls(app, 0, app.settings_tab))
			if (control == id)
				return screen_device_action(app, 0, id);
		return false;
	}
	if (app.screen == Screen::settings)
	{
		const std::string page = id == "menu.avatar" || id == "menu.profile"    ? "profile"
		                         : id == "menu.language" || id == "menu.pseudo" ? "language"
		                         : id == "menu.motion" || id == "menu.dust" || id == "menu.grid" ||
		                                 id == "settings.motion" || id == "settings.dust" || id == "settings.grid"
		                             ? "display"
		                         : id == "menu.save-game" || id == "menu.export-save" ||
		                                 id.starts_with("lobby.approve/") || id.starts_with("network.approve/") ||
		                                 id.starts_with("network.gm/")
		                             ? "session"
		                             : "";
		if (!page.empty() && page != app.settings_tab)
			return false;
	}
	if (app.button_click)
		for (const auto &control : navigation_controls(app))
			if (control.id == id && !control.editable && !control.read_only && control.placement.enabled)
			{
				app.button_click();
				break;
			}
	if (id == "ai.config")
	{
		if (!ai_configuration_available(app))
			return false;
		app.settings_return = app.screen;
		app.screen = Screen::settings;
		app.settings_tab = "ai";
		return true;
	}
	if (id.starts_with("ai."))
	{
		if (!app.can_host || !app.session || app.screen != Screen::settings || app.settings_tab != "ai")
			return false;
		if (id == "ai.pair")
			return app.session->inference_command({{"operation", "pair"}});
		for (const auto &[control, label] : inference_controls(app))
			if (control == id && id.starts_with("ai.select/"))
			{
				const auto a = id.find('/', 10), b = a == std::string::npos ? a : id.find('/', a + 1);
				if (b == std::string::npos)
					return false;
				return app.session->inference_command({{"console", id.substr(10, a - 10)},
				                                       {"controller", id.substr(a + 1, b - a - 1)},
				                                       {"provider", id.substr(b + 1)}});
			}
		return false;
	}
	const bool active = game_active(app);
	// Reject stale UI/accessibility actions as well as hiding navigation during play.
	if (active &&
	    (id == "home" || id == "host" || id == "join" || id == "view" || id == "edit" || id == "lobby" ||
	     id == "locale" || id == "game.console" || id == "game.scene" || id == "game.follow" || id == "game.local-gm"))
		return false;
	const bool settings_available = app.can_host || app.native_menu || active;
	if (id.starts_with("settings.tab/"))
	{
		if (app.screen != Screen::settings || !settings_available)
			return false;
		for (const auto &[tab, label] : settings_tabs(app))
			if (tab == id)
			{
				app.settings_tab = id.substr(13);
				if (app.settings_tab != "controllers" && app.pairing_screen == screen_identity(app, 0))
					app.pairing_screen.clear();
				if (app.focus_control)
					app.focus_control(id);
				return true;
			}
		return false;
	}
	if (id == "settings.quit")
	{
#ifndef __EMSCRIPTEN__
		if (app.settings_return == Screen::console)
			return false;
#endif
		if (app.screen != Screen::settings || app.settings_tab != "session" || !active || !app.return_home)
			return false;
		app.return_home();
		return true;
	}
	if ((id == "settings.dust" || id == "settings.grid" || id == "settings.motion") && app.screen == Screen::settings &&
	    settings_available && app.settings_tab == "display")
	{
		if (id == "settings.dust")
			app.dust_motes = !app.dust_motes;
		else if (id == "settings.grid")
			app.reference_grid = !app.reference_grid;
		else
			app.reduced_motion = !app.reduced_motion;
		remember_preferences(app);
	}
	else if (id == "settings" && settings_available)
	{
		if (app.screen == Screen::settings)
			app.screen = app.settings_return;
		else
		{
			app.settings_return = app.screen;
			app.screen = Screen::settings;
		}
	}
	else if (id == "settings.close" && app.screen == Screen::settings && settings_available)
	{
		if (app.pairing_screen == screen_identity(app, 0))
			app.pairing_screen.clear();
		app.screen = app.settings_return;
	}
	else if (id == "lobby" && app.native_menu)
		app.screen = app.can_host ? Screen::server_lobby : Screen::client_lobby;
	else if (id == "home")
		app.screen = Screen::home;
	else if (id == "host" && app.can_host)
		app.screen = Screen::server_lobby;
	else if (id == "join")
		app.screen = Screen::client_lobby;
	else if (id == "view")
		app.screen = Screen::viewscreen;
	else if (id == "edit" && app.can_edit)
		app.screen = Screen::workshop;
	else if (id == "locale")
		app.pseudo_locale = !app.pseudo_locale;
	else if (app.screen_action)
		return app.screen_action(id);
	else
		return false;
	return true;
}

} // namespace phoenix
