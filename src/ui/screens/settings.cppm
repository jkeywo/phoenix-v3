module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.settings;

export import phoenix.navigation;
export namespace phoenix::settings_ui
{
struct Audio
{
	const char *channel, *menu_id, *label;
	int Application::*volume;
	inline std::string id(bool menu) const
	{
		return menu ? menu_id : std::string(channel) + "-volume";
	}
	inline std::string label_id(bool menu) const
	{
		return menu ? id(true) + "-label" : std::string(channel) + "-label";
	}
	inline std::string readout_id(bool menu) const
	{
		return menu ? id(true) + "-value" : std::string(channel) + "-percent";
	}
};
inline constexpr Audio audio[] = {{"master", "menu-master", "Master volume", &Application::master_volume},
                                  {"music", "menu-volume", "Music volume", &Application::music_volume},
                                  {"siren", "menu-siren", "Siren volume", &Application::siren_volume},
                                  {"sfx", "menu-sfx", "SFX volume", &Application::sfx_volume}};
const char *audio_channel(const std::string &id);
std::vector<std::pair<std::string, std::string>> tabs(Application &app, std::string *focus = nullptr);
std::string audio_markup(bool menu);
void register_audio(Application &app, bool menu);
void refresh_audio(const Application &app, Rml::ElementDocument &document, bool menu);
std::vector<std::pair<std::string, std::string>> display_controls(const Application &app);
bool approval(const std::string &id);
bool session_action(Application &app, const std::string &id);
std::vector<std::pair<std::string, std::string>> approvals(const Application &app);
} // namespace phoenix::settings_ui
