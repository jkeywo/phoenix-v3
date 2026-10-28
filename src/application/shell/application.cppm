module;
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <memory>
#include <optional>
#include <set>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.application;
export import phoenix.session.api;
export import phoenix.input;
export import phoenix.menu;
export import phoenix.ui.interaction;

export namespace phoenix
{
enum class Screen
{
	home,
	server_lobby,
	client_lobby,
	viewscreen,
	console,
	settings,
	workshop,
	gm
};
struct Application
{
	enum class MenuBranch
	{
		root,
		host,
		scenarios,
		ships,
		join_ship,
		join_player,
		join_gm
	};
	bool native_menu = false, exit_requested = false, reduced_motion = false, packs_popup = false;
	bool dust_motes = true, reference_grid = true;
	MenuBranch menu_branch = MenuBranch::root;
	std::string selected_scenario, menu_focus;
	std::string base_reference = "base.zip";
	std::shared_ptr<MenuLibrary> library;
	std::shared_ptr<Catalog> menu_catalog;
	bool content_loading = false;
	std::string content_error;
	std::function<void()> content_action;
	std::function<void(std::function<void()>)> await_content;
	std::function<void(std::vector<LibraryPack>)> change_packs;
	std::string player_name = "<username>";
	Bytes avatar;
	struct NativeDisplay
	{
		int id;
		std::string name;
		std::string identity;
	};
	std::vector<NativeDisplay> native_displays;
	int host_display = -1;
	std::map<int, std::string> display_consoles;
	std::map<std::string, std::map<std::string, std::string>> remembered_station_displays;
	std::weak_ptr<SessionPort> restored_display_session;
	std::string restored_display_ship;
	std::map<std::uint64_t, std::string> local_player_names;
	int inspector_display = -1;
	std::function<void()> apply_displays;
	std::function<void()> reset_displays;
	std::function<void()> return_home;
	std::function<std::shared_ptr<SessionPort>()> new_host;
	std::vector<ConsoleRegion> console_regions;
	std::optional<ConsoleRegion> viewscreen_region;
	std::optional<ConsoleRegion> gm_region;
	bool tools_keyboard_focus = false, embedded_tools = false;
	struct ToolPointer
	{
		enum Phase
		{
			down,
			move,
			up,
			cancel
		};
		Phase phase;
		std::uint64_t id;
		float x, y;
	};
	std::vector<ToolPointer> tools_pointers;
	std::map<std::string, ConsoleId> gamepad_assignments;
	std::map<std::string, ScreenDevices> screen_devices;
	std::map<ConsoleId, std::string> screen_identities;
	struct GamepadInfo
	{
		std::string name, serial, path, model;
	};
	std::map<std::string, GamepadInfo> gamepad_devices;
	std::map<std::string, std::string> paired_gamepads;
	std::string pairing_screen;
	std::set<std::string> gamepad_wait_neutral;
	std::vector<std::pair<std::string, std::string>> audio_outputs;
	std::function<void(ConsoleId)> test_screen_audio;
	ConsoleId sound_route = 0;
	Screen screen = Screen::home;
	Screen settings_return = Screen::home;
	std::uint64_t settings_console = 0;
	std::string settings_tab = "audio";
	Screen launch_screen = Screen::viewscreen;
	int music_volume = 50, sfx_volume = 50, master_volume = 50;
	int siren_volume = 50;
	int tools_scale = 100;
	std::function<void()> button_click;
	std::string network_code;
	std::string network_service = default_network_service, network_profile = "default", join_page;
	std::function<void(const std::string &, const std::string &, const std::string &, JoinRole)> connect;
	std::string status, preference_error;
	std::string preference_persistence_error, preference_pending_error;
	std::string accessible_status;
	bool can_host = false;
	bool can_edit = false;
	bool pseudo_locale = false;
	bool text_probe = false;
	std::string language = "en";
	std::map<std::string, double> frame_metrics;
	std::shared_ptr<SessionPort> session;
	std::vector<std::pair<std::string, std::string>> screen_controls;
	std::map<std::string, bool> screen_toggles;
	std::vector<std::pair<std::string, std::string>> screen_readouts;
	using TextControl = phoenix::TextControl;
	std::vector<TextControl> screen_inputs;
	std::function<bool(const std::string &, const std::string &)> set_control_value;
	std::function<bool(const std::string &)> screen_action;
	using ControlPlacement = phoenix::ControlPlacement;
	std::unordered_map<std::string, ControlPlacement> control_placements;
	std::function<void(const std::string &)> focus_control;
	bool console_surface_active = false;
	bool budget_console_preparation = false;
};
const char *screen_name(Screen screen);
bool game_active(const Application &app);
} // namespace phoenix
