module;
#include <string>
#include <utility>
#include <vector>

export module phoenix.application.preference_store;
export import phoenix.application;
export import phoenix.application.preferences;

export namespace phoenix
{
void remember_preferences(Application &app);
void load_display_preferences(Application &app);
bool set_music_volume(Application &app, const std::string &value);
bool set_audio_volume(Application &app, const std::string &channel, const std::string &value);
std::vector<std::pair<std::string, std::string>> settings_tabs(const Application &app);
} // namespace phoenix
