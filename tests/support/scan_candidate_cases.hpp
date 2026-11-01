// Scoped objectives and observed targets, with no lookup of undisclosed findings.
void scan_candidate_contract()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"specified","behavior":"scan","tier":"objective_other","target":2,"system":"scanner","title_key":"Inspect specified contact"},
	 {"id":"survey","behavior":"scan","tier":"directive_other","title_key":"Survey known contacts"}])json");
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "scanner")
		{
			controller.erase("fallback");
			controller["tree"] = "test.scored-scanner";
		}
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"] = {"scanner"};
	auto &human = ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"];
	human.erase(std::remove(human.begin(), human.end(), "scanner"), human.end());
	source["maps"][0]["entities"][1]["x_mm"] = 90000;
	source["maps"][0]["entities"][2]["y_mm"] = 40000;
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.scan-selected","context":"controller","command_bindings":[{"channel":"target","action":"sensor_target"}],
	  "controller_action":{"label":"Inspect the selected permitted contact","inputs":[{"name":"target","type":"integer"}]},
	  "initial":"scan","states":[{"id":"scan","running":[{"action":"write.target","arguments":[{"observation":"input.target"}]}],
	    "exit":[{"action":"write.target","arguments":[0]}]}]},
	 {"id":"test.scored-scanner","context":"controller","scan_observations":true,
	  "command_bindings":[{"channel":"target","action":"sensor_target"}],
	  "action_bindings":[{"id":"scan","tree":"test.scan-selected"}],
	  "objective_bindings":[{"id":"scan","behavior":"scan","candidate_source":"scans","action":"scan",
	   "available":[{"left":{"observation":"scan_candidate.available"},"op":"eq","right":true}],
	   "target":{"observation":"scan_candidate.target"},"arguments":[{"observation":"scan_candidate.target"}],
	   "commitment":{"observation":"scan_candidate.continuing"},
	   "factors":{"progress":1000,"travel":{"observation":"scan_candidate.distance"}}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	{
		auto bearing_source = source;
		bearing_source["entities"][0]["systems"]["radar"]["range_mm"] = 30000;
		bearing_source["entities"][0]["systems"]["radar"]["long_range_mm"] = 100000;
		World bearing;
		bearing.load(load(bearing_source), "test.map");
		require(bearing.claim(101, 1, "drive"), "claim bearing-only scanner fixture");
		ticks(bearing, 3);
		const auto observation = *bearing.observe(101);
		bool selected = false;
		for (const auto &controller : observation.gameplay.at("controllers"))
			if (controller.at("id") == "scanner")
				selected = controller.at("commands").at("target").at("value") == 2;
		require(!observation.bearing_contacts.empty() && selected,
		        "a scoped bearing-only target can initiate ordinary active acquisition without hidden range");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim scored scanner Console");
	ticks(world, 3);
	const auto scanner = [](const World &value)
	{
		const auto view = *value.observe(101);
		for (const auto &controller : view.gameplay.at("controllers"))
			if (controller.at("id") == "scanner")
				return controller;
		throw std::runtime_error("missing scored scanner");
	};
	require(scanner(world).at("commands").at("target").at("value") == 2,
	        "a scoped scan objective outranks a nearer survey contact through common arbitration");
	auto changed = source;
	changed["maps"][0]["sensor_findings"][0]["target_entity"] = 3;
	World alternate;
	alternate.load(load(changed), "test.map");
	require(alternate.claim(101, 1, "drive"), "claim alternate hidden finding fixture");
	ticks(alternate, 3);
	require(scanner(world).at("commands") == scanner(alternate).at("commands") &&
	            scanner(world).at("objective_candidates") == scanner(alternate).at("objective_candidates"),
	        "undisclosed finding declarations cannot choose scan targets or alter utility");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 20);
	ticks(resumed, 20);
	require(world.save() == resumed.save(), "physical scoped scanning resumes identically");
	const auto view = *world.observe(101);
	bool acquired = false;
	for (const auto &contact : view.gameplay.at("sensors").at("contacts"))
		if (contact.at("id") == 2)
			acquired |= contact.at("findings").contains("shield_sectors");
	require(acquired, "ordinary scored scan orders acquire useful permitted evidence");
	const auto wire = network::bounded_gameplay(view.gameplay);
	require(wire.at("sensors").at("scanners")[0].contains("powered") &&
	            wire.at("sensors").at("scanners")[0].contains("capabilities"),
	        "native and browser projections preserve scoped scanner capability facts");
	auto hidden = world.save();
	for (auto &body : hidden["bodies"])
		if (body.at("body").at("id") == 2)
			body["body"]["position"]["x"] = 900000;
	world.restore(catalog, hidden);
	ticks(world, 3);
	require(scanner(world).at("commands").at("target").at("value") == 3,
	        "an unavailable scoped scan leaves its objective active and falls through to known survey work");
}
