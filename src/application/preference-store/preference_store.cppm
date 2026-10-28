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
std::string screen_identity(const Application &, ConsoleId route);
std::string screen_audio_output(const Application &, ConsoleId route);
void restore_screen_gamepads(Application &);
bool assign_screen_gamepad(Application &, ConsoleId route, const std::string &device);
bool screen_gamepad_event(Application &, const std::string &device, const std::string &control, float value);
std::vector<std::pair<std::string, std::string>> screen_device_controls(const Application &, ConsoleId,
                                                                        const std::string &tab);
bool screen_device_action(Application &, ConsoleId, const std::string &action);
} // namespace phoenix
