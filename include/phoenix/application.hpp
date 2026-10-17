#pragma once
#include <string>
#include <vector>
namespace phoenix {
enum class Screen { home, server_lobby, client_lobby, viewscreen, workshop, gm };
struct Application {
    Screen screen = Screen::home;
    std::string network_code;
    std::string status = "Foundation build";
    std::vector<std::string> packs;
    bool can_host = false;
    bool can_edit = false;
    bool pseudo_locale = false;
};
const char* screen_name(Screen screen);
}
