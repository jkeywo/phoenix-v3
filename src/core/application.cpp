#include <phoenix/application.hpp>
namespace phoenix {
const char* screen_name(Screen screen) {
    switch (screen) {
    case Screen::home: return "Home";
    case Screen::server_lobby: return "Server lobby";
    case Screen::client_lobby: return "Client lobby";
    case Screen::viewscreen: return "Viewscreen";
    case Screen::workshop: return "Workshop";
    case Screen::gm: return "GM inspector";
    }
    return "Unknown";
}
}
