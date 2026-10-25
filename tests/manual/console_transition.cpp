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
		app.viewscreen_region = ConsoleRegion{1, 0, 0, 3840, 2160};
		app.console_regions = {{3, 3840, 0, 1920, 1080}, {4, 3840, 1080, 1920, 1080}};
		for (const auto &[route, seat] : std::vector<std::pair<unsigned, std::string>>{{3, "helm"}, {4, "support"}})
		{
			LobbyCommand command{LobbyAction::assign_local_console, "", "courier", "", seat};
			command.screen = route;
			require(app.session->lobby_command(command), "Assign Courier Console");
		}
		open_window();
		SetWindowSize(5760, 2160);
		SetTargetFPS(60);
		bool saw_station_loading = false;
		unsigned loading_frames = 0;
		auto frame = [&]
		{
			app.session->poll();
			if (app.session->running())
				app.session->tick();
			BeginDrawing();
			draw_screen(app);
			EndDrawing();
			if (!saw_station_loading &&
			    std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                [](const auto &row)
			                {
				                return row.first == "console/3/loading";
			                }) &&
			    ++loading_frames >= 3)
			{
				require(std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                    [](const auto &row)
				                    {
					                    return row.first.starts_with("menu.");
				                    }),
				        "Host lobby remains interactive during station loading");
				require(app.screen_action && app.screen_action("menu.local-ready/3"),
				        "Host can ready a station through the live lobby while station assets load");
				const auto loading_lobby = app.session->lobby();
				require(std::any_of(loading_lobby.consoles.begin(), loading_lobby.consoles.end(),
				                    [](const auto &seat)
				                    {
					                    return seat.local_screen == 3 && seat.ready;
				                    }),
				        "Loading does not block authoritative lobby actions");
				LobbyCommand unready{LobbyAction::unready};
				unready.screen = 3;
				require(app.session->lobby_command(unready), "Restore readiness after the loading interaction");
				TakeScreenshot("build/console-transition-local-loading.png");
				saw_station_loading = true;
			}
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
				                return row.first == "console/3/station-page/personnel";
			                }))
				break;
		}
		for (int i = 0; i < 5; ++i)
			frame();
		TakeScreenshot("build/console-transition-running.png");
		auto helm_capture = LoadImageFromScreen();
		ImageCrop(&helm_capture, {3840, 0, 1920, 1080});
		ExportImage(helm_capture, "build/console-transition-helm-1080p.png");
		UnloadImage(helm_capture);
		const auto untouched = app.session->console_observation(3);
		require(saw_station_loading, "Loading progress appears on the assigned station monitor");
		require(untouched && untouched->requested_speed == 0, "Untouched Helm starts with zero requested speed");
		require(untouched->own.vx == 0 && untouched->own.vy == 0, "Untouched ship remains stationary through loading");
		require(std::none_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                     [](const auto &row)
		                     {
			                     return row.first == "console/3/resource/reactor" ||
			                            row.first == "console/3/resource/shield-cell";
		                     }),
		        "Support machinery cards are absent from Helm");
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
			require(probe_console_fit(route), "Radar fits its own 1080p monitor beside a 4K host");
		}
		for (const auto &[route, tabs] : std::vector<std::pair<unsigned, std::vector<std::string>>>{
		         {3, {"tactical", "helm"}}, {4, {"comms", "navigator", "support"}}})
			for (const auto &tab : tabs)
			{
				require(probe_console_tab(route, "game.console/" + tab),
				        ("Pointer switches Console tab " + tab).c_str());
				frame();
				frame();
			}
		const auto after_tabs = app.session->console_observation(3);
		require(after_tabs && after_tabs->requested_speed == 0, "Tab navigation does not submit a flight command");
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
		action("console/3/station-page/personnel");
		require(!has("console/3/local-settings.tab/display"), "Personnel excludes preference tabs");
		require(has("console/4/station-page/personnel"), "Other Console remains usable");
		TakeScreenshot("build/console-transition-personnel.png");
		action("console/3/local-settings.close");
		action("console/3/settings");
		require(has("console/3/local-settings.larger"), "Settings opens Display after Personnel");
		require(!has("console/3/local-settings.tab/away"), "Settings excludes gameplay tabs");
		action("console/3/local-settings.larger");
		TakeScreenshot("build/console-transition-settings.png");
		action("console/3/local-settings.close");
		action("console/3/station-page/away");
		require(has("console/3/station-page/away"), "Away missions is an integrated station tab");
		TakeScreenshot("build/console-transition-away.png");
		action("console/3/local-settings.close");
		std::cout << "PASS mixed-resolution Courier layout, pointer tabs, no unintended motion and independent pages"
		          << std::endl;
		close_window();
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
