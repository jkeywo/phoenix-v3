#include <raylib.h>
#include <emscripten.h>
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

import phoenix.presentation;
import phoenix.navigation;
import phoenix.session.api;
#if defined(PHOENIX_SERVER) && defined(PHOENIX_BROWSER_THREADS)
import phoenix.session.local;
#endif
#ifdef PHOENIX_SERVER
import phoenix.network.lobby;
#else
import phoenix.network.lobby.protocol;
#endif
#if defined(PHOENIX_WORKSHOP) || defined(PHOENIX_SERVER)
import phoenix.tools;
import phoenix.workshop.browser_test_client;
#endif
import phoenix.diagnostics.browser_profile;

phoenix::Application app;
double session_work_seconds = 0;
// Browser storage and page navigation belong to the shell; sessions remain in C++.
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, return_to_menu, (), { Module.phoenixReturnToMenu?.(); });
EM_JS(void, startup_phase, (const char *phase), { Module.phoenixStartupStage?.(UTF8ToString(phase)); });
// clang-format on
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_preferences_result(const char *error)
{
	phoenix::complete_preference_persistence(app, error);
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_session_pump()
{
	const double now = GetTime();
	static double previous = now;
	const double elapsed = std::clamp(now - previous, 0.0, 0.25);
	previous = now;
	static double last_persist = 0;
	if (GetTime() - last_persist > 1)
	{
		phoenix::persist_preferences(app);
		last_persist = GetTime();
	}
	if (app.native_menu && (app.screen == phoenix::Screen::home || app.exit_requested))
		return_to_menu();
#if defined(PHOENIX_SERVER) && defined(PHOENIX_BROWSER_THREADS)
	if (app.session)
		app.session = phoenix::make_frame_session(std::move(app.session));

#endif
	app.budget_console_preparation = true;
	if (app.session)
		app.session->poll();
#ifdef PHOENIX_SERVER
	static double accumulated = 0;
	accumulated += elapsed;
	while (accumulated >= 1.0 / 20)
	{
		if (app.session)
			app.session->tick();
		accumulated -= 1.0 / 20;
	}
#endif
	session_work_seconds += GetTime() - now;
}
void frame()
{
#if defined(PHOENIX_SERVER) && defined(PHOENIX_BROWSER_THREADS)
	static phoenix::BrowserFrameProfile profile(phoenix::frame_session_profile);
#else
	static phoenix::BrowserFrameProfile profile;
#endif
	const double started = GetTime();
	if (profile.enabled)
		app.frame_metrics.clear();
	BeginDrawing();
	phoenix::draw_screen(app);
#if defined(PHOENIX_WORKSHOP) || defined(PHOENIX_SERVER)
	phoenix::pump_browser_workshop_test(app);
	if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm)
		phoenix::draw_workshop(app);
#endif
	const double drawn = GetTime();
	EndDrawing();
	static bool first_frame = true;
	if (std::exchange(first_frame, false))
	{
		// The loading screen ends only once the runtime has presented a frame.
		EM_ASM({ Module.phoenixStartupReady ?.(); });
	}
	profile.record(started, drawn, GetTime(), std::exchange(session_work_seconds, 0.0), app);
}
int main(int argc, char **argv)
{
	phoenix::NetworkOptions network;
	network.link.url = phoenix::default_network_service;
	std::string action, scenario, slot, ship, save, base_file = "/assets/base.zip", menu_scope;
#ifdef PHOENIX_SERVER
	app.can_host = true;
#endif
#ifdef PHOENIX_WORKSHOP
	app.can_edit = true;
	app.screen = phoenix::Screen::workshop;
#endif
	for (int i = 1; i + 1 < argc; i += 2)
	{
		const std::string argument = argv[i], value = argv[i + 1];
		if (argument == "--action")
			action = value;
		if (argument == "--scenario")
			scenario = value;
		if (argument == "--slot")
			slot = value;
		if (argument == "--ship")
			ship = value;
		if (argument == "--base")
			base_file = value;
		if (argument == "--base-url")
			app.base_reference = value;
		if (argument == "--menu-scope")
			menu_scope = value;
		if (argument == "--save")
			save = value;
		if (std::string(argv[i]) == "--text-probe")
			app.text_probe = std::string(argv[i + 1]) == "1";
		if (std::string(argv[i]) == "--language")
			app.language = argv[i + 1];
		if (std::string(argv[i]) == "--relay")
			network.link.force_relay = std::string(argv[i + 1]) == "1";
		if (std::string(argv[i]) == "--code")
			app.network_code = argv[i + 1];
		if (std::string(argv[i]) == "--service")
			network.link.url = argv[i + 1];
		if (std::string(argv[i]) == "--profile")
			network.profile = argv[i + 1];
		if (std::string(argv[i]) == "--page")
			app.join_page = argv[i + 1];
#ifdef PHOENIX_WORKSHOP
		if (std::string(argv[i]) == "--mode" && std::string(argv[i + 1]) == "gm")
			app.screen = phoenix::Screen::gm;
#endif
	}
	app.network_service = network.link.url;
	app.network_profile = network.profile;
	app.return_home = []
	{
		return_to_menu();
	};
	if (!app.network_code.empty())
	{
		const auto target = phoenix::parse_join_code(app.network_code);
		network.link.room = target.room;
		network.server = target.server;
	}
	try
	{
		if (!action.empty())
		{
			startup_phase("Reading the launch base and saved settings…");
			app.native_menu = true;
#if defined(PHOENIX_SERVER) || defined(PHOENIX_WORKSHOP)
			const auto root_bytes = phoenix::read_file(base_file);
			const auto root = phoenix::base_configuration(phoenix::read_pack(root_bytes));
			app.library = std::make_shared<phoenix::MenuLibrary>(
			    "/launcher/menu", menu_scope.empty() ? network.profile + "/" + root.id : menu_scope);
			app.library->load();
			app.library->launch_base(root_bytes);
#else
			const auto menu =
			    phoenix::parse_json(phoenix::read_file(base_file + ".menu.json", 1024 * 1024), "root menu");
			app.library = std::make_shared<phoenix::MenuLibrary>(
			    "/launcher/menu",
			    menu_scope.empty() ? network.profile + "/" + menu.at("id").get<std::string>() : menu_scope);
			app.library->load();
			app.library->launch_metadata(menu);
#endif
			network.expected_base = app.library->base();
			for (const auto &pack : app.library->packs)
				if (!pack.url.empty())
					network.pack_urls[pack.hash] = pack.url;
			network.pack_urls[network.expected_base.hash] = app.base_reference;

			const auto &preferences = app.library->preferences;
			app.music_volume = preferences.volume;
			app.sfx_volume = preferences.sfx_volume;
			app.siren_volume = preferences.siren_volume;
			app.master_volume = preferences.master_volume;
			app.tools_scale = preferences.tools_scale;
			app.language = preferences.language;
			app.pseudo_locale = preferences.pseudo;
			app.reduced_motion = preferences.reduced_motion;
			app.dust_motes = preferences.dust_motes;
			app.reference_grid = preferences.reference_grid;
			app.player_name = preferences.player_name;
			app.avatar = preferences.avatar;
			network.player_name = app.player_name;
			network.avatar = app.avatar;
		}
#ifdef PHOENIX_SERVER
		startup_phase("Preparing the game lobby…");
		if (action.empty())
			network.link.self = network.server;
		if (action == "join")
			app.session = phoenix::join_game_lobby(network, phoenix::JoinRole::ship_server);
		else
		{
			app.session = phoenix::make_game_lobby(network);
			app.new_host = [network]
			{
				return phoenix::make_game_lobby(network);
			};
			if (app.library)
			{
				startup_phase("Loading selected mods into the game…");
				app.session->replace_packs(app.library->archives(app.library->packs));
			}
			startup_phase("Preparing the selected level…");
			if (action == "recover")
				app.session->restore(app.library->read_save(save));
			else if (action == "host")
				app.session->prepare_lobby({scenario, {{slot, ship}}, slot, ""});
			else if (action == "host-gm")
			{
				phoenix::GameSetup setup{scenario};
				setup.gm_host = true;
				app.session->prepare_lobby(setup);
			}
		}
		app.screen = action.empty() ? phoenix::Screen::home : phoenix::Screen::server_lobby;
#elif defined(PHOENIX_WORKSHOP)
		if (app.screen == phoenix::Screen::gm)
		{
			network.inspector = true;
			app.session = phoenix::join_client_lobby(network, phoenix::JoinRole::gm);
			app.screen = phoenix::Screen::client_lobby;
		}
#else
		app.screen = phoenix::Screen::client_lobby;
		app.session = phoenix::join_client_lobby(network, phoenix::JoinRole::player);
#endif
	}
	catch (const std::exception &error)
	{
		app.status = error.what();
		app.screen = phoenix::Screen::client_lobby;
		if (!action.empty())
		{
			EM_ASM({ Module.phoenixLaunchError ?.(UTF8ToString($0)); }, app.status.c_str());
			app.session.reset();
			return 1;
		}
	}
	if (app.native_menu && !app.session && app.screen != phoenix::Screen::workshop)
	{
		EM_ASM({ Module.phoenixLaunchError ?.(UTF8ToString($0)); }, app.status.c_str());
		return 1;
	}
	phoenix::load_display_preferences(app);
	startup_phase("Starting graphics and loading interface artwork…");
	phoenix::open_window();
	phoenix::select_language(app.language, app.pseudo_locale);
#if defined(PHOENIX_WORKSHOP) || defined(PHOENIX_SERVER)
	phoenix::begin_tools();
#endif
	EM_ASM({ Module.phoenixStartSessionPump(); });
	startup_phase("Drawing the first game frame…");
	// Avoid oversubscribing presentation on high-refresh browser displays.
	emscripten_set_main_loop(frame, 60, true);
}
