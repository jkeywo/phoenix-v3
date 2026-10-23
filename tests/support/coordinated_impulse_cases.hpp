#pragma once
void coordinated_impulse_cases()
{
	auto source = explicit_fixture(fixture());
	auto &ship = source["entities"][0];
	ship["systems"]["impulse-two"] = ship["systems"]["impulse"];
	ship["stations"][0]["consoles"][0]["feeds"].push_back("impulse-two");
	auto &layout = ship["control_layout"];
	for (auto &controller : layout["controllers"])
		for (auto at = controller["commands"].begin(); at != controller["commands"].end();)
			if (at->at("action") == "impulse")
				at = controller["commands"].erase(at);
			else
				++at;
	for (const auto *name : {"impulse", "impulse-two"})
		layout["controllers"].push_back({{"id", std::string("paired.") + name},
		                                 {"console", "helm"},
		                                 {"fallback", "hold"},
		                                 {"commands", Json::array({{{"id", "pulse"},
		                                                            {"action", "impulse"},
		                                                            {"semantics", "request"},
		                                                            {"consumers", {name}},
		                                                            {"individual_stars", 3},
		                                                            {"coordinated_group", "pair"}}})}});
	layout["station_complexity"] = {"bridge"};
	layout["default_profile"] = "assisted";
	layout["profiles"] = Json::array();
	for (const auto stars : {1, 3})
	{
		Json profile{{"id", stars == 1 ? "assisted" : "full"}, {"stars", stars}, {"consoles", Json::object()}};
		for (const auto &c : layout["controllers"])
			profile["consoles"][c.at("console").get<std::string>()]["human"].push_back(c.at("id"));
		layout["profiles"].push_back(profile);
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	const auto observed = *world.observe(101);
	OperatingCommand individual{1, "impulse", 1, {}};
	individual.controller = "paired.impulse";
	individual.channel = "pulse";
	individual.request_id = 1;
	for (const auto &c : observed.gameplay.at("controllers"))
		if (c.at("id") == "paired.impulse")
			individual.generation = c.at("generation");
	require(!world.submit(101, individual), "One star refuses individual impulse execution");
	OperatingCommand group;
	for (const auto &d : observed.gameplay.at("controls"))
		if (d.at("action") == "flight.impulse" && d.at("value") == 1)
			group = decode_operating_command(d);
	require(group.request_id && world.submit(101, group), "Coordinated impulse descriptor admits a request");
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == telemetry(world, "impulse-two", "impulse_phase") &&
	            telemetry(world, "impulse", "impulse_phase") != 0,
	        "Both impulse executors receive the coordinated request");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	require(world.submit(101, group), "Repeated coordinated request is idempotent");
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "A repeated group request neither replays nor diverges across recovery");
}
