module;
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(int, phoenix_display_preferences, (), {
    let p={}; try { p=JSON.parse(localStorage.getItem('phoenix-display-preferences-v1')||'{}')||{}; } catch {}
    return (p.dust_motes!==false?1:0)|(p.reference_grid!==false?2:0)|
        ((typeof p.reduced_motion==='boolean'?p.reduced_motion:matchMedia('(prefers-reduced-motion: reduce)').matches)?4:0);
});
EM_JS(void, phoenix_store_display_preferences, (int bits), {
    try { localStorage.setItem('phoenix-display-preferences-v1',JSON.stringify({dust_motes:!!(bits&1),reference_grid:!!(bits&2),reduced_motion:!!(bits&4)})); } catch {}
});
EM_JS(void, phoenix_persist_preferences, (), { Module.phoenixPersistMenu?.().catch(() => {}); });
// clang-format on
#endif
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

namespace phoenix
{
namespace
{
Json translations = Json::object();
std::string language = "en";
bool pseudo_language = false;
std::uint64_t translation_revision = 0;
} // namespace
void configure_translations(const Json &catalog)
{
	if (catalog.at("schema") != 1 || !catalog.at("languages").is_object() || catalog.at("languages").size() > 64 ||
	    !catalog.at("languages").contains("en"))
		throw std::runtime_error("Invalid translation catalog");
	for (const auto &[locale, labels] : catalog.at("languages").items())
	{
		if (locale.empty() || locale.size() > 32 || !labels.is_object() || labels.size() > 4096)
			throw std::runtime_error("Invalid language resource");
		for (const auto &[key, text] : labels.items())
			if (key.empty() || key.size() > 256 || !text.is_string() ||
			    text.get_ref<const std::string &>().size() > 4096)
				throw std::runtime_error("Invalid translated label");
	}
	translations = catalog.at("languages");
	++translation_revision;
}
void select_language(const std::string &locale, bool pseudo)
{
	if (locale != "en" && !translations.contains(locale))
		throw std::runtime_error("Language is not installed: " + locale);
	language = locale;
	pseudo_language = pseudo;
}
std::string translate(const std::string &key, bool pseudo)
{
	if (key.empty())
		return {};
	struct Cache
	{
		std::string locale;
		bool pseudo = false;
		std::uint64_t revision = 0;
		std::unordered_map<std::string, std::string> labels;
	};
	thread_local Cache cache;
	if (cache.locale != language || cache.pseudo != pseudo || cache.revision != translation_revision)
	{
		cache.labels.clear();
		cache.locale = language;
		cache.pseudo = pseudo;
		cache.revision = translation_revision;
	}
	const bool cacheable = key.size() <= 1024;
	if (cacheable)
		if (const auto found = cache.labels.find(key); found != cache.labels.end())
			return found->second;

	static constexpr std::pair<const char *, const char *> labels[] = {
	    {"home", "Home"},         {"host", "New game"}, {"join", "Join console"},
	    {"view", "Viewscreen"},   {"edit", "Workshop"}, {"locale", "Toggle pseudo-localisation"},
	    {"settings", "Settings"}, {"lobby", "Lobby"}};
	std::string result = key;
	for (const auto &[id, text] : labels)
		if (key == id)
			result = text;
	if (translations.contains(language))
	{
		const auto &labels = translations.at(language);
		if (labels.contains(key))
			result = labels.at(key);
		else if (labels.contains(result))
			result = labels.at(result);
	}
	if (pseudo)
		result = "[ " + result + " ~~~ ]";
	if (cacheable)
	{
		if (cache.labels.size() >= 4096)
			cache.labels.clear();
		cache.labels.emplace(key, result);
	}
	return result;
}
std::string localise(const std::string &key)
{
	return translate(key, pseudo_language);
}
std::vector<std::string> installed_languages()
{
	std::vector<std::string> result;
	for (const auto &[id, labels] : translations.items())
		result.push_back(id);
	if (result.empty())
		result.push_back("en");
	return result;
}
void remember_preferences(Application &app)
{
#ifdef __EMSCRIPTEN__
	if (!app.library)
		phoenix_store_display_preferences((app.dust_motes ? 1 : 0) | (app.reference_grid ? 2 : 0) |
		                                  (app.reduced_motion ? 4 : 0));
#endif
	if (!app.library)
		return;
	try
	{
		app.library->preferences = {app.music_volume, app.language,       app.pseudo_locale, app.reduced_motion,
		                            app.dust_motes,   app.reference_grid, app.player_name,   app.avatar};
		app.library->preferences.sfx_volume = app.sfx_volume;
		app.library->preferences.siren_volume = app.siren_volume;
		app.library->preferences.master_volume = app.master_volume;
		app.library->preferences.tools_scale = app.tools_scale;
		app.library->preferences.station_displays = app.remembered_station_displays;
		app.library->remember();
#ifdef __EMSCRIPTEN__
		phoenix_persist_preferences();
#endif
	}
	catch (const std::exception &error)
	{
		app.status = error.what();
	}
}
void remember_station_display(Application &app, const std::string &console, int monitor)
{
	if (!app.session)
		return;
	const auto lobby = app.session->lobby();
	std::string own_slot;
	for (const auto &member : lobby.members)
		if (member.id == lobby.self)
			own_slot = member.slot;
	for (const auto &seat : lobby.consoles)
		if (seat.id == console && seat.slot == own_slot)
			for (const auto &slot : lobby.slots)
				if (slot.id == seat.slot)
				{
					auto &saved = app.remembered_station_displays[slot.ship];
					if (monitor < 0)
						saved.erase(console);
					else
						for (const auto &display : app.native_displays)
							if (display.id == monitor)
							{
								const auto key = display.identity.empty() ? display.name : display.identity;
								std::erase_if(saved,
								              [&](const auto &entry)
								              {
									              return entry.second == key;
								              });
								saved[console] = key;
							}
					remember_preferences(app);
					return;
				}
}
void restore_station_displays(Application &app)
{
	if (!app.can_host || !app.session || app.screen != Screen::server_lobby ||
	    !app.session->preparation_phase().empty())
		return;
	const auto lobby = app.session->lobby();
	if (!lobby.active || lobby.launched)
		return;
	for (const auto &member : lobby.members)
		if (member.id == lobby.self)
			for (const auto &slot : lobby.slots)
				if (slot.id == member.slot)
				{
					if (app.restored_display_session.lock() == app.session && app.restored_display_ship == slot.ship)
						return;
					app.restored_display_session = app.session;
					app.restored_display_ship = slot.ship;
					if (!app.remembered_station_displays.contains(slot.ship))
						return;
					bool changed = false;
					for (const auto &[console, identity] : app.remembered_station_displays.at(slot.ship))
						for (const auto &display : app.native_displays)
							if (display.id != app.host_display && display.id != app.inspector_display &&
							    (display.identity.empty() ? display.name : display.identity) == identity &&
							    !app.display_consoles.contains(display.id))
							{
								LobbyCommand command{LobbyAction::assign_local_console, "", slot.id, "", console};
								command.screen = display.id + 2;
								if (app.session->lobby_command(command))
								{
									app.display_consoles[display.id] = console;
									changed = true;
								}
							}
					if (changed && app.apply_displays)
						app.apply_displays();
					return;
				}
}
void load_display_preferences(Application &app)
{
#ifdef __EMSCRIPTEN__
	if (!app.library)
	{
		const int bits = phoenix_display_preferences();
		app.dust_motes = (bits & 1) != 0;
		app.reference_grid = (bits & 2) != 0;
		app.reduced_motion = (bits & 4) != 0;
	}
#endif
}
bool ai_configuration_available(const Application &app)
{
	return app.can_host && app.session && app.session->inference_state().value("configured", false);
}
std::vector<std::pair<std::string, std::string>> inference_controls(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> controls;
	if (!app.can_host || !app.session)
		return controls;
	controls.emplace_back("ai.pair", "Pair inference bridge");
	const auto state = app.session->inference_state();
	for (const auto &row : state.value("controllers", Json::array()))
	{
		if (!row.at("eligible").get<bool>())
			continue;
		auto providers = state.value("providers", Json::array());
		providers.push_back("tree");
		for (const auto &provider : providers)
		{
			if (provider != "tree" && !row.at("eligible").get<bool>())
				continue;
			const auto console = row.at("console").get<std::string>(),
			           controller = row.at("controller").get<std::string>(), p = provider.get<std::string>();
			controls.emplace_back("ai.select/" + console + "/" + controller + "/" + p,
			                      console + " / " + controller + ": " + (p == "tree" ? "State tree" : p) +
			                          (row.at("selected") == p ? " (selected)" : ""));
		}
	}
	return controls;
}
std::vector<std::pair<std::string, std::string>> inference_readouts(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> rows;
	if (!app.can_host || !app.session)
		return rows;
	const auto state = app.session->inference_state();
	rows.emplace_back("ai.health",
	                  state.value("online", false) ? "Inference bridge connected" : "Inference bridge offline");
	rows.emplace_back("ai.latency",
	                  "Last inference: " + std::to_string(state.value("latency_ms", std::uint64_t{0})) + " ms");
	if (state.contains("pair") && !state.value("paired", false))
		rows.emplace_back("ai.pair-code", "Pairing token: " + state.at("pair").get<std::string>());
	for (const auto &row : state.value("controllers", Json::array()))
		rows.emplace_back(
		    "ai.status/" + row.at("console").get<std::string>() + "/" + row.at("controller").get<std::string>(),
		    row.at("controller").get<std::string>() + ": " +
		        (row.value("human", false) ? std::string("Human operator") : row.at("effective").get<std::string>()) +
		        "; " + row.at("reason").get<std::string>() +
		        (row.at("eligible").get<bool>() ? "" : "; no model candidates authored"));
	return rows;
}
std::vector<std::pair<std::string, std::string>> settings_tabs(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> tabs{{"settings.tab/audio", "Audio"}};
	if (app.native_menu)
	{
		tabs.emplace_back("settings.tab/profile", "Profile");
		tabs.emplace_back("settings.tab/language", "Language");
	}
	tabs.emplace_back("settings.tab/display", "Display");
	if (app.can_host && app.session)
		tabs.emplace_back("settings.tab/ai", "AI configuration");
	if (game_active(app) || (app.session && app.session->lobby().active))
		tabs.emplace_back("settings.tab/session", "Session");
	return tabs;
}
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
		app.screen = app.settings_return;
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
bool set_music_volume(Application &app, const std::string &value)
{
	return set_audio_volume(app, "music", value);
}
bool set_audio_volume(Application &app, const std::string &channel, const std::string &value)
{
	if ((!app.can_host && !app.native_menu && !game_active(app)) || app.screen != Screen::settings ||
	    app.settings_tab != "audio")
		return false;
	int *volume = channel == "music"    ? &app.music_volume
	              : channel == "siren"  ? &app.siren_volume
	              : channel == "sfx"    ? &app.sfx_volume
	              : channel == "master" ? &app.master_volume
	                                    : nullptr;
	if (!volume)
		return false;
	float percent = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), percent);
	if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(percent) ||
	    percent < 0 || percent > 100)
		return false;
	const auto changed = static_cast<int>(std::lround(percent));
	if (changed != *volume)
	{
		*volume = changed;
		remember_preferences(app);
	}
	return true;
}
} // namespace phoenix
