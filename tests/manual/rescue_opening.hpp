// Explicit manual smoke: normal authored cargo and rescue, rendered with real Consoles.
int rescue_opening_smoke(const std::string &directory, bool cruiser, bool &opened, const std::string &checkpoint = {},
                         bool preview = false, bool render = true, bool seize = false, bool takeover = false,
                         bool recapture = false)
{
	Application app;
	app.can_host = true;
	app.native_menu = false;
	app.session = make_local_session();
	std::cout << "WAIT Rescue loading content" << std::endl;
	const std::string fit = cruiser ? "base.delivery-cruiser" : "base.rescue-courier";
	if (checkpoint.empty())
	{
		app.session->add_pack(read_file(directory + "/base.zip"));
		std::cout << "WAIT Rescue starting fresh session" << std::endl;
		std::map<std::string, std::map<std::string, std::string>> profiles;
		for (const auto *console :
		     cruiser ? std::vector<const char *>{"helm", "engineering", "tactical", "comms", "science", "navigator"}
		             : std::vector<const char *>{"helm", "support", "tactical", "comms", "navigator", "navigation"})
			profiles["courier"][console] = (cruiser && std::string(console) == "comms") ||
			                                       (!cruiser && !seize && std::string(console) == "tactical")
			                                   ? "simple"
			                                   : "detailed";
		app.session->start({"base.rescue-under-threat", {{"courier", fit}}, "courier", "helm", profiles});
		std::cout << "WAIT Rescue session started" << std::endl;
	}
	else
	{
		app.session->restore(read_file(checkpoint, runtime_save_archive_limit));
		app.session->resume_local();
	}
	const auto ship = app.session->observation()->own.id;
	auto ensure = [](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(why);
	};
#ifndef __EMSCRIPTEN__
	auto write_checkpoint = [&](const std::string &path, const auto &archive)
	{
		std::ofstream file(path, std::ios::binary);
		ensure(bool(file), "Cannot create checkpoint: " + path);
		file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
		file.close();
		ensure(bool(file), "Could not write checkpoint: " + path);
	};
#endif
	ensure(app.session->claim_console(2, {ship, cruiser ? "engineering" : "support"}), "Delivery engineering claim");
	if (cruiser)
	{
		int route = 3;
		for (const auto *seat : {"tactical", "comms", "science", "captain"})
			ensure(app.session->claim_console(route++, {ship, seat}), std::string("Claim ") + seat);
	}
	if (render)
	{
		open_window();
		opened = true;
		SetWindowSize(2560, 1080);
		SetTargetFPS(0);
	}
	app.screen = Screen::console;
	app.console_regions = {{1, 0, 0, 1280, 1080}, {2, 1280, 0, 1280, 1080}};
	const std::string prefix = std::string(cruiser ? "cruiser" : "courier") + (recapture  ? "-recapture"
	                                                                           : seize    ? "-seize"
	                                                                           : takeover ? "-takeover"
	                                                                                      : "");
	auto frame = [&]
	{
		if (!render)
			return;
		BeginDrawing();
		ClearBackground(BLACK);
		draw_screen(app);
		EndDrawing();
#ifdef __EMSCRIPTEN__
		emscripten_sleep(0);
#endif
	};
	auto capture = [&](const std::string &name)
	{
		if (!render)
			return;
		for (int i = 0; i < 2; ++i)
			frame();
#ifdef __EMSCRIPTEN__
		// Capture before yielding: the browser can discard the WebGL drawing buffer afterwards.
		BeginDrawing();
		ClearBackground(BLACK);
		draw_screen(app);
		EndDrawing();
		EM_ASM({if(!Module.missionCaptures)Module.missionCaptures={};Module.missionCaptures[UTF8ToString($0)]=Module.canvas.toDataURL("image/png");},(prefix+"-"+name).c_str());
#else
		TakeScreenshot(("build/rescue-" + prefix + "-" + name + ".png").c_str());
#endif
	};
	auto advance = [&](int n)
	{
		for (int i = 0; i < n; ++i)
			app.session->tick();
#ifdef __EMSCRIPTEN__
		emscripten_sleep(0);
#endif
	};
	auto view = [&](const std::string &console) -> std::pair<std::uint64_t, Observation>
	{
		for (unsigned route = 1; route <= (cruiser ? 6u : 2u); ++route)
		{
			auto o = app.session->console_observation(route);
			if (!o)
				continue;
			if (o->console == console)
				return {route, *o};
			for (const auto &a : o->additional_consoles)
				if (a.console == console)
					return {route, a};
		}
		throw std::runtime_error("No claimed watch for " + console);
	};
	auto channel = [&](const std::string &console, const std::string &action, std::int64_t value,
	                   const std::string &controller = std::string{})
	{
		const auto [route, o] = view(console);
		bool accepted = false;
		for (const auto &c : o.gameplay.at("controllers"))
			if ((controller.empty() || c.at("id") == controller) &&
			    !(controller.empty() && action == "weapon_target" &&
			      c.at("id").get<std::string>().find("pdc") != std::string::npos))
				for (auto it = c.at("commands").begin(); it != c.at("commands").end(); ++it)
					if (it.value().at("action") == action)
					{
						OperatingCommand cmd{ship, {}, value, {}};
						cmd.console = console;
						cmd.controller = c.at("id");
						cmd.channel = it.key();
						cmd.generation = c.at("generation");
						accepted |= app.session->console_command(route, cmd);
					}
		if (!accepted)
			throw std::runtime_error("Channel refused " + console + "/" + action + "/" + controller);
	};
	auto domain = [&](const std::string &console, const std::string &kind, const std::string &verb,
	                  const std::string &system, const std::string &text = std::string{})
	{
		const auto [route, o] = view(console);
		std::int64_t revision = 0;
		if (kind == "away")
		{
			for (const auto &m : o.gameplay.at("away_missions"))
				if (m.at("id") == system)
					revision = m.at("revision");
		}
		if (kind == "team")
			for (const auto &t : o.gameplay.at("teams"))
				if (t.at("id") == system)
					revision = t.at("revision");
		if (kind == "logistics")
			revision = o.gameplay.at("logistics").at("revision");
		if (kind == "dock")
			revision = o.gameplay.at("docking").at("revision");
		for (const auto &c : o.gameplay.at("controllers"))
		{
			OperatingCommand cmd{ship, kind + "." + verb, revision, system};
			cmd.console = console;
			cmd.controller = c.at("id");
			cmd.generation = c.at("generation");
			cmd.text = text;
			if (app.session->console_command(route, cmd))
				return;
		}
		std::cerr << o.gameplay.dump() << std::endl;
		throw std::runtime_error("Order refused " + console + " " + kind + "." + verb + " " + system + " " + text);
	};
	bool smoke_hostile = false;
	BodyView last_raider{};
	bool observed_raider = false;
	std::int64_t last_raider_heading = 0;
	std::uint64_t last_raider_tick = 0;
	auto until = [&](auto done, const std::string &label, int limit = 18000)
	{
		std::cout << "WAIT " << prefix << " " << label << std::endl;
		const int stride = label == "combat manoeuvre" ? 10 : 50;
		for (int i = 0; i < limit && !done(); i += stride)
		{
			const auto [comms_route, comms] = view("comms");
			for (const auto &thread : comms.gameplay.at("conversations").at("threads"))
				if (thread.at("prompt") != 0)
				{
					bool answered = false;
					for (const auto &preferred :
					     cruiser
					         ? std::vector<std::string>{"bridge", "reactor", "wounded", "propulsion", "slow",
					                                    seize ? "seize" : "rescue", takeover ? "withdraw" : "refuse"}
					         : std::vector<std::string>{"wounded", "reactor", "propulsion", "slow",
					                                    seize ? "seize" : "rescue", takeover ? "withdraw" : "refuse"})
						for (const auto &choice : thread.at("choices"))
							if (!answered && choice.at("id") == preferred && choice.value("enabled", true))
								for (const auto &controller : comms.gameplay.at("controllers"))
								{
									OperatingCommand command{ship, "comms.answer", thread.at("id")};
									command.console = "comms";
									command.controller = controller.at("id");
									command.generation = controller.at("generation");
									command.request_id = thread.at("prompt");
									command.text = preferred;
									if (app.session->console_command(comms_route, command))
									{
										answered = true;
										smoke_hostile |= preferred == "refuse";
										break;
									}
								}
				}
			if (smoke_hostile)
			{
				const auto tactical = view("tactical").second;
				for (const auto &contact : tactical.contacts)
					if (contact.disposition == "hostile")
					{
						if (!cruiser)
							channel("tactical", "sensor_target", contact.id, "scanner");
						bool manual_target = false;
						for (const auto &controller : tactical.gameplay.at("controllers"))
							if (controller.value("human", false))
								for (const auto &command : controller.at("commands"))
									manual_target |= command.at("action") == "weapon_target";
						if (manual_target)
							channel("tactical", "weapon_target", contact.id);
						if (!cruiser && label == "combat manoeuvre")
						{
							// Stay astern using only observed pose/turn rate. The Courier can
							// turn inside the raider; a wide orbit lets its nose catch up.
							const auto own = view("helm").second.own;
							const double nose = contact.heading * 3.141592653589793 / 180000;
							double aspect = nose + 3.141592653589793;
							// A beam striking ruined stern rooms cannot finish the surviving bow.
							// Change aspect only after our own scanner reports most rooms disabled.
							for (const auto &scan : tactical.gameplay.at("sensors").at("contacts"))
								if (scan.at("id") == contact.id &&
								    scan.at("findings").contains("compartment_condition"))
								{
									const auto &finding = scan.at("findings").at("compartment_condition");
									const auto &rooms = finding.at("data");
									const auto disabled =
									    std::count_if(rooms.begin(), rooms.end(),
									                  [](const auto &r)
									                  {
										                  return r.at("disabled").template get<bool>();
									                  });
									if (finding.value("live", false) && disabled * 3 >= rooms.size() * 2)
									{
										std::int64_t most_hp = 0;
										for (const auto &room : rooms)
											if (room.at("hp").template get<std::int64_t>() > most_hp)
											{
												most_hp = room.at("hp");
												aspect = nose + std::atan2(room.at("y_mm").template get<double>(),
												                           room.at("x_mm").template get<double>());
											}
									}
								}
							const double rear_x = 85000 * std::cos(aspect), rear_y = 85000 * std::sin(aspect);
							const auto now = app.session->ticks();
							const double turn =
							    last_raider_tick && now > last_raider_tick
							        ? std::clamp(
							              double((contact.heading - last_raider_heading + 540000) % 360000 - 180000) *
							                  20 / (now - last_raider_tick),
							              -12000.0, 12000.0) *
							              3.141592653589793 / 180000
							        : 0;
							last_raider_heading = contact.heading;
							last_raider_tick = now;
							double vx = contact.vx + (contact.x + rear_x - own.x) / 2 - turn * rear_y;
							double vy = contact.vy + (contact.y + rear_y - own.y) / 2 + turn * rear_x;
							const double magnitude = std::hypot(vx, vy);
							if (magnitude > 22000)
							{
								vx *= 22000 / magnitude;
								vy *= 22000 / magnitude;
							}
							const auto bearing = (static_cast<std::int64_t>(std::llround(
							                          std::atan2(vy * 3 - own.vy * 2, vx * 3 - own.vx * 2) * 180000 /
							                          3.141592653589793)) +
							                      360000) %
							                     360000;
							const double facing = own.heading * 3.141592653589793 / 180000;
							channel("helm", "heading_mdeg", bearing);
							channel("helm", "speed_mm_s",
							        std::clamp<std::int64_t>(
							            static_cast<std::int64_t>(vx * std::cos(facing) + vy * std::sin(facing)), 0,
							            22000));
						}
						last_raider = contact;
						observed_raider = true;
						break;
					}
			}
			advance(stride);
			if (i % 1000 == 0)
			{
#ifndef __EMSCRIPTEN__

				{
					const auto archive = app.session->save();
					write_checkpoint("build/rescue-" + prefix + "-progress.zip", archive);
					const auto state = read_pack(archive, ArchivePurpose::Save).draft();
					std::ofstream("build/rescue-" + prefix + "-diagnostic.json")
					    << Json::parse(state.files.at("session.json")).at("state").dump(2);
				}
#endif
				const auto o = view("helm").second;
				std::cout << "PROGRESS " << label << " tick=" << app.session->ticks() << " pos=" << o.own.x << ","
				          << o.own.y << " docking=" << o.gameplay.at("docking").dump()
				          << " away=" << o.gameplay.at("away_missions").dump() << std::endl;
			}
		}
		ensure(done(), "Timed out: " + label);
	};
	auto objective = [&](const std::string &title)
	{
		ConsoleModel m;
		m.refresh(app.session, 1);
		for (const auto &o : m.objectives())
			if (o.at("title") == title && o.at("status") == "completed")
				return true;
		return false;
	};
	auto show = [&](std::uint64_t route, const std::string &console)
	{
		frame();
		if (app.screen_action)
			app.screen_action("console/" + std::to_string(route) + "/game.console/" + console);
		frame();
	};
	auto wait_report = [&](std::uint64_t route)
	{
		if (!render)
			return;
		const auto control = "console/" + std::to_string(route) + "/mission.leave";
		auto ready = [&]
		{
			return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                   [&](const auto &c)
			                   {
				                   return c.first == control;
			                   });
		};
		for (int i = 0; i < 400 && !ready(); ++i)
			frame();
		ensure(ready(), "Report controls did not become ready");
	};
	auto panel = [&](std::uint64_t route, const std::string &name)
	{
		if (!render)
			return;
		frame();
		const auto root = "console/" + std::to_string(route) + "/";
		if (name == "logistics" || name == "docking")
		{
			for (int wait = 0; wait < 250 && std::none_of(
			                                     app.screen_controls.begin(), app.screen_controls.end(),
			                                     [&](const auto &c)
			                                     {
				                                     return c.first == root + "settings";
			                                     });
			     ++wait)
				frame();
			if (!app.screen_action || !app.screen_action(root + "settings"))
			{
				for (const auto &c : app.screen_controls)
					std::cerr << c.first << " : " << c.second << std::endl;
				throw std::runtime_error("Open settings");
			}
			frame();
			ensure(app.screen_action(root + "local-settings.tab/" + name), "Open panel " + name);
		}
		else
			ensure(app.screen_action && app.screen_action(root + name), "Panel " + name);
		frame();
	};
	auto stop = [&]
	{
		channel("helm", "speed_mm_s", 0);
		until(
		    [&]
		    {
			    const auto o = view("helm").second.own;
			    if (std::hypot(double(o.vx), double(o.vy)) < 100)
				    return true;
			    const auto course = (static_cast<std::int64_t>(std::llround(std::atan2(double(o.vy), double(o.vx)) *
			                                                                180000 / 3.141592653589793)) +
			                         360000) %
			                        360000;
			    channel("helm", "heading_mdeg", course);
			    channel("helm", "speed_mm_s", 0);
			    return false;
		    },
		    "stop");
	};
	auto move = [&](std::int64_t x, std::int64_t y, std::int64_t radius)
	{
		stop();
		const auto start = view("helm").second.own;
		const auto heading = (static_cast<std::int64_t>(std::llround(
		                          std::atan2(double(y - start.y), double(x - start.x)) * 180000 / 3.141592653589793)) +
		                      360000) %
		                     360000;
		channel("helm", "heading_mdeg", heading);
		until(
		    [&]
		    {
			    const auto own = view("helm").second.own;
			    return std::abs((heading - own.heading + 540000) % 360000 - 180000) < 100;
		    },
		    "turn");
		until(
		    [&]
		    {
			    const auto own = view("helm").second.own;
			    const double dist = std::hypot(double(x - own.x), double(y - own.y));
			    if (dist < radius)
			    {
				    channel("helm", "speed_mm_s", 0);
				    return true;
			    }
			    channel("helm", "speed_mm_s",
			            std::min<std::int64_t>(
			                20000, std::max<std::int64_t>(1000, static_cast<std::int64_t>((dist - radius) / 8))));
			    return false;
		    },
		    "navigate");
		stop();
	};
	for (int i = 0; render && i < 800 && view("helm").second.gameplay.at("mission_report").is_null() &&
	                std::none_of(
	                    app.screen_controls.begin(), app.screen_controls.end(),
	                    [](const auto &c)
	                    {
		                    return c.first == "console/1/settings";
	                    });
	     ++i)
		frame();
	const std::string engineering = cruiser ? "engineering" : "support";
	const std::string dispatch = cruiser ? "tactical" : "helm", transport = cruiser ? "science" : "helm";
	auto shields = [&](bool enabled)
	{
		const std::string console = cruiser ? "science" : "support";
		const auto o = view(console).second;
		for (const auto &c : o.gameplay.at("controllers"))
			if (c.at("id").get<std::string>().starts_with("shield-"))
				for (const auto &command : c.at("commands"))
					if (command.at("action") == "system_enabled" || command.at("action") == "shield_discharge")
						channel(console, command.at("action"), enabled ? 1 : 0, c.at("id"));
	};
	if (!view("helm").second.gameplay.at("mission_report").is_null())
	{
		ensure(render, "Use delivery-cruiser to preview an existing report");
		const auto route = view("navigator").first;
		show(route, "navigator");
		app.console_regions[1].console = route;
		wait_report(route);
		capture("report");
		close_window();
		opened = false;
		std::cout << "PASS " << prefix << " saved report graphical smoke" << std::endl;
		return 0;
	}
	if (!cruiser && seize && !checkpoint.empty())
	{
		// A separate seizure walkthrough can resume the real post-combat arrival.
		// Select the ordinary detailed Tactical profile to give the crew Security authority.
		const auto layout = compile_controllable_layout(app.session->catalog(), fit);
		for (std::size_t i = 0; i < layout.profiles.size(); ++i)
			if (layout.profiles[i].id == "detailed")
			{
				OperatingCommand command{ship, "preset", static_cast<std::int64_t>(i), {}};
				command.console = "tactical";
				ensure(app.session->console_command(view("tactical").first, command),
				       "Select detailed Tactical authority");
				advance(1);
			}
	}
	shields(true);
	const auto tactical_initial = view("tactical").second;
	for (const auto &controller : tactical_initial.gameplay.at("controllers"))
		if (controller.value("human", false) && controller.at("id").get<std::string>().find("pdc") != std::string::npos)
			for (const auto &command : controller.at("commands"))
				if (command.at("action") == "weapon_mode")
					channel("tactical", "weapon_mode", 1, controller.at("id"));
	auto courier_combat = [&]
	{
		if (cruiser || takeover)
			return;
		smoke_hostile = true;
		until(
		    [&]
		    {
			    const auto tactical = view("tactical").second;
			    bool covering = !observed_raider && !checkpoint.empty(), radar_available = false;
			    for (const auto &sensor : tactical.systems)
				    if (sensor.kind == OperatingSystem::Radar && sensor.enabled && sensor.effective_range > 0)
				    {
					    radar_available = true;
					    covering |= std::hypot(double(last_raider.x - tactical.own.x),
					                           double(last_raider.y - tactical.own.y)) < sensor.effective_range;
				    }
			    ensure(radar_available, "Courier lost radar; combat outcome unresolved");
			    // No contact is not a victory when blind or outside the last known position's coverage.
			    return (observed_raider || !checkpoint.empty()) && covering &&
			           std::none_of(tactical.contacts.begin(), tactical.contacts.end(),
			                        [](const auto &c)
			                        {
				                        return c.disposition == "hostile";
			                        });
		    },
		    "combat manoeuvre");
		stop();
		auto port_ready = [&]
		{
			const auto helm = view("helm").second;
			for (const auto &port : helm.gameplay.at("docking").at("ports"))
				if (port.at("id") == "fore")
					return port.at("working").get<bool>();
			return false;
		};
		if (!port_ready())
		{
			const auto engineering_view = view(engineering).second;
			for (const auto &room : engineering_view.gameplay.at("combat").at("compartments"))
				if (room.at("id") == "bow-port")
					channel(engineering, "repair_compartment", room.at("index"), "team-1");
			advance(1);
			const auto repair_view = view(engineering).second;
			for (const auto &controller : repair_view.gameplay.at("controllers"))
				if (controller.at("id") == "team-1")
					for (const auto &[id, command] : controller.at("commands").items())
						if (command.at("action") == "repair_system")
							channel(engineering, "repair_system", command.at("maximum"), "team-1");
			until(port_ready, "restore own docking compartment");
			channel(engineering, "repair_compartment", 0, "team-1");
		}
	};
	if (checkpoint.empty())
	{
		std::cout << "WAIT Rescue initial simulation" << std::endl;
		advance(40);
		const auto engineering_view = view(engineering).second;
		for (const auto &c : engineering_view.gameplay.at("controllers"))
			for (const auto &command : c.at("commands"))
				if (command.at("action") == "generator_output")
					channel(engineering, "generator_output", command.at("maximum"), c.at("id"));
		const auto initial_teams = view(engineering).second.gameplay.at("teams");
		for (const auto &team : initial_teams)
			if ((team.at("type") == "repair" || team.at("type") == "engineering") && team.at("manual") == true &&
			    team.at("reserved") == false)
				domain(engineering, "team", "cancel", team.at("id"));
		std::cout << "BRIEFING " << mission_briefing(app.session->catalog().maps.at("base.rescue-under-threat"), fit)
		          << std::endl;
		capture("arrival-consoles");
		app.screen = Screen::viewscreen;
		app.console_regions.clear();
		capture("arrival-scene");
		if (cruiser)
		{
			OperatingCommand camera{ship, "camera_direction", 3, {}};
			camera.console = "helm";
			ensure(app.session->console_command(1, camera), "Viewscreen rear-right camera");
			advance(1);
			capture("star-scene");
			camera.value = 0;
			ensure(app.session->console_command(1, camera), "Restore forward camera");
			advance(1);
		}
		if (preview)
		{
			const auto stocks = view(engineering).second.gameplay.at("logistics").at("stores");
			for (const auto &store : stocks)
				if (store.at("id") == "cargo-hold")
					std::cout << "STARTING CARGO " << store.at("quantities").dump() << std::endl;
			std::cout << "STARTING PERSONNEL " << view("tactical").second.gameplay.at("personnel").dump() << std::endl;
			close_window();
			opened = false;
			std::cout << "PASS " << prefix << " opening scenery preview" << std::endl;
			return 0;
		}
		app.screen = Screen::console;
		app.console_regions = {{1, 0, 0, 1280, 1080}, {2, 1280, 0, 1280, 1080}};
		if (cruiser)
		{
			const auto board = view("tactical").second.gameplay.at("logistics");
			ensure(!board.at("cargo_craft").empty(), "Utility shuttle available");
			const auto craft = board.at("cargo_craft")[0].at("id").get<std::uint64_t>();
			domain("tactical", "logistics", "craft_allow", std::to_string(craft), "1");
			advance(1);
			domain(engineering, "logistics", "deliver", "cargo-hold",
			       std::to_string(craft) + "/cargo/2/receiving/perishable-essentials/100");
			advance(2);
			panel(2, "logistics");
			capture("cargo-order");
			panel(2, "local-settings.close");
			advance(1);
			move(950000, 0, 20000);
			advance(30);
		}
		else
		{
			move(1450000, 0, 20000);
			domain("helm", "dock", "assist", "fore", "2/west");
			panel(1, "docking");
			capture("station-approach");
			until(
			    [&]
			    {
				    const auto helm = view("helm").second;
				    for (const auto &p : helm.gameplay.at("docking").at("ports"))
					    if (p.at("id") == "fore" && p.at("phase") == "docked")
						    return true;
				    return false;
			    },
			    "station docking");
			capture("docked");
			domain(engineering, "logistics", "transfer", "cargo-hold", "2/receiving/station-supplies/100");
			advance(1);
			until(
			    [&]
			    {
				    return objective("Deliver all 100 units to Cairn Receiving");
			    },
			    "physical receipt");
			panel(2, "logistics");
			capture("received");
			panel(2, "local-settings.close");
			domain("helm", "dock", "undock", "fore");
			advance(160);
			panel(1, "local-settings.close");
#ifndef __EMSCRIPTEN__
			write_checkpoint("build/rescue-courier-delivered.zip", app.session->save());
#endif
			courier_combat();
			// Stay in the station corridor before turning into the southern rescue opening.
			move(950000, 0, 20000);
		}
		// Pass west of a disabled raider holding the southern entrance.
		// These are ordinary navigation waypoints within the charted clear corridor.
		if (!cruiser)
		{
			move(700000, 0, 20000);
			move(700000, 900000, 85000);
		}
		move(950000, 900000, cruiser ? 260000 : 85000);
		until(
		    [&]
		    {
			    return objective("Approach Cairn orbital starbase");
		    },
		    "halfway approach trigger");
		until(
		    [&]
		    {
			    const auto v = view(dispatch).second;
			    for (const auto &m : v.gameplay.at("away_missions"))
				    if (m.at("id") == "repairs" && m.at("phase") == "available")
					    return true;
			    return false;
		    },
		    "delivered distress and clearance");
	}
	courier_combat();
	if (takeover)
	{
		stop();
		const bool already_recaptured =
		    recapture &&
		    view("comms")
		            .second.gameplay.at("conversations")
		            .dump()
		            .find("Bridge secured. Prize crew is taking command of the ship.") != std::string::npos;
		if (recapture && !already_recaptured)
		{
			ensure(cruiser, "Recapture driver requires Cruiser transport authority");
			// Truce allows a close approach. This is ordinary Helm movement from a real saved attempt.
			move(950000, 650000, 15000);
			shields(false);
			advance(150);
		}
		if (!already_recaptured)
		{
			until(
			    [&]
			    {
				    const auto contacts = view("tactical").second.contacts;
				    for (const auto &contact : contacts)
					    if (contact.id == 1 && contact.disposition == "hostile")
						    return true;
				    return false;
			    },
			    "physical pirate takeover");
#ifndef __EMSCRIPTEN__
			write_checkpoint("build/rescue-" + prefix + "-occupied.zip", app.session->save());
#endif
		}

		if (recapture)
		{
			std::string boarding;
			const auto board = view(dispatch).second;
			for (const auto &mission : board.gameplay.at("away_missions"))
				if (mission.at("target") == 1 && mission.at("id").template get<std::string>().starts_with("board."))
					boarding = mission.at("id");
			ensure(!boarding.empty(), "Ordinary Retake Morrow mission is visible");
			if (!already_recaptured)
			{
				domain(dispatch, "away", "open", boarding);
				advance(1);
				domain("tactical", "away", "assign", boarding, "security/team:security-1");
				advance(1);
				domain("science", "away", "assign", boarding, "medical/team:medical-1");
				advance(1);
				domain(transport, "away", "transport", boarding, "displacement");
				advance(1);
				until(
				    [&]
				    {
					    const auto observation = view(dispatch).second;
					    for (const auto &mission : observation.gameplay.at("away_missions"))
						    if (mission.at("id") == boarding)
							    return mission.at("blocker") == "";
					    return false;
				    },
				    "recapture party ready");
				domain(dispatch, "away", "send", boarding);
				until(
				    [&]
				    {
					    const auto observation = view(dispatch).second;
					    for (const auto &mission : observation.gameplay.at("away_missions"))
						    if (mission.at("id") == boarding)
							    return mission.at("phase") == "deployed";
					    return false;
				    },
				    "recapture physical insertion");
				shields(true);
				until(
				    [&]
				    {
					    const auto contacts = view("tactical").second.contacts;
					    return std::any_of(contacts.begin(), contacts.end(),
					                       [](const auto &c)
					                       {
						                       return c.id == 1 && c.disposition == "friendly";
					                       });
				    },
				    "recapture boarding encounter");
			}

			bool recovered = false;
			for (const auto &operation : board.gameplay.at("away_missions"))
				if (operation.at("id") == boarding)
					recovered = operation.at("phase") == "recovered";
			if (!recovered)
			{
				// Follow the public obstruction assessment around terrain before closing for recovery.
				const auto approach = view("helm").second;
				if (approach.gameplay.at("navigation").value("blocked", false) &&
				    std::any_of(approach.contacts.begin(), approach.contacts.end(),
				                [&](const auto &contact)
				                {
					                return contact.id == 1 && std::hypot(double(contact.x - approach.own.x),
					                                                     double(contact.y - approach.own.y)) > 300000;
				                }))
				{
					const auto bearing =
					    (approach.gameplay.at("navigation").at("left_heading").get<std::int64_t>() + 60000) % 360000;
					const double angle = bearing * 3.141592653589793 / 180000;
					move(approach.own.x + static_cast<std::int64_t>(180000 * std::cos(angle)),
					     approach.own.y + static_cast<std::int64_t>(180000 * std::sin(angle)), 15000);
				}

				const auto tracked = view("tactical").second;
				for (const auto &contact : tracked.contacts)
					if (contact.id == 1)
					{
						const auto own = view("helm").second.own;
						const double distance =
						    std::max(1.0, std::hypot(double(own.x - contact.x), double(own.y - contact.y)));
						if (distance > 260000)
							move(contact.x + static_cast<std::int64_t>((own.x - contact.x) * 250000 / distance),
							     contact.y + static_cast<std::int64_t>((own.y - contact.y) * 250000 / distance), 15000);
					}

				shields(false);
				advance(150);
				// Combat can incapacitate the qualified navigator. Use an available utility
				// shuttle through the normal replacement-transport controls in that case.
				const auto recovery_view = view(dispatch).second;
				for (const auto &mission : recovery_view.gameplay.at("away_missions"))
					if (mission.at("id") == boarding && mission.at("blocker") == "Qualified navigator required")
					{
						const auto engineering_view = view(engineering).second;
						bool selected = false;
						for (const auto &operation : engineering_view.gameplay.at("away_missions"))
							if (operation.at("id") == boarding)
								for (const auto &transport_option : operation.at("transports"))
									if (!selected && transport_option.get<std::string>().starts_with("craft:"))
									{
										domain(engineering, "away", "transport", boarding, transport_option);
										selected = true;
									}
						ensure(selected, "Replacement recovery shuttle available");
						advance(1);
					}
				domain(dispatch, "away", "recall", boarding);
				until(
				    [&]
				    {
					    const auto observation = view(dispatch).second;
					    for (const auto &mission : observation.gameplay.at("away_missions"))
						    if (mission.at("id") == boarding)
							    return mission.at("phase") == "recovered";
					    return false;
				    },
				    "recapture selective recovery");
				shields(true);
			}
		}
		else
			until(
			    [&]
			    {
				    return objective("Secure the casualty escape");
			    },
			    "pirate group departure");
	}
	else
	{
		app.screen = Screen::viewscreen;
		app.console_regions.clear();
		capture("rescue-scene");
		app.screen = Screen::console;
		app.console_regions = {{1, 0, 0, 1280, 1080}, {2, 1280, 0, 1280, 1080}};
		show(cruiser ? 4 : 2, "comms");
		if (cruiser)
			app.console_regions[1].console = 4;
		for (const auto &control : app.screen_controls)
			if (control.first.starts_with("console/" + std::to_string(cruiser ? 4 : 2) + "/comms/select/") &&
			    control.second.find("Rescue") != std::string::npos)
			{
				const auto id = control.first;
				ensure(app.screen_action(id), "Open distress conversation");
				break;
			}
		advance(1);
		frame();
		capture("distress");
		if (cruiser)
			app.console_regions[1].console = 2;
		show(2, engineering);
		advance(1);
#ifndef __EMSCRIPTEN__
		{
			const auto archive = app.session->save();
			write_checkpoint("build/rescue-" + prefix + "-arrival.zip", archive);
		}
#endif
		domain(dispatch, "away", "open", "repairs");
		advance(1);
		domain(engineering, "away", "assign", "repairs",
		       std::string("repair-team/team:") + (cruiser ? "repair-1" : "team-1"));
		advance(1);
		domain(cruiser ? "science" : "support", "away", "assign", "repairs",
		       std::string("medical-team/team:") + (cruiser ? "medical-1" : "medical-team"));
		advance(1);
		if (seize)
		{
			domain("tactical", "away", "assign", "repairs",
			       std::string("security-team/team:") + (cruiser ? "security-1" : "security-team"));
			advance(1);
		}
		if (!cruiser)
		{
			domain("helm", "dock", "assist", "fore", "1/fore");
			advance(1);
			until(
			    [&]
			    {
				    const auto helm = view("helm").second;
				    for (const auto &port : helm.gameplay.at("docking").at("ports"))
					    if (port.at("id") == "fore" && port.at("phase") == "docked")
						    return true;
				    return false;
			    },
			    "casualty docking");
		}
		domain(transport, "away", "transport", "repairs", cruiser ? "displacement" : "ship");
		advance(1);
		if (cruiser)
			shields(false);
		advance(150);
		until(
		    [&]
		    {
			    const auto o = view(dispatch).second;
			    for (const auto &m : o.gameplay.at("away_missions"))
				    if (m.at("id") == "repairs")
					    return m.at("blocker") == "";
			    return false;
		    },
		    "party ready at transport");
		domain(dispatch, "away", "send", "repairs");
		advance(100);
		if (cruiser)
		{
			until(
			    [&]
			    {
				    const auto missions = view(dispatch).second.gameplay.at("away_missions");
				    for (const auto &m : missions)
					    if (m.at("id") == "repairs")
						    return m.at("phase") == "deployed";
				    return false;
			    },
			    "physical insertion");
			shields(true);
		}
		until(
		    [&]
		    {
			    return objective("Restore casualty propulsion");
		    },
		    "repair work");
		until(
		    [&]
		    {
			    return view("comms")
			               .second.gameplay.at("conversations")
			               .dump()
			               .find(seize ? "Your Security team has taken control" : "All work is resolved.") !=
			           std::string::npos;
		    },
		    "completed repair operation");
		if (cruiser)
		{
			shields(false);
			advance(150);
		}
		domain(dispatch, "away", "recall", "repairs");
		until(
		    [&]
		    {
			    return objective("Recover the repair party");
		    },
		    "party recovery");
		if (!cruiser)
		{
			domain("helm", "dock", "undock", "fore");
			advance(160);
		}
		if (!seize)
			until(
			    [&]
			    {
				    return objective("Secure the casualty escape");
			    },
			    "casualty escape");
	}
	until(
	    [&]
	    {
		    return objective("Deliver all 100 units to Cairn Receiving");
	    },
	    "station receipt");
	if (cruiser)
		until(
		    [&]
		    {
			    auto b = view(engineering).second.gameplay.at("logistics");
			    for (const auto &j : b.at("cargo"))
				    if (j.at("phase") != "complete")
					    return false;
			    return true;
		    },
		    "shuttle recovery");
	// Holding a charged drive while a prize cools needs its normal cooling allocation.
	const auto departure_engineering = view(engineering).second;
	for (const auto &controller : departure_engineering.gameplay.at("controllers"))
		if (controller.at("id") == "ftl.cooling")
			for (const auto &command : controller.at("commands"))
				if (command.at("action") == "cooling_allocation")
					channel(engineering, "cooling_allocation", command.at("maximum"), controller.at("id"));
	channel("navigator", "system_enabled", 1, "ftl.drive");
	advance(1);
	until(
	    [&]
	    {
		    return view("navigator").second.gameplay.at("ftl")[0].at("ready").get<bool>();
	    },
	    "departure charge");
	const auto [route, o] = view("navigator");
	OperatingCommand depart{ship, "ftl.depart", 0, "ftl.drive", "ftl.drive"};
	depart.console = "navigator";
	for (const auto &c : o.gameplay.at("controllers"))
		if (c.at("id") == "ftl.drive")
			depart.generation = c.at("generation");
	ensure(app.session->console_command(route, depart), "Departure accepted");
	advance(20);
	const auto report = view("helm").second.gameplay.at("mission_report");
	ensure(report.at("result") == (seize || recapture ? "claimed"
	                               : takeover         ? "taken"
	                                                  : "rescued"),
	       "Rescue result: " + report.dump());
	ensure(report.at("assessments")[0].at("result") == (cruiser ? "prompt" : "delivered"),
	       "Independent delivery result: " + report.dump());
	show(route, "navigator");
	app.console_regions[1].console = route;
	wait_report(route);
	capture("report");
	std::cout << "DEBRIEF " << report.dump() << std::endl;
	advance(1);
	{
		const auto archive = app.session->save();
#ifdef __EMSCRIPTEN__
		EM_ASM({Module.missionCheckpoint=URL.createObjectURL(new Blob([HEAPU8.slice($0,$0+$1)],{type:"application/zip"}));},archive.data(),archive.size());
#else
		write_checkpoint("build/rescue-" + prefix + "-report.zip", archive);
#endif
	}
	if (render)
		close_window();
	opened = false;
	std::cout << "PASS " << prefix
	          << (render ? " delivery and rescue graphical smoke" : " delivery and rescue fresh checkpoint")
	          << std::endl;
	return 0;
}
