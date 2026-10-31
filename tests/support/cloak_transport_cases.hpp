#pragma once
void cloak_transport_cases()
{
	auto source = assisted_docking_source();
	auto &hull = source["entities"][0];
	hull["control_layout"]["default_profile"] = "direct";
	hull["systems"]["scan"]["range_mm"] = 20000;
	hull["faction"] = "workers";
	source["maps"][0]["factions"] = Json::array({{{"id", "workers"}, {"label", "Workers"}}});
	source["maps"][0]["entities"][0]["faction"] = "workers";
	source["maps"][0]["ship_slots"][0]["faction"] = "workers";
	for (auto &port : hull["docking"]["ports"])
	{
		port["auto_accept_allies"] = true;
		port["separation_ticks"] = 3;
	}
	if (!source.contains("effects"))
		source["effects"] = Json::array();
	source["effects"].push_back(Json::parse(
	    R"json({"id":"test.transport-cloak","kind":"psychic","psychic_kind":"cloak","label":"Cloak","strength_permille":1000})json"));
	source["effects"].push_back(Json::parse(
	    R"json({"id":"test.transport-veil","kind":"ability","label":"Veil","sustained":true,"target":"self","energy_per_second":10,"recovery_ticks":2,"recovery_on":"stop","shortage":"proportional","require_healthy":true,"psychic_effect":"test.transport-cloak"})json"));
	hull["systems"]["choir"] = Json::parse(
	    R"json({"kind":"psychic","compartment":"bridge","power_store":"cell","supported_abilities":["test.transport-veil"],"seats":[{"duty":"cloak-seat","controller":"choir","console":"drive","enabled":"cloak-enabled","target":"cloak-target"}]})json");
	hull["control_layout"]["controllers"].push_back(Json::parse(
	    R"json({"id":"choir","console":"drive","fallback":"hold","commands":[{"id":"apparatus","action":"system_enabled","initial":1,"consumers":["choir"]},{"id":"cloak-enabled","action":"intent_boolean","consumers":[]},{"id":"cloak-target","action":"intent_integer","minimum":0,"maximum":1000000000,"consumers":[]}]})json"));
	for (auto &profile : hull["control_layout"]["profiles"])
		profile["consoles"]["drive"]["human"].push_back("choir");
	hull["stations"][0]["consoles"][0]["feeds"].push_back("choir");
	hull["personnel"]["duties"].push_back({{"id", "conductor"},
	                                       {"label", "Conductor"},
	                                       {"controller", "choir"},
	                                       {"console", "drive"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"}});
	hull["personnel"]["duties"].push_back({{"id", "cloak-seat"},
	                                       {"label", "Cloak seat"},
	                                       {"choir", "choir"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"}});
	hull["personnel"]["officers"].push_back({{"id", "cloaker"},
	                                         {"name", "Cloaker"},
	                                         {"department", "navigation"},
	                                         {"compartment", "bridge"},
	                                         {"duty", "cloak-seat"},
	                                         {"traits", Json::array()},
	                                         {"abilities", {"test.transport-veil"}}});
	for (auto &entity : source["entities"])
		if (entity.at("id") == "test.receiving-ship")
		{
			auto receiving = hull;
			receiving["id"] = "test.receiving-ship";
			entity = std::move(receiving);
		}

	const auto catalog = load(source);
	World world;

	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto ship = *world.ship_for_slot("crew");
	require(world.claim(101, ship, "drive") && world.claim(102, ship, "observer") && world.claim(103, 1, "drive"),
	        "claim transport cloak fixture");
	const auto cloak = [&](std::uint64_t claim)
	{
		const auto observed = world.observe(claim);
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("id") == "choir")
				require(fixture_native_submit(world, claim,
				                              {world.observe(claim)->own.id,
				                               {},
				                               1,
				                               {},
				                               "choir",
				                               "cloak-enabled",
				                               controller.at("generation")}),
				        "request funded cloak");
	};
	const auto emission = [&](std::uint64_t claim)
	{
		return world.observe(claim)->gameplay.at("psychic").at("choirs")[0].at("emission").get<int>();
	};
	const auto dock = [&](const std::string &action)
	{
		const auto observed = world.observe(101);
		OperatingCommand command{ship, "dock." + action, observed->gameplay.at("docking").at("revision"), "fore",
		                         "dispatch"};
		command.console = "drive";
		command.text = action == "connect" ? "1/aft" : "";
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("id") == "dispatch")
				command.generation = controller.at("generation");
		require(world.submit(101, command), "ordinary docking command admitted");
	};

	cloak(101);
	cloak(103);
	ticks(world, 5);
	require(emission(101) < 1000 && emission(103) < 1000 &&
	            world.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") > 0 &&
	            world.observe(103)->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") > 0,
	        "quiet ships sustain funded concealment before transport disruption");

	dock("connect");
	ticks(world, 6);
	require(emission(101) == 1000 && emission(103) == 1000, "physical docking exposes both ends of the connection");
	dock("undock");
	ticks(world, 6);
	require(emission(101) < 1000 && emission(103) < 1000, "separation restores otherwise eligible cloak");

	require(world.submit(101, order(world, "open")), "open away work");
	ticks(world, 1);
	require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "stage the repair party");
	ticks(world, 1);
	require(world.submit(101, order(world, "transport", "transport")), "select displacement transport");
	ticks(world, 20);
	require(emission(101) < 1000, "preparing an away mission does not disrupt cloak");

	require(world.submit(101, order(world, "send")), "launch the staged party");
	ticks(world, 1);
	require(emission(101) == 1000, "launching an away party disrupts cloak");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 5);
	ticks(resumed, 5);
	require(world.save() == resumed.save() && emission(101) == 1000,
	        "deployed-party disruption survives exact continuation");
	require(world.submit(101, order(world, "recall")), "recall the away party");
	ticks(world, 1);
	require(emission(101) == 1000, "returning personnel still disrupt cloak");
	ticks(world, 12);
	require(board(world)[0].at("phase") == "recovered" && emission(101) < 1000,
	        "recovered personnel permit quiet cloak again");
}
