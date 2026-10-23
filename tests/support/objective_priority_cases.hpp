// Synthetic objectives exercise real publication, command admission and Controller reads.
void objective_priority_cases()
{
	auto source = controller_fixture(true);
	source["consoles"].push_back({{"id", "test.captain"}, {"objectives", true}, {"widgets", Json::array()}});
	auto &ship = source["entities"][0];
	ship["stations"][0]["consoles"][1]["definition"] = "test.captain";
	auto &layout = ship["control_layout"];
	layout["profiles"][0]["stars"] = 3;
	layout["profiles"][1]["stars"] = 1;
	layout["fixed_profiles"] = {{"observer", "automatic"}};
	layout["profiles"][1]["consoles"]["drive"] = {{"human", {"port_drive"}}, {"ai", {"starboard_drive"}}};
	layout["controllers"][1].erase("fallback");
	layout["controllers"][1]["tree"] = "test.priority-reader";
	source["trees"].push_back(Json::parse(R"json({"id":"test.priority-reader","context":"controller",
 "record_bindings":[{"name":"goal","schema":"phoenix.objective","field":"destination_x","scope":"ship","subject":"priority_or_latest"}],
 "command_bindings":[{"channel":"speed"}],"initial":"read",
 "states":[{"id":"read","running":[{"action":"write.speed","arguments":[{"observation":"goal"}]}]}]})json"));
	auto &map = source["maps"][0];
	map["regions"] = Json::parse(R"json([
 {"id":"first","shape":"circle","x_mm":1000,"y_mm":100000,"radius_mm":10},
 {"id":"second","shape":"circle","x_mm":2000,"y_mm":100000,"radius_mm":10}])json");
	map["scenario"] = {{"tree", "test.priority-scenario"},
	                   {"bindings", {{"actor", {{"entity", 1}}}, {"objective", "first"}}},
	                   {"objectives", Json::array()}};
	for (const auto *name : {"first", "second", "secret"})
	{
		Json recipients{{"ship", {{"entity", 1}}}};
		if (std::string(name) == "secret")
			recipients["consoles"] = {"drive"};
		map["scenario"]["objectives"].push_back({{"id", name},
		                                         {"type", "reach_region"},
		                                         {"region", std::string(name) == "secret" ? "second" : name},
		                                         {"actor", {{"entity", 1}}},
		                                         {"recipients", recipients},
		                                         {"title_key", name}});
	}
	source["trees"].push_back(
	    Json::parse(R"json({"id":"test.priority-scenario","context":"scenario","initial":"publish",
 "states":[{"id":"publish","entry":[
 {"action":"add_objective","arguments":["first",{"observation":"self.entity"},"first"]},
 {"action":"add_objective","arguments":["second",{"observation":"self.entity"},"second"]},
 {"action":"add_objective","arguments":["secret",{"observation":"self.entity"},"secret"]}],
 "transitions":[{"target":"cancel","conditions":[{"left":{"observation":"tick"},"op":"ge","right":40}]}]},
 {"id":"cancel","entry":[{"action":"set_objective_status","arguments":[{"observation":"objective.id"},4]}]}]})json"));
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(!world.prepare_console_profile({1, "observer"}, "direct"),
	        "fixed Console rejects another initial workload");
	require(world.prepare_console_profile({1, "drive"}, "automatic"), "prepare partially assisted Console");
	require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "claim objective fixture");
	for (int i = 0; i < 6; ++i)
		world.tick();
	const auto board = world.observe(102)->gameplay.at("objective_board");
	require(network::bounded_gameplay(world.observe(102)->gameplay).at("objective_board") == board,
	        "network projection preserves objective list and priority authority");
	require(board.at("objectives").size() == 2 && board.at("can_select") == true,
	        "Captain sees only ship publications and partial AI enables priority");
	const auto first = board.at("objectives")[0].at("id").get<std::int64_t>();
	OperatingCommand choose{1, "objective.priority", first};
	choose.generation = board.at("revision");
	require(!world.submit(101, choose) && world.submit(102, choose), "only Objectives Console may select priority");
	world.tick();
	world.tick();
	require(controller(world, 101, "starboard_drive")["commands"]["speed"]["value"] == 1000,
	        "objective reader prefers selected destination over latest publication");
	std::int64_t secret = 0;
	const auto snapshot = world.save();
	for (const auto &objective : snapshot.at("scenario").at("runtime").at("objectives"))
		if (objective.at("definition") == "secret")
			secret = objective.at("id");
	require(secret != 0, "private scenario objective exists");
	OperatingCommand hidden{1, "objective.priority", secret};
	hidden.generation = world.observe(102)->gameplay.at("objective_board").at("revision");
	require(!world.submit(102, hidden), "private objective cannot become ship priority");
	const auto priority = [&](std::int64_t id)
	{
		OperatingCommand request{1, "objective.priority", id};
		request.generation = world.observe(102)->gameplay.at("objective_board").at("revision");
		require(world.submit(102, request), "admit current priority choice");
		world.tick();
	};
	priority(board.at("objectives")[1].at("id"));
	require(!world.submit(102, choose), "stale priority selection cannot overwrite a newer choice");
	priority(0);
	require(world.observe(102)->gameplay.at("objective_board").at("selected") == 0, "clear selected priority");
	priority(first);
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "priority and AI reads continue identically");
	require(world.submit(101, {1, "preset", 0}), "take full manual control");
	world.tick();
	require(!world.observe(102)->gameplay.at("objective_board").at("can_select").get<bool>(),
	        "no AI disables priority edits");
	require(world.observe(102)->gameplay.at("objective_board").at("selected") == first,
	        "full manual control retains the readable priority");
	require(!world.submit(102, {1, "preset", 0}), "fixed workload cannot be elevated live");
	for (int i = 0; i < 45; ++i)
		world.tick();
	require(world.observe(102)->gameplay.at("objective_board").at("selected") == 0,
	        "cancelled objective clears priority without selecting a replacement");
}
