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

module phoenix.application;

namespace phoenix
{
const char *screen_name(Screen screen)
{
	switch (screen)
	{
	case Screen::home:
		return "Home";
	case Screen::server_lobby:
		return "Server lobby";
	case Screen::client_lobby:
		return "Client lobby";
	case Screen::viewscreen:
		return "Viewscreen";
	case Screen::console:
		return "Player console";
	case Screen::workshop:
		return "Workshop";
	case Screen::gm:
		return "GM inspector";
	}
	return "Unknown";
}
} // namespace phoenix
