// Explicit native graphical regression journey; never registered with CTest.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <string>
import phoenix.presentation;
import phoenix.network.lobby;
import phoenix.session.local;
using namespace phoenix;
using phoenix::test::require;
int main(int argc, char **argv)
{
	try
	{
		require(argc == 2 || argc == 3, "Supply the native pack directory and optional private signalling URL");
		Application app;
		app.can_host = app.native_menu = true;
		NetworkOptions options;
		options.link.self = "host";
		options.link.room = "CONSOLE";
		options.data_location = "build/console-transition-data";
		if (argc == 3)
			options.link.url = argv[2];
		app.session = make_frame_session(make_game_lobby(options, argc == 2));
		app.budget_console_preparation = true;
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->prepare_lobby({"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", ""});
		app.screen = Screen::server_lobby;
		app.viewscreen_region = ConsoleRegion{1, 0, 0, 1280, 720};
		app.console_regions = {{3, 1280, 0, 1280, 720}, {4, 2560, 0, 1280, 720}};
		for (const auto &[route, seat] : std::vector<std::pair<unsigned, std::string>>{{3, "helm"}, {4, "support"}})
		{
			LobbyCommand command{LobbyAction::assign_local_console, "", "courier", "", seat};
			command.screen = route;
			require(app.session->lobby_command(command), "Assign Courier Console");
		}
		open_window();
		SetWindowSize(3840, 720);
		SetTargetFPS(60);
		auto frame = [&]
		{
			app.session->poll();
			BeginDrawing();
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 30; ++i)
			frame();
		for (unsigned route : {3u, 4u})
		{
			LobbyCommand command{LobbyAction::ready};
			command.screen = route;
			require(app.session->lobby_command(command), "Ready Courier Console");
		}
		for (int i = 0; i < 1200 && !app.session->running(); ++i)
			frame();
		require(app.session->running(), "Lobby launches");
		app.screen = Screen::viewscreen;
		for (int i = 0; i < 900; ++i)
		{
			frame();
			if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                [](const auto &row)
			                {
				                return row.first == "console/3/personnel";
			                }))
				break;
		}
		for (int i = 0; i < 5; ++i)
			frame();
		TakeScreenshot("build/console-transition-running.png");
		for (unsigned route : {3u, 4u})
		{
			const auto prefix = "console/" + std::to_string(route) + "/";
			const auto count = std::count_if(app.screen_controls.begin(), app.screen_controls.end(),
			                                 [&](const auto &row)
			                                 {
				                                 return row.first.starts_with(prefix + "game.");
			                                 });
			std::cout << "Route " << route << " gameplay controls: " << count << std::endl;
			require(count > 0, "Routed Console has visible gameplay controls after launch");
		}
		auto has = [&](const std::string &id)
		{
			return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                   [&](const auto &row)
			                   {
				                   return row.first == id;
			                   });
		};
		auto action = [&](const std::string &id)
		{
			require(invoke_navigation(app, id), ("Admit visible action " + id).c_str());
			frame();
			frame();
		};
		action("console/3/personnel");
		require(!has("console/3/local-settings.tab/display"), "Personnel excludes preference tabs");
		require(has("console/4/personnel"), "Other Console remains usable");
		TakeScreenshot("build/console-transition-personnel.png");
		action("console/3/local-settings.close");
		action("console/3/settings");
		require(has("console/3/local-settings.larger"), "Settings opens Display after Personnel");
		require(!has("console/3/local-settings.tab/away"), "Settings excludes gameplay tabs");
		action("console/3/local-settings.larger");
		TakeScreenshot("build/console-transition-settings.png");
		action("console/3/local-settings.close");
		action("console/3/away");
		require(has("console/3/local-settings.tab/away"), "Away missions opens gameplay pages");
		TakeScreenshot("build/console-transition-away.png");
		action("console/3/local-settings.close");
		close_window();
		std::cout << "PASS Courier lobby transition and independent gameplay controls\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
