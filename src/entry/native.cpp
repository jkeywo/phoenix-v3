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
	std::string mission, slot, ship;
	bool console = false;
	std::string runtime_save;
	bool resume_local = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
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
	if (argc > 1 && std::strcmp(argv[1], "--platform-check") == 0)
	{
		app.screen = phoenix::Screen::client_lobby;
		bool passed = phoenix::probe_native_accessibility(app);
		phoenix::close_window();
		std::cout << "Native UIA label/invoke queue: " << (passed ? "PASS" : "FAIL") << '\n';
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
				app.session->start({mission, {{slot, ship}}, slot});
				app.screen = console ? phoenix::Screen::console : phoenix::Screen::viewscreen;
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
	const auto smoke_started = GetTime();
	while (!phoenix::close_requested())
	{
		app.session->poll();
		accumulated += std::min(static_cast<double>(GetFrameTime()), 0.25);
		while (accumulated >= 1.0 / phoenix::World::ticks_per_second)
		{
			app.session->tick();
			accumulated -= 1.0 / phoenix::World::ticks_per_second;
		}
		BeginDrawing();
		phoenix::draw_screen(app);
		if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm)
			phoenix::draw_workshop(app);
		EndDrawing();
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
}
