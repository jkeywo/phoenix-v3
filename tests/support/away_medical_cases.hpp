#pragma once
// Independent authored three-job flow. Values deliberately differ from shipped tuning.
void away_medical_cases()
{
	for (const int mode : {0, 1, 2, 3}) // wounded first, wounded last, no Medical, withdraw Medical
	{
		auto source = away_fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["medical"] = {
		    {"kind", "team"}, {"type", "clinical"}, {"capabilities", {"medical"}}, {"compartment", "workshop"}};
		auto controller = ship["control_layout"]["controllers"][3];
		controller["id"] = "medical";
		controller["commands"][0]["consumers"] = {"medical"};
		ship["control_layout"]["controllers"].push_back(controller);
		ship["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back("medical");
		ship["stations"][1]["consoles"][0]["feeds"].push_back("medical");
		ship["personnel"]["duties"].push_back({{"id", "medical-control"},
		                                       {"label", "Medical"},
		                                       {"controller", "medical"},
		                                       {"console", "observer"},
		                                       {"department", "engineering"},
		                                       {"compartment", "workshop"},
		                                       {"effects", Json::object()}});
		ship["personnel"]["duties"].push_back({{"id", "medical-duty"},
		                                       {"label", "Medical"},
		                                       {"team", "medical"},
		                                       {"department", "engineering"},
		                                       {"compartment", "workshop"},
		                                       {"effects", Json::object()}});
		source["maps"][0]["away_missions"][0]["slots"].push_back(
		    {{"id", "medical"}, {"kind", "team"}, {"required", false}, {"capability", "medical"}});
		source["maps"][0]["scenario"] = {{"tree", "test.clock"}, {"events", {"test.emergency"}}};
		source["trees"].push_back(Json::parse(
		    R"({"id":"test.clock","context":"scenario","initial":"wait","states":[{"id":"wait","transitions":[{"target":"alert","conditions":[{"left":{"elapsed":true},"op":"ge","right":6}]}]},{"id":"alert","entry":[{"action":"emit_scenario_event","arguments":["test.emergency",0,false,""]}]}]})"));
		for (auto &tree : source["trees"])
			if (tree["id"] == "test.dialogue")
			{
				tree["states"] = Json::parse(R"([{"id":"start","entry":[{"action":"say","arguments":["intro"]}]}])");
				tree["prompts"]["choice"]["timeout_ticks"] = 1;
				tree["prompts"]["choice"]["default_choice"] = mode == 1 ? "reactor" : "wounded";
				tree["prompts"]["choice"]["choices"] = Json::parse(
				    R"([{"id":"wounded","text_key":"Rescue wounded","requires_capability":"medical","unavailable_reason":"Medical required"},{"id":"reactor","text_key":"Reactor"},{"id":"propulsion","text_key":"Propulsion"}])");
				if (mode == 1)
					std::swap(tree["prompts"]["choice"]["choices"][0], tree["prompts"]["choice"]["choices"][2]);
			}
			else if (tree["id"] == "test.away-tree")
				tree = Json::parse(
				    R"({"id":"test.away-tree","context":"away","initial":"wait","flags":["wounded","reactor","propulsion"],"team_capabilities":["medical"],"events":["test.emergency"],"states":[
 {"id":"wait","transitions":[{"target":"ask","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true},{"left":{"observation":"events.test.emergency"},"op":"eq","right":true}]}]},
 {"id":"ask","entry":[{"action":"pause","arguments":[""]},{"action":"offer","arguments":["choice"]}],"transitions":[
 {"target":"done","conditions":[{"left":{"observation":"flags.reactor"},"op":"eq","right":true},{"left":{"observation":"flags.propulsion"},"op":"eq","right":true},{"left":{"observation":"flags.wounded"},"op":"eq","right":true}]},
 {"target":"lost","conditions":[{"left":{"observation":"flags.reactor"},"op":"eq","right":true},{"left":{"observation":"flags.propulsion"},"op":"eq","right":true},{"left":{"observation":"team_capabilities.medical"},"op":"eq","right":false}]},
 {"target":"wounded","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"wounded"}]},
 {"target":"reactor","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"reactor"}]},
 {"target":"propulsion","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"propulsion"}]}]},
 {"id":"wounded","entry":[{"action":"checkpoint","arguments":["wounded"]},{"action":"require_team","arguments":["medical"]},{"action":"timed_work","arguments":[20]}],"transitions":[
 {"target":"ask","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true},{"left":{"observation":"team_capabilities.medical"},"op":"eq","right":false}]},
 {"target":"late","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true},{"left":{"observation":"flags.reactor"},"op":"eq","right":true},{"left":{"observation":"flags.propulsion"},"op":"eq","right":true}]},
 {"target":"saved","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"reactor","entry":[{"action":"checkpoint","arguments":["reactor"]},{"action":"timed_work","arguments":[3]}],"transitions":[{"target":"reactor-done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"reactor-done","entry":[{"action":"mark","arguments":["reactor"]}],"transitions":[{"target":"ask","conditions":[]}]},
 {"id":"propulsion","entry":[{"action":"checkpoint","arguments":["propulsion"]},{"action":"timed_work","arguments":[3]}],"transitions":[{"target":"propulsion-done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"propulsion-done","entry":[{"action":"mark","arguments":["propulsion"]}],"transitions":[{"target":"ask","conditions":[]}]},
 {"id":"late","entry":[{"action":"crew_deaths","arguments":[1]}],"transitions":[{"target":"saved","conditions":[]}]},
 {"id":"saved","entry":[{"action":"mark","arguments":["wounded"]}],"transitions":[{"target":"ask","conditions":[]}]},
 {"id":"lost","entry":[{"action":"crew_deaths","arguments":[7]},{"action":"mark","arguments":["wounded"]}],"transitions":[{"target":"done","conditions":[]}]},
 {"id":"done","entry":[{"action":"log","arguments":["Three jobs resolved"]},{"action":"complete","arguments":["resolved"]}]}]})");
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto id = *world.ship_for_slot("crew");
		require(world.claim(101, id, "drive") && world.claim(102, id, "observer"), "medical scenario claims");
		ticks(world, 10);
		require(world.save().at("scenario").dump().find("test.emergency") != std::string::npos &&
		            board(world)[0].at("phase") == "available",
		        "arrival clock escalates before deployment");
		require(world.submit(101, order(world, "open")), "medical open");
		ticks(world, 1);
		require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "medical repair assign");
		ticks(world, 1);
		auto medical_order = [&]
		{
			auto command = order(world, "assign", "medical/team:medical", 102);
			command.controller = "medical";
			const auto observation = world.observe(102);
			for (const auto &c : observation->gameplay.at("controllers"))
				if (c.at("id") == "medical")
					command.generation = c.at("generation");
			return command;
		};
		if (mode != 2)
		{
			require(world.submit(102, medical_order()), "medical assign");
			ticks(world, 1);
		}
		require(world.submit(101, order(world, "transport", "transport")), "medical transport");
		ticks(world, 12);
		require(world.submit(101, order(world, "send")), "medical send");
		world.tick();
		if (mode == 3)
		{
			for (int i = 0; i < 20 && world.save()["away_missions"][0]["progress"] < 3000; ++i)
				world.tick();
			require(world.save()["away_missions"][0]["checkpoint"] == "wounded", "withdraw mid treatment");
			require(world.submit(101, order(world, "recall")), "recall medical");
			ticks(world, 5);
			require(board(world)[0]["phase"] == "recovered", "medical recalled");
			require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "redeploy only repair");
			ticks(world, 12);
			require(world.submit(101, order(world, "send")), "send without medical");
		}
		for (int i = 0; i < 100; ++i)
		{
			world.tick();
			auto saved = world.save();
			World restored;
			restored.restore(catalog, saved);
			require(restored.save() == saved, "medical flow exact continuation");
		}
		const auto state = world.save().at("away_missions")[0];
		require(state.at("outcome") == "complete:resolved" && state.at("flags").size() == 3,
		        "three priorities resolve");
		require(state.at("crew_deaths") == (mode >= 2   ? 7
		                                    : mode == 1 ? 1
		                                                : 0),
		        "wounded priority and absence produce distinct losses");
	}
}
