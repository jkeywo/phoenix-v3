// Synthetic objective scope, current targeting and bounded retained-location decisions.
void contact_candidate_contract()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["radar"]["capabilities"] = {{"basic_identity", 0}};
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"specific","behavior":"engage","tier":"objective_combat","target":2,"title_key":"Specified hostile"},
	 {"id":"known","behavior":"engage","tier":"directive_combat","title_key":"Known hostiles"}])json");
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "gun")
		{
			controller.erase("fallback");
			controller["tree"] = "test.contacts";
			for (const auto *id : {"retained_target", "retained_x"})
				controller["commands"].push_back({{"id", id},
				                                  {"action", "intent_integer"},
				                                  {"minimum", 0},
				                                  {"maximum", 1000000000},
				                                  {"consumers", Json::array()}});
		}
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"] = {"gun"};
	auto &human = ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"];
	human.erase(std::remove(human.begin(), human.end(), "gun"), human.end());
	source["maps"][0]["entities"][1]["x_mm"] = 90000;
	source["maps"][0]["entities"][2]["y_mm"] = 40000;
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.contact.aim","context":"controller","command_bindings":[{"channel":"target"}],
	  "controller_action":{"label":"Aim at observed hostile","inputs":[{"name":"contact","type":"integer"}]},
	  "initial":"act","states":[{"id":"act","running":[{"attempt":[{"tasks":[
	   {"action":"write.target","arguments":[{"observation":"input.contact"}]},
	   {"action":"action.succeed","arguments":["aim_admitted"]}]},
	   {"tasks":[{"action":"action.fail","arguments":["aim_refused"]}]}]}]}]},
	 {"id":"test.contact.retain","context":"controller","command_bindings":[{"channel":"retained_target"},{"channel":"retained_x"}],
	  "controller_action":{"label":"Retain bounded pursuit intent","inputs":[{"name":"contact","type":"integer"},{"name":"x","type":"integer"}]},
	  "initial":"act","states":[{"id":"act","running":[
	   {"action":"write.retained_target","arguments":[{"observation":"input.contact"}]},
	   {"action":"write.retained_x","arguments":[{"observation":"input.x"}]},
	   {"action":"action.succeed","arguments":["intent_admitted"]}]}]},
	 {"id":"test.contacts","context":"controller","contact_observations":true,
	  "command_bindings":[{"channel":"target"},{"channel":"retained_target"},{"channel":"retained_x"}],
	  "action_bindings":[{"id":"aim","tree":"test.contact.aim"},{"id":"retain","tree":"test.contact.retain"}],
	  "objective_bindings":[
	   {"id":"aim","behavior":"engage","candidate_source":"contacts","action":"aim",
	    "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true}],
	    "target":{"observation":"contact_candidate.id"},"arguments":[{"observation":"contact_candidate.id"}],
	    "commitment":{"observation":"contact_candidate.continuing"},
	    "factors":{"progress":1000,"travel":{"observation":"contact_candidate.distance"}}},
	   {"id":"retain","behavior":"engage","candidate_source":"contacts","action":"retain",
	    "available":[{"left":{"observation":"contact_candidate.available"},"op":"eq","right":true},
	      {"left":{"observation":"contact_candidate.current"},"op":"eq","right":false},
	      {"left":{"observation":"contact_candidate.age"},"op":"le","right":10},
	      {"left":{"observation":"contact_candidate.distance"},"op":"le","right":100000}],
	    "target":{"observation":"contact_candidate.id"},
	    "arguments":[{"observation":"contact_candidate.id"},{"observation":"contact_candidate.x"}],
	    "factors":{"progress":100,"risk":{"observation":"contact_candidate.age"}}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.contacts")
			for (auto &binding : tree["objective_bindings"])
				binding["available"].push_back(
				    {{"left", {{"observation", "contact_candidate.category"}}}, {"op", "eq"}, {"right", "ship"}});
	{
		auto category_source = source;
		const auto target_definition = category_source["maps"][0]["entities"][1]["definition"];
		Json apparent;
		for (const auto &entity : category_source["entities"])
			if (entity["id"] == target_definition)
				apparent = entity;
		apparent["id"] = "test.rock-contact";
		apparent["render"]["radar_icon"] = "asteroid";
		apparent["render"]["sprite"]["radar_icon"] = "asteroid";
		category_source["entities"].push_back(apparent);
		category_source["maps"][0]["entities"][1]["definition"] = "test.rock-contact";
		const auto category_catalog = load(category_source);
		World category;
		category.load(category_catalog, "test.map");
		claim(category);
		advance(category, 3);
		const auto assert_category = [](const World &value)
		{
			const auto observed = *value.observe(101);
			bool reported = false;
			for (const auto &record : observed.gameplay.at("records"))
				if (record.at("schema") == "phoenix.contact.report" && record.at("subject") == 2)
					reported |= record.at("fields").at("category") == "asteroid";
			for (const auto &controller : observed.gameplay.at("controllers"))
				if (controller.at("id") == "gun")
					require(
					    controller.at("commands").at("target").at("value") == 3 &&
					        controller.at("commands").at("retained_target").at("value") == 0,
					    "authored ship eligibility rejects an observed non-ship category before and after retention");
			require(reported,
			        "retained reports preserve the acquired category rather than inferring it from a definition");
		};
		assert_category(category);
		auto hidden = category.save();
		for (auto &body : hidden["bodies"])
			if (body.at("body").at("id") == 2)
				body["body"]["position"]["x"] = 900000;
		category.restore(category_catalog, hidden);
		advance(category, 2);
		assert_category(category);
	}
	{
		auto neutral_source = source;
		neutral_source["maps"][0]["relationships"][0]["relation"] = "neutral";
		World neutral;
		neutral.load(load(neutral_source), "test.map");
		claim(neutral);
		advance(neutral, 2);
		const auto observed = *neutral.observe(101);
		bool refusal = false;
		for (const auto &controller : observed.gameplay.at("controllers"))
			if (controller.at("id") == "gun")
			{
				require(controller.at("commands").at("target").at("value") == 0,
				        "neutral contacts cannot create an engagement action");
				for (const auto &candidate : controller.at("objective_candidates"))
					refusal |= candidate.at("reason") == "no_known_hostile";
			}
		require(refusal, "engagement diagnostics distinguish missing hostile evidence");
	}
	// The authored geometry is a calculation over the selected permitted point,
	// and has no authority to acquire a hidden current position.
	for (const auto mode : {"approach", "withdraw", "withdraw-zero"})
	{
		auto geometry_source = source;
		for (auto &tree : geometry_source["trees"])
			if (tree["id"] == "test.contacts")
			{
				tree["objective_bindings"].erase(tree["objective_bindings"].begin());
				auto &binding = tree["objective_bindings"][0];
				binding["navigation"] = {{"mode", std::string(mode) == "withdraw-zero" ? "withdraw" : mode},
				                         {"distance", std::string(mode) == "withdraw-zero" ? 0 : 20000}};
				binding["arguments"][1] = {{"observation", "navigation.x"}};
			}
		// Withdrawal can request negative coordinates through this logical fixture.
		for (auto &controller : geometry_source["entities"][0]["control_layout"]["controllers"])
			if (controller["id"] == "gun")
				for (auto &command : controller["commands"])
					if (command["id"] == "retained_x")
						command["minimum"] = -1000000000;
		const auto geometry_catalog = load(geometry_source);
		World geometry;
		geometry.load(geometry_catalog, "test.map");
		claim(geometry);
		advance(geometry, 3);
		auto state = geometry.save();
		for (auto &body : state["bodies"])
			if (body.at("body").at("id") == 2)
				body["body"]["position"]["x"] = 900000;
		geometry.restore(geometry_catalog, state);
		World opposite;
		for (auto &body : state["bodies"])
			if (body.at("body").at("id") == 2)
				body["body"]["position"]["x"] = -900000;
		opposite.restore(geometry_catalog, state);
		advance(geometry, 2);
		advance(opposite, 2);
		const auto commands = [](const World &value)
		{
			const auto view = *value.observe(101);
			for (const auto &controller : view.gameplay.at("controllers"))
				if (controller.at("id") == "gun")
					return controller.at("commands");
			throw std::runtime_error("missing geometry Controller");
		};
		require(commands(geometry).at("retained_x").at("value") == (std::string(mode) == "approach"        ? 70000
		                                                            : std::string(mode) == "withdraw-zero" ? 0
		                                                                                                   : -20000) &&
		            commands(geometry) == commands(opposite),
		        "authored standoff and withdrawal use retained geometry, never the hidden enemy position");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	advance(world, 3);
	const auto gun_commands = [](const World &world)
	{
		const auto observed = *world.observe(101);
		for (const auto &controller : observed.gameplay.at("controllers"))
			if (controller.at("id") == "gun")
				return controller.at("commands");
		throw std::runtime_error("missing scored weapon Controller");
	};
	require(gun_commands(world).at("target").at("value") == 2,
	        "an objective's specified hostile takes precedence over a nearer unscoped contact");
	auto hidden_state = world.save();
	for (auto &body : hidden_state["bodies"])
		if (body.at("body").at("id") == 2)
			body["body"]["position"]["x"] = 900000;
	world.restore(catalog, hidden_state);
	World other_hidden;
	for (auto &body : hidden_state["bodies"])
		if (body.at("body").at("id") == 2)
			body["body"]["position"]["x"] = -900000;
	other_hidden.restore(catalog, hidden_state);
	advance(world, 2);
	advance(other_hidden, 2);
	require(gun_commands(world).at("retained_target").at("value") == 2 &&
	            gun_commands(world).at("retained_x").at("value") == 90000 &&
	            gun_commands(world) == gun_commands(other_hidden),
	        "bounded retained-location intent uses the last permitted report and ignores hidden enemy movement");
	const auto first = *world.observe(101), second = *other_hidden.observe(101);
	for (std::size_t i = 0; i < first.gameplay.at("controllers").size(); ++i)
		if (first.gameplay.at("controllers")[i].at("id") == "gun")
			require(first.gameplay.at("controllers")[i].at("objective_candidates") ==
			            second.gameplay.at("controllers")[i].at("objective_candidates"),
			        "undisclosed enemy position cannot change contact eligibility, utility or selected-action reasons");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 14);
	advance(resumed, 14);
	require(gun_commands(world).at("target").at("value") == 3 && world.save() == resumed.save(),
	        "expired pursuit feasibility falls through to current hostile work and resumes deterministically");
}
