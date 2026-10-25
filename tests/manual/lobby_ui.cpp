// Explicit graphical smoke through the real offline lobby and menu renderer.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <nlohmann/json.hpp>
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
import phoenix.network.lobby.protocol;
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
	TakeScreenshot((name.starts_with("build/") ? name : "build/rescue-" + name).c_str());
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
		app.session->prepare_lobby({"base.rescue-under-threat", {{"courier", "base.cruiser"}}, "courier", ""});
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
		std::shared_ptr<SessionPort> network_host;
		auto frame = [&]
		{
			if (network_host)
				network_host->poll();
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
		// Wait for initial graphics preparation before exercising lobby claims.
		wait_for_board();
		for (const auto *fit : {"base.rescue-courier", "base.cruiser"})
		{
			require(app.session->lobby_command({LobbyAction::claim_ship, "", "courier", fit}),
			        "Select the delivery fit in the lobby");
			frame();
			frame();
			wait_for_board();
			for (int settled = 0; settled < 5; ++settled)
				frame();
			const auto expected =
			    localise(mission_briefing(app.session->catalog().maps.at("base.rescue-under-threat"), fit));
			require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                    [&](const auto &readout)
			                    {
				                    return readout.first == "menu.mission-briefing" && readout.second == expected;
			                    }),
			        "The visible lobby briefing follows the selected delivery fit");
			capture(std::string("build/rescue-lobby-") + fit + ".png");
		}
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
		SetWindowSize(1600, 900);
		action("menu.console-screen/" + first.id + "/1");
		app.viewscreen_region = ConsoleRegion{1, 0, 0, 800, 900};
		app.console_regions = {{3, 800, 0, 800, 900}};
		frame();
		frame();
		capture("lobby-local-complexity.png");
		app.viewscreen_region.reset();
		app.console_regions.clear();
		if (argc > 2)
		{
			options.link.url = argv[2];
			options.link.room = "smoke-" + random_token();
			network_host = make_game_lobby(options);
			network_host->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
			network_host->prepare_lobby(
			    {"base.rescue-under-threat", {{"courier", "base.cruiser"}}, "courier", ""});
			LobbyCommand reserve{LobbyAction::assign_local_console, "", first.slot, "", first.id};
			reserve.screen = 3;
			require(network_host->lobby_command(reserve), "Native station is reserved before phone join");
			options.expected_base = network_host->catalog().base;
			options.server = "host";
			options.link.self = "phone";
			app.session = join_client_lobby(options, JoinRole::player);
			app.can_host = false;
			app.screen = Screen::client_lobby;
			SetWindowSize(480, 900);
			const auto other = lobby.consoles.at(1);
			const auto claim = "lobby.seat/" + other.slot + "/" + other.id;
			for (int i = 0; i < 600; ++i)
			{
				frame();
				if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                [&](const auto &control)
				                {
					                return control.first == claim;
				                }))
					break;
			}
			frame();
			frame();
			capture("lobby-phone-choices.png");
			action(claim);
			for (int i = 0; i < 600; ++i)
			{
				frame();
				if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                [](const auto &control)
				                {
					                return control.first == "lobby.ready";
				                }))
					break;
			}
			require(std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                    [](const auto &control)
			                    {
				                    return control.first.starts_with("lobby.profile/");
			                    }),
			        "Selected remote station exposes complexity choices");
			frame();
			frame();
			capture("lobby-phone-ready.png");
			action("menu.settings");
			require(app.screen == Screen::settings, "Phone lobby opens Settings");
			action("menu.close-dialog");
			action("lobby.seat/" + other.slot + "/");
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
