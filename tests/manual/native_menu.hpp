#pragma once
// Explicitly invoked framebuffer/keyboard journey; never part of automatic CI.
struct NativeMenuSmoke
{
	bool enabled = false, large = false;
	bool assign_displays = false;
	bool virtual_displays = false;
	bool display_transition_only = false;
	void configure_displays(phoenix::Application &app)
	{
		if (!virtual_displays)
			return;
		if (display_transition_only)
			stage = 4; // Start at Host Game; unrelated settings/pack journeys have their own smoke.
		app.native_displays = {{0, "Test host region"}, {1, "Test Console region"}};
		app.host_display = 0;
		app.apply_displays = [&app]()
		{
			if (app.display_consoles.empty() && app.inspector_display < 0)
			{
				app.reset_displays();
				return;
			}
			SetWindowSize(1600, 900);
			app.viewscreen_region = phoenix::ConsoleRegion{2, 800, 0, 800, 900};
			app.console_regions.clear();
			if (app.display_consoles.contains(1))
				app.console_regions.push_back({3, 0, 0, 800, 900});
		};
		app.reset_displays = [&app]()
		{
			app.console_regions.clear();
			app.viewscreen_region.reset();
			app.gm_region.reset();
			app.display_consoles.clear();
			app.local_player_names.clear();
			app.inspector_display = -1;
			SetWindowSize(1280, 720);
		};
	}
	int display_step = 0;
	int profile_step = 0;
	int image_step = 0;
	int complexity_step = 0;
	std::string complexity_slot, complexity_console, complexity_initial, complexity_selected;
	int navigation_aid_step = 0, settings_step = 0;
	bool initial_dust = true, initial_grid = true, initial_motion = false;
	int stage = 0, pack_edits = 0, console_frames = 0;
	int load_error_step = 0;
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
		if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
		                [](const auto &control)
		                {
			                return control.first == "loading.cancel";
		                }))
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
			if (large)
			{
				action(app, "menu.host");
				stage = 4;
				break;
			}
			action(app, "menu.settings");
			break;
		case 1:
			if (phoenix::invoke_navigation(app, "menu.host"))
				throw std::runtime_error("Modal allowed a background semantic action");
			if (settings_step == 0)
			{
				if (!app.set_control_value("menu-volume", "25") || !app.set_control_value("menu-master", "60") ||
				    !app.set_control_value("menu-sfx", "35") || phoenix::invoke_navigation(app, "menu.pseudo"))
					throw std::runtime_error("Audio tab exposes only its ranges");
				action(app, "settings.tab/profile");
				++settings_step;
				changed = GetTime();
				return false;
			}
			if (settings_step == 1)
			{
				if (image_step < 2)
				{
					action(app, "menu.avatar-choice/" + std::to_string(image_step++));
					changed = GetTime();
					return false;
				}
				if (app.set_control_value("menu-volume", "10") || !phoenix::probe_menu_keyboard(app) ||
				    !phoenix::probe_native_accessibility(app))
					throw std::runtime_error("Profile tab focus/semantics or hidden range admission failed");
				capture("settings-profile");
				action(app, "settings.tab/language");
				++settings_step;
				changed = GetTime();
				return false;
			}
			if (settings_step == 2)
			{
				if (!app.pseudo_locale)
				{
					action(app, "menu.pseudo");
					changed = GetTime();
					return false;
				}
				capture("settings-language");
				action(app, "settings.tab/display");
				++settings_step;
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
			if (settings_step == 3)
			{
				if (!phoenix::probe_menu_keyboard(app) || !phoenix::probe_native_accessibility(app))
					throw std::runtime_error("Display tab keyboard/UIA selection failed");
				capture("settings-display");
				action(app, "settings.tab/audio");
				++settings_step;
				changed = GetTime();
				return false;
			}
			if (app.master_volume != 60 || app.music_volume != 25 || app.sfx_volume != 35)
				throw std::runtime_error("Switching tabs lost audio settings");
			if (!phoenix::probe_menu_keyboard(app) || !phoenix::probe_native_accessibility(app))
				throw std::runtime_error("Settings keyboard/UIA range traversal failed");
			capture("settings");
			action(app, "menu.close-dialog");
			break;
		case 2:
			action(app, "menu.packs");
			break;
		case 3:
			if (auto editable = std::find_if(app.library->packs.begin(), app.library->packs.end(),
			                                 [&](const auto &pack)
			                                 {
				                                 return pack.hash != app.library->base().hash;
			                                 });
			    editable != app.library->packs.end() && pack_edits < 4)
			{
				std::string id = "menu.apply-packs";
				if (pack_edits < 2)
					id = "menu.pack-toggle/" + editable->hash;
				if (pack_edits == 2)
					for (const auto &pack : app.library->packs)
						if (pack.id == "test")
							id = "menu.pack-toggle/" + pack.hash;
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
			{
				auto selected = app.session->catalog().maps.begin()->first;
				for (const auto &[id, map] : app.session->catalog().maps)
					if (id.starts_with("test."))
					{
						selected = id;
						break;
					}
				action(app, "menu.scenario/" + selected);
			}
			break;
		case 7:
		{
			if (app.screen == phoenix::Screen::server_lobby)
				break;
			capture("ships");
			if (large)
				load_error_step = 2;
			const auto &slot = app.session->catalog().maps.at(app.selected_scenario).at("ship_slots").at(0);
			const auto choice = "menu.ship/" + slot.at("id").get<std::string>() + "/" +
			                    slot.at("allowed_ships").at(0).get<std::string>();
			if (load_error_step == 0)
			{
				const auto selected = app.selected_scenario;
				app.selected_scenario = "smoke.missing";
				const bool accepted = phoenix::invoke_navigation(app, choice);
				app.selected_scenario = selected;
				if (accepted)
					throw std::runtime_error("Invalid scenario unexpectedly loaded");
				++load_error_step;
				changed = GetTime();
				return false;
			}
			if (load_error_step == 1)
			{
				if (app.accessible_status.find("Choose a scenario, slot and allowed ship") == std::string::npos)
					throw std::runtime_error("Scenario load error is missing from accessible status");
				capture("load-error");
				action(app, "menu.dismiss-error");
				++load_error_step;
				changed = GetTime();
				return false;
			}
			action(app, choice);
			break;
		}
		case 8:
		{
			if (large)
				complexity_step = 3;
			const auto lobby = app.session->lobby();
			if (assign_displays && !display_step)
			{
				if (app.native_displays.size() < 2 || lobby.consoles.empty())
					throw std::runtime_error("Display smoke needs a console and two monitors; found " +
					                         std::to_string(app.native_displays.size()) + " monitors and " +
					                         std::to_string(lobby.consoles.size()) + " Consoles");
				for (const auto &display : app.native_displays)
					if (display.id != app.host_display)
					{
						const auto id =
						    "menu.console-screen/" + lobby.consoles.front().id + "/" + std::to_string(display.id);
						if (std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
						                 [&](const auto &control)
						                 {
							                 return control.first == id;
						                 }))
						{
							throw std::runtime_error("Console screen claim must be visible without a tab");
						}
						action(app,
						       "menu.console-screen/" + lobby.consoles.front().id + "/" + std::to_string(display.id));
						display_step = 1;
						changed = GetTime();
						return false;
					}
			}
			if (assign_displays && display_step == 1)
			{
				if (app.console_regions.size() != 1 || !app.viewscreen_region)
					throw std::runtime_error("Assigned Console was not placed before launch");
				if (display_transition_only)
				{
					capture("assigned-console");
					for (const auto &seat : lobby.consoles)
						if (seat.local_screen)
						{
							phoenix::LobbyCommand ready{phoenix::LobbyAction::ready};
							ready.screen = seat.local_screen;
							app.session->lobby_command(ready);
						}
					break;
				}
				const auto route = app.console_regions.front().console;
				const auto prefix = "console/" + std::to_string(route) + "/";
				if (profile_step == 0)
				{
					action(app, prefix + "profile");
					profile_step = 1;
					changed = GetTime();
					return false;
				}
				if (profile_step == 1)
				{
					capture("console-profile");
					if (!app.set_control_value(prefix + "profile-name", "Test navigator"))
						throw std::runtime_error("Local console profile field unavailable");
					action(app, prefix + "avatar/2");
					action(app, prefix + "profile-save");
					profile_step = 2;
					changed = GetTime();
					return false;
				}
				if (std::none_of(lobby.members.begin(), lobby.members.end(),
				                 [](const auto &member)
				                 {
					                 return member.name == "Test navigator" && !member.avatar.empty();
				                 }))
					throw std::runtime_error("Local console profile was not saved");
				const auto ready = "console/" + std::to_string(route) + "/ready";
				if (std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
				                 [&](const auto &c)
				                 {
					                 return c.first == ready;
				                 }))
					throw std::runtime_error("Assigned Console must immediately show its own Ready card");
				capture("assigned-console");
				if (phoenix::invoke_navigation(app, "console/" + std::to_string(route) + "/back"))
					throw std::runtime_error("A local Console must not expose Leave/Back");

				for (const auto &display : app.native_displays)
					if (display.id != app.host_display && !app.display_consoles.contains(display.id))
					{
						action(app, "menu.inspector-screen/" + std::to_string(display.id));
						break;
					}
				display_step = 2;
				changed = GetTime();
				return false;
			}
			for (const auto &member : lobby.members)
				if (member.id == lobby.self && member.console.empty())
					for (const auto &[id, label] : app.session->lobby_controls())
						if (id.starts_with("lobby.seat/") && !id.ends_with('/'))
						{
							action(app, id);
							changed = GetTime();
							return false;
						}
			if (complexity_step < 3 && app.screen != phoenix::Screen::settings)
			{
				for (const auto &[id, label] : app.screen_controls)
					if (id.starts_with("lobby.profile/"))
						throw std::runtime_error("Complexity choices leaked onto lobby board");
				action(app, "menu.settings");
				changed = GetTime();
				return false;
			}
			if (complexity_step < 3 && app.settings_tab != "session")
			{
				action(app, "settings.tab/session");
				changed = GetTime();
				return false;
			}
			if (complexity_step == 0)
			{
				const auto controls = app.session->lobby_controls();
				for (const auto &seat : lobby.consoles)
					for (const auto &choice : seat.profiles)
					{
						const auto id = "lobby.profile/" + seat.slot + "/" + seat.id + "/" + choice;
						if (choice != seat.profile && std::any_of(controls.begin(), controls.end(),
						                                          [&](const auto &control)
						                                          {
							                                          return control.first == id;
						                                          }))
						{
							complexity_slot = seat.slot;
							complexity_console = seat.id;
							complexity_initial = seat.profile;
							complexity_selected = choice;
							action(app, id);
							complexity_step = 1;
							changed = GetTime();
							return false;
						}
					}
				throw std::runtime_error("Lobby complexity smoke requires a permitted alternate Console profile");
			}
			if (complexity_step == 1 || complexity_step == 2)
			{
				const auto seat =
				    std::find_if(lobby.consoles.begin(), lobby.consoles.end(),
				                 [&](const auto &value)
				                 {
					                 return value.slot == complexity_slot && value.id == complexity_console;
				                 });
				const auto expected = complexity_step == 1 ? complexity_selected : complexity_initial;
				if (seat == lobby.consoles.end() || seat->profile != expected)
					throw std::runtime_error("Lobby complexity selection did not reach authoritative snapshot");
				if (complexity_step == 1)
				{
					capture("complexity");
					TraceLog(LOG_INFO, "Menu complexity smoke selected %s for %s", complexity_selected.c_str(),
					         complexity_console.c_str());
					action(app,
					       "lobby.profile/" + complexity_slot + "/" + complexity_console + "/" + complexity_initial);
					complexity_step = 2;
					changed = GetTime();
					return false;
				}
				TraceLog(LOG_INFO, "Menu complexity smoke restored %s before Ready", complexity_initial.c_str());
				complexity_step = 3;
				action(app, "menu.close-dialog");
				changed = GetTime();
				return false;
			}
			if (!phoenix::probe_native_accessibility(app))
				throw std::runtime_error("Lobby UIA readouts failed");
			if (large && !assign_displays)
			{
				const auto ready = app.control_placements.find("lobby.ready");
				if (ready == app.control_placements.end())
					throw std::runtime_error("High-resolution lobby lost Ready");
				const auto &r = ready->second;
				if (r.width < 200 || r.height < 90 || r.x < 0 || r.y < 0 || r.x + r.width > GetScreenWidth() + 1 ||
				    r.y + r.height > GetScreenHeight() + 1)
					throw std::runtime_error("High-resolution Ready is tiny or outside the window");
			}
			capture("lobby");
			if (app.session->ticks() || app.session->running())
				throw std::runtime_error("Menu selection advanced the game before readiness");
			if (assign_displays)
			{
				for (const auto &seat : lobby.consoles)
					if (seat.local_screen)
						action(app, "console/" + std::to_string(seat.local_screen) + "/ready");
			}
			else
				action(app, "lobby.ready");
			break;
		}
		case 9:
			if (app.session->lobby().countdown_ms < 0)
				return false;
			capture("countdown");
			break;
		case 10:
		{
			if (!app.session->running() || app.screen != phoenix::Screen::viewscreen)
				return false;
			if (++console_frames < 3)
				return false;
			capture("running");
			if (assign_displays)
			{
				if (!app.viewscreen_region || app.console_regions.size() != 1 ||
				    (app.native_displays.size() >= 3 && !app.gm_region))
					throw std::runtime_error("Assigned display roles were not applied");
				if (!app.session->console_observation(app.console_regions.front().console))
					return false;
				if (display_transition_only)
				{
					std::cout << "PASS assigned Console and host Viewscreen survive lobby launch (manual smoke)\n";
					return true;
				}
				const auto route = app.console_regions.front().console;
				if (console_frames == 3 && !phoenix::probe_console_helm(app, route))
					throw std::runtime_error("Routed throttle drag or radar ring input failed");
				if (console_frames < 6)
					return false;
				const auto observed = app.session->console_observation(route);
				if (observed->requested_heading != 90000 || observed->requested_speed <= 0)
					throw std::runtime_error("Second-screen Helm actions did not commit while host was on Viewscreen");
				capture("helm-active");
				if (console_frames == 6)
				{
					action(app, "console/" + std::to_string(route) + "/settings");
					if (app.screen != phoenix::Screen::viewscreen)
						throw std::runtime_error("Console settings changed the host screen");
					return false;
				}
				if (phoenix::invoke_navigation(app, "console/" + std::to_string(route) + "/release"))
					throw std::runtime_error("Native player escaped its station");
			}
			const auto controls = phoenix::navigation_controls(app);
			if (std::any_of(controls.begin(), controls.end(),
			                [](const auto &control)
			                {
				                return control.id != "settings" && !control.id.starts_with("console/");
			                }) ||
			    phoenix::invoke_navigation(app, "lobby") || phoenix::invoke_navigation(app, "home") ||
			    phoenix::invoke_navigation(app, "game.console") || phoenix::invoke_navigation(app, "game.follow") ||
			    phoenix::invoke_navigation(app, "game.local-gm"))
				throw std::runtime_error("Viewscreen exposed navigation outside Settings");
			if (large)
			{
				std::cout << "PASS high-resolution menu, visible Ready bounds, countdown and launch\n";
				return true;
			}
			action(app, "settings");
			break;
		}
		case 11:
			if (assign_displays)
			{
				const auto route = app.console_regions.front().console;
				const auto prefix = "console/" + std::to_string(route) + "/";
				const auto controls = phoenix::navigation_controls(app);
				if (std::none_of(controls.begin(), controls.end(),
				                 [&](const auto &c)
				                 {
					                 return c.id == prefix + "local-settings.close";
				                 }))
					throw std::runtime_error("Host settings removed the independent player settings");
				if (phoenix::invoke_navigation(app, prefix + "settings.quit"))
					throw std::runtime_error("Native player could quit the host session");
			}
			if (app.settings_tab != "session")
			{
				action(app, "settings.tab/session");
				changed = GetTime();
				return false;
			}
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
