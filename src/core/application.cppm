module;
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.application;
export import phoenix.session.api;

export namespace phoenix
{
enum class Screen
{
	home,
	server_lobby,
	client_lobby,
	viewscreen,
	console,
	workshop,
	gm
};
struct Application
{
	Screen screen = Screen::home;
	std::string network_code;
	std::string network_service = "ws://127.0.0.1:9080/phoenix", network_profile = "default", join_page;
	std::function<void(const std::string &, const std::string &, const std::string &, bool)> connect;
	std::string status = "Foundation build";
	std::string accessible_status;
	std::vector<std::string> packs;
	bool can_host = false;
	bool can_edit = false;
	bool pseudo_locale = false;
	std::shared_ptr<SessionPort> session;
	std::vector<std::pair<std::string, std::string>> screen_controls;
	std::function<bool(const std::string &)> screen_action;
};
const char *screen_name(Screen screen);
} // namespace phoenix
