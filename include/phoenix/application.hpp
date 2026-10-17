#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
namespace phoenix {
class SessionPort;
enum class Screen { home, server_lobby, client_lobby, viewscreen, console, workshop, gm };
struct Application {
    Screen screen = Screen::home;
    std::string network_code;
    std::string status = "Foundation build";
    std::vector<std::string> packs;
    bool can_host = false;
    bool can_edit = false;
    bool pseudo_locale = false;
    std::shared_ptr<SessionPort> session;
    std::vector<std::pair<std::string,std::string>> screen_controls;
    std::function<bool(const std::string&)> screen_action;
};
const char* screen_name(Screen screen);
}
