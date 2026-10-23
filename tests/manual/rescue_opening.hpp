// Explicit manual smoke: normal authored cargo and rescue, rendered with real Consoles.
int rescue_opening_smoke(const std::string &directory, bool cruiser, bool &opened, const std::string &checkpoint = {},
                         bool preview = false)
{
	Application app;
	app.can_host = true;
	app.native_menu = false;
	app.session = make_local_session();
	const std::string fit = cruiser ? "base.delivery-cruiser" : "base.rescue-courier";
	if (checkpoint.empty())
	{
		app.session->add_pack(read_file(directory + "/base.zip"));
		std::map<std::string, std::map<std::string, std::string>> profiles;
		if (!cruiser)
			for (const auto *console : {"helm", "support", "tactical", "comms", "navigator", "navigation"})
				profiles["courier"][console] = "detailed";
		app.session->start({"base.rescue-under-threat", {{"courier", fit}}, "courier", "helm", profiles});
	}
	else
	{
		app.session->restore(read_file(checkpoint));
		app.session->resume_local();
	}
	const auto ship = app.session->observation()->own.id;
	auto ensure = [](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(why);
	};
	ensure(app.session->claim_console(2, {ship, cruiser ? "engineering" : "support"}), "Delivery engineering claim");
	if (cruiser)
	{
		int route = 3;
		for (const auto *seat : {"tactical", "comms", "science", "captain"})
			ensure(app.session->claim_console(route++, {ship, seat}), std::string("Claim ") + seat);
	}
	open_window();
	opened = true;
	SetWindowSize(2560, 1080);
	SetTargetFPS(0);
	app.screen = Screen::console;
	app.console_regions = {{1, 0, 0, 1280, 1080}, {2, 1280, 0, 1280, 1080}};
	const std::string prefix = cruiser ? "cruiser" : "courier";
	auto frame = [&]
	{
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
			if (controller.empty() || c.at("id") == controller)
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
	auto until = [&](auto done, const std::string &label, int limit = 18000)
	{
		std::cout << "WAIT " << prefix << " " << label << std::endl;
		for (int i = 0; i < limit && !done(); i += 50)
		{
			const auto [comms_route, comms] = view("comms");
			for (const auto &thread : comms.gameplay.at("conversations").at("threads"))
				if (thread.at("prompt") != 0)
				{
					bool answered = false;
					for (const auto &preferred :
					     cruiser ? std::vector<std::string>{"reactor", "wounded", "propulsion", "slow"}
					             : std::vector<std::string>{"wounded", "reactor", "propulsion", "slow"})
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
										break;
									}
								}
				}
			advance(50);
			if (i % 1000 == 0)
			{
#ifndef __EMSCRIPTEN__
				if (label == "repair work" || label == "physical receipt" || label == "casualty docking")
				{
					const auto archive = app.session->save();
					const auto state = read_pack(archive).draft();
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
	auto panel = [&](std::uint64_t route, const std::string &name)
	{
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
			    return std::hypot(double(o.vx), double(o.vy)) < 100;
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
	for (int i = 0; i < 800 && view("helm").second.gameplay.at("mission_report").is_null() &&
	                std::none_of(
	                    app.screen_controls.begin(), app.screen_controls.end(),
	                    [](const auto &c)
	                    {
		                    return c.first == "console/1/personnel";
	                    });
	     ++i)
		frame();
	const std::string engineering = cruiser ? "engineering" : "support";
	const std::string dispatch = cruiser ? "comms" : "helm", transport = cruiser ? "science" : "helm";
	if (!view("helm").second.gameplay.at("mission_report").is_null())
	{
		const auto route = view("navigator").first;
		show(route, "navigator");
		app.console_regions[1].console = route;
		capture("report");
		close_window();
		opened = false;
		std::cout << "PASS " << prefix << " saved report graphical smoke" << std::endl;
		return 0;
	}
	if (checkpoint.empty())
	{
		advance(40);
		const auto engineering_view = view(engineering).second;
		for (const auto &c : engineering_view.gameplay.at("controllers"))
			for (const auto &command : c.at("commands"))
				if (command.at("action") == "generator_output")
					channel(engineering, "generator_output", command.at("maximum"), c.at("id"));
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
			domain(engineering, "logistics", "deliver", "mission-hold",
			       std::to_string(craft) + "/cargo/2/receiving/perishable-essentials/100");
			advance(2);
			panel(2, "logistics");
			capture("cargo-order");
			panel(2, "local-settings.close");
			channel("comms", "system_enabled", 0, "external-comms");
			advance(1);
			move(950000, 0, 20000);
			channel("comms", "system_enabled", 1, "external-comms");
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
			domain(engineering, "logistics", "transfer", "mission-hold", "2/receiving/station-supplies/100");
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
			// Stay in the station corridor before turning into the southern rescue opening.
			move(950000, 0, 20000);
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
		std::ofstream file("build/rescue-" + prefix + "-arrival.zip", std::ios::binary);
		file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
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
	for (const auto *console :
	     cruiser ? std::vector<const char *>{"science"} : std::vector<const char *>{"support", "tactical"})
	{
		const auto o = view(console).second;
		for (const auto &c : o.gameplay.at("controllers"))
			if (c.at("id").get<std::string>().starts_with("shield-"))
				for (const auto &command : c.at("commands"))
					if (command.at("action") == "system_enabled")
						channel(console, "system_enabled", 0, c.at("id"));
	}
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
	until(
	    [&]
	    {
		    return objective("Restore casualty propulsion");
	    },
	    "repair work");
	until(
	    [&]
	    {
		    return view("comms").second.gameplay.at("conversations").dump().find("All work is resolved.") !=
		           std::string::npos;
	    },
	    "completed repair operation");
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
	until(
	    [&]
	    {
		    return objective("Secure the casualty escape");
	    },
	    "casualty escape");
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
	ensure(report.at("result") == "success", "Rescue result: " + report.dump());
	ensure(report.at("assessments")[0].at("result") == (cruiser ? "prompt" : "delivered"),
	       "Independent delivery result: " + report.dump());
	show(route, "navigator");
	app.console_regions[1].console = route;
	capture("report");
	std::cout << "DEBRIEF " << report.dump() << std::endl;
#ifndef __EMSCRIPTEN__
	advance(1);
	{
		const auto archive = app.session->save();
		std::ofstream file("build/rescue-" + prefix + "-report.zip", std::ios::binary);
		file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
	}
#endif
	close_window();
	opened = false;
	std::cout << "PASS " << prefix << " delivery and rescue graphical smoke" << std::endl;
	return 0;
}
