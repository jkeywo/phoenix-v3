#pragma once
void received_information_cases()
{
	for (const bool scanner_human : {false, true})
		for (const bool comms_human : {false, true})
			for (const bool acquire : {false, true})
			{
				auto source = fixture();
				auto &ship = source["entities"][0];
				ship["systems"]["radar"]["capabilities"] = {{"findings", 0}, {"basic_identity", 0}};
				ship["control_layout"]["controllers"].push_back(
				    {{"id", "scanner"},
				     {"console", "drive"},
				     {"fallback", "hold"},
				     {"commands", Json::array({{{"id", "target"},
				                                {"action", "sensor_target"},
				                                {"initial", 0},
				                                {"consumers", {"radar"}}}})}});
				auto &profile = ship["control_layout"]["profiles"][0]["consoles"]["drive"];
				profile["human"] = Json::array({"propulsion"});
				profile["ai"] = Json::array();
				profile[scanner_human ? "human" : "ai"].push_back("scanner");
				profile[comms_human ? "human" : "ai"].push_back("comms");
				ship["control_layout"]["profiles"][1]["consoles"]["drive"]["ai"].push_back("scanner");
				// Only the observing ship has a scanner; the casualty cannot target itself.
				auto target = ship;
				target["id"] = "test.remote";
				target["control_layout"]["controllers"].back()["commands"][0]["initial"] = 0;
				source["entities"].push_back(target);
				if (acquire && !scanner_human)
				{
					source["entities"][0]["control_layout"]["controllers"].back()["tree"] = "test.scan-policy";
					source["trees"].push_back(Json::parse(
					    R"({"id":"test.scan-policy","context":"controller","command_bindings":[{"channel":"target","action":"sensor_target"}],"initial":"scan","states":[{"id":"scan","running":[{"action":"write.target","arguments":[{"observation":"objective.target"}]}]}]})"));
				}
				source["maps"][0]["entities"][1]["definition"] = "test.remote";
				source["maps"][0]["sensor_findings"] = Json::array({{{"id", "warning"},
				                                                     {"target_entity", 2},
				                                                     {"text_key", "Containment warning"},
				                                                     {"scan_ticks", 3},
				                                                     {"requires", {"findings"}},
				                                                     {"requires_event", "test.objective"},
				                                                     {"tag", "reactor_critical"},
				                                                     {"destination_station", "drive"}}});
				source["maps"][0]["scenario"] = {{"tree", "test.objective"}, {"events", {"test.objective"}}};
				source["trees"].push_back(Json::parse(
				    R"({"id":"test.objective","context":"scenario","initial":"start","states":[{"id":"start","entry":[{"action":"emit_scenario_event","arguments":["test.objective",0,false,""]}]}]})"));
				source["maps"][0]["conversations"] = Json::parse(
				    R"([{"id":"decision","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]}])");
				for (auto &tree : source["trees"])
					if (tree["id"] == "test.dialogue")
					{
						tree["prompts"]["question"]["ai_choice"] = "no";
						tree["prompts"]["question"]["ai_rules"] =
						    Json::array({{{"received_tag", "reactor_critical"}, {"choice", "yes"}}});
					}
				const auto catalog = load(source);
				World world;
				world.load(catalog, "test.map");
				require(world.claim(1, 1, "drive"), "information console claimed");
				world.tick();
				if (acquire && scanner_human)
				{
					const auto observed = world.observe(1);
					for (const auto &controller : observed->gameplay.at("controllers"))
						if (controller.at("id") == "scanner")
							require(world.submit(1,
							                     OperatingCommand{
							                         1, {}, 2, {}, "scanner", "target", controller.at("generation")}),
							        "human scan ordered");
				}
				for (int n = 0; n < 35; ++n)
					world.tick();
				const auto saved = world.save();
				const auto &prompt = saved.at("conversations").at("threads")[0].at("prompt");
				require(prompt.at("answer") == (comms_human ? ""
				                                : acquire   ? "yes"
				                                            : "no"),
				        ("AI uses only received tags; human retains decision: " + std::to_string(scanner_human) + "/" +
				         std::to_string(comms_human) + "/" + std::to_string(acquire) + " " + prompt.dump())
				            .c_str());
				int messages = 0;
				const auto observation = world.observe(1);
				const auto knowledge = observation->gameplay;
				for (const auto &message : knowledge.at("messages"))
					if (message.at("body_key") == "Containment warning")
						++messages;
				require(messages == (acquire && !scanner_human && !comms_human ? 1 : 0),
				        "only two AI operators announce forwarding");
				World restored;
				restored.restore(catalog, saved);
				for (int n = 0; n < 5; ++n)
				{
					world.tick();
					restored.tick();
				}
				require(world.save() == restored.save(), "received information and replies resume without duplicates");
			}
}
