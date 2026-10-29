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
	source["trees"].push_back(Json::parse(
	    R"json({"id":"test.paired-ai","context":"controller","command_bindings":[{"channel":"pulse","action":"impulse","type":"boolean","semantics":"request"}],"initial":"run","states":[{"id":"run","running":[{"attempt":[{"tasks":[{"action":"write.pulse","arguments":[true]}]},{"tasks":[]}]}]}]})json"));
	for (auto &controller : layout["controllers"])
		if (controller.at("id").get<std::string>().starts_with("paired."))
		{
			controller.erase("fallback");
			controller["tree"] = "test.paired-ai";
		}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 0 && telemetry(world, "impulse-two", "impulse_phase") == 0,
	        "Human coordinated launch grant refuses autonomous individual triggers");
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
	bool presented = false;
	for (const auto &widget : console_control_widgets(catalog, observed))
		for (const auto &control : widget.controls)
			presented =
			    presented || (control.value("action", std::string{}) == "flight.impulse" && control.at("value") == 1);
	require(presented, "Coordinated launch is rendered by its logical widget at the assisted rating");
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
	{
		auto limited = source;
		for (auto &controller : limited["entities"][0]["control_layout"]["controllers"])
			if (controller.at("id") == "paired.impulse-two")
				controller["commands"][0]["maximum"] = 0;
		World bounded;
		bounded.load(load(limited), "test.map");
		claim(bounded);
		bounded.tick();
		OperatingCommand launch;
		const auto bounded_view = bounded.observe(101);
		for (const auto &control : bounded_view->gameplay.at("controls"))
			if (control.at("action") == "flight.impulse" && control.at("value") == 1)
				launch = decode_operating_command(control);
		const auto before = bounded.save();
		require(launch.request_id && !bounded.can_submit(101, launch) && !bounded.submit(101, launch),
		        "Coordinated launch preflights every participant's authored command bounds");
		require(bounded.save() == before, "Refused group launch cannot stage a request on an earlier participant");
	}
}
