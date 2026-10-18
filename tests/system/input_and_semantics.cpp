#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

import phoenix.input;
import phoenix.navigation;

using namespace phoenix;
void require(bool value, const char *contract)
{
	if (!value)
		throw std::runtime_error(contract);
}
int main()
{
	try
	{
		InputRouter router;
		require(router.set_regions({{11, 0, 0, 1920, 1080},
		                            {22, 1920, 0, 1920, 1080},
		                            {33, 0, 1080, 1920, 1080},
		                            {44, 1920, 1080, 1920, 1080},
		                            {55, 3840, 1080, 1920, 1080}}),
		        "arbitrary five-console layout must load");
		require(router.select_keyboard(22), "explicit keyboard assignment must succeed");
		auto first = router.pointer_down(101, 10, 10);
		auto second = router.pointer_down(102, 2000, 10);
		require(first && second && first->console == 11 && second->console == 22 && second->x == 80,
		        "simultaneous touch contacts must route independently in local coordinates");
		auto crossed = router.pointer_move(101, 2100, 20);
		require(crossed && crossed->console == 11, "pointer capture must survive crossing another console");
		require(router.keyboard_console() == 22, "touch must not redirect keyboard/accessibility focus");
		require(router.pointer_up(101, 2100, 20)->released, "pointer release must reach captured console");
		require(!router.pointer_move(101, 20, 20), "released contact must lose capture");
		for (unsigned i = 0; i < 5; ++i)
		{
			router.assign_gamepad("pad" + std::to_string(i), (i + 1) * 11);
			router.gamepad("pad" + std::to_string(i), "throttle", 0.75f);
		}
		auto inputs = router.drain_gamepad_inputs();
		require(inputs.size() == 5 && inputs.back().console == 55,
		        "routing contract must support five independently assigned controllers");
		router.assign_gamepad("pad0", 22);
		router.disconnect_gamepad("pad1");
		inputs = router.drain_gamepad_inputs();
		require(inputs.size() == 2 && inputs[0].console == 11 && inputs[0].value == 0 && inputs[1].console == 22 &&
		            inputs[1].value == 0,
		        "reassignment/disconnect must neutralise the old console");
		router.gamepad("pad1", "throttle", 1);
		require(router.drain_gamepad_inputs().empty(), "disconnected/unassigned input must not claim a station");
		require(!router.set_regions({{11, 0, 0, 10, 10}, {22, 5, 5, 10, 10}}),
		        "overlapping console regions must be rejected atomically");
		require(router.pointer_move(102, 2000, 10)->console == 22,
		        "invalid layout must not discard an existing capture");
		Application client;
		require(client.music_volume == 50 && !invoke_navigation(client, "settings"),
		        "music settings start at half volume and remain server-only");
		Application host;
		host.can_host = true;
		host.screen = Screen::console;
		require(invoke_navigation(host, "settings") && host.screen == Screen::settings,
		        "the server can open settings during a mission");
		require(set_music_volume(host, "0") && host.music_volume == 0 && set_music_volume(host, "100.000000") &&
		            host.music_volume == 100 && !set_music_volume(host, "nan") && !set_music_volume(host, "101") &&
		            !set_music_volume(host, "50junk") && !set_music_volume(client, "20"),
		        "slider and accessible volume edits share bounded numeric admission, including mute");
		host.screen_inputs = {{"music-volume", "Music volume", "100", true}};
		require(navigation_controls(host).back().percentage,
		        "music volume must publish range semantics rather than an unlabelled control");
		require(invoke_navigation(host, "settings.close") && host.screen == Screen::console && host.music_volume == 100,
		        "closing settings returns to the prior screen and retains volume");
		require(!invoke_navigation(client, "host") && !invoke_navigation(client, "edit"),
		        "semantic actions must preserve role restrictions");
		require(navigation_controls(client).size() == 4, "client semantics must not expose host/editor actions");
		require(invoke_navigation(client, "join") && client.screen == Screen::client_lobby,
		        "semantic action must use production navigation");
		invoke_navigation(client, "locale");
		require(navigation_controls(client).front().label == translate("home", true),
		        "graphics and semantics must share translated names");
		client.console_surface_active = true;
		client.screen_controls = {{"console/11/game.faster", "Increase speed"},
		                          {"console/22/game.contacts", "Read radar contacts"}};
		client.control_placements["console/22/game.contacts"] = {1920, 100, 300, 40, true, true};
		const auto semantics = navigation_controls(client);
		require(semantics.size() == 2 && semantics[1].placement.x == 1920 && semantics[1].placement.focused,
		        "Console semantics must retain per-region geometry and focus without unrelated global navigation");
		std::cout << "PASS console routing, safe release, semantic admission and translation\n";
		configure_translations(
		    Json{{"schema", 1}, {"languages", {{"en", {{"home", "Home"}}}, {"test", {{"home", "Translated home"}}}}}});
		select_language("test");
		require(translate("home", false) == "Translated home",
		        "UI and semantics must resolve the selected language resource");
		client.screen_inputs = {{"join-code", "Network code", "room/server"}};
		const auto with_input = navigation_controls(client);
		require(with_input.back().editable && with_input.back().value == "room/server",
		        "Editable semantic controls must expose their current text separately from their names");
		select_language("en");
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
