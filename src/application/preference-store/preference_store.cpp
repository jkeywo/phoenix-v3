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

module phoenix.application.preference_store;

namespace phoenix
{
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
		app.library->preferences.screen_devices = app.screen_devices;
		app.library->remember();
		app.preference_error.clear();
#ifdef __EMSCRIPTEN__
		phoenix_persist_preferences();
#endif
	}
	catch (const std::exception &error)
	{
		app.status = error.what();
		app.preference_error = error.what();
		app.accessible_status = error.what();
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

std::vector<std::pair<std::string, std::string>> settings_tabs(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> tabs{{"settings.tab/audio", "Audio"}};
#ifndef __EMSCRIPTEN__
	tabs.emplace_back("settings.tab/controllers", "Controllers");
#endif
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
} // namespace phoenix
