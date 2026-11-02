// Synthetic objectives exercise real publication, command admission and Controller reads.
Json objective_priority_fixture()
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
	for (const auto &tree : Json::parse(R"json([
 {"id":"test.priority-advance","context":"controller","command_bindings":[{"channel":"speed"}],
 "controller_action":{"label":"Advance the selected goal","inputs":[{"name":"speed","type":"integer"}]},
 "initial":"write","states":[{"id":"write","running":[{"action":"write.speed","arguments":[{"observation":"input.speed"}]},{"action":"action.succeed","arguments":["admitted"]}]}]},
 {"id":"test.priority-reader","context":"controller","command_bindings":[{"channel":"speed"}],
 "action_bindings":[{"id":"advance","tree":"test.priority-advance"}],
 "objective_bindings":[{"id":"navigate","behavior":"navigate","action":"advance","arguments":[{"observation":"objective.record.destination_x"}]}],
 "initial":"observe","states":[{"id":"observe"}]}])json"))
		source["trees"].push_back(tree);
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
	materialise_fixture_widgets(source);
	for (auto &objective : source["maps"][0]["scenario"]["objectives"])
		if (objective["id"] == "second")
			objective["urgency"] = 10;
	return source;
}
void objective_binding_cases()
{
	const auto fixture = []
	{
		auto source = objective_priority_fixture();
		for (auto &tree : source["trees"])
			if (tree["id"] == "test.priority-reader")
				tree = Json::parse(R"json({"id":"test.priority-reader","context":"controller",
 "command_bindings":[{"channel":"speed"}],
 "action_bindings":[{"id":"advance","tree":"test.objective-executor"}],
 "objective_bindings":[{"behavior":"navigate","action":"advance",
   "available":[{"left":{"observation":"objective.record.cruise_speed"},"op":"le","right":0}],
   "arguments":[{"observation":"objective.record.destination_x"}]}],
 "initial":"observe","states":[{"id":"observe"}]})json");
		source["trees"].push_back(Json::parse(R"json({"id":"test.objective-executor","context":"controller",
 "controller_action":{"label":"Advance disclosed objective","inputs":[{"name":"objective_speed","type":"integer"}]},
 "command_bindings":[{"channel":"speed"}],"initial":"advance",
 "states":[{"id":"advance","running":[{"action":"write.speed","arguments":[{"observation":"input.objective_speed"}]}]}]})json"));
		return source;
	};
	for (const auto violation : {"duplicate", "selector", "producer"})
	{
		auto malformed = fixture();
		std::string expected;
		if (std::string(violation) == "producer")
		{
			malformed["entities"][0]["stations"][0]["consoles"][0]["knowledge_producers"] = {"phoenix.standing"};
			expected = "reserved objective producer";
		}
		else
			for (auto &tree : malformed["trees"])
				if (tree["id"] == "test.priority-reader")
				{
					if (std::string(violation) == "duplicate")
					{
						tree["objective_bindings"].push_back(tree["objective_bindings"][0]);
						expected = "duplicate objective";
					}
					else
					{
						tree["states"][0]["running"] = Json::parse(
						    R"json([{"attempt":[{"tasks":[{"action":"select.advance","arguments":["bypass",1000]}]}]}])json");
						expected = "Objective policies";
					}
				}
		bool refused = false;
		try
		{
			(void)load(malformed);
		}
		catch (const std::exception &error)
		{
			refused = std::string(error.what()).find(expected) != std::string::npos;
		}
		require(refused,
		        "Production verification refuses duplicate candidates, nested selectors and reserved producer grants");
	}
	for (const auto mode : {"tier", "reaction", "infeasible", "tie", "blocked", "provider", "provider-large"})
	{
		auto source = fixture();
		for (auto &objective : source["maps"][0]["scenario"]["objectives"])
		{
			objective["urgency"] = std::string(mode) == "tie" ? 0 : objective.value("urgency", 0);
			objective["tier"] = objective["id"] == "first" ? (std::string(mode) == "reaction" ? "reaction"
			                                                  : std::string(mode) == "tie"    ? "objective_other"
			                                                                                  : "scripted_high")
			                                               : "objective_other";
			if (std::string(mode) == "infeasible" && objective["id"] == "first")
				objective["scoring_policy"] = "unavailable";
		}
		if (std::string(mode) == "blocked")
			for (auto &tree : source["trees"])
			{
				if (tree["id"] == "test.priority-reader")
					tree["objective_bindings"][0]["retry_ticks"] = 20;
				if (tree["id"] == "test.objective-executor")
					// Inject a refusal through real command admission, without changing
					// any production tuning or relying on a shipped ship's bounds.
					tree["states"][0]["running"] = Json::parse(R"json([{"attempt":[
 {"conditions":[{"left":{"observation":"input.objective_speed"},"op":"eq","right":1000}],
  "tasks":[{"attempt":[
    {"tasks":[{"action":"write.speed","arguments":[9223372036854775807]},
              {"action":"action.succeed","arguments":["unexpected_admission"]}]},
    {"tasks":[{"action":"action.fail","arguments":["admission_refused"]}]}]}]},
 {"tasks":[{"action":"write.speed","arguments":[{"observation":"input.objective_speed"}]}]}
]}])json");
			}
		if (std::string(mode) == "provider-large")
		{
			source["maps"][0]["scenario"]["objectives"][2]["recipients"].erase("consoles");
			for (auto &tree : source["trees"])
				if (tree["id"] == "test.priority-reader")
				{
					const auto binding = tree["objective_bindings"][0];
					tree["objective_bindings"] = Json::array();
					for (unsigned i = 0; i < 64; ++i)
					{
						auto variant = binding;
						variant["id"] = "variant-" + std::to_string(i);
						tree["objective_bindings"].push_back(std::move(variant));
					}
				}
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.prepare_console_profile({1, "drive"}, "automatic") && world.claim(301, 1, "drive") &&
		            world.claim(302, 1, "observer"),
		        "prepare scored binding fixture");
		for (int i = 0; i < 6; ++i)
			world.tick();
		const auto speed = [&]
		{
			return controller(world, 301, "starboard_drive")["commands"]["speed"]["value"].get<std::int64_t>();
		};
		require(speed() == (std::string(mode) == "infeasible" || std::string(mode) == "blocked" ? 2000 : 1000),
		        "Compiled bindings choose feasible tiers before utility and use stable identity ties");
		if (std::string(mode).starts_with("provider"))
		{
			const ControllerAddress address{1, "drive", "starboard_drive"};
			const auto before_provider = world.controller_decision_snapshot(address).at("basis");
			require(world.configure_controller_provider(address, "anyjev"),
			        "Scored Controllers expose permitted decisions to model providers");
			const auto snapshot = world.controller_decision_snapshot(address);
			require(snapshot.at("basis").at("action_serial") == before_provider.at("action_serial") &&
			            snapshot.at("basis").at("action_status") == before_provider.at("action_status"),
			        "Provider changes preserve the scored action continuation and physical work");
			require(!snapshot.is_null() &&
			            snapshot.at("objective_candidates").size() ==
			                (std::string(mode) == "provider-large" ? 16 : 2) &&
			            snapshot.at("selected_objective") != 0 && snapshot.at("candidates").size() == 2,
			        "Provider snapshots retain per-Controller score reasons without an alternate action selector");
			if (std::string(mode) == "provider-large")
			{
				const auto diagnostic = world.controller_decision_snapshot(address, true);
				require(diagnostic.dump().size() > 48000 && snapshot.dump().size() <= 48000 &&
				            diagnostic.at("objective_candidates").size() == 192 &&
				            snapshot.at("omitted_objective_candidates") == 176 &&
				            diagnostic.at("evidence") == snapshot.at("evidence") &&
				            diagnostic.at("facts") == snapshot.at("facts") &&
				            std::any_of(snapshot.at("objective_candidates").begin(),
				                        snapshot.at("objective_candidates").end(),
				                        [](const auto &row)
				                        {
					                        return row.value("selected", false);
				                        }),
				        "Bounded provider diagnostics retain the selected reason and intact evidence; tools can "
				        "inspect every row");
			}
			require(!world.submit_controller_decision({{"snapshot", snapshot}, {"candidate", "advance"}}),
			        "Provider cannot inject an action outside scored arbitration");
			require(world.submit_controller_decision({{"snapshot", snapshot}, {"candidate", "continue"}}),
			        "Provider can acknowledge continuation of scored work");
		}
		const auto board = world.observe(302)->gameplay.at("objective_board");
		const auto mark = [&](std::int64_t objective)
		{
			OperatingCommand command{1, "objective.mark", objective};
			command.controller = "bridge";
			command.generation = controller(world, 302, "bridge").at("generation");
			command.arguments = {world.observe(302)->gameplay.at("objective_board").at("revision").get<std::int64_t>()};
			require(world.submit(302, command), "mark an objective for shared action arbitration");
			world.tick();
			world.tick();
		};
		mark(board.at("objectives")[1].at("id"));
		require(speed() == (std::string(mode) == "reaction" ? 1000 : 2000),
		        "Reaction precedes Captain marks while marks preempt remaining feasible actions");
		mark(board.at("objectives")[0].at("id"));
		require(speed() ==
		            (std::string(mode) == "infeasible" || std::string(mode) == "tie" || std::string(mode) == "blocked"
		                 ? 2000
		                 : 1000),
		        "Multiple marks retain tier order, same-tier commitment and infeasible fallthrough");
		const auto diagnostics = controller(world, 301, "starboard_drive").at("objective_candidates");
		require(diagnostics.size() == (std::string(mode) == "provider-large" ? 192 : 2) &&
		            std::count_if(diagnostics.begin(), diagnostics.end(),
		                          [](const auto &candidate)
		                          {
			                          return candidate.at("selected").template get<bool>();
		                          }) == 1,
		        "Objective diagnostics contain only shipwide candidates and explain one selection");
		if (std::string(mode) == "infeasible")
			require(std::any_of(diagnostics.begin(), diagnostics.end(),
			                    [](const auto &candidate)
			                    {
				                    return candidate.at("reason") == "unsupported_scoring_policy" &&
				                           !candidate.at("feasible").template get<bool>();
			                    }),
			        "Unsupported scoring policy is explicitly infeasible without ending its objective");
		if (std::string(mode) == "blocked")
			require(std::any_of(diagnostics.begin(), diagnostics.end(),
			                    [](const auto &candidate)
			                    {
				                    return candidate.at("reason") == "execution_backoff" &&
				                           candidate.at("attempt") == 1;
			                    }),
			        "Refused high-priority work exposes its private retry receipt while useful work proceeds");
		World resumed;
		resumed.restore(catalog, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(),
		        "Selected action, commitment identity and continuation survive checkpoints");
		if (std::string(mode) == "blocked")
		{
			for (int i = 0; i < 25; ++i)
			{
				world.tick();
				resumed.tick();
			}
			require(world.save() == resumed.save() && speed() == 2000,
			        "Retry expiry allocates a new attempt without monopolising or losing checkpoint continuation");
			const auto saved = world.save();
			for (const auto &owner : saved.at("bodies")[0].at("console_controllers").at("controllers"))
				if (owner.at("id") == "starboard_drive")
					require(owner.at("objective_retries")[0].at("attempt").template get<std::uint64_t>() >= 2,
					        "An expired failed execution retries through a distinct action invocation");
			require(world.submit(301, {1, "preset", 0}), "handover the failed execution's authority to a human");
			world.tick();
			const auto handover = world.save();
			for (const auto &owner : handover.at("bodies")[0].at("console_controllers").at("controllers"))
				if (owner.at("id") == "starboard_drive")
					require(!owner.contains("objective_retries"),
					        "Authority generation invalidates old retry receipts");
		}
	}
}
void objective_priority_cases()
{
	objective_binding_cases();
	auto source = objective_priority_fixture();
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
	choose.controller = "bridge";
	choose.generation = controller(world, 102, "bridge").at("generation");
	choose.arguments = {board.at("revision").get<std::int64_t>()};
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
	hidden.controller = "bridge";
	hidden.generation = controller(world, 102, "bridge").at("generation");
	hidden.arguments = {world.observe(102)->gameplay.at("objective_board").at("revision").get<std::int64_t>()};
	require(!world.submit(102, hidden), "private objective cannot become ship priority");
	const auto priority = [&](std::int64_t id)
	{
		OperatingCommand request{1, "objective.priority", id};
		request.controller = "bridge";
		request.generation = controller(world, 102, "bridge").at("generation");
		request.arguments = {world.observe(102)->gameplay.at("objective_board").at("revision").get<std::int64_t>()};
		require(world.submit(102, request), "admit current priority choice");
		world.tick();
	};
	priority(board.at("objectives")[1].at("id"));
	require(!world.submit(102, choose), "stale priority selection cannot overwrite a newer choice");
	priority(0);
	require(world.observe(102)->gameplay.at("objective_board").at("selected").empty(), "clear selected priority");
	priority(first);
	const auto second = board.at("objectives")[1].at("id").get<std::int64_t>();
	const auto edit_priority = [&](const std::string &action, std::int64_t id)
	{
		OperatingCommand request{1, action, id};
		request.controller = "bridge";
		request.generation = controller(world, 102, "bridge").at("generation");
		request.arguments = {world.observe(102)->gameplay.at("objective_board").at("revision").get<std::int64_t>()};
		require(world.submit(102, request), "explicit multi-priority operation admitted");
		world.tick();
	};
	edit_priority("objective.mark", second);
	require(world.observe(102)->gameplay.at("objective_board").at("selected").size() == 2,
	        "Captain may mark multiple objectives");
	edit_priority("objective.mark", second);
	require(world.observe(102)->gameplay.at("objective_board").at("selected").size() == 2,
	        "marking an existing priority is idempotent");
	edit_priority("objective.unmark", second);
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "priority and AI reads continue identically");
	require(world.submit(101, {1, "preset", 0}), "take full manual control");
	world.tick();
	require(!world.observe(102)->gameplay.at("objective_board").at("can_select").get<bool>(),
	        "no AI disables priority edits");
	require(world.observe(102)->gameplay.at("objective_board").at("selected") == Json::array({first}),
	        "full manual control retains the readable priority");
	require(!world.submit(102, {1, "preset", 0}), "fixed workload cannot be elevated live");
	for (int i = 0; i < 45; ++i)
		world.tick();
	require(world.observe(102)->gameplay.at("objective_board").at("selected").empty(),
	        "cancelled objective clears priority without selecting a replacement");
	// Ending the newer diversion must resume the existing older destination.
	// No replacement objective or scripted navigation order is published.
	source["maps"][0]["scenario"]["bindings"]["objective"] = "second";
	const auto diversion_catalog = load(source);
	World interrupted;
	interrupted.load(diversion_catalog, "test.map");
	require(interrupted.prepare_console_profile({1, "drive"}, "automatic") && interrupted.claim(201, 1, "drive"),
	        "prepare interrupted navigation");
	for (int i = 0; i < 6; ++i)
		interrupted.tick();
	require(controller(interrupted, 201, "starboard_drive")["commands"]["speed"]["value"] == 2000,
	        "newer active destination temporarily interrupts the original objective");
	World diversion_resumed;
	diversion_resumed.restore(diversion_catalog, interrupted.save());
	for (int i = 0; i < 45; ++i)
	{
		interrupted.tick();
		diversion_resumed.tick();
	}
	require(interrupted.save() == diversion_resumed.save(), "interrupted objective selection resumes exactly");
	require(controller(interrupted, 201, "starboard_drive")["commands"]["speed"]["value"] == 1000,
	        "terminal diversion resumes the original unfinished objective without a new instruction");
}

void navigation_waypoint_chain_cases()
{
	auto source = objective_priority_fixture();
	auto &ship = source["entities"][0];
	ship["stations"][0]["consoles"].push_back(
	    {{"id", "navigation"}, {"definition", "test.console"}, {"systems", Json::array()}});
	source["control_widgets"].push_back(Json::parse(R"json(
 {"id":"test.navigation-widget","schema":1,"equipment":{},"tree":"test.waypoint-planner",
  "commands":[{"id":"waypoint","action":"waypoint","initial":0,"arguments":[0,0],"consumers":[]}],
  "parts":["main"],"data":{},"levels":[{"id":"automated","human":[]},{"id":"manual","human":["waypoint"]}]})json"));
	ship["control_layout"]["controllers"].push_back(Json::parse(R"json(
 {"id":"planner","console":"navigation","widget":"test.navigation-widget","equipment":{},
  "star_levels":{"1":"manual","2":"manual","3":"manual"}})json"));
	for (auto &objective : source["maps"][0]["scenario"]["objectives"])
		if (objective["id"] == "first")
			objective["tier"] = "scripted_high";
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.priority-reader")
		{
			tree["record_bindings"] = Json::parse(R"json([
 {"name":"waypoint.active","scope":"ship","schema":"phoenix.waypoint","field":"active","subject":"latest","type":"boolean"},
 {"name":"waypoint.x","scope":"ship","schema":"phoenix.waypoint","field":"x","subject":"latest","type":"integer"},
 {"name":"waypoint.y","scope":"ship","schema":"phoenix.waypoint","field":"y","subject":"latest","type":"integer"}])json");
			auto &binding = tree["objective_bindings"][0];
			binding["available"] = Json::parse(R"json([
 {"left":{"observation":"waypoint.active"},"op":"eq","right":true},
 {"left":{"observation":"waypoint.x"},"op":"eq","right":{"observation":"objective.record.destination_x"}},
 {"left":{"observation":"waypoint.y"},"op":"eq","right":{"observation":"objective.record.destination_y"}}])json");
			binding["arguments"] = Json::array({{{"observation", "waypoint.x"}}});
		}
	for (const auto &tree : Json::parse(R"json([
 {"id":"test.waypoint-action","context":"controller","command_bindings":[{"channel":"waypoint","action":"waypoint","type":"boolean"}],
  "controller_action":{"label":"Plot selected objective","inputs":[{"name":"x","type":"integer"},{"name":"y","type":"integer"}]},
  "initial":"plot","states":[{"id":"plot","running":[
   {"action":"write.waypoint","arguments":[true,{"observation":"input.x"},{"observation":"input.y"}]},
   {"action":"action.succeed","arguments":["plotted"]}]}]},
 {"id":"test.waypoint-planner","context":"controller","command_bindings":[{"channel":"waypoint","action":"waypoint","type":"boolean"}],
  "action_bindings":[{"id":"plot","tree":"test.waypoint-action"}],
  "objective_bindings":[{"id":"navigate","behavior":"navigate","candidate_source":"single",
   "navigation":{"mode":"destination","distance":0},"action":"plot",
   "arguments":[{"observation":"navigation.x"},{"observation":"navigation.y"}],"factors":{"progress":1000}}],
  "initial":"observe","states":[{"id":"observe"}]}
 ])json"))
		source["trees"].push_back(tree);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.prepare_console_profile({1, "drive"}, "automatic") && world.claim(401, 1, "drive"),
	        "Admit independent Helm and Navigation Controllers");
	bool published = false;
	for (int i = 0; i < 8 && !published; ++i)
	{
		world.tick();
		const auto checkpoint = world.save();
		for (const auto &owner : checkpoint.at("bodies")[0].at("console_controllers").at("controllers"))
			if (owner.at("id") == "planner")
				published = owner.at("commands")[0].at("value") == 1;
		require(controller(world, 401, "starboard_drive")["commands"]["speed"]["value"] == 0,
		        "Helm cannot consume a Navigation waypoint during its publishing evaluation");
	}
	require(published, "Navigation publishes the selected objective through the ordinary ship waypoint producer");
	for (int i = 0; i < 4; ++i)
		world.tick();
	bool shared = false;
	const auto observed = *world.observe(401);
	for (const auto &record : observed.gameplay.at("shared"))
		shared |= record.at("schema") == "phoenix.waypoint" && record.at("fields").at("x") == 1000;
	require(controller(world, 401, "starboard_drive")["commands"]["speed"]["value"] == 1000,
	        "Helm follows the higher-priority objective's shared waypoint without radar evidence");
	require(shared, "The accepted waypoint reaches Helm through the shared ship blackboard");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 45; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(controller(world, 401, "starboard_drive")["commands"]["speed"]["value"] == 2000 &&
	            world.save() == resumed.save(),
	        "Navigation and Helm resume unfinished lower-priority travel after interruption, including restore");
}

void scored_objective_cases()
{
	const std::vector<std::string> tiers{"reaction",     "scripted_high",   "objective_combat", "directive_combat",
	                                     "scripted_low", "objective_other", "directive_other"};
	for (std::size_t tier = 0; tier + 1 < tiers.size(); ++tier)
	{
		auto source = objective_priority_fixture();
		for (auto &objective : source["maps"][0]["scenario"]["objectives"])
		{
			if (objective["id"] == "first")
			{
				objective["tier"] = tiers[tier];
				objective["urgency"] = 0;
			}
			if (objective["id"] == "second")
			{
				objective["tier"] = tiers[tier + 1];
				objective["urgency"] = 1000000;
			}
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.prepare_console_profile({1, "drive"}, "automatic") && world.claim(301, 1, "drive") &&
		            world.claim(302, 1, "observer"),
		        "scored fixture admitted");
		for (int n = 0; n < 6; ++n)
			world.tick();
		require(controller(world, 301, "starboard_drive")["commands"]["speed"]["value"] == 1000,
		        "tier beats an arbitrarily larger score");
		const auto board = world.observe(302)->gameplay.at("objective_board");
		OperatingCommand mark{1, "objective.mark", board.at("objectives")[1].at("id")};
		mark.controller = "bridge";
		mark.generation = controller(world, 302, "bridge").at("generation");
		mark.arguments = {board.at("revision").get<std::int64_t>()};
		require(world.submit(302, mark), "Captain marks a lower-tier objective");
		world.tick();
		world.tick();
		require(controller(world, 301, "starboard_drive")["commands"]["speed"]["value"] == (tier == 0 ? 1000 : 2000),
		        "Reaction precedes marks; marks precede other tiers");
		require(controller(world, 301, "starboard_drive").at("objective_candidates").size() == 2,
		        "permitted score diagnostics accompany decisions");
		World restored;
		restored.restore(catalog, world.save());
		world.tick();
		restored.tick();
		require(world.save() == restored.save(), "scored choices and priorities resume deterministically");
	}
}

void ai_priority_snapshot_cases()
{
	auto source = objective_priority_fixture();
	auto &controllers = source["entities"][0]["control_layout"]["controllers"];
	Json captain;
	for (auto at = controllers.begin(); at != controllers.end(); ++at)
		if (at->at("id") == "bridge")
		{
			captain = *at;
			controllers.erase(at);
			break;
		}
	require(!captain.is_null(), "synthetic Captain widget exists");
	controllers.insert(controllers.begin(), captain);
	for (auto &widget : source["control_widgets"])
		if (widget.at("id") == captain.at("widget"))
		{
			widget.erase("policy");
			widget.erase("fallback");
			widget["tree"] = "test.ai-captain";
		}
	for (const auto &tree : Json::parse(R"json([
 {"id":"test.ai-captain.mark","context":"controller","controller_action":{"label":"Mark an unmarked objective","inputs":[{"name":"objective","type":"integer"},{"name":"revision","type":"integer"}]},
 "initial":"mark","states":[{"id":"mark","running":[{"action":"objective.mark","arguments":["",{"observation":"input.objective"},{"observation":"input.revision"}]},{"action":"action.succeed","arguments":["mark_admitted"]}]}]},
 {"id":"test.ai-captain","context":"controller","objective_scoring":{"default":{"urgency":0}},
 "action_bindings":[{"id":"mark","tree":"test.ai-captain.mark"}],
 "objective_bindings":[{"id":"mark","behavior":"navigate","action":"mark","available":[{"left":{"observation":"objective.record.marked"},"op":"eq","right":false}],
 "arguments":[{"observation":"objective.record.id"},{"observation":"objective.priority_revision"}]}],
 "initial":"observe","states":[{"id":"observe"}]}])json"))
		source["trees"].push_back(tree);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.prepare_console_profile({1, "drive"}, "automatic") && world.claim(401, 1, "drive") &&
	            world.claim(402, 1, "observer") && world.submit(402, {1, "away", 1}),
	        "AI Captain uses the ordinary Away and Controller authority paths");
	for (int tick = 0; tick < 6; ++tick)
	{
		const auto board = world.observe(402)->gameplay.at("objective_board");
		const auto previous = board.at("selected");
		const auto expected =
		    previous.size() == 1 && !board.at("objectives").empty() && previous[0] == board.at("objectives")[0].at("id")
		        ? 1000
		        : 2000;
		world.tick();
		require(controller(world, 401, "starboard_drive")["commands"]["speed"]["value"] == expected,
		        "later Controllers see the captured marks, never same-tick Captain writes");
	}
	require(world.observe(402)->gameplay.at("objective_board").at("selected").size() == 2,
	        "AI Captain can mark multiple objectives using the observed board revision");
}
