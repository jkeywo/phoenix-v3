#pragma once
// Explicitly invoked framebuffer/keyboard journey; never part of automatic CI.
struct NativeMenuSmoke
{
	bool enabled = false;
	int navigation_aid_step = 0;
	bool initial_dust = true, initial_grid = true, initial_motion = false;
	int stage = 0, pack_edits = 0, console_frames = 0;
	double changed = 0;
	std::size_t saves_before = 0;
	void action(phoenix::Application &app, const std::string &id)
	{
		if (!phoenix::invoke_navigation(app, id))
			throw std::runtime_error("Menu smoke action failed: " + id);
	}
	bool frame(phoenix::Application &app)
	{
		if (!enabled || GetTime() - changed < .4)
			return false;
		auto capture = [](const char *name)
		{
			TakeScreenshot((std::string("build/menu-native-") + name + ".png").c_str());
		};
		switch (stage)
		{
		case 0:
			if (!phoenix::probe_menu_keyboard(app))
				throw std::runtime_error("Main-menu Tab traversal failed");
			capture("home");
			action(app, "menu.settings");
			break;
		case 1:
			if (phoenix::invoke_navigation(app, "menu.host"))
				throw std::runtime_error("Modal allowed a background semantic action");
			if (app.music_volume != 25)
			{
				if (!app.set_control_value("menu-volume", "25"))
					throw std::runtime_error("Settings range action failed");
				changed = GetTime();
				return false;
			}
			if (!app.pseudo_locale)
			{
				action(app, "menu.pseudo");
				changed = GetTime();
				return false;
			}
			if (navigation_aid_step < 6)
			{
				if (!navigation_aid_step)
				{
					initial_dust = app.dust_motes;
					initial_grid = app.reference_grid;
					initial_motion = app.reduced_motion;
				}
				const char *controls[] = {"menu.dust", "menu.grid", "menu.motion"};
				const int control = navigation_aid_step % 3;
				const bool before = control == 0   ? app.dust_motes
				                    : control == 1 ? app.reference_grid
				                                   : app.reduced_motion;
				action(app, controls[control]);
				const bool after = control == 0   ? app.dust_motes
				                   : control == 1 ? app.reference_grid
				                                  : app.reduced_motion;
				if (before == after)
					throw std::runtime_error("Navigation aid Settings actions failed");
				++navigation_aid_step;
				changed = GetTime();
				return false;
			}
			if (app.dust_motes != initial_dust || app.reference_grid != initial_grid ||
			    app.reduced_motion != initial_motion)
				throw std::runtime_error("Navigation aid Settings did not retain independent values");
			if (!phoenix::probe_menu_keyboard(app) || !phoenix::probe_native_accessibility(app))
				throw std::runtime_error("Settings keyboard/UIA range traversal failed");
			capture("settings");
			action(app, "menu.close-dialog");
			break;
		case 2:
			action(app, "menu.packs");
			break;
		case 3:
			if (!app.library->packs.empty() && pack_edits < 3)
			{
				const auto id =
				    pack_edits < 2 ? "menu.pack-toggle/" + app.library->packs.front().hash : "menu.apply-packs";
				action(app, id);
				++pack_edits;
				changed = GetTime();
				return false;
			}
			if (!phoenix::probe_menu_keyboard(app))
				throw std::runtime_error("Mod-popup Tab traversal failed");
			capture("packs");
			action(app, "menu.close-dialog");
			break;
		case 4:
			action(app, "menu.host");
			break;
		case 5:
			capture("host");
			action(app, "menu.new");
			break;
		case 6:
			if (app.menu_branch == phoenix::Application::MenuBranch::ships &&
			    app.screen != phoenix::Screen::server_lobby)
				break;
			if (app.screen == phoenix::Screen::server_lobby)
			{
				stage = 7;
				break;
			}
			capture("scenarios");
			if (app.session->catalog().maps.empty())
				throw std::runtime_error("Menu smoke needs --game-pack with a scenario");
			action(app, "menu.scenario/" + app.session->catalog().maps.begin()->first);
			break;
		case 7:
		{
			if (app.screen == phoenix::Screen::server_lobby)
				break;
			capture("ships");
			const auto &slot = app.session->catalog().maps.at(app.selected_scenario).at("ship_slots").at(0);
			action(app, "menu.ship/" + slot.at("id").get<std::string>() + "/" +
			                slot.at("allowed_ships").at(0).get<std::string>());
			break;
		}
		case 8:
		{
			const auto lobby = app.session->lobby();
			for (const auto &member : lobby.members)
				if (member.id == lobby.self && member.console.empty())
					for (const auto &[id, label] : app.session->lobby_controls())
						if (id.starts_with("lobby.seat/") && !id.ends_with('/'))
						{
							action(app, id);
							changed = GetTime();
							return false;
						}
			if (!phoenix::probe_native_accessibility(app))
				throw std::runtime_error("Lobby UIA readouts failed");
			capture("lobby");
			if (app.session->ticks() || app.session->running())
				throw std::runtime_error("Menu selection advanced the game before readiness");
			action(app, "lobby.ready");
			break;
		}
		case 9:
			if (app.session->lobby().countdown_ms < 0)
				return false;
			capture("countdown");
			break;
		case 10:
			if (!app.session->running() || app.screen != phoenix::Screen::viewscreen)
				return false;
			if (++console_frames < 3)
				return false;
			capture("running");
			if (phoenix::navigation_controls(app).size() != 1 || phoenix::invoke_navigation(app, "lobby") ||
			    phoenix::invoke_navigation(app, "home") || phoenix::invoke_navigation(app, "game.console") ||
			    phoenix::invoke_navigation(app, "game.follow") || phoenix::invoke_navigation(app, "game.local-gm"))
				throw std::runtime_error("Viewscreen exposed navigation outside Settings");
			action(app, "settings");
			break;
		case 11:
			saves_before = app.library->saves.size();
			action(app, "menu.save-game");
			break;
		case 12:
			if (app.library->saves.size() != saves_before + 1)
				throw std::runtime_error("Managed save did not complete from in-game Settings");
			capture("game-settings");
			action(app, "menu.close-dialog");
			break;
		case 13:
			if (app.screen != phoenix::Screen::viewscreen)
				throw std::runtime_error("Settings failed to restore the Viewscreen");
			action(app, "settings");
			break;
		case 14:
			action(app, "settings.quit");
			break;
		case 15:
			if (app.screen != phoenix::Screen::home || app.session->running() || app.session->lobby().active)
				throw std::runtime_error("Quit failed to end the session before returning to Main Menu");
			std::cout << "PASS native cascading menu, popup keyboard traversal, range action and readiness countdown "
			             "and fixed Viewscreen, in-game Settings/save/quit (manual smoke)\n";
			return true;
		}
		++stage;
		changed = GetTime();
		return false;
	}
};
