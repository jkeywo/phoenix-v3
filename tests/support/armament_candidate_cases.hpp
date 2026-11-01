void armament_candidate_contract()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	auto payload = ship["systems"]["gun"]["projectile"];
	payload["speed_mm_s"] = 10000;
	ship["systems"]["mag"]["projectile"] = payload;
	for (const auto &[id, speed] : std::vector<std::pair<std::string, int>>{{"long", 80000}, {"middle", 40000}})
	{
		payload["speed_mm_s"] = speed;
		add_fit(source, id,
		        {{"kind", "magazine"},
		         {"compartment", "fore"},
		         {"capacity", 1},
		         {"reload", {{"strategy", "none"}}},
		         {"projectile", payload}},
		        {});
	}
	ship["systems"]["gun"]["magazines"] = {"mag", "long", "middle"};
	ship["systems"]["gun"]["load_ticks"] = 2;
	ship["systems"]["gun"]["fire_ticks"] = 1000;
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"engage","behavior":"engage","tier":"directive_combat","system":"gun","title_key":"Engage known hostiles"}])json");
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "gun")
		{
			controller.erase("fallback");
			controller["tree"] = "test.armament";
			controller["commands"].push_back(
			    {{"id", "magazine"}, {"action", "magazine_select"}, {"consumers", {"gun"}}});
		}
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"] = {"gun"};
	auto &human = ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"];
	human.erase(std::remove(human.begin(), human.end(), "gun"), human.end());
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.armament.execute","context":"controller","command_bindings":[{"channel":"target"},{"channel":"magazine"}],
	  "controller_action":{"label":"Engage with scored ammunition","inputs":[{"name":"target","type":"integer"},{"name":"magazine","type":"integer"}]},
	  "initial":"operate","states":[{"id":"operate","running":[{"attempt":[{"tasks":[
	   {"action":"write.target","arguments":[{"observation":"input.target"}]},
	   {"action":"write.magazine","arguments":[{"observation":"input.magazine"}]},
	   {"action":"action.succeed","arguments":["armament_admitted"]}]},
	   {"tasks":[{"action":"action.fail","arguments":["armament_refused"]}]}]}]}]},
	 {"id":"test.armament","context":"controller","contact_observations":true,"armament_observations":true,
	  "command_bindings":[{"channel":"target"},{"channel":"magazine"}],
	  "action_bindings":[{"id":"engage","tree":"test.armament.execute"}],
	  "objective_bindings":[{"id":"engage","behavior":"engage","candidate_source":"armament","action":"engage",
	   "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true},
	    {"left":{"observation":"armament.available"},"op":"eq","right":true},
	    {"left":{"observation":"armament.enabled"},"op":"eq","right":true},
	    {"left":{"observation":"armament.in_range"},"op":"eq","right":true},
	    {"left":{"observation":"armament.fire_allowed"},"op":"eq","right":true}],
	   "target":{"observation":"contact_candidate.id"},
	   "arguments":[{"observation":"contact_candidate.id"},{"observation":"armament.index"}],
	   "commitment":{"observation":"armament.selected"},
	   "factors":{"progress":1000,"travel":{"observation":"contact_candidate.distance"},"resources":{"observation":"armament.range"}}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.armament")
			tree["objective_bindings"].push_back({{"id", "hold"},
			                                      {"behavior", "hold"},
			                                      {"action", "engage"},
			                                      {"arguments", Json::array({0, 0})},
			                                      {"factors", {{"progress", 1}}}});
	{
		auto separate = source;
		auto &owner = separate["entities"][0];
		for (auto &controller : owner["control_layout"]["controllers"])
			if (controller["id"] == "gun")
			{
				controller.erase("tree");
				controller["fallback"] = "hold";
				auto &commands = controller["commands"];
				commands.erase(std::remove_if(commands.begin(), commands.end(),
				                              [](const auto &row)
				                              {
					                              return row.at("id") == "magazine";
				                              }),
				               commands.end());
			}
		owner["control_layout"]["controllers"].push_back(
		    {{"id", "loader"},
		     {"console", "drive"},
		     {"tree", "test.ammunition-only"},
		     {"commands", Json::array({{{"id", "magazine"}, {"action", "magazine_select"}, {"consumers", {"gun"}}}})}});
		auto &seat = owner["control_layout"]["profiles"][0]["consoles"]["drive"];
		seat["ai"] = {"loader"};
		seat["human"].push_back("gun");
		separate["maps"][0]["entities"][1]["x_mm"] = 150000;
		separate["maps"][0]["entities"][1]["y_mm"] = 0;
		separate["maps"][0]["entities"][2]["x_mm"] = 0;
		separate["maps"][0]["entities"][2]["y_mm"] = 20000;
		for (const auto &tree : Json::parse(R"json([
 {"id":"test.ammunition-select","context":"controller","command_bindings":[{"channel":"magazine","action":"magazine_select"}],
 "controller_action":{"label":"Fit ammunition to accepted target","inputs":[{"name":"magazine","type":"integer"}]},
 "initial":"select","states":[{"id":"select","running":[{"action":"write.magazine","arguments":[{"observation":"input.magazine"}]},{"action":"action.succeed","arguments":["selected"]}]}]},
 {"id":"test.ammunition-only","context":"controller","contact_observations":true,"armament_observations":true,
 "command_bindings":[{"channel":"magazine","action":"magazine_select"}],"action_bindings":[{"id":"select","tree":"test.ammunition-select"}],
 "objective_bindings":[{"id":"accepted-target","behavior":"engage","candidate_source":"armament","action":"select",
 "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true},
 {"left":{"observation":"armament.available"},"op":"eq","right":true},{"left":{"observation":"armament.in_range"},"op":"eq","right":true},
 {"left":{"observation":"contact_candidate.id"},"op":"eq","right":{"observation":"armament.target"}}],
 "target":{"observation":"contact_candidate.id"},"arguments":[{"observation":"armament.index"}],"factors":{"progress":1000,"resources":{"observation":"armament.range"}}}],
 "initial":"observe","states":[{"id":"observe"}]}])json"))
			separate["trees"].push_back(tree);
		const auto catalog = load(separate);
		World coordinated;
		coordinated.load(catalog, "test.map");
		claim(coordinated);
		advance(coordinated, 1);
		submit(coordinated, 101, "gun", "target", 2);
		advance(coordinated, 4);
		if (state(coordinated)["systems"]["gun"]["selected_magazine"] != 1)
			std::cerr
			    << coordinated.observe(101)->gameplay.at("directed").dump() << '\n'
			    << coordinated.controller_decision_snapshot({1, "drive", "loader"}).at("objective_candidates").dump()
			    << '\n';
		require(state(coordinated)["systems"]["gun"]["selected_magazine"] == 1,
		        "A separate ammunition Controller fits the frozen accepted target rather than a nearer hostile");
		const auto choices =
		    coordinated.controller_decision_snapshot({1, "drive", "loader"}).at("objective_candidates");
		require(std::any_of(choices.begin(), choices.end(),
		                    [](const auto &row)
		                    {
			                    return row.value("selected", false) && row.at("target") == 2;
		                    }),
		        "Ammunition diagnostics identify the accepted target shared through the declared weapon feed");
		World resumed;
		resumed.restore(catalog, coordinated.save());
		advance(coordinated, 12);
		advance(resumed, 12);
		require(coordinated.save() == resumed.save(),
		        "Independent ammunition selection preserves finite loading continuation");
	}
	{
		auto held_source = source;
		held_source["entities"][0]["standing_directives"].push_back(
		    {{"id", "hold"}, {"behavior", "hold"}, {"tier", "scripted_high"}, {"title_key", "Published hold fire"}});
		const auto held_catalog = load(held_source);
		World held;
		held.load(held_catalog, "test.map");
		claim(held);
		advance(held, 3);
		require(state(held)["systems"]["gun"]["shots"] == 0,
		        "a published scripted hold advances through ordinary weapon admission ahead of standing engagement");
		const auto observed = *held.observe(101);
		bool selected_hold = false;
		for (const auto &controller : observed.gameplay.at("controllers"))
			if (controller.at("id") == "gun")
			{
				require(controller.at("commands").at("target").at("value") == 0,
				        "the admitted hold clears an offensive target");
				for (const auto &candidate : controller.at("objective_candidates"))
					selected_hold |= candidate.value("selected", false) && candidate.at("binding") == "hold";
			}
		require(selected_hold, "hold selection is explained by shared objective diagnostics");
		World resumed;
		resumed.restore(held_catalog, held.save());
		advance(held, 4);
		advance(resumed, 4);
		require(held.save() == resumed.save(), "published hold continuation restores identically");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	advance(world, 1);
	require(state(world)["systems"]["gun"]["selected_magazine"] == 2,
	        "authored armament utility compares all covering options instead of selecting the first feasible magazine");
	bool range_reason = false;
	const auto observed = *world.observe(101);
	for (const auto &controller : observed.gameplay.at("controllers"))
		if (controller.at("id") == "gun")
			for (const auto &candidate : controller.at("objective_candidates"))
				range_reason |= candidate.at("reason") == "target_outside_payload_range";
	require(range_reason, "armament diagnostics explain why an observed option cannot advance the objective");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 40);
	advance(resumed, 40);
	require(state(world)["systems"]["gun"]["selected_magazine"] == 1 && state(world)["systems"]["gun"]["shots"] > 0,
	        "finite loading and firing consume the preferred round before scored selection falls through to another "
	        "magazine");
	require(world.save() == resumed.save(),
	        "scored ammunition choices preserve completed physical work through continuation");
}
