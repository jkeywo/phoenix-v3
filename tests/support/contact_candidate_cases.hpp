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
