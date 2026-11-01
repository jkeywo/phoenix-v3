void scored_ftl_cases()
{
	auto fixture = ftl_fixture();
	fixture["maps"][0]["entities"].erase(fixture["maps"][0]["entities"].begin() + 1,
	                                     fixture["maps"][0]["entities"].end());
	auto &ship = fixture["entities"][0];
	ship["resources"]["stores"][0]["initial"] = 0;
	ship["resources"]["generators"] = Json::array(
	    {{{"id", "plant"}, {"store", "cell"}, {"maximum_rate", 1000}, {"response_rate", 1000}, {"initial_output", 0}}});
	ship["systems"]["reactor"] = {{"kind", "generator"}, {"generator", "plant"}};
	ship["stations"][0]["consoles"][0]["feeds"].push_back("reactor");
	ship["control_layout"]["controllers"].push_back(
	    Json::parse(R"json({"id":"engineer","console":"drive","tree":"test.ftl.power",
        "commands":[{"id":"output","action":"generator_output","consumers":["reactor"]}]})json"));
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["drive"]["ai"].push_back("engineer");
	ship["standing_directives"] = Json::parse(R"json([
  {"id":"ready","behavior":"ready","tier":"directive_other","title_key":"Maintain readiness"},
  {"id":"leave","behavior":"depart","tier":"scripted_low","system":"jump","title_key":"Leave when prepared"}])json");
	for (auto &owner : ship["control_layout"]["controllers"])
		if (owner["id"] == "jump")
		{
			owner.erase("fallback");
			owner["tree"] = "test.ftl.scored";
		}
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["navigator"] = {{"ai", {"jump"}}};
	for (const auto &tree : Json::parse(R"json([
  {"id":"test.ftl.charge","context":"controller","command_bindings":[{"channel":"enabled","type":"boolean"}],
   "controller_action":{"label":"Prepare a selected reserve"},"initial":"charge","states":[{"id":"charge","running":[
    {"action":"write.enabled","arguments":[true]},{"action":"action.succeed","arguments":["charging_admitted"]}]}]},
  {"id":"test.ftl.depart","context":"controller","ftl_commands":true,
   "controller_action":{"label":"Depart on the selected goal","inputs":[{"name":"drive","type":"text"}]},
   "initial":"depart","states":[{"id":"depart","running":[
    {"action":"ftl.depart","arguments":[{"observation":"input.drive"}]},{"action":"action.succeed","arguments":["departure_admitted"]}]}]},
  {"id":"test.ftl.wait","context":"controller","controller_action":{"label":"Maintain other readiness"},
   "initial":"wait","states":[{"id":"wait","running":[{"action":"action.succeed","arguments":["maintained"]}]}]},
  {"id":"test.ftl.scored","context":"controller","ftl_system":"jump","command_bindings":[{"channel":"enabled","type":"boolean"}],
   "action_bindings":[{"id":"charge","tree":"test.ftl.charge"},{"id":"depart","tree":"test.ftl.depart"},{"id":"wait","tree":"test.ftl.wait"}],
   "objective_bindings":[
    {"id":"charge","behavior":"depart","action":"charge","available":[
     {"left":{"observation":"objective.record.system"},"op":"eq","right":"jump"},
     {"left":{"observation":"objective.record.target"},"op":"eq","right":0},
     {"left":{"observation":"ftl.ready"},"op":"eq","right":false},
     {"left":{"observation":"command.enabled"},"op":"eq","right":false}],"factors":{"progress":100}},
    {"id":"depart","behavior":"depart","action":"depart","available":[
     {"left":{"observation":"objective.record.system"},"op":"eq","right":"jump"},
     {"left":{"observation":"objective.record.target"},"op":"eq","right":0},
     {"left":{"observation":"ftl.ready"},"op":"eq","right":true}],"arguments":["jump"],"factors":{"progress":1000}},
    {"id":"wait","behavior":"ready","action":"wait","factors":{"progress":1}}],"initial":"observe","states":[{"id":"observe"}]}
 ])json"))
		fixture["trees"].push_back(tree);
	for (const auto &tree : Json::parse(R"json([
        {"id":"test.ftl.power-action","context":"controller","command_bindings":[{"channel":"output","action":"generator_output"}],
         "controller_action":{"label":"Supply the selected departure","inputs":[{"name":"amount","type":"integer"}]},
         "initial":"supply","states":[{"id":"supply","running":[{"action":"write.output","arguments":[{"observation":"input.amount"}]},
             {"action":"action.succeed","arguments":["supply_admitted"]}]}]},
        {"id":"test.ftl.power","context":"controller","command_bindings":[{"channel":"output","action":"generator_output"}],
         "system_bindings":[{"name":"power.maximum","system":"reactor","field":"maximum_rate"}],
         "action_bindings":[{"id":"supply","tree":"test.ftl.power-action"}],
         "objective_bindings":[{"id":"departure","behavior":"depart","action":"supply","available":[
             {"left":{"observation":"objective.record.target"},"op":"eq","right":0},
             {"left":{"observation":"objective.record.system"},"op":"eq","right":"jump"}],
             "arguments":[{"observation":"power.maximum"}],"factors":{"progress":1000}},
             {"id":"idle","behavior":"ready","action":"supply","arguments":[0],"factors":{"progress":1}}],
         "initial":"observe","states":[{"id":"observe"}]}
    ])json"))
		fixture["trees"].push_back(tree);
	for (const auto *case_id : {"missing", "wrong_target", "wrong_drive", "human"})
	{
		auto source = fixture;
		auto &goal = source["entities"][0]["standing_directives"][1];
		if (std::string(case_id) == "missing")
			source["entities"][0]["standing_directives"].erase(1);
		else if (std::string(case_id) == "wrong_target")
			goal["target"] = 2;
		else if (std::string(case_id) == "wrong_drive")
			goal["system"] = "other";
		else
			for (auto &profile : source["entities"][0]["control_layout"]["profiles"])
				profile["consoles"]["navigator"] = {{"human", {"jump"}}};
		World world;
		world.load(load(source), "test.map");
		require(world.claim(101, 1, "drive"), "Claim scoped departure fixture");
		ticks(world, 40);
		require(!battery(world).at("enabled").get<bool>() &&
		            world.observe(101)->gameplay.at("mission_report").is_null(),
		        "FTL preparation requires a relevant objective and delegated command authority");
	}
	const auto catalog = load(fixture);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim autonomous scored FTL fixture");
	ticks(world, 30);
	require(battery(world).at("enabled") == true && battery(world).at("energy") > 0 &&
	            battery(world).at("ready") == false,
	        "A shipwide departure objective coordinates ordinary generator supply and finite reserve charging");
	bool waiting = false;
	const auto observed = navigator(world);
	for (const auto &owner : observed.gameplay.at("controllers"))
		if (owner.at("id") == "jump")
			waiting = owner.at("action").at("id") == "wait";
	require(waiting, "Pending physical preparation leaves other feasible readiness work selectable");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 1000 && world.observe(101)->gameplay.at("mission_report").is_null(); ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(!world.observe(101)->gameplay.at("mission_report").is_null() && world.save() == resumed.save(),
	        "A ready scored departure executes once and resumes exactly from partial preparation");
}
