module;
#include <map>
#include <string>
#include <vector>

export module phoenix.application.preferences;
export import phoenix.data;

export namespace phoenix
{
struct ScreenDevices
{
	std::string audio_output = "default";
	std::string gamepad_serial, gamepad_path, gamepad_model, gamepad_name;
	std::string audio_name;
};
struct MenuPreferences
{
	int volume = 50;
	std::string language = "en";
	bool pseudo = false, reduced_motion = false;
	bool dust_motes = true, reference_grid = true;
	std::string player_name = "<username>";
	Bytes avatar;
	int sfx_volume = 50, master_volume = 50;
	int siren_volume = 50;
	int tools_scale = 100;
	std::map<std::string, std::map<std::string, std::string>> station_displays;
	std::map<std::string, ScreenDevices> screen_devices;
};
// Shared localisation resources. This interface owns no Application, session,
// renderer or input state; all presentation consumers use the same catalogue.
void configure_translations(const Json &catalog);
void select_language(const std::string &language, bool pseudo = false);
std::string translate(const std::string &key, bool pseudo);
std::string localise(const std::string &key);
std::vector<std::string> installed_languages();
} // namespace phoenix
