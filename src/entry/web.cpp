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
import phoenix.session.api;
#ifdef PHOENIX_SERVER
import phoenix.network.lobby;
#else
import phoenix.network.lobby.protocol;
#endif
#ifdef PHOENIX_WORKSHOP
import phoenix.tools;
#endif

phoenix::Application app;
// Browser storage and page navigation belong to the shell; sessions remain in C++.
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, persist_menu, (), { Module.phoenixPersistMenu?.().catch(() => {}); });
EM_JS(void, return_to_menu, (), { Module.phoenixReturnToMenu?.(); });
// clang-format on
void frame()
{
	static double last_persist = 0;
	if (GetTime() - last_persist > 1)
	{
		persist_menu();
		last_persist = GetTime();
	}
	if (app.native_menu && (app.screen == phoenix::Screen::home || app.exit_requested))
		return_to_menu();
	if (app.session)
		app.session->poll();
#ifdef PHOENIX_SERVER
	static double accumulated = 0;
	accumulated += std::min(static_cast<double>(GetFrameTime()), 0.25);
	while (accumulated >= 1.0 / 20)
	{
		if (app.session)
			app.session->tick();
		accumulated -= 1.0 / 20;
	}
#endif
	BeginDrawing();
	phoenix::draw_screen(app);
#ifdef PHOENIX_WORKSHOP
	if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm)
		phoenix::draw_workshop(app);
#endif
	EndDrawing();
}
int main(int argc, char **argv)
{
	phoenix::NetworkOptions network;
	network.link.url = phoenix::default_network_service;
	std::string action, scenario, slot, ship, save;
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
			app.native_menu = true;
			app.library = std::make_shared<phoenix::MenuLibrary>("/launcher/menu");
			app.library->load();
			if (EM_ASM_INT({ return Module.phoenixMigrateBundled ? 1 : 0; }))
				app.library->install_bundled(phoenix::read_file("/launcher/bundled-base.zip"),
				                             phoenix::read_file("/launcher/bundled-training.zip"));
#ifdef PHOENIX_WORKSHOP
			if (action == "workshop")
				app.library->install_bundled(phoenix::read_file("/assets/base.zip"),
				                             phoenix::read_file("/assets/training.zip"));
#endif
			const auto &preferences = app.library->preferences;
			app.music_volume = preferences.volume;
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
				app.session->replace_packs(app.library->archives(app.library->packs));
			if (action == "recover")
				app.session->restore(app.library->read_save(save));
			else if (action == "host")
				app.session->prepare_lobby({scenario, {{slot, ship}}, slot, ""});
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
	phoenix::open_window();
	phoenix::select_language(app.language, app.pseudo_locale);
#ifdef PHOENIX_WORKSHOP
	phoenix::begin_tools();
#endif
	emscripten_set_main_loop(frame, 0, true);
}
