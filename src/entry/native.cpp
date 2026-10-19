#include <raylib.h>
#include <cstring>
#include <iostream>
#include <algorithm>
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

import phoenix.navigation;
import phoenix.network.client;
import phoenix.network.host;
import phoenix.network.lobby;
import phoenix.presentation;
import phoenix.session.api;
import phoenix.session.local;
import phoenix.simulation;
import phoenix.tools;
import phoenix.workshop_test;
#include "workshop_bridge.hpp"
#include "performance.hpp"
#include "../../tests/manual/native_menu.hpp"

int main(int argc, char **argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--build-check") == 0)
	{
		phoenix::World world;
		world.tick();
		std::cout << "Phoenix native: Flecs tick " << world.ticks() << '\n';
		return world.ticks() == 1 ? 0 : 1;
	}
	phoenix::Application app;
	app.can_host = true;
	app.native_menu = true;
	app.can_edit = true;
	phoenix::NetworkOptions network;
	network.link.url = phoenix::default_network_service;
	bool remote = false, gm = false, local_only = false, custom_data = false;
	NativeMenuSmoke menu_smoke;
	bool qr_smoke = false;
	bool join_smoke = false, join_submitted = false, claim_submitted = false;
	unsigned join_ready_frames = 0;
	int smoke_frames = 0;
	bool helm_smoke = false;
	std::uint64_t helm_smoke_tick = 0;
	bool workshop_smoke = false;
	std::string workshop_pack;
	std::string game_pack;
	std::string mission, slot, ship, seat;
	bool console = false;
	std::string runtime_save;
	std::string display_layout;
	bool span_displays = false;
	bool vsync = false, uncapped = false;
	unsigned frame_limit = 0;
	phoenix::FrameCapture performance;
	bool resume_local = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--menu-smoke")
			menu_smoke.enabled = true;
		if (argument == "--lobby-regions-smoke")
		{
			menu_smoke.enabled = menu_smoke.assign_displays = menu_smoke.virtual_displays = true;
		}
		if (argument == "--lobby-displays-smoke")
		{
			menu_smoke.enabled = true;
			menu_smoke.assign_displays = true;
		}
		if (argument == "--vsync")
			vsync = true;
		if (argument == "--uncapped")
			uncapped = true;
		if (argument == "--frame-limit" && i + 1 < argc)
			frame_limit = std::clamp(static_cast<unsigned>(std::stoul(argv[++i])), 1u, 1000u);
		if (argument == "--text-probe")
			app.text_probe = true;
		if (argument == "--language" && i + 1 < argc)
			app.language = argv[++i];
		if (argument == "--performance-output" && i + 1 < argc)
			performance.output = argv[++i];
		if (argument == "--performance-frames" && i + 1 < argc)
			performance.target = std::clamp(static_cast<unsigned>(std::stoul(argv[++i])), 60u, 36000u);
		if (argument == "--span-displays")
			span_displays = true;
		if (argument == "--display-layout" && i + 1 < argc)
			display_layout = argv[++i];
		if (argument == "--client")
			remote = true;
		if (argument == "--gm")
		{
			remote = true;
			gm = true;
		}
		if (argument == "--local-only")
			local_only = true;
		if (argument == "--relay")
			network.link.force_relay = true;
		if (argument == "--service" && i + 1 < argc)
			network.link.url = argv[++i];
		if (argument == "--profile" && i + 1 < argc)
			network.profile = argv[++i];
		if (argument == "--data" && i + 1 < argc)
		{
			network.data_location = argv[++i];
			custom_data = true;
		}
		if (argument == "--code" && i + 1 < argc)
			app.network_code = argv[++i];
		if (argument == "--page" && i + 1 < argc)
			app.join_page = argv[++i];
		if (argument == "--qr-smoke")
		{
			qr_smoke = true;
			app.native_menu = false;
			smoke_frames = 6;
			app.screen = phoenix::Screen::server_lobby;
		}
		if (argument == "--helm-smoke")
		{
			helm_smoke = true;
			smoke_frames = 90;
		}
		if (argument == "--join-smoke")
		{
			join_smoke = true;
			app.native_menu = false;
			remote = true;
		}
		if (std::strcmp(argv[i], "--smoke") == 0)
			smoke_frames = 6;
		if (std::strcmp(argv[i], "--workshop") == 0)
		{
			app.screen = phoenix::Screen::workshop;
			workshop_smoke = true;
		}
		if (std::strcmp(argv[i], "--workshop-pack") == 0 && i + 1 < argc)
		{
			workshop_pack = argv[++i];
			app.screen = phoenix::Screen::workshop;
			workshop_smoke = true;
		}
		if (std::strcmp(argv[i], "--game-pack") == 0 && i + 1 < argc)
			game_pack = argv[++i];
		if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc)
			mission = argv[++i];
		if (std::strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
			slot = argv[++i];
		if (std::strcmp(argv[i], "--ship") == 0 && i + 1 < argc)
			ship = argv[++i];
		if (argument == "--seat" && i + 1 < argc)
			seat = argv[++i];
		if (std::strcmp(argv[i], "--console") == 0)
			console = true;
		if (std::strcmp(argv[i], "--runtime-save") == 0 && i + 1 < argc)
			runtime_save = argv[++i];
		if (std::strcmp(argv[i], "--resume-local") == 0)
			resume_local = true;
	}
	if (!app.network_code.empty())
	{
		const auto target = phoenix::parse_join_code(app.network_code);
		network.link.room = target.room;
		network.server = target.server;
	}
	network.inspector = gm;
	if (!remote)
		network.link.self = network.server;
	try
	{
		if (!custom_data)
			network.data_location = phoenix::native_data_directory();
		app.session = remote ? phoenix::make_network_client(network) : phoenix::make_game_lobby(network, local_only);
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	if (remote)
	{
		app.can_host = false;
		app.screen = gm ? phoenix::Screen::gm : phoenix::Screen::client_lobby;
	}
	app.network_service = network.link.url;
	app.network_profile = network.profile;
	app.connect = [&app, network](const std::string &code, const std::string &service, const std::string &profile,
	                              phoenix::JoinRole role) mutable
	{
		const auto target = phoenix::parse_join_code(code);
		auto options = network;
		options.link.room = target.room;
		options.server = target.server;
		options.link.self.clear();
		options.link.url = service;
		options.profile = profile;
		options.inspector = role == phoenix::JoinRole::gm;
		options.player_name = app.player_name;
		options.avatar = app.avatar;
		phoenix::verify_link_options({service, options.link.room, options.server});
		auto connected = phoenix::join_game_lobby(options, role);
		app.session = std::move(connected);
		app.can_host = role == phoenix::JoinRole::ship_server;
		app.screen = app.can_host ? phoenix::Screen::server_lobby : phoenix::Screen::client_lobby;
		app.network_code = code;
		app.network_service = service;
		app.network_profile = profile;
	};
	app.new_host = [&app, network, local_only]()
	{
		auto options = network;
		options.link.room.clear();
		options.link.self.clear();
		options.server.clear();
		options.link.url = app.network_service;
		options.profile = app.network_profile;
		options.player_name = app.player_name;
		options.avatar = app.avatar;
		return phoenix::make_game_lobby(options, local_only);
	};
	app.return_home = [&app]()
	{
		if (app.reset_displays)
			app.reset_displays();
		auto candidate = app.new_host();
		if (app.library)
			candidate->replace_packs(app.library->archives(app.library->packs));
		if (app.session)
			app.session->lobby_command({phoenix::LobbyAction::leave});
		app.session = std::move(candidate);
		app.can_host = true;
		app.screen = phoenix::Screen::home;
		app.menu_branch = phoenix::Application::MenuBranch::root;
	};
	try
	{
		app.library = std::make_shared<phoenix::MenuLibrary>(network.data_location + "/menu");
		app.library->load();
		if (!remote)
			app.library->install_bundled(
			    phoenix::read_file(std::string(GetApplicationDirectory()) + "assets/base.zip"),
			    phoenix::read_file(std::string(GetApplicationDirectory()) + "assets/training.zip"));
		const auto &prefs = app.library->preferences;
		app.music_volume = prefs.volume;
		app.sfx_volume = prefs.sfx_volume;
		app.master_volume = prefs.master_volume;
		app.language = prefs.language;
		app.pseudo_locale = prefs.pseudo;
		app.reduced_motion = prefs.reduced_motion;
		app.dust_motes = prefs.dust_motes;
		app.reference_grid = prefs.reference_grid;
		app.player_name = prefs.player_name;
		app.avatar = prefs.avatar;
		app.session->set_player_profile(app.player_name, app.avatar);
		if (!remote)
			app.session->replace_packs(app.library->archives(app.library->packs));
	}
	catch (const std::exception &error)
	{
		app.status = error.what();
	}
	phoenix::open_window();
	app.host_display = GetCurrentMonitor();
	for (int monitor = 0; monitor < GetMonitorCount() && app.native_displays.size() < 6; ++monitor)
	{
		const auto position = GetMonitorPosition(monitor);
		if (std::any_of(app.native_displays.begin(), app.native_displays.end(),
		                [&](const auto &display)
		                {
			                const auto other = GetMonitorPosition(display.id);
			                return other.x == position.x && other.y == position.y;
		                }))
			continue;
		app.native_displays.push_back(
		    {monitor, "Screen " + std::to_string(monitor + 1) + " (" + GetMonitorName(monitor) + ")"});
	}
	bool lobby_span = false;
	const auto apply_frame_pacing = [&]()
	{
		SetTargetFPS(frame_limit ? static_cast<int>(frame_limit)
		             : (vsync || uncapped || !app.console_regions.empty() || app.gm_region) ? 0
		                                                                                    : 60);
	};
	app.apply_displays = [&app, &lobby_span, &apply_frame_pacing]()
	{
		if (app.display_consoles.empty() && app.inspector_display < 0)
		{
			if (lobby_span && app.reset_displays)
				app.reset_displays();
			return;
		}
		std::vector<int> monitors{app.host_display};
		for (const auto &[monitor, console] : app.display_consoles)
			monitors.push_back(monitor);
		if (app.inspector_display >= 0)
			monitors.push_back(app.inspector_display);
		float left = GetMonitorPosition(app.host_display).x, top = GetMonitorPosition(app.host_display).y;
		float right = left, bottom = top;
		for (const auto monitor : monitors)
		{
			const auto position = GetMonitorPosition(monitor);
			left = std::min(left, position.x);
			top = std::min(top, position.y);
			right = std::max(right, position.x + GetMonitorWidth(monitor));
			bottom = std::max(bottom, position.y + GetMonitorHeight(monitor));
		}
		std::vector<phoenix::ConsoleRegion> all;
		app.console_regions.clear();
		app.viewscreen_region.reset();
		app.gm_region.reset();
		for (const auto monitor : monitors)
		{
			const auto position = GetMonitorPosition(monitor);
			phoenix::ConsoleRegion region{static_cast<std::uint64_t>(monitor + 2), position.x - left, position.y - top,
			                              static_cast<float>(GetMonitorWidth(monitor)),
			                              static_cast<float>(GetMonitorHeight(monitor))};
			all.push_back(region);
			if (monitor == app.host_display)
				app.viewscreen_region = region;
			else if (monitor == app.inspector_display)
				app.gm_region = region;
			else
				app.console_regions.push_back(region);
		}
		phoenix::InputRouter validator;
		if (all.size() > 6 || !validator.set_regions(all))
			throw std::runtime_error("Assigned screens overlap or exceed the six-screen limit");
		SetWindowState(FLAG_WINDOW_UNDECORATED);
		ClearWindowState(FLAG_WINDOW_RESIZABLE);
		SetWindowPosition(static_cast<int>(left), static_cast<int>(top));
		SetWindowSize(static_cast<int>(right - left), static_cast<int>(bottom - top));
		lobby_span = true;
		apply_frame_pacing();
	};
	app.reset_displays = [&app, &lobby_span, &apply_frame_pacing]()
	{
		if (lobby_span)
		{
			ClearWindowState(FLAG_WINDOW_UNDECORATED);
			SetWindowState(FLAG_WINDOW_RESIZABLE);
			SetWindowSize(1280, 720);
			const auto position = GetMonitorPosition(app.host_display);
			SetWindowPosition(static_cast<int>(position.x + 40), static_cast<int>(position.y + 40));
			app.console_regions.clear();
			app.viewscreen_region.reset();
			app.gm_region.reset();
		}
		app.display_consoles.clear();
		app.local_player_names.clear();
		app.inspector_display = -1;
		lobby_span = false;
		apply_frame_pacing();
	};
	try
	{
		phoenix::select_language(app.language, app.pseudo_locale);
	}
	catch (const std::exception &error)
	{
		app.language = "en";
		app.status = error.what();
	}
	if (span_displays && display_layout.empty())
	{
		const int count = GetMonitorCount();
		float left = 0, top = 0, right = 0, bottom = 0;
		for (int monitor = 0; monitor < count; ++monitor)
		{
			const auto position = GetMonitorPosition(monitor);
			const float width = static_cast<float>(GetMonitorWidth(monitor)),
			            height = static_cast<float>(GetMonitorHeight(monitor));
			if (app.console_regions.size() == 6 || std::any_of(app.console_regions.begin(), app.console_regions.end(),
			                                                   [&](const auto &region)
			                                                   {
				                                                   return position.x < region.x + region.width &&
				                                                          position.x + width > region.x &&
				                                                          position.y < region.y + region.height &&
				                                                          position.y + height > region.y;
			                                                   }))
				continue;
			if (!monitor)
			{
				left = position.x;
				top = position.y;
			}
			left = std::min(left, position.x);
			top = std::min(top, position.y);
			right = std::max(right, position.x + width);
			bottom = std::max(bottom, position.y + height);
			app.console_regions.push_back(
			    {static_cast<std::uint64_t>(app.console_regions.size() + 1), position.x, position.y, width, height});
			std::cout << "Display " << monitor + 1 << ": " << GetMonitorName(monitor) << " / " << position.x << ','
			          << position.y << " / " << width << 'x' << height << '\n';
		}
		for (auto &region : app.console_regions)
		{
			region.x -= left;
			region.y -= top;
		}
		if (app.console_regions.size() > 1)
		{
			app.viewscreen_region = app.console_regions.front();
			app.console_regions.erase(app.console_regions.begin());
		}
		SetWindowState(FLAG_WINDOW_UNDECORATED);
		ClearWindowState(FLAG_WINDOW_RESIZABLE);
		SetWindowSize(static_cast<int>(right - left), static_cast<int>(bottom - top));
		SetWindowPosition(static_cast<int>(left), static_cast<int>(top));
	}
	if (!display_layout.empty())
	{
		try
		{
			const auto bytes = phoenix::read_file(display_layout);
			const auto layout = nlohmann::json::parse(bytes);
			const int width = layout.at("width"), height = layout.at("height");
			if (width < 320 || height < 240 || width > 16384 || height > 16384)
				throw std::runtime_error("Invalid display window dimensions");
			std::vector<phoenix::ConsoleRegion> all_regions;
			for (const auto &region : layout.at("regions"))
			{
				phoenix::ConsoleRegion value{region.at("route"), region.at("x"), region.at("y"), region.at("width"),
				                             region.at("height")};
				if (value.x < 0 || value.y < 0 || value.x + value.width > width || value.y + value.height > height)
					throw std::runtime_error("Console region is outside the display window");
				all_regions.push_back(value);
				if (region.value("role", std::string("console")) == "viewscreen")
				{
					if (app.viewscreen_region)
						throw std::runtime_error("Only one Viewscreen region is supported");
					app.viewscreen_region = value;
				}
				else if (region.value("role", std::string("console")) == "gm")
				{
					if (app.gm_region || !app.can_host)
						throw std::runtime_error("A local GM region requires one native host");
					app.gm_region = value;
				}
				else if (region.value("role", std::string("console")) == "console")
					app.console_regions.push_back(value);
				else
					throw std::runtime_error("Unknown display region role");
			}
			phoenix::InputRouter validator;
			if (app.console_regions.empty() || all_regions.size() > 6 || !validator.set_regions(all_regions))
				throw std::runtime_error("Display layout requires one to six non-overlapping Console regions");
			SetWindowState(FLAG_WINDOW_UNDECORATED);
			ClearWindowState(FLAG_WINDOW_RESIZABLE);
			SetWindowSize(width, height);
			SetWindowPosition(layout.value("x", 0), layout.value("y", 0));
		}
		catch (const std::exception &error)
		{
			std::cerr << "Display layout: " << error.what() << '\n';
			phoenix::close_window();
			return 1;
		}
	}
	if (vsync)
		SetWindowState(FLAG_VSYNC_HINT);
	apply_frame_pacing();
	if (argc > 1 && std::strcmp(argv[1], "--platform-check") == 0)
	{
		app.native_menu = false;
		app.screen = phoenix::Screen::client_lobby;
		BeginDrawing();
		phoenix::draw_screen(app);
		EndDrawing();
		bool passed = phoenix::probe_native_accessibility(app);
		phoenix::close_window();
		std::cout << "Native UIA label/bounds/focus/invoke queue: " << (passed ? "PASS" : "FAIL") << '\n';
		return passed ? 0 : 1;
	}
	phoenix::begin_tools();
	if (!game_pack.empty())
	{
		try
		{
			const auto archive = phoenix::read_file(game_pack);
			const auto imported_hash = phoenix::read_pack(archive).hash();
			if (menu_smoke.enabled)
			{
				app.library->install(archive);
				auto selected = app.library->packs;
				for (auto &pack : selected)
					if (pack.hash == imported_hash)
						pack.enabled = true;
				app.library->apply(selected);
			}
			if (std::none_of(app.session->catalog().ordered_packs.begin(), app.session->catalog().ordered_packs.end(),
			                 [&](const auto &pack)
			                 {
				                 return pack.second == imported_hash;
			                 }))
				app.session->add_pack(archive);
			if (!mission.empty())
			{
				if (seat.empty())
				{
					const auto available = phoenix::resolve_consoles(app.session->catalog(), ship);
					if (!available.empty())
						seat = available.front().id;
				}
				app.session->start({mission, {{slot, ship}}, slot, seat});
				app.screen =
				    console || !app.console_regions.empty() ? phoenix::Screen::console : phoenix::Screen::viewscreen;
			}
		}
		catch (const std::exception &error)
		{
			std::cerr << error.what() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return 1;
		}
	}
	if (!runtime_save.empty())
	{
		try
		{
			if (resume_local)
				app.session = local_only ? phoenix::make_local_session() : phoenix::make_network_host(network);
			app.session->restore(phoenix::read_file(runtime_save));
			app.screen = phoenix::Screen::server_lobby;
			if (resume_local)
			{
				app.session->resume_local();
				app.screen = console ? phoenix::Screen::console : phoenix::Screen::viewscreen;
			}
			std::cout << "Verified runtime save at tick " << app.session->ticks()
			          << (resume_local ? " / local console reclaimed\n" : " / recovery paused\n");
		}
		catch (const std::exception &error)
		{
			std::cerr << error.what() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return 1;
		}
	}
	if (!workshop_pack.empty())
	{
		try
		{
			phoenix::open_workshop_pack(workshop_pack);
		}
		catch (const std::exception &error)
		{
			std::cerr << error.what() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return 1;
		}
	}
	double accumulated = 0;
	if (!performance.output.empty())
	{
		app.session->release_console(1);
		for (const auto &region : app.console_regions)
			for (const auto &seat : app.session->console_seats())
				if (seat.available && app.session->claim_console(region.console, seat.address))
					break;
	}
	menu_smoke.configure_displays(app);
	const auto smoke_started = GetTime();
	menu_smoke.changed = smoke_started;
	double previous_frame = smoke_started;
	while (!app.exit_requested && !phoenix::close_requested())
	{
		const auto frame_started = GetTime();
		app.session->poll();
		accumulated += std::min(static_cast<double>(GetFrameTime()), 0.25);
		while (accumulated >= 1.0 / phoenix::World::ticks_per_second)
		{
			app.session->tick();
			accumulated -= 1.0 / phoenix::World::ticks_per_second;
		}
		phoenix::pump_native_workshop_test(app, GetFrameTime());
		const auto simulation_finished = GetTime();
		if (app.screen != phoenix::Screen::console && app.screen != phoenix::Screen::viewscreen)
			app.tools_keyboard_focus = false;
		if (app.gm_region && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		{
			const auto &r = *app.gm_region;
			app.tools_keyboard_focus = CheckCollisionPointRec(GetMousePosition(), {r.x, r.y, r.width, r.height});
		}
		BeginDrawing();
		phoenix::draw_screen(app);
		if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm)
			phoenix::draw_workshop(app);
		else if ((app.screen == phoenix::Screen::console || app.screen == phoenix::Screen::viewscreen) && app.gm_region)
			phoenix::draw_gm_region(app);
		const auto drawing_finished = GetTime();
		EndDrawing();
		const auto frame_finished = GetTime();
		if (performance.record(simulation_finished - frame_started, drawing_finished - simulation_finished,
		                       frame_finished - previous_frame, app))
			break;
		previous_frame = frame_finished;
		if (menu_smoke.enabled)
		{
			try
			{
				if (menu_smoke.frame(app))
					break;
			}
			catch (const std::exception &error)
			{
				std::cerr << error.what() << '\n';
				phoenix::end_tools();
				phoenix::close_window();
				return 1;
			}
			if (GetTime() - smoke_started > (menu_smoke.assign_displays ? 90 : 30))
			{
				std::cerr << "Menu smoke timed out at stage " << menu_smoke.stage << " / tick " << app.session->ticks()
				          << '\n';
				phoenix::end_tools();
				phoenix::close_window();
				return 1;
			}
		}
		if (join_smoke)
		{
			if (!join_submitted)
			{
				phoenix::invoke_navigation(app, "game.connect-client");
				join_submitted = true;
			}
			if (!claim_submitted)
				for (const auto &[id, label] : app.session->lobby_controls())
					if (id == "network.claim")
					{
						phoenix::invoke_navigation(app, id);
						claim_submitted = true;
					}
			if (app.session->observation() && app.screen == phoenix::Screen::console)
				++join_ready_frames;
			else
				join_ready_frames = 0;
			if (join_ready_frames >= 3)
			{
				TakeScreenshot("network-console-smoke.png");
				std::cout << "Native join form, station claim and permitted console: PASS\n";
				break;
			}
			if (GetTime() - smoke_started > 20)
			{
				std::cerr << "Native join smoke failed: " << app.session->connection_status() << '\n';
				phoenix::end_tools();
				phoenix::close_window();
				return 1;
			}
		}
		if (qr_smoke && smoke_frames == 6)
			phoenix::invoke_navigation(app, "game.qr-client");
		if (helm_smoke)
		{
			if (smoke_frames == 80)
			{
				helm_smoke_tick = app.session->ticks();
				if (!phoenix::probe_console_helm(app, 1))
					throw std::runtime_error("Helm pointer probe failed");
			}
			if (smoke_frames == 60)
			{
				app.settings_console = 1;
				phoenix::invoke_navigation(app, "settings");
				phoenix::invoke_navigation(app, "settings.tab/session");
			}
			if (smoke_frames == 50 && !phoenix::invoke_navigation(app, "settings.away"))
				throw std::runtime_error("Settings Away action failed");
			if (smoke_frames == 30 && !phoenix::invoke_navigation(app, "settings.resume"))
				throw std::runtime_error("Settings resume action failed");
			if (smoke_frames == 20)
				phoenix::invoke_navigation(app, "settings.close");
			if (smoke_frames == 1)
			{
				if (app.session->ticks() <= helm_smoke_tick)
					throw std::runtime_error("Helm input stalled simulation");
				std::cout << "Helm smoke passed: direct throttle, heading, Settings Away/resume and advancing ticks\n";
			}
		}
		if (smoke_frames && --smoke_frames == 0)
		{
			TakeScreenshot(qr_smoke                                    ? "invitation-smoke.png"
			               : workshop_smoke                            ? "workshop-smoke.png"
			               : app.screen == phoenix::Screen::console    ? "console-smoke.png"
			               : app.screen == phoenix::Screen::viewscreen ? "viewscreen-smoke.png"
			                                                           : "home-smoke.png");
			break;
		}
	}
	phoenix::end_tools();
	phoenix::close_window();
	if (!performance.output.empty() && !performance.completed)
	{
		std::cerr << "Manual performance capture ended before the requested sample count\n";
		return 1;
	}
}
