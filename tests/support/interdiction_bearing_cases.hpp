#pragma once

void unobserved_player_does_not_block_boarding(Json &trace, bool bearing_only = false)
{
	auto fixture = interdiction_fixture();
	fixture["maps"][0]["ship_slots"][0]["x_mm"] = -100000;
	fixture["maps"][0]["interdictions"][0]["engagement_radius_mm"] = 1000000;
	auto &pirate = fixture["entities"].back();
	if (bearing_only)
		for (const auto *id : {"radar", "source-scanner"})
		{
			pirate["systems"][id]["range_mm"] = 100000;
			pirate["systems"][id]["long_range_mm"] = 1000000;
		}
	for (auto &controller : pirate["control_layout"]["controllers"])
		if (!bearing_only && (controller["id"] == "radar" || controller["id"] == "source-scanner"))
			for (auto &command : controller["commands"])
				if (command["id"] == "enabled")
					command["initial"] = 0;
	const auto catalog = load(fixture);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	bool continued = false;
	for (int n = 0; n < 2400 && !state(world)["captured"].get<bool>(); ++n)
	{
		world.tick();
		if (state(world)["ships"].empty())
			continue;
		auto saved = world.save();
		const auto actor = state(world)["ships"][0].get<EntityId>();
		require(body(saved, actor)["combat"]["systems"]["gun"]["shots"] == 0,
		        "A stale shared target cannot fire on an unobserved player");
		require(!state(world)["blocked"].get<bool>(),
		        "True nearby player geometry cannot block boarding without sensor evidence");
		if (!continued && body(saved, actor).contains("dock_connection"))
		{
			continuation(catalog, world, 3, "Unobserved threat and boarding approach resume exactly");
			continued = true;
		}
	}
	require(continued && state(world)["captured"].get<bool>(),
	        "Raider physically docks and takes its objective while a nearby player is unobserved");
	trace.push_back(world.save());
}

// Included inside the interdiction suite's helper namespace. All fitted values
// are synthetic and exercise sensor/command/loader boundaries together.
void bearing_engagement(Json &trace)
{
	auto fixture = interdiction_fixture();
	auto &map = fixture["maps"][0];
	map["ship_slots"][0]["x_mm"] = 20000;
	auto &encounter = map["interdictions"][0];
	encounter["combat_standoff_mm"] = 5000;
	encounter["variants"]["default"][0]["x_mm"] = 200000;
	auto &pirate = fixture["entities"].back();
	for (const auto *id : {"radar", "source-scanner"})
	{
		pirate["systems"][id]["range_mm"] = 40000;
		pirate["systems"][id]["long_range_mm"] = 500000;
	}
	pirate["systems"]["gun"]["projectile"]["lifetime_ticks"] = 120;
	pirate["systems"]["gun"]["fire_ticks"] = 20;
	Json payload = {
	    {"visual", "test.away"}, {"speed_mm_s", 20000}, {"lifetime_ticks", 400}, {"penetration", 1},
	    {"damage", 1},           {"damage_per_m", 1},   {"guidance", "homing"},  {"seeker_range_mm", 500000}};
	for (const auto *id : {"long-mag", "short-mag"})
	{
		if (std::string(id) == "short-mag")
		{
			payload["guidance"] = "chaotic";
			payload["lifetime_ticks"] = 30;
		}
		pirate["systems"][id] = {{"kind", "magazine"},
		                         {"compartment", "bridge"},
		                         {"capacity", 100},
		                         {"reload", {{"strategy", "none"}}},
		                         {"projectile", payload}};
		pirate["stations"][0]["consoles"][0]["feeds"].push_back(id);
	}
	pirate["systems"]["launcher"] = {
	    {"kind", "weapon"},   {"compartment", "bridge"}, {"magazines", {"long-mag", "short-mag"}},
	    {"range_mm", 500000}, {"arc_mdeg", 360000},      {"fire_ticks", 20},
	    {"load_ticks", 2}};
	pirate["control_layout"]["controllers"].push_back(
	    {{"id", "launcher"},
	     {"console", "drive"},
	     {"tree", "test.pirate.launcher"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"launcher"}}},
	                   {{"id", "target"}, {"action", "weapon_target"}, {"consumers", {"launcher"}}},
	                   {{"id", "magazine"}, {"action", "magazine_select"}, {"consumers", {"launcher"}}}})}});
	pirate["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"].push_back("launcher");
	pirate["stations"][0]["consoles"][0]["feeds"].push_back("launcher");
	pirate["personnel"]["duties"].push_back({{"id", "launcher"},
	                                         {"label", "Launcher"},
	                                         {"controller", "launcher"},
	                                         {"department", "navigation"},
	                                         {"compartment", "bridge"}});
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.pirate.launch","context":"controller",
	  "command_bindings":[{"channel":"target"},{"channel":"enabled","type":"boolean"},{"channel":"magazine"}],
	  "controller_action":{"label":"Engage using permitted targeting and scored ammunition","inputs":[
	   {"name":"target","type":"integer"},{"name":"magazine","type":"integer"}]},
	  "initial":"launch","states":[{"id":"launch","running":[
	   {"action":"write.enabled","arguments":[true]},
	   {"action":"write.target","arguments":[{"observation":"input.target"}]},
	   {"action":"write.magazine","arguments":[{"observation":"input.magazine"}]},
	   {"action":"action.succeed","arguments":["launch_admitted"]}]}]},
	 {"id":"test.pirate.launcher","context":"controller","contact_observations":true,"armament_observations":true,
	  "command_bindings":[{"channel":"target"},{"channel":"enabled","type":"boolean"},{"channel":"magazine"}],
	  "action_bindings":[{"id":"launch","tree":"test.pirate.launch"}],
	  "objective_bindings":[
	   {"id":"engage","behavior":"engage","candidate_source":"armament","action":"launch",
	    "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true},
	     {"left":{"observation":"armament.available"},"op":"eq","right":true},
	     {"left":{"observation":"armament.enabled"},"op":"eq","right":true},
	     {"left":{"observation":"armament.in_range"},"op":"eq","right":true}],
	    "target":{"observation":"contact_candidate.id"},"arguments":[{"observation":"contact_candidate.id"},{"observation":"armament.index"}],
	    "commitment":{"observation":"armament.selected"},
	    "factors":{"progress":1000,"resources":{"observation":"armament.range"}}},
	   {"id":"hold","behavior":"hold","action":"launch","arguments":[0,{"observation":"command.magazine"}],"factors":{"progress":1}},
	   {"id":"ready","behavior":"ready","action":"launch","arguments":[0,{"observation":"command.magazine"}],"factors":{"progress":1}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		fixture["trees"].push_back(tree);
	const auto catalog = load(fixture);
	World w;
	w.load(catalog, "test.map", {{"crew", "test.away"}});
	bool homing = false, close = false, cannon = false;
	for (int n = 0; n < 1400 && !(homing && close && cannon); ++n)
	{
		w.tick();
		const auto saved = w.save();
		if (state(w)["ships"].empty())
			continue;
		const auto actor = state(w)["ships"][0].get<EntityId>();
		auto copy = saved;
		const auto &origin = body(copy, actor)["body"]["position"];
		const auto &target = body(copy, 2)["body"]["position"];
		const auto dx = origin["x"].get<std::int64_t>() - target["x"].get<std::int64_t>();
		const auto dy = origin["y"].get<std::int64_t>() - target["y"].get<std::int64_t>();
		const auto distance_squared = dx * dx + dy * dy;
		bool positional = false;
		for (const auto &sensor : saved["sensors"])
			if (sensor["ship"] == actor)
				for (const auto &track : sensor["tracks"])
					if (track["target"] == 2 && track["state"]["seen"].get<std::uint64_t>() + 1 >= w.ticks())
						positional = true;
		for (const auto &projectile : saved["projectiles"])
			if (projectile["source"] == actor)
			{
				if (!homing && projectile["definition"]["guidance"] == "homing")
					require(
					    positional,
					    "A Controller waits for permitted current targeting evidence before its first homing launch");
				homing |= projectile["definition"]["guidance"] == "homing";
				if (!close && projectile["definition"]["guidance"] == "chaotic")
					require(distance_squared <= 30000LL * 30000,
					        "Short round waits for observed range within its physical reach");
				close |= projectile["definition"]["guidance"] == "chaotic";
			}
		const auto shots = body(copy, actor)["combat"]["systems"]["gun"]["shots"].get<int>();
		if (!cannon && shots > 0)
			require(positional, "Short-range cannon waits for positional acquisition");
		cannon |= shots > 0;
		if (n == 40)
			continuation(catalog, w, 5, "Acquisition and scored homing ammunition resume exactly");
	}
	require(
	    homing && close && cannon,
	    "Ordinary Controllers acquire targeting evidence and fire scored long/short ammunition after bearing pursuit");
	trace.push_back(w.save());
}
