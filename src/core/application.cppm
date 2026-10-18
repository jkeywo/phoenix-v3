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
	MenuBranch menu_branch = MenuBranch::root;
	std::string selected_scenario, menu_focus;
	std::shared_ptr<MenuLibrary> library;
	std::function<void()> return_home;
	std::function<std::shared_ptr<SessionPort>()> new_host;
	std::vector<ConsoleRegion> console_regions;
	std::optional<ConsoleRegion> viewscreen_region;
	std::optional<ConsoleRegion> gm_region;
	bool tools_keyboard_focus = false, embedded_tools = false;
	std::map<std::string, ConsoleId> gamepad_assignments;
	Screen screen = Screen::home;
	Screen settings_return = Screen::home;
	int music_volume = 50;
	std::string network_code;
	std::string network_service = "ws://127.0.0.1:9080/phoenix", network_profile = "default", join_page;
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
} // namespace phoenix
