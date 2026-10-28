#include <raylib.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

import phoenix.content.controllers;
import phoenix.content.distribution;
import phoenix.navigation;
import phoenix.network.client;
import phoenix.network.host;
import phoenix.network.lobby;
import phoenix.presentation;
import phoenix.native.displays;
import phoenix.session.api;
import phoenix.session.local;
import phoenix.simulation;
import phoenix.tools;
import phoenix.workshop_test;
#include "../browser/workshop_bridge.hpp"
#include "../diagnostics/performance.hpp"
#include "../../../tests/manual/native_smokes.hpp"

int main(int argc, char **argv)
try
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
	NativeSmoke smoke;
	std::string inference_pair;
	std::string workshop_pack;
	std::string game_pack;
	std::string base_file;
	std::string mission, slot, ship, seat;
	bool console = false;
	std::string runtime_save;
	std::string display_layout;
	bool span_displays = false;
	bool vsync = false, uncapped = false;
	unsigned frame_limit = 0;
	phoenix::FrameCapture performance;
	bool resume_local = false, synchronous_session = false;
	phoenix::Bytes launch_bytes;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--synchronous-session")
			synchronous_session = true;
		if (argument == "--base")
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--base requires a pack file\n";
				return 1;
			}
			base_file = argv[++i];
			if (base_file.empty())
			{
				std::cerr << "--base requires a nonempty pack file\n";
				return 1;
			}
		}
		if (argument == "--base-url" && i + 1 < argc)
			app.base_reference = argv[++i];
		if (argument == "--inference-pair" && i + 1 < argc)
			inference_pair = argv[++i];
		if (argument == "--ai-config-smoke")
			smoke.ai_config = true;
		if (argument == "--menu-intro-smoke")
			smoke.menu.enabled = smoke.menu.intro = true;
		if (argument == "--menu-smoke")
			smoke.menu.enabled = true;
		if (argument == "--menu-smoke-large")
			smoke.menu.enabled = smoke.menu.large = true;
		if (argument == "--lobby-regions-smoke")
		{
			smoke.menu.enabled = smoke.menu.assign_displays = smoke.menu.virtual_displays = true;
		}
		if (argument == "--display-transition-smoke")
		{
			smoke.menu.enabled = smoke.menu.assign_displays = smoke.menu.virtual_displays =
			    smoke.menu.display_transition_only = true;
		}
		if (argument == "--lobby-monitor-list-smoke")
		{
			smoke.menu.enabled = smoke.menu.assign_displays = smoke.menu.display_inventory_only = true;
		}
		if (argument == "--lobby-displays-smoke")
		{
			smoke.menu.enabled = true;
			smoke.menu.assign_displays = true;
		}
		if (argument == "--bridge-ui-smoke" && i + 2 < argc)
		{
			smoke.bridge_width = std::clamp(std::stoi(argv[++i]), 640, 3840);
			smoke.bridge_height = std::clamp(std::stoi(argv[++i]), 480, 2160);
			smoke.frames = 150;
		}
		if (argument == "--capture-frames" && i + 1 < argc)
			smoke.frames = std::clamp(std::stoi(argv[++i]), 1, 3600);
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
		if (argument == "--performance-seconds" && i + 1 < argc)
		{
			performance.seconds = std::stod(argv[++i]);
			if (!(performance.seconds >= 1 && performance.seconds <= 600))
				throw std::runtime_error("Performance capture duration must be 1 to 600 seconds");
		}
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
			smoke.qr = true;
			app.native_menu = false;
			smoke.frames = 6;
			app.screen = phoenix::Screen::server_lobby;
		}
		if (argument == "--gui-navigation-smoke")
		{
			smoke.gui = true;
			smoke.frames = 100;
		}
		if (argument == "--tools-presentation-smoke")
		{
			smoke.tools_presentation = true;
			smoke.frames = 100;
			app.screen = phoenix::Screen::workshop;
		}

		if (argument == "--readout-smoke")
		{
			smoke.readout = true;
			smoke.frames = 90;
		}
		if (argument == "--helm-smoke")
		{
			smoke.helm = true;
			smoke.frames = 90;
		}
		if (argument == "--controller-report-smoke")
		{
			smoke.controller_report = smoke.helm = true;
			smoke.frames = 90;
		}
		if (argument == "--personnel-smoke")
		{
			smoke.personnel = true;
			smoke.frames = 90;
		}
		if (argument == "--engineering-smoke")
		{
			smoke.engineering = true;
			smoke.frames = 90;
		}
		if (argument == "--on-screen-smoke")
		{
			smoke.on_screen = true;
			smoke.frames = 150;
		}
		if (argument == "--navigation-smoke")
		{
			smoke.navigation = true;
			smoke.frames = 50;
		}
		if (argument == "--join-smoke")
		{
			smoke.join = true;
			app.native_menu = false;
			remote = true;
		}
		if (std::strcmp(argv[i], "--smoke") == 0)
			smoke.frames = 6;
		if (argument == "--controller-workshop-smoke")
		{
			smoke.controller_workshop = smoke.workshop = true;
			app.screen = phoenix::Screen::workshop;
			smoke.frames = 180;
		}
		if (std::strcmp(argv[i], "--workshop") == 0)
		{
			app.screen = phoenix::Screen::workshop;
			smoke.workshop = true;
		}
		if (std::strcmp(argv[i], "--workshop-pack") == 0 && i + 1 < argc)
		{
			workshop_pack = argv[++i];
			app.screen = phoenix::Screen::workshop;
			smoke.workshop = true;
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
	const auto content_started = std::chrono::steady_clock::now();
	const auto content_elapsed = [&]
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - content_started).count();
	};
	const std::string root_path =
	    base_file.empty() ? std::string(GetApplicationDirectory()) + "assets/base.zip" : base_file;
	phoenix::Json root_menu;
	bool staged = !remote && game_pack.empty() && mission.empty() && runtime_save.empty() && workshop_pack.empty() &&
	              app.screen == phoenix::Screen::home;
	try
	{
		try
		{
			root_menu = phoenix::parse_json(phoenix::read_file(root_path + ".menu.json", 1024 * 1024), "root menu");
			phoenix::MenuLibrary metadata("unused");
			metadata.launch_metadata(root_menu);
			network.expected_base = metadata.base();
		}
		catch (...)
		{
			staged = false;
			launch_bytes = phoenix::read_file(root_path);
			const auto root = phoenix::read_pack(launch_bytes);
			network.expected_base = phoenix::base_configuration(root);
			root_menu = phoenix::pack_menu({root});
		}

		if (!base_file.empty() && app.base_reference == "base.zip")
			app.base_reference = base_file.substr(
			    base_file.find_last_of("/\\") == std::string::npos ? 0 : base_file.find_last_of("/\\") + 1);
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
		if (app.library)
			options.expected_base = app.library->base();
		for (const auto &pack : app.library->packs)
			if (!pack.url.empty())
				options.pack_urls[pack.hash] = pack.url;
		options.pack_urls[options.expected_base.hash] = app.base_reference;
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
		if (app.library)
			options.expected_base = app.library->base();
		for (const auto &pack : app.library->packs)
			if (!pack.url.empty())
				options.pack_urls[pack.hash] = pack.url;
		options.pack_urls[options.expected_base.hash] = app.base_reference;
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
		if (app.library && !app.content_loading)
			candidate->replace_packs(app.library->archives(app.library->packs));
		if (app.session)
			app.session->lobby_command({phoenix::LobbyAction::leave});
		app.session = std::move(candidate);
		app.can_host = true;
		app.screen = phoenix::Screen::home;
		app.menu_branch = phoenix::Application::MenuBranch::root;
	};
	struct PreparedMenu
	{
		std::shared_ptr<phoenix::MenuLibrary> library;
		std::shared_ptr<phoenix::SessionPort> session;
		std::uint64_t revision = 0;
	};
	struct PendingMenu
	{
		std::uint64_t revision;
		std::future<PreparedMenu> future;
	};
	std::vector<PendingMenu> preparations;
	std::function<void()> retry_content;
	bool start_content_after_frame = false;
	std::uint64_t content_revision = 0;
	auto prepare_menu = [root_path, network, local_only, base_file, remote, base_reference = app.base_reference]()
	{
		PreparedMenu result;
		result.library = std::make_shared<phoenix::MenuLibrary>(network.data_location + "/menu",
		                                                        network.profile + "/" + network.expected_base.id);
		result.library->load();
		result.library->defer_index_writes(true);
		auto bytes = phoenix::read_file(root_path);
		if (base_file.empty())
		{
			const auto addon =
			    phoenix::read_file((std::filesystem::path(root_path).parent_path() / "test-missions.zip").string());
			result.library->install_bundled(bytes, addon);
			for (const auto &entry : result.library->packs)
				if (entry.id == "phoenix-base")
					bytes = result.library->read_pack_archive(entry.hash);
		}
		result.library->launch_base(bytes);
		auto options = network;
		options.expected_base = result.library->base();
		options.pack_urls[options.expected_base.hash] = base_reference;
		for (const auto &pack : result.library->packs)
			if (!pack.url.empty())
				options.pack_urls[pack.hash] = pack.url;
		result.session = remote ? phoenix::make_network_client(options) : phoenix::make_game_lobby(options, local_only);
		if (!remote)
			result.session->replace_packs(result.library->archives(result.library->packs));
		return result;
	};
	try
	{
		app.library = std::make_shared<phoenix::MenuLibrary>(network.data_location + "/menu",
		                                                     network.profile + "/" + network.expected_base.id);
		app.library->load();
		app.library->launch_metadata(root_menu);
		const auto &prefs = app.library->preferences;
		app.music_volume = prefs.volume;
		app.sfx_volume = prefs.sfx_volume;
		app.siren_volume = prefs.siren_volume;
		app.master_volume = prefs.master_volume;
		app.tools_scale = prefs.tools_scale;
		app.remembered_station_displays = prefs.station_displays;
		app.language = prefs.language;
		app.pseudo_locale = prefs.pseudo;
		app.reduced_motion = prefs.reduced_motion;
		app.dust_motes = prefs.dust_motes;
		app.reference_grid = prefs.reference_grid;
		app.player_name = prefs.player_name;
		app.avatar = prefs.avatar;
		app.session->set_player_profile(app.player_name, app.avatar);
		if (staged)
		{
			std::vector<phoenix::Json> menus{root_menu};
			for (const auto &pack : app.library->packs)
				if (pack.enabled && pack.id != network.expected_base.id)
					menus.push_back(app.library->metadata(pack));
			app.menu_catalog = std::make_shared<phoenix::Catalog>(phoenix::menu_catalog(menus));
			app.content_loading = true;
			retry_content = [&]()
			{
				app.content_error.clear();
				app.content_loading = true;
				preparations.push_back({content_revision, std::async(std::launch::async, prepare_menu)});
			};
			start_content_after_frame = true;
		}
		else if (!remote || gm)
		{
			auto ready = prepare_menu();
			ready.library->defer_index_writes(false);
			ready.library->remember();
			app.library = std::move(ready.library);
			app.session = std::move(ready.session);
		}
		app.change_packs = [&, network](std::vector<phoenix::LibraryPack> selected)
		{
			std::vector<phoenix::Json> menus;
			for (const auto &pack : selected)
				if (pack.enabled)
					menus.push_back(app.library->metadata(pack));
			app.menu_catalog = std::make_shared<phoenix::Catalog>(phoenix::menu_catalog(menus));
			app.content_loading = true;
			app.content_error.clear();
			app.content_action = {};
			const auto revision = ++content_revision;
			auto library = std::make_shared<phoenix::MenuLibrary>(*app.library);
			library->defer_index_writes(true);
			retry_content = [&, library, selected, revision, network, local_only]()
			{
				app.content_error.clear();
				app.content_loading = true;
				preparations.push_back({revision, std::async(std::launch::async,
				                                             [library, selected, revision, network, local_only,
				                                              base_reference = app.base_reference]()
				                                             {
					                                             library->apply(selected);
					                                             auto options = network;
					                                             options.expected_base = library->base();
					                                             options.pack_urls[options.expected_base.hash] =
					                                                 base_reference;
					                                             for (const auto &pack : selected)
						                                             if (!pack.url.empty())
							                                             options.pack_urls[pack.hash] = pack.url;
					                                             auto session =
					                                                 phoenix::make_game_lobby(options, local_only);
					                                             session->replace_packs(library->archives(selected));
					                                             return PreparedMenu{library, session, revision};
				                                             })});
			};
			retry_content();
		};
		app.await_content = [&](std::function<void()> action)
		{
			if (!app.content_error.empty() && retry_content)
				retry_content();
			if (app.content_loading)
				app.content_action = std::move(action);
			else
				action();
		};
	}
	catch (const std::exception &error)
	{
		std::cerr << "Launch failed: " << error.what() << "\n";
		return 1;
	}

	phoenix::open_window();
	phoenix::NativeDisplayAdapter displays(app, frame_limit, vsync, uncapped);
	try
	{
		phoenix::select_language(app.language, app.pseudo_locale);
	}
	catch (const std::exception &error)
	{
		app.language = "en";
		app.status = error.what();
	}
	try
	{
		displays.configure(span_displays, display_layout);
	}
	catch (const std::exception &error)
	{
		std::cerr << "Display layout: " << error.what() << '\n';
		phoenix::close_window();
		return 1;
	}
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
	if (game_pack.empty() && !mission.empty())
	{
		try
		{
			if (seat.empty())
			{
				auto seats = phoenix::resolve_consoles(app.session->catalog(), ship);
				if (!seats.empty())
					seat = seats.front().id;
			}
			app.session->start({mission, {{slot, ship}}, slot, seat});
			app.screen = console ? phoenix::Screen::console : phoenix::Screen::viewscreen;
		}
		catch (const std::exception &e)
		{
			std::cerr << e.what() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return 1;
		}
	}
	if (!game_pack.empty())
	{
		try
		{
			const auto imported = phoenix::read_pack_path(game_pack);
			const auto &archive = imported.archive();
			const auto imported_hash = imported.hash();
			const auto imported_id = imported.manifest().at("id").get<std::string>();
			if (smoke.menu.enabled)
			{
				app.library->install(archive);
				auto selected = app.library->packs;
				for (auto &pack : selected)
					if (pack.id == imported_id)
						pack.enabled = pack.hash == imported_hash;
				app.library->apply(selected);
			}
			if (std::none_of(app.session->catalog().ordered_packs.begin(), app.session->catalog().ordered_packs.end(),
			                 [&](const auto &pack)
			                 {
				                 return pack.second == imported_hash;
			                 }))
			{
				const auto &ordered = app.session->catalog().ordered_packs;
				const bool replacing = std::any_of(ordered.begin(), ordered.end(),
				                                   [&](const auto &pack)
				                                   {
					                                   return pack.first == imported_id;
				                                   });
				if (replacing && app.library)
				{
					auto candidate = app.library->archives(app.library->packs);
					for (auto &bytes : candidate)
						if (phoenix::read_pack(bytes).manifest().at("id") == imported_id)
							bytes = archive;
					app.session->replace_packs(candidate);
				}
				else
					app.session->add_pack(archive);
			}
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
			app.session->restore(phoenix::read_file(runtime_save, phoenix::runtime_save_archive_limit));
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
	if (!inference_pair.empty() && app.can_host)
		app.session->set_inference_connection(
		    std::make_shared<phoenix::InferenceConnection>(inference_pair, network.link.url));
	bool performance_released = false, performance_ready = false;
	smoke.menu.configure_displays(app);
	if (smoke.menu.large)
		SetWindowSize(std::min(3800, GetMonitorWidth(GetCurrentMonitor()) - 48),
		              std::min(2160, GetMonitorHeight(GetCurrentMonitor()) - 108));
	smoke.started = GetTime();
	smoke.menu.changed = smoke.started;
	double previous_frame = smoke.started;
	if (smoke.bridge_width)
		SetWindowSize(smoke.bridge_width, smoke.bridge_height);
	while (!app.exit_requested && !phoenix::close_requested())
	{
		displays.poll(!smoke.menu.virtual_displays && display_layout.empty() && !span_displays);
		if (!performance.output.empty())
			app.frame_metrics.clear();
		const auto frame_started = GetTime();
		for (auto it = preparations.begin(); it != preparations.end();)
		{
			if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				++it;
				continue;
			}
			try
			{
				auto ready = it->future.get();
				if (ready.revision == content_revision)
				{
					ready.library->preferences = app.library->preferences;
					ready.library->defer_index_writes(false);
					ready.library->remember();
					app.library = std::move(ready.library);
					if (app.screen == phoenix::Screen::home || app.content_action ||
					    app.screen == phoenix::Screen::settings)
						app.session = std::move(ready.session);
					app.session->set_player_profile(app.player_name, app.avatar);
					app.content_loading = false;
					std::cout << "Content verified at " << content_elapsed() << " seconds\n";
					if (app.content_action)
					{
						auto action = std::move(app.content_action);
						app.content_action = {};
						action();
					}
				}
			}
			catch (const std::exception &error)
			{
				// A superseded selection cannot cancel the current job.
				if (it->revision == content_revision)
				{
					app.content_error = error.what();
					app.status = error.what();
					app.content_loading = false;
				}
			}
			it = preparations.erase(it);
		}

		if (!synchronous_session && app.session)
			app.session = phoenix::make_frame_session(std::move(app.session));
		app.budget_console_preparation = !synchronous_session;
		app.session->poll();
		smoke.prepare(app);
		if (!performance.output.empty() && !performance_ready && app.session->running())
		{
			if (!performance_released)
			{
				app.session->release_console(1);
				performance_released = true;
			}
			for (const auto &region : app.console_regions)
				if (!app.session->console_observation(region.console))
					for (const auto &seat : app.session->console_seats())
						if (seat.available && app.session->claim_console(region.console, seat.address))
							break;
			performance_ready = std::all_of(app.console_regions.begin(), app.console_regions.end(),
			                                [&](const auto &region)
			                                {
				                                return app.session->console_observation(region.console).has_value();
			                                });
		}
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
		else if ((app.screen == phoenix::Screen::console || app.screen == phoenix::Screen::viewscreen ||
		          app.screen == phoenix::Screen::server_lobby) &&
		         app.gm_region)
			phoenix::draw_gm_region(app);
		const auto drawing_finished = GetTime();
		EndDrawing();
		const auto frame_finished = GetTime();
		if (start_content_after_frame)
		{
			start_content_after_frame = false;
			std::cout << "Menu first frame at " << content_elapsed() << " seconds\n";
			retry_content();
		}
		if (performance_ready &&
		    performance.record(simulation_finished - frame_started, drawing_finished - simulation_finished,
		                       frame_finished - previous_frame, app))
			break;
		previous_frame = frame_finished;
		const auto smoke_result = smoke.frame(app);
		if (smoke_result == NativeSmokeResult::Failed)
			return 1;
		if (smoke_result == NativeSmokeResult::Repeat)
			continue;
		if (smoke_result == NativeSmokeResult::Stop)
			break;
	}
	phoenix::end_tools();
	phoenix::close_window();
	if (!performance.output.empty() && !performance.completed)
	{
		std::cerr << "Manual performance capture ended before the requested sample count\n";
		return 1;
	}
}

catch (const std::exception &error)
{
	std::cerr << "Native application: " << error.what() << std::endl;
	return 1;
}
