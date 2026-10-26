// Explicit native graphical regression journey; never registered with CTest.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
import phoenix.presentation;
import phoenix.console;
import phoenix.network.lobby;
import phoenix.session.local;
using namespace phoenix;
using phoenix::test::require;
void verify_planet(const std::string &directory)
{
	Application planet;
	planet.can_host = true;
	planet.session = make_local_session();
	auto draft = read_pack(read_file(directory + "/base.zip")).draft();
	auto definitions = parse_json(draft.files.at("definitions.json"), "near-planet graphical journey");
	for (auto &map : definitions["maps"])
		if (map["id"] == "base.rescue-under-threat")
		{
			map["ship_slots"][0]["x_mm"] = -700000;
			map["ship_slots"][0]["y_mm"] = -1200000;
		}
	draft.files["definitions.json"] = json_bytes(definitions);
	planet.session->add_pack(write_pack(draft));
	planet.session->start({"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", "helm"});
	planet.screen = Screen::console;
	planet.console_regions = {{1, 0, 0, 1920, 1080}};
	auto planet_frame = [&]
	{
		planet.session->tick();
		BeginDrawing();
		draw_screen(planet);
		EndDrawing();
	};
	for (int i = 0; i < 900; ++i)
	{
		planet_frame();
		if (std::any_of(planet.screen_controls.begin(), planet.screen_controls.end(),
		                [](const auto &field)
		                {
			                return field.first == "console/1/radar-systems/radar";
		                }))
			break;
	}
	const auto planet_view = planet.session->observation();
	require(planet_view && std::any_of(planet_view->contacts.begin(), planet_view->contacts.end(),
	                                   [&](const auto &body)
	                                   {
		                                   if (body.id != 3)
			                                   return false;
		                                   const auto distance = std::hypot(double(body.x - planet_view->own.x),
		                                                                    double(body.y - planet_view->own.y));
		                                   return std::any_of(planet_view->systems.begin(), planet_view->systems.end(),
		                                                      [&](const auto &sensor)
		                                                      {
			                                                      return sensor.kind == OperatingSystem::Radar &&
			                                                             distance > sensor.maximum &&
			                                                             distance - body.radius < sensor.maximum &&
			                                                             std::find(sensor.contact_ids.begin(),
			                                                                       sensor.contact_ids.end(),
			                                                                       body.id) != sensor.contact_ids.end();
		                                                      });
	                                   }),
	        "Near-planet journey has a sensed surface with its centre outside radar range");
	require(probe_console_tab(1, "radar-contact/3"), "Planet surface is selectable with its centre beyond radar range");
	for (int i = 0; i < 10; ++i)
		planet_frame();
	require(std::any_of(planet.screen_readouts.begin(), planet.screen_readouts.end(),
	                    [](const auto &field)
	                    {
		                    return field.first == "console/1/target/speed";
	                    }),
	        "Planet information appears directly in the contact panel");
	require(probe_console_fit(1), "Selected planet details fit the Helm panel at 1080p");
	TakeScreenshot("build/helm-planet-surface.png");
}
int main(int argc, char **argv)
{
	try
	{
		require(argc == 2 || argc == 3, "Supply the native pack directory and optional private signalling URL");
		if (argc == 3 && std::string(argv[2]) == "--planet-only")
		{
			open_window();
			SetWindowSize(1920, 1080);
			verify_planet(argv[1]);
			close_window();
			std::cout << "PASS near-planet Helm radar and inline contact panel" << std::endl;
			return 0;
		}
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
		for (int i = 0; i < 600 && !saw_station_loading; ++i)
			frame();
		require(saw_station_loading, "Assigned station exposes responsive loading before readiness");
		for (unsigned route : {3u, 4u})
		{
			LobbyCommand command{LobbyAction::ready};
			command.screen = route;
			require(app.session->lobby_command(command), "Ready Courier Console");
		}
		for (int i = 0; i < 1200 && !app.session->running(); ++i)
			frame();
		require(app.session->running(),
		        ("Lobby launches: " + app.session->lobby().status + " / " + app.session->connection_status()).c_str());
		app.screen = Screen::viewscreen;
		for (int i = 0; i < 900; ++i)
		{
			frame();
			if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                [](const auto &row)
			                {
				                return row.first == "console/3/console-page/helm/station.main";
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
				                                 return row.first.starts_with(prefix + "task/") ||
				                                        row.first.starts_with(prefix + "game.") ||
				                                        row.first.starts_with(prefix + "inline/") ||
				                                        row.first.starts_with(prefix + "support-");
			                                 });
			std::cout << "Route " << route << " gameplay controls: " << count << std::endl;
			require(std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
			                     [&](const auto &row)
			                     {
				                     return row.second == "Flight operations" && row.first.starts_with(prefix);
			                     }),
			        "Courier stations without craft omit Flight operations");
			require(app.screen_action && !app.screen_action(prefix + "station-page/craft") &&
			            !app.screen_action(prefix + "craft"),
			        "Courier rejects stale Flight operations navigation");
			require(count > 0, "Routed Console has visible gameplay controls after launch");
			require(probe_console_fit(route), "Radar fits its own 1080p monitor beside a 4K host");
		}
		auto frames = [&](int count = 3)
		{
			for (int i = 0; i < count; ++i)
				frame();
		};
		auto has = [&](const std::string &key)
		{
			return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                   [&](const auto &r)
			                   {
				                   return r.first == key;
			                   });
		};
		auto action = [&](const std::string &key)
		{
			require(probe_console_tab(key.starts_with("console/3/") ? 3 : 4, key.substr(10)),
			        ("Pointer action " + key).c_str());
			frames();
		};
		auto capture = [&](const std::string &name, unsigned route)
		{
			auto picture = LoadImageFromScreen();
			ImageCrop(&picture, {3840, route == 3 ? 0.f : 1080.f, 1920, 1080});
			ExportImage(picture, ("build/compact-" + name + ".png").c_str());
			UnloadImage(picture);
		};
		require(has("console/3/console-page/helm/station.away"),
		        "Dispatch keeps Away Missions available at one star before reveal");
		require(!has("console/3/console-page/helm/station.operations") && !has("console/3/console-page/tactical/"),
		        "No redundant Helm pages");
		require(has("console/3/task/weapons/0"), "Compact weapon toggles are visible");
		const bool weapon_was_on = app.screen_toggles.at("console/3/task/weapons/0");
		action("console/3/task/weapons/0");
		frames(20);
		require(app.screen_toggles.at("console/3/task/weapons/0") != weapon_was_on,
		        "Weapon toggle reaches authoritative state");
		action("console/3/radar-systems/radar");
		require(has("console/3/systems-close"), "System status opens from beside the radar");
		action("console/3/systems-close");
		if (const auto view = app.session->console_snapshot(3); view && !view->contacts.empty())
		{
			action("console/3/radar-contact/" + std::to_string(view->contacts.front().id));
			require(!has("console/3/task-detail/target"), "Contact details need no extra button");
			require(std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                    [](const auto &field)
			                    {
				                    return field.first == "console/3/target/speed";
			                    }),
			        "Selected contact speed is shown directly in the Helm panel");
		}
		capture("helm", 3);
		require(probe_console_fit(3), "Helm operational components fit at 1080p");
		action("console/3/on-screen/radar");
		frames(20);
		require(app.session->public_board().value("display", Json::object()).value("kind", "") == "radar",
		        "Radar publication reaches shared viewscreen board");
		TakeScreenshot("build/compact-shared-radar.png");
		action("console/3/on-screen/radar");
		frames(10);
		require(!app.session->public_board().contains("display"), "Second activation returns viewscreen to camera");
		for (const auto &page : {"support/station.support", "support/station.engineering", "navigation/", "comms/"})
		{
			action("console/4/console-page/" + std::string(page));
			frames(12);
			capture(std::string(page).substr(0, std::string(page).find('/')), 4);
			require(probe_console_fit(4), "Support operational components fit at 1080p");
			require(std::none_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                     [&](const auto &r)
			                     {
				                     return r.first.starts_with("console/4/resource/");
			                     }),
			        "No generic machinery dump on task pages");
			if (std::string(page) == "comms/")
			{
				std::string conversation;
				for (const auto &[key, label] : app.screen_controls)
					if (key.starts_with("console/4/comms/select/"))
					{
						conversation = key;
						break;
					}
				require(!conversation.empty(), "Comms offers a permitted conversation");
				action(conversation);
				frames(15);
				require(probe_console_conversation(4), "Conversation rows and text use available history width");
				if (has("console/4/comms/screen"))
				{
					action("console/4/comms/screen");
					frames(10);
					require(app.session->public_board().at("display").at("kind") == "conversations",
					        "Comms publishes to viewscreen");
					action("console/4/comms/screen");
				}
			}
			if (std::string(page) == "navigation/")
			{
				require(!has("console/4/nav.in") && !has("console/4/nav.set"),
				        "Navigation has no zoom bank or unselected waypoint action");
				require(probe_console_navigation(4, 0), "Selecting empty map space does not change waypoint");
				frames();
				action("console/4/nav.set");
				frames(10);
				require(has("console/4/nav.clear"), "Waypoint action becomes contextual Clear");
				action("console/4/nav.clear");
				frames(10);
				require(probe_console_navigation(4, 1), "Keyboard moves selection without committing a waypoint");
				frames();
				action("console/4/on-screen/map");
				frames(10);
				require(app.session->public_board().at("display").at("kind") == "navigation",
				        "Navigation publishes to assigned viewscreen");
				TakeScreenshot("build/compact-shared-navigation.png");
				action("console/4/on-screen/map");
			}
		}
		action("console/4/console-page/support/station.engineering");
		require(std::none_of(app.screen_inputs.begin(), app.screen_inputs.end(),
		                     [](const auto &r)
		                     {
			                     return r.id.starts_with("console/4/power/");
		                     }),
		        "One-star power remains read-only");
		ConsoleModel power;
		power.refresh(app.session, 4);
		require(power.command("preset", 2), "Select detailed Engineering");
		frames(40);
		require(has("console/4/power-details"), "Individual tuning is contextual");
		require(app.set_control_value("console/4/power/weapons-distribution/allocation", "50"),
		        "Authored power group slider admits allocation");
		frames(20);
		power.refresh(app.session, 4);
		std::int64_t commanded = -1, allocated = -2;
		for (const auto &c : power.observation()->gameplay.at("controllers"))
			if (c.at("id") == "weapons-distribution")
				commanded = c.at("commands").at("allocation").at("value");
		for (const auto &g : power.observation()->gameplay.at("power").at("groups"))
			if (g.at("id") == "combat-power.weapons")
				allocated = g.at("allocation");
		require(commanded == allocated && allocated > 0, "Slider changes the real authored resource group");
		capture("engineering-detailed", 4);
		require(probe_console_fit(4), "Detailed Engineering fits at 1080p");
		action("console/4/task/team-roster/0");
		require(has("console/4/systems-close"), "Team detail opens permitted activity and deployment controls");
		action("console/4/systems-close");
		for (unsigned stage = 0; stage < 5; ++stage)
		{
			require(
			    probe_console_throttle(3, stage),
			    ("Touch throttle retains captured control and zero deadzone, stage " + std::to_string(stage)).c_str());
			frames(10);
		}
		action("console/3/settings");
		action("console/3/local-settings.larger");
		action("console/3/local-settings.close");
		capture("helm-large-text", 3);
		require(probe_console_fit(3), "Enlarged Helm controls fit without clipping");
		Application cruiser;
		cruiser.can_host = true;
		cruiser.session = make_local_session();
		cruiser.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		cruiser.session->start({"base.rescue-under-threat", {{"courier", "base.cruiser"}}, "courier", "engineering"});
		cruiser.screen = Screen::console;
		cruiser.console_regions = {{1, 0, 0, 1920, 1080}};
		SetWindowSize(1920, 1080);
		auto cruiser_frame = [&]
		{
			cruiser.session->tick();
			BeginDrawing();
			draw_screen(cruiser);
			EndDrawing();
		};
		for (int i = 0; i < 900; ++i)
		{
			cruiser_frame();
			if (std::any_of(cruiser.screen_controls.begin(), cruiser.screen_controls.end(),
			                [](const auto &r)
			                {
				                return r.first == "console/1/console-page/engineering/station.engineering";
			                }))
				break;
		}
		require(probe_console_tab(1, "console-page/engineering/station.engineering"),
		        "Cruiser shares Engineering component layout");
		for (int i = 0; i < 20; ++i)
			cruiser_frame();
		TakeScreenshot("build/compact-cruiser-engineering.png");
		require(probe_console_fit(1), "Cruiser Engineering fits actual authored groups and team roster");
		verify_planet(argv[1]);
		close_window();
		std::cout << "PASS compact native stations, shared viewscreen, star controls, mixed-resolution geometry"
		          << std::endl;
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
