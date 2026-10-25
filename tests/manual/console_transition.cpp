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
				                                 return row.first.starts_with(prefix + "game.") ||
				                                        row.first.starts_with(prefix + "inline/");
			                                 });
			std::cout << "Route " << route << " gameplay controls: " << count << std::endl;
			require(count > 0, "Routed Console has visible gameplay controls after launch");
			require(probe_console_fit(route), "Radar fits its own 1080p monitor beside a 4K host");
		}
		for (const auto &[route, tabs] :
		     std::vector<std::pair<unsigned, std::vector<std::string>>>{{3, {"helm"}}, {4, {"comms", "support"}}})
			for (const auto &tab : tabs)
			{
				require(probe_console_tab(route, "game.console/" + tab),
				        ("Pointer switches Console tab " + tab).c_str());
				frame();
				frame();
				if (route == 4 && tab == "comms")
				{
					require(std::none_of(app.screen_readouts.begin(), app.screen_readouts.end(),
					                     [](const auto &row)
					                     {
						                     return row.first.starts_with("console/4/resource/");
					                     }),
					        "Comms tab excludes Support machinery readouts");
					TakeScreenshot("build/console-transition-comms.png");
				}
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
		require(has("console/4/support-page/repair"), "Support has a Repair tab");
		require(!has("console/4/game.console/navigator") && !has("console/4/game.console/navigation"),
		        "Remaining Console functions are integrated into Support");
		require(!has("console/4/resource/weapon-cell"), "Power minutiae stay out of the Support summary");
		require(probe_console_tab(4, "support-page/repair"), "Pointer opens Repair panel");
		frame();
		frame();
		require(!has("console/4/game.status/reactor"), "Repair excludes reactor controls");
		require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                    [](const auto &row)
		                    {
			                    return row.first == "console/4/support/repair/summary";
		                    }),
		        "Repair has a concise damage summary");
		TakeScreenshot("build/support-repair.png");
		require(probe_console_tab(4, "support-details/repair"), "Open repair details popup");
		frame();
		frame();
		require(has("console/4/systems-close"), "Repair details share the status popup");
		TakeScreenshot("build/support-repair-details.png");
		std::string priority, automatic;
		for (const auto &[name, label] : app.screen_controls)
			if (name.starts_with("console/4/popup-control/game.control/"))
			{
				if (label == "Reactor")
					priority = name.substr(std::string("console/4/").size());
				if (label.starts_with("Automatic repair priorities"))
					automatic = name.substr(std::string("console/4/").size());
			}
		require(!priority.empty() && probe_console_tab(4, priority), "Repair popup submits a real priority choice");
		for (int i = 0; i < 10; ++i)
			frame();
		bool prioritised = false;
		for (int attempt = 0; attempt < 60 && !prioritised; ++attempt)
		{
			const auto priority_view = app.session->console_observation(4);
			for (const auto &controller : priority_view->gameplay.at("controllers"))
				if (controller.at("id") == "repair-priority")
					prioritised = controller.at("commands").at("system").at("value").get<std::int64_t>() != 0;
			if (!prioritised)
				frame();
		}
		require(prioritised, "Repair priority reaches authoritative Controller state");
		require(!automatic.empty() && probe_console_tab(4, automatic), "Repair popup restores automatic priority");
		for (int i = 0; i < 10; ++i)
			frame();
		action("console/4/systems-close");
		require(probe_console_tab(4, "game.console/support"), "Return to Support");
		frame();
		frame();
		require(probe_console_tab(4, "game.status/reactor"), "Open reactor details popup");
		frame();
		frame();
		require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                    [](const auto &row)
		                    {
			                    return row.first.starts_with("console/4/popup/reactor/");
		                    }),
		        "Reactor telemetry lives in popup");
		TakeScreenshot("build/support-reactor-details.png");
		action("console/4/systems-close");
		require(probe_console_tab(4, "support-navigation"), "Open Navigation details");
		frame();
		frame();
		require(has("console/4/support-close-details"), "Navigation has popup close control");
		TakeScreenshot("build/support-navigation-details.png");
		action("console/4/settings");
		require(!has("console/4/support-close-details"), "Settings hides Navigation popup controls");
		action("console/4/local-settings.close");
		require(has("console/4/support-close-details"), "Closing Settings restores Navigation details");
		action("console/4/support-close-details");
		TakeScreenshot("build/support-summary.png");
		for (unsigned stage = 0; stage < 5; ++stage)
		{
			require(probe_console_throttle(3, stage), ("Touch throttle stage " + std::to_string(stage)).c_str());
			for (int i = 0; i < 10; ++i)
				frame();
		}
		TakeScreenshot("build/console-transition-throttle-stop.png");
		require(!has("console/3/game.console/tactical"), "No redundant Tactical tab");
		require(!has("console/4/station-page/docking"), "Support has no Docking tab");
		require(!has("console/3/game.combat-target/2"), "No target button list");
		auto weapon_state = [&]
		{
			std::map<std::string, bool> result;
			const auto observation = app.session->console_observation(3);
			for (const auto &extra : observation->additional_consoles)
				for (const auto &system : extra.systems)
					if (system.kind == OperatingSystem::Weapon)
						result[system.id] = system.enabled;
			return result;
		};
		const auto before_weapon = weapon_state();
		std::string toggle;
		for (const auto &[name, label] : app.screen_controls)
			if (name.starts_with("console/3/inline/game.control/") &&
			    (label.ends_with("Enabled") || label.ends_with("Disabled")))
			{
				toggle = name.substr(std::string("console/3/").size());
				break;
			}
		require(!toggle.empty() && probe_console_tab(3, toggle), "Pointer toggles a weapon from Helm");
		for (int i = 0; i < 60 && weapon_state() == before_weapon; ++i)
			frame();
		require(weapon_state() != before_weapon, "Weapon toggle changes authoritative tactical system state");
		require(probe_console_tab(3, "radar-contact/2"), "Click starbase on Helm radar");
		frame();
		frame();
		require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                    [](const auto &row)
		                    {
			                    return row.first == "console/3/target/name" &&
			                           row.second.find("#2") != std::string::npos;
		                    }),
		        "Radar selection populates target panel");
		TakeScreenshot("build/console-transition-target.png");
		require(probe_console_tab(3, "systems"), "Open one Systems popup");
		frame();
		frame();
		require(has("console/3/systems-close"), "System statuses have one close action");
		TakeScreenshot("build/console-transition-systems.png");
		action("console/3/systems-close");
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
