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
// clang-format on
#endif
#include <array>
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
		return "[ " + result + " ~~~ ]";
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
		app.library->preferences = {app.music_volume,   app.language,   app.pseudo_locale,
		                            app.reduced_motion, app.dust_motes, app.reference_grid};
		app.library->remember();
	}
	catch (const std::exception &error)
	{
		app.status = error.what();
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
std::vector<SemanticControl> navigation_controls(const Application &app)
{
	std::vector<SemanticControl> controls;
	for (const char *id : {"home", "host", "join", "view", "edit", "locale", "settings", "lobby"})
	{
		if (app.console_surface_active)
			break;
		if (game_active(app) && app.screen == Screen::settings)
			break;
		if (game_active(app) && std::string(id) != "settings")
			continue;
		if (app.native_menu)
		{
			if (app.screen == Screen::home || app.screen == Screen::server_lobby ||
			    app.screen == Screen::client_lobby || app.screen == Screen::settings || app.packs_popup)
				break;
			if (std::string(id) != "settings")
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
		controls.push_back({id, translate(id, app.pseudo_locale)});
	}
	for (const auto &[id, label] : app.screen_controls)
		controls.push_back({id, translate(label, app.pseudo_locale)});
	for (const auto &[id, label] : app.screen_readouts)
		controls.push_back({id, label, {}, false, {}, false, true});
	for (const auto &input : app.screen_inputs)
		controls.push_back(
		    {input.id, translate(input.label, app.pseudo_locale), {}, true, input.value, input.percentage});
	for (auto &control : controls)
		if (auto found = app.control_placements.find(control.id); found != app.control_placements.end())
			control.placement = found->second;
	return controls;
}
bool invoke_navigation(Application &app, const std::string &id)
{
	const bool active = game_active(app);
	// Reject stale UI/accessibility actions as well as hiding navigation during play.
	if (active &&
	    (id == "home" || id == "host" || id == "join" || id == "view" || id == "edit" || id == "lobby" ||
	     id == "locale" || id == "game.console" || id == "game.scene" || id == "game.follow" || id == "game.local-gm"))
		return false;
	const bool settings_available = app.can_host || app.native_menu || active;
	if (id == "settings.quit")
	{
		if (app.screen != Screen::settings || !active || !app.return_home)
			return false;
		app.return_home();
		return true;
	}
	if ((id == "settings.dust" || id == "settings.grid" || id == "settings.motion") && app.screen == Screen::settings &&
	    settings_available)
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
	if ((!app.can_host && !app.native_menu && !game_active(app)) || app.screen != Screen::settings)
		return false;
	float percent = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), percent);
	if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(percent) ||
	    percent < 0 || percent > 100)
		return false;
	const auto changed = static_cast<int>(std::lround(percent));
	if (changed != app.music_volume)
	{
		app.music_volume = changed;
		remember_preferences(app);
	}
	return true;
}
} // namespace phoenix
