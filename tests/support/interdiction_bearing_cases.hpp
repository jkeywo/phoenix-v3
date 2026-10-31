#pragma once

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
	     {"fallback", "hold"},
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
					require(!positional, "First homing launch occurs outside positional radar coverage");
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
			continuation(catalog, w, 5, "Bearing pursuit and homing ammunition resume exactly");
	}
	require(homing && close && cannon,
	        "Encounter closes on long-range bearing, fires homing, then cannon and short rounds");
	trace.push_back(w.save());
}
