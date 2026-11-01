void docking_candidate_cases()
{
	auto source = docking_fixture();
	auto &ship = source["entities"][0];
	ship["render"]["radar_icon"] = "ship";
	ship["tags"] = {"ship"};
	ship["systems"]["radar"] = {
	    {"kind", "radar"}, {"range_mm", 100000}, {"shows", {"ship", "asteroid"}}, {"compartment", "bridge"}};
	ship["stations"][0]["consoles"][0]["feeds"].push_back("radar");
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "radar"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"radar"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("radar");
	ship["personnel"]["duties"].push_back({{"id", "radar"},
	                                       {"label", "Radar"},
	                                       {"controller", "radar"},
	                                       {"console", "drive"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"},
	                                       {"effects", Json::object()}});
	ship["standing_directives"] = Json::parse(R"json([
  {"id":"dock","behavior":"ready","tier":"directive_other","target":1,"system":"aft","title_key":"Prepare docking"}])json");
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "engine")
		{
			controller.erase("fallback");
			controller["tree"] = "test.docking.policy";
		}
	// A claimed Console delegates this Controller to AI; other machinery remains human.
	auto &profile = ship["control_layout"]["profiles"][0]["consoles"]["drive"];
	auto &human = profile["human"];
	human.erase(std::remove(human.begin(), human.end(), "engine"), human.end());
	profile["ai"] = {"engine"};
	for (const auto &tree : Json::parse(R"json([
  {"id":"test.docking.action","context":"controller","docking_commands":true,
   "controller_action":{"label":"Admit the chosen docking approach","inputs":[
    {"name":"port","type":"text"},{"name":"remote","type":"text"},{"name":"revision","type":"integer"}]},
   "initial":"admit","states":[{"id":"admit","running":[
    {"action":"dock.assist","arguments":[{"observation":"input.port"},{"observation":"input.remote"},{"observation":"input.revision"}]},
    {"action":"action.succeed","arguments":["approach_admitted"]}]}]},
  {"id":"test.docking.policy","context":"controller","docking_observations":true,"docking_commands":true,
   "action_bindings":[{"id":"assist","tree":"test.docking.action"}],
   "objective_bindings":[{"id":"dock","behavior":"ready","candidate_source":"docks","docking_mode":"assist","action":"assist",
    "available":[{"left":{"observation":"docking_candidate.available"},"op":"eq","right":true},
                 {"left":{"observation":"tick"},"op":"ge","right":10}],
    "arguments":[{"observation":"docking_candidate.port"},{"observation":"docking_candidate.remote"},{"observation":"docking_candidate.revision"}],
    "factors":{"progress":1000}}],"initial":"watch","states":[{"id":"watch"}]}
 ])json"))
		source["trees"].push_back(tree);
	for (const bool missing : {false, true})
	{
		auto invalid = source;
		if (missing)
			invalid["trees"].back()["docking_observations"] = false;
		else
			invalid["trees"].back()["objective_bindings"][0]["docking_mode"] = "invent";
		bool refused = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Dock candidates require a declared feed contract and supported operation");
	}

	const auto connected = [](const Json &saved)
	{
		for (const auto &body : saved.at("bodies"))
			if (body.at("body").at("id") == 2 && body.contains("dock_connection"))
				return true;
		return false;
	};
	for (const auto position : {25000, 75000})
	{
		auto hidden = source;
		hidden["entities"][0]["systems"]["radar"]["range_mm"] = 1;
		hidden["maps"][0]["entities"][0]["x_mm"] = position;
		World unknown;
		unknown.load(load(hidden), "test.map", {{"crew", "test.away"}});
		require(unknown.claim(201, 2, "drive"), "Claim docking Console without positional target evidence");
		ticks(unknown, 12);
		require(!connected(unknown.save()), "Scenario docking permission cannot substitute for positional evidence");
	}
	{
		auto human_source = source;
		auto &human_profile = human_source["entities"][0]["control_layout"]["profiles"][0]["consoles"]["drive"];
		human_profile["human"].push_back("engine");
		human_profile["ai"] = Json::array();
		World operated;
		operated.load(load(human_source), "test.map", {{"crew", "test.away"}});
		require(operated.claim(202, 2, "drive"), "Claim human docking operations");
		ticks(operated, 12);
		require(!connected(operated.save()), "Observed compatible ports do not override human operation ownership");
	}

	{
		auto fed = source;
		fed["entities"][0]["stations"][1]["consoles"][0]["docking_feed"] = true;
		World reader;
		reader.load(load(fed), "test.map", {{"crew", "test.away"}});
		require(reader.claim(203, 2, "observer"), "Claim a declared docking status feed");
		ticks(reader, 5);
		const auto observed = *reader.observe(203);
		const auto &board = observed.gameplay.at("docking");
		require(board.at("complete") == true && board.at("ports").size() == 2,
		        "Declared feed exposes the complete own-ship docking state");
		OperatingCommand denied{2, "dock.assist", board.at("revision").get<std::int64_t>(), "aft"};
		denied.console = "observer";
		denied.controller = "engine";
		denied.generation = 1;
		denied.text = "1/aft";
		require(!reader.submit(203, denied), "A read-only docking feed transfers no operating authority");
	}
	const auto content = load(source);
	World world;
	world.load(content, "test.map", {{"crew", "test.away"}});
	require(world.claim(101, 2, "drive"), "Claim docking policy Console with delegated engine");
	ticks(world, 5);
	const auto observed = *world.observe(101);
	bool both = false;
	for (const auto &target : observed.gameplay.at("docking").at("targets"))
		if (target.at("entity") == 1)
			both |= target.at("local_ports").size() == 2;
	require(both, "Projection exposes every compatible owned local port");
	ticks(world, 8);
	require(world.claim(102, 2, "observer"), "Claim Console without a docking feed");
	const auto saved = world.save();
	bool admitted = false;
	for (const auto &body : saved.at("bodies"))
		if (body.at("body").at("id") == 2 && body.contains("dock_connection"))
			admitted = body.at("dock_connection").at("port") == 1;
	require(admitted, "Scored Controller admits the second compatible local port through ordinary docking");
	const auto docking = world.observe(101)->gameplay.at("docking");
	require(docking.at("complete") == true && docking.at("ports")[1].at("follower") == true &&
	            docking.at("ports")[1].at("assisted") == true && docking_projection::board(docking) == docking,
	        "Scoped follower and assistance evidence survives native/browser projection");
	const auto partial = world.observe(102)->gameplay.at("docking");
	require(partial.at("ports").empty() && partial.at("complete") == false,
	        "An unconnected Console cannot infer a complete docking view");
	World restored;
	restored.restore(content, saved);
	ticks(world, 20);
	ticks(restored, 20);
	require(world.save() == restored.save(), "Selected docking work restores deterministically");
}
