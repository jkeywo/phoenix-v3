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
import phoenix.presentation;
import phoenix.session.api;
import phoenix.session.local;
import phoenix.simulation;
import phoenix.tools;
#include "performance.hpp"

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
	app.can_edit = true;
	phoenix::NetworkOptions network;
	network.link.url = "ws://127.0.0.1:9080/phoenix";
	bool remote = false, gm = false, local_only = false;
	bool qr_smoke = false;
	bool join_smoke = false, join_submitted = false, claim_submitted = false;
	unsigned join_ready_frames = 0;
	int smoke_frames = 0;
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
			network.data_location = argv[++i];
		if (argument == "--code" && i + 1 < argc)
			app.network_code = argv[++i];
		if (argument == "--page" && i + 1 < argc)
			app.join_page = argv[++i];
		if (argument == "--qr-smoke")
		{
			qr_smoke = true;
			smoke_frames = 6;
			app.screen = phoenix::Screen::server_lobby;
		}
		if (argument == "--join-smoke")
		{
			join_smoke = true;
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
		auto separator = app.network_code.find('/');
		network.link.room = app.network_code.substr(0, separator);
		if (separator != std::string::npos)
			network.server = app.network_code.substr(separator + 1);
	}
	network.inspector = gm;
	if (!remote)
		network.link.self = network.server;
	try
	{
		app.session = local_only ? phoenix::make_local_session()
		              : remote   ? phoenix::make_network_client(network)
		                         : phoenix::make_network_host(network);
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
	                              bool inspector) mutable
	{
		const auto split = code.find('/');
		if (split == std::string::npos || !split || split + 1 == code.size())
			throw std::runtime_error("Enter a network code in room/server form");
		auto options = network;
		options.link.room = code.substr(0, split);
		options.server = code.substr(split + 1);
		options.link.self.clear();
		options.link.url = service;
		options.profile = profile;
		options.inspector = inspector;
		phoenix::verify_link_options({service, options.link.room, options.server});
		auto connected = phoenix::make_network_client(options);
		app.session = std::move(connected);
		app.can_host = false;
		app.screen = inspector ? phoenix::Screen::gm : phoenix::Screen::client_lobby;
		app.network_code = code;
		app.network_service = service;
		app.network_profile = profile;
	};
	phoenix::open_window();
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
	if (vsync || uncapped || !app.console_regions.empty())
		SetTargetFPS(0);
	if (frame_limit)
		SetTargetFPS(static_cast<int>(frame_limit));
	if (argc > 1 && std::strcmp(argv[1], "--platform-check") == 0)
	{
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
			app.session->add_pack(phoenix::read_file(game_pack));
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
	const auto smoke_started = GetTime();
	double previous_frame = smoke_started;
	while (!phoenix::close_requested())
	{
		const auto frame_started = GetTime();
		app.session->poll();
		accumulated += std::min(static_cast<double>(GetFrameTime()), 0.25);
		while (accumulated >= 1.0 / phoenix::World::ticks_per_second)
		{
			app.session->tick();
			accumulated -= 1.0 / phoenix::World::ticks_per_second;
		}
		const auto simulation_finished = GetTime();
		if (app.screen != phoenix::Screen::console)
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
		else if (app.screen == phoenix::Screen::console && app.gm_region)
			phoenix::draw_gm_region(app);
		const auto drawing_finished = GetTime();
		EndDrawing();
		const auto frame_finished = GetTime();
		if (performance.record(simulation_finished - frame_started, drawing_finished - simulation_finished,
		                       frame_finished - previous_frame, app))
			break;
		previous_frame = frame_finished;
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
