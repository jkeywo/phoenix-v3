// Explicit graphical smoke through the real offline lobby and menu renderer.
#include "../support/assertions.hpp"
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
import phoenix.presentation;
import phoenix.network.lobby;
using namespace phoenix;
using phoenix::test::require;
namespace
{
void capture(const std::string &name)
{
#ifdef __EMSCRIPTEN__
	// clang-format off
	EM_ASM({
		if (!Module.lobbyCaptures) Module.lobbyCaptures = {};
		Module.lobbyCaptures[UTF8ToString($0)] = Module.canvas.toDataURL("image/png");
	}, name.c_str());
	// clang-format on
#else
	TakeScreenshot(name.c_str());
#endif
}
} // namespace
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		require(argc > 1, "Supply pack directory");
		Application app;
		app.can_host = app.native_menu = true;
		NetworkOptions options;
		options.link.self = "host";
		options.link.room = "ABC123";
		options.data_location = "lobby-smoke-data";
		options.player_name = "Bridge host";
		app.session = make_game_lobby(options, true);
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->prepare_lobby({"base.free-flight", {{"base.player", "base.cruiser"}}, "base.player", ""});
		app.screen = Screen::server_lobby;
		app.return_home = []
		{
		};
		app.native_displays = {{0, "Host screen"},        {1, "Port screen"},    {2, "Starboard screen"},
		                       {3, "Engineering screen"}, {4, "Science screen"}, {5, "Captain screen"}};
		app.host_display = 0;
		unsigned display_changes = 0;
		app.apply_displays = [&]
		{
			++display_changes;
		};
		open_window();
		opened = true;
		auto frame = [&]
		{
			app.session->poll();
			BeginDrawing();
			draw_screen(app);
			EndDrawing();
		};
		auto wait_for_board = [&]
		{
			for (int loading = 0; loading < 300 && app.screen == Screen::server_lobby &&
			                      std::none_of(
			                          app.screen_controls.begin(), app.screen_controls.end(),
			                          [](const auto &control)
			                          {
				                          return control.first == "menu.copy-code";
			                          });
			     ++loading)
				frame();
		};
		auto action = [&](const std::string &id)
		{
			require(app.screen_action && app.screen_action(id), ("Visible lobby action is admitted: " + id).c_str());
			frame();
			frame();
			wait_for_board();
		};
		frame();
		frame();
		// Graphics preparation also runs again when a lobby claim changes the setup.
		wait_for_board();
		const auto lobby = app.session->lobby();
		require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                    [&](const auto &readout)
		                    {
			                    return readout.first == "menu.lobby-mission" &&
			                           readout.second == localise(lobby.scenario_name);
		                    }),
		        "The lobby exposes its mission title instead of game branding");
		require(lobby.consoles.size() == 6, "Cruiser smoke renders its six authored consoles");
		const auto first = lobby.consoles.front();
		require(std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
		                     [](const auto &control)
		                     {
			                     return control.first.starts_with("lobby.seat/");
		                     }),
		        "The ship host keeps its Viewscreen; native Consoles are claimed through display controls");
		for (const auto &seat : lobby.consoles)
			require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                    [&](const auto &readout)
			                    {
				                    return readout.first == "menu.console/" + seat.slot + "/" + seat.id &&
				                           readout.second.find(localise(seat.name)) != std::string::npos;
			                    }),
			        "Each console is represented together on the board");
		require(std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
		                     [](const auto &control)
		                     {
			                     return control.first.starts_with("menu.lobby-tab/") ||
			                            control.first.starts_with("menu.console-next") ||
			                            control.first.starts_with("lobby.approve/");
		                     }),
		        "The board has no console paging, tabs or manual join approvals");
#ifndef __EMSCRIPTEN__
		action("menu.console-screen/" + first.id + "/1");
		require(app.display_consoles.at(1) == first.id, "Native console is claimable without opening a tab");
		action("menu.inspector-screen/1");
		require(app.inspector_display == 1 && !app.display_consoles.contains(1), "GM replaces the player display role");
		for (const auto &seat : app.session->lobby().consoles)
			require(seat.local_screen != 3, "Claiming GM releases the previous authoritative local seat");
		capture("lobby-gm-claimed.png");
		action("menu.clear-inspector-screen");
		require(app.inspector_display < 0 && display_changes == 3,
		        "Releasing GM immediately reapplies the native layout");
		action("menu.inspector-screen/2");
		action("menu.console-screen/" + first.id + "/2");
		require(app.inspector_display < 0 && app.display_consoles.at(2) == first.id,
		        "A player console can reclaim a GM display");
		action("menu.clear-console-screen/" + first.id);
#endif
		action("menu.settings");
		require(app.screen == Screen::settings, "Lobby settings remain available");
		action("menu.close-dialog");
		require(app.screen == Screen::server_lobby, "Closing settings returns to the lobby board");
		for (const auto [width, height] : std::vector<std::pair<int, int>>{{1280, 720}, {1100, 720}, {800, 900}})
		{
#ifdef __EMSCRIPTEN__
			// clang-format off
			EM_ASM({ Module.canvas.style.width = $0 + "px"; Module.canvas.style.height = $1 + "px"; }, width, height);
			// clang-format on
#endif
			SetWindowSize(width, height);
			frame();
			frame();
			for (const auto &[id, rect] : app.control_placements)
				if (id.starts_with("menu.console-screen/") || id.starts_with("lobby.profile/") ||
				    id.starts_with("menu.inspector-screen/"))
					require(rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
					            rect.x + rect.width <= width + 1 && rect.y + rect.height <= height + 1,
					        ("Console and GM controls fit the viewport: " + id).c_str());
			capture("lobby-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
		}
		close_window();
		opened = false;
		std::cout << "PASS lobby console board, visible controls and native console/GM display claims\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
