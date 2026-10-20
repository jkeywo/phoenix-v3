module;
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.application;
export import phoenix.session.api;
export import phoenix.input;
export import phoenix.menu;

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
	std::shared_ptr<MenuLibrary> library;
	std::string player_name = "<username>";
	Bytes avatar;
	struct NativeDisplay
	{
		int id;
		std::string name;
	};
	std::vector<NativeDisplay> native_displays;
	int host_display = -1;
	std::map<int, std::string> display_consoles;
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
	Screen screen = Screen::home;
	Screen settings_return = Screen::home;
	std::uint64_t settings_console = 0;
	std::string settings_tab = "audio";
	Screen launch_screen = Screen::viewscreen;
	int music_volume = 50, sfx_volume = 50, master_volume = 50;
	int siren_volume = 50;
	std::function<void()> button_click;
	std::string network_code;
	std::string network_service = default_network_service, network_profile = "default", join_page;
	std::function<void(const std::string &, const std::string &, const std::string &, JoinRole)> connect;
	std::string status;
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
	struct TextControl
	{
		std::string id, label, value;
		bool percentage = false;
	};
	std::vector<TextControl> screen_inputs;
	std::function<bool(const std::string &, const std::string &)> set_control_value;
	std::function<bool(const std::string &)> screen_action;
	struct ControlPlacement
	{
		float x = 0, y = 0, width = 0, height = 0;
		bool focused = false, enabled = true;
	};
	std::map<std::string, ControlPlacement> control_placements;
	std::function<void(const std::string &)> focus_control;
	bool console_surface_active = false;
};
const char *screen_name(Screen screen);
bool game_active(const Application &app);
} // namespace phoenix
