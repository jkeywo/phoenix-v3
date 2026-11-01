// Explicit accepted intent feeds coordinate independently admitted flight actions.
void destination_intent_cases()
{
	auto source = controller_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
	ship["stations"][0]["consoles"][0]["feeds"].push_back("steering");
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"first-course","behavior":"navigate","tier":"scripted_high","title_key":"First course","destination_x":10000,"destination_y":10000},
	 {"id":"second-course","behavior":"navigate","tier":"objective_other","title_key":"Second course","destination_x":-10000,"destination_y":20000}])json");
	ship["control_layout"] = Json::parse(R"json({"schema":1,"default_profile":"automatic","controllers":[
	 {"id":"planner","console":"observer","tree":"test.course-planner","commands":[
	  {"id":"course","action":"destination_intent","arguments":[0,0,0],"consumers":[]}]},
	 {"id":"propulsion","console":"drive","tree":"test.course-consumer","commands":[
	  {"id":"destination","action":"destination","arguments":[0,0],"consumers":["port","starboard"]}]},
	 {"id":"helm","console":"drive","tree":"test.course-consumer","commands":[
	  {"id":"destination","action":"destination","arguments":[0,0],"consumers":["steering"]}]}],
	 "profiles":[
	 {"id":"automatic","consoles":{"drive":{"ai":["propulsion","helm"]},"observer":{"ai":["planner"]}}},
	 {"id":"manual-plan","consoles":{"drive":{"ai":["propulsion","helm"]},"observer":{"human":["planner"]}}},
	 {"id":"manual-helm","consoles":{"drive":{"ai":["propulsion"],"human":["helm"]},"observer":{"human":["planner"]}}}
	 ]})json");
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.course-publish","context":"controller","command_bindings":[{"channel":"course","action":"destination_intent"}],
	  "controller_action":{"label":"Publish accepted destination intent","inputs":[{"name":"objective","type":"integer"},
	   {"name":"x","type":"integer"},{"name":"y","type":"integer"},{"name":"revision","type":"integer"}]},
	  "initial":"publish","states":[{"id":"publish","running":[
	   {"action":"write.course","arguments":[{"observation":"input.objective"},{"observation":"input.x"},{"observation":"input.y"},{"observation":"input.revision"}]}],
	   "exit":[{"action":"write.course","arguments":[0,0,0,0]}]}]},
	 {"id":"test.course-planner","context":"controller","command_bindings":[{"channel":"course","action":"destination_intent"}],
	  "action_bindings":[{"id":"publish","tree":"test.course-publish"}],
	  "objective_bindings":[{"id":"publish","behavior":"navigate","action":"publish",
	   "arguments":[{"observation":"objective.record.id"},{"observation":"objective.record.destination_x"},
	     {"observation":"objective.record.destination_y"},{"observation":"objective.record.revision"}],"factors":{"progress":1000}}],
	  "initial":"observe","states":[{"id":"observe"}]},
	 {"id":"test.course-execute","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"}],
	  "controller_action":{"label":"Advance the accepted shared course","inputs":[{"name":"x","type":"integer"},{"name":"y","type":"integer"}]},
	  "initial":"advance","states":[{"id":"advance","running":[
	   {"action":"write.destination","arguments":[true,{"observation":"input.x"},{"observation":"input.y"}]}],
	   "exit":[{"action":"write.destination","arguments":[false,0,0]}]}]},
	 {"id":"test.course-consumer","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"}],
	  "command_reads":[
	   {"name":"course.objective","console":"observer","controller":"planner","channel":"course"},
	   {"name":"course.x","console":"observer","controller":"planner","channel":"course","field":"x"},
	   {"name":"course.y","console":"observer","controller":"planner","channel":"course","field":"y"},
	   {"name":"course.objective_revision","console":"observer","controller":"planner","channel":"course","field":"objective_revision"},
	   {"name":"course.accepted","console":"observer","controller":"planner","channel":"course","field":"accepted"}],
	  "action_bindings":[{"id":"advance","tree":"test.course-execute"}],
	  "objective_bindings":[{"id":"advance","behavior":"navigate","action":"advance","available":[
	   {"left":{"observation":"course.accepted"},"op":"eq","right":true},
	   {"left":{"observation":"course.objective"},"op":"eq","right":{"observation":"objective.record.id"}},
	   {"left":{"observation":"course.objective_revision"},"op":"eq","right":{"observation":"objective.record.revision"}}],
	   "arguments":[{"observation":"course.x"},{"observation":"course.y"}],"factors":{"progress":1000}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "claim shared-course Consoles");
	world.tick();
	require(controller(world, 102, "planner").at("commands").at("course").at("value") > 0 &&
	            controller(world, 101, "propulsion").at("commands").at("destination").at("value") == 0 &&
	            controller(world, 101, "helm").at("commands").at("destination").at("value") == 0,
	        "a newly written accepted intent becomes a peer decision input on the next tick");
	world.tick();
	const auto matching = [&](const World &value, Json point)
	{
		for (const auto *id : {"propulsion", "helm"})
		{
			const auto command = controller(value, 101, id).at("commands").at("destination");
			if (command.at("value") != 1 || command.at("arguments") != point || !command.at("accepted").get<bool>())
				return false;
		}
		return true;
	};
	require(matching(world, Json::array({10000, 10000})),
	        "Helm and propulsion execute the same accepted destination through their own admission");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 10; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save() && (world.observe(101)->own.x != 0 || world.observe(101)->own.y != 0),
	        "shared-course physical navigation advances and resumes identically");
	const auto observed = *world.observe(102);
	KnowledgeId second = 0;
	for (const auto &record : observed.gameplay.at("shared"))
		if (record.at("schema") == "phoenix.objective" && record.at("fields").value("title", "") == "Second course")
			second = record.at("subject");
	require(second > 0 && fixture_native_submit(world, 102, {1, "preset", 1}),
	        "hand shared-course intent to its human owner");
	world.tick();
	const auto publish = [&](KnowledgeTick revision)
	{
		OperatingCommand command{1, {}, static_cast<std::int64_t>(second)};
		command.controller = "planner";
		command.channel = "course";
		command.generation = controller(world, 102, "planner").at("generation");
		command.arguments = {-10000, 20000, static_cast<std::int64_t>(revision)};
		require(world.submit(102, command), "admit an owned shared destination intent");
		world.tick();
		world.tick();
	};
	publish(999);
	require(controller(world, 101, "propulsion").at("commands").at("destination").at("value") == 0 &&
	            controller(world, 101, "helm").at("commands").at("destination").at("value") == 0,
	        "an intent for another objective revision cannot advance the current objective");
	publish(1);
	require(matching(world, Json::array({-10000, 20000})),
	        "a valid replacement intent coordinates both flight Controllers");
	OperatingCommand stolen{1, {}, 1};
	stolen.controller = "propulsion";
	stolen.channel = "destination";
	stolen.generation = controller(world, 101, "propulsion").at("generation");
	stolen.arguments = {1, 2};
	require(!world.submit(102, stolen), "an accepted intent feed transfers no actuator authority");
	require(fixture_native_submit(world, 101, {1, "preset", 2}), "hand Helm destinations to its human operator");
	world.tick();
	OperatingCommand manual{1, {}, 1};
	manual.controller = "helm";
	manual.channel = "destination";
	manual.generation = controller(world, 101, "helm").at("generation");
	manual.arguments = {25000, 0};
	require(world.submit(101, manual), "admit an explicit human destination");
	for (int i = 0; i < 4; ++i)
		world.tick();
	require(controller(world, 101, "helm").at("commands").at("destination").at("arguments") == manual.arguments,
	        "shared intent cannot overwrite a human-owned flight command");
	for (const auto field : {"unknown_argument", "wrong_type", "undeclared_console"})
	{
		auto invalid = source;
		for (auto &tree : invalid["trees"])
			if (tree["id"] == "test.course-consumer")
			{
				if (std::string(field) == "unknown_argument")
					tree["command_reads"][1]["field"] = "hidden_position";
				if (std::string(field) == "wrong_type")
					tree["command_reads"][1]["type"] = "boolean";
				if (std::string(field) == "undeclared_console")
					tree["command_reads"][1].erase("console");
			}
		bool refused = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "invalid or undeclared accepted-command feeds fail production verification");
	}
}
