#pragma once
// Independent jobs and hulls: exercise scoped workflow facts and ordinary
// admission, physical docking, team preparation, recovery and continuation.
void autonomous_away_cases()
{
	for (const int mode : {0, 1, 2})
	{
		const bool offered = mode != 0, cover = mode == 2;
		auto source = assisted_docking_source();
		auto &hull = source["entities"][0];
		hull["systems"]["pulse"] = {
		    {"kind", "impulse"},
		    {"compartment", "bridge"},
		    {"power_store", "cell"},
		    {"charge_ticks", 2},
		    {"charge_energy", 2},
		    {"minimum_reserve", 2},
		    {"power_rate", 5},
		    {"targets",
		     Json::array({{{"system", "engine"}, {"property", "max_speed_mm_s"}, {"multiplier_permille", 2000}}})}};
		hull["stations"][0]["consoles"][0]["feeds"].push_back("pulse");
		hull["control_layout"]["controllers"].push_back({{"id", "pulse"},
		                                                 {"console", "drive"},
		                                                 {"tree", "test.dock-pulse"},
		                                                 {"commands", Json::array({{{"id", "pulse"},
		                                                                            {"action", "impulse"},
		                                                                            {"semantics", "request"},
		                                                                            {"initial", 0},
		                                                                            {"consumers", {"pulse"}}}})}});
		for (auto &profile : hull["control_layout"]["profiles"])
			profile["consoles"]["drive"]["human"].push_back("pulse");
		hull["personnel"]["duties"].push_back({{"id", "pulse"},
		                                       {"label", "Impulse"},
		                                       {"controller", "pulse"},
		                                       {"console", "drive"},
		                                       {"department", "navigation"},
		                                       {"compartment", "bridge"},
		                                       {"effects", Json::object()}});
		for (auto &port : hull["docking"]["ports"])
			port["controller"] = "engine";
		for (auto &entity : source["entities"])
			for (auto &port : entity["docking"]["ports"])
				port["auto_accept_allies"] = true;
		source["maps"][0]["factions"] = Json::array({{{"id", "workers"}, {"label", "Workers"}}});
		source["maps"][0]["entities"][0]["faction"] = "workers";
		source["maps"][0]["entities"][0]["x_mm"] = 40000;
		source["maps"][0]["ship_slots"][0]["faction"] = "workers";
		auto &mission = source["maps"][0]["away_missions"][0];
		mission["ai_dispatch"] = offered;
		mission["ship_support"] = cover ? "combat_cover" : "hold";
		for (auto &controller : hull["control_layout"]["controllers"])
			if (controller["id"] == "dispatch" || controller["id"] == "engine")
			{
				controller.erase("fallback");
				controller["tree"] = controller["id"] == "dispatch" ? "test.offered-dispatch" : "test.offered-dock";
				if (controller["id"] == "engine")
					controller["commands"].push_back(
					    {{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", {"engine"}}});
			}
			else if (controller["id"] == "steering")
				controller["commands"].push_back(
				    {{"id", "heading"}, {"action", "heading_mdeg"}, {"initial", 0}, {"consumers", {"steering"}}});
		source["trees"].push_back(Json::parse(R"json({
 "id":"test.dock-pulse","context":"controller","workflow_observations":true,
 "command_bindings":[{"channel":"pulse","action":"impulse","type":"boolean"}],"initial":"wait",
 "states":[{"id":"wait","transitions":[{"target":"pulse","conditions":[{"left":{"observation":"workflow.dock_phase"},"op":"eq","right":"approach"}]}]},
 {"id":"pulse","entry":[{"attempt":[{"tasks":[{"action":"write.pulse","arguments":[true]}]},{"tasks":[]}]}],
 "transitions":[{"target":"stop","conditions":[{"left":{"elapsed":true},"op":"ge","right":3}]}]},
 {"id":"stop","entry":[{"attempt":[{"tasks":[{"action":"write.pulse","arguments":[false]}]},{"tasks":[]}]}]}]})json"));
		source["trees"].push_back(Json::parse(R"json({
 "id":"test.offered-dispatch","context":"controller","workflow_observations":true,"initial":"watch",
 "states":[{"id":"watch","transitions":[
 {"target":"open","conditions":[{"left":{"observation":"workflow.phase"},"op":"eq","right":"available"}]},
 {"target":"send","conditions":[{"left":{"observation":"workflow.phase"},"op":"eq","right":"open"},{"left":{"observation":"workflow.launch_ready"},"op":"eq","right":true}]},
 {"target":"recall","conditions":[{"left":{"observation":"workflow.phase"},"op":"eq","right":"deployed"},{"left":{"observation":"workflow.work_complete"},"op":"eq","right":true}]}]},
 {"id":"open","entry":[{"attempt":[{"tasks":[{"action":"away.open","arguments":[{"observation":"workflow.mission"},"",{"observation":"workflow.mission_revision"}]}]},{"tasks":[]}]}],"transitions":[{"target":"watch"}]},
 {"id":"send","entry":[{"attempt":[{"tasks":[{"action":"away.send","arguments":[{"observation":"workflow.mission"},"",{"observation":"workflow.mission_revision"}]}]},{"tasks":[]}]}],"transitions":[{"target":"watch"}]},
 {"id":"recall","entry":[{"attempt":[{"tasks":[{"action":"away.recall","arguments":[{"observation":"workflow.mission"},"",{"observation":"workflow.mission_revision"}]}]},{"tasks":[]}]}],"transitions":[{"target":"watch"}]}]})json"));
		source["trees"].push_back(Json::parse(R"json({
 "id":"test.offered-dock","context":"controller","workflow_observations":true,"docking_commands":true,"initial":"dock",
 "states":[{"id":"dock","running":[{"attempt":[{"tasks":[{"action":"dock.assist","arguments":[{"observation":"workflow.port"},{"observation":"workflow.dock_target"},{"observation":"workflow.dock_revision"}]}]},{"tasks":[]}]}]}]})json"));
		for (auto &tree : source["trees"])
			if (tree["id"] == "test.away-tree")
				tree = Json::parse(R"json({"id":"test.away-tree","context":"away","initial":"wait","states":[
 {"id":"wait","transitions":[{"target":"work","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},
 {"id":"work","entry":[{"action":"checkpoint","arguments":["service"]},{"action":"timed_work","arguments":[4]}],
 "transitions":[{"target":"done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"done","entry":[{"action":"complete","arguments":["serviced"]}]}]})json");
		if (cover)
			for (auto &tree : source["trees"])
			{
				if (tree["id"] == "test.away-tree")
					tree["states"][1]["entry"][1]["arguments"][0] = 80;
				if (tree["id"] == "test.offered-dock")
				{
					tree["states"][0]["transitions"] = Json::parse(
					    R"([{"target":"release","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":true},{"left":{"observation":"workflow.dock_phase"},"op":"eq","right":"docked"}]}])");
					tree["states"].push_back(Json::parse(
					    R"({"id":"release","entry":[{"attempt":[{"tasks":[{"action":"dock.undock","arguments":[{"observation":"workflow.port"},"",{"observation":"workflow.dock_revision"}]}]},{"tasks":[]}]}],"transitions":[{"target":"cover"}]})"));
					tree["states"].push_back(Json::parse(
					    R"({"id":"cover","transitions":[{"target":"dock","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":false}]}]})"));
				}
			}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto ship = *world.ship_for_slot("crew");
		require(world.claim(101, ship, "drive"), "human Dispatch retains the offered job decision");
		ticks(world, 15);
		require(world.save().at("away_missions")[0].at("phase") == "available",
		        "autonomous job policy cannot take a human-owned mission operation");
		world.release(101);
		bool deployed = false, recovered = false, continued = false, pulsed = false, detached = false;
		for (int i = 0; i < 800; ++i)
		{
			world.tick();
			const auto saved = world.save();
			const auto &job = saved.at("away_missions")[0];
			for (const auto &controller : saved.at("bodies").back().at("console_controllers").at("controllers"))
				if (controller.at("id") == "pulse" && controller.at("commands")[0].at("value") == 1)
				{
					pulsed = true;
					require(saved.at("bodies").back().contains("dock_connection"),
					        "delegated impulse cannot cancel its admitted docking approach");
				}
			detached |= job.at("phase") == "deployed" && !saved.at("bodies").back().contains("dock_connection");
			deployed |= job.at("ever_deployed").get<bool>();
			recovered |= job.at("phase") == "recovered";
			if (offered && !continued && saved.at("bodies").back().contains("dock_connection"))
			{
				World resumed;
				resumed.restore(catalog, saved);
				world.tick();
				resumed.tick();
				require(world.save() == resumed.save(), "offered-job docking continuation is deterministic");
				continued = true;
			}
			if (recovered || (!offered && i > 20))
				break;
		}
		require(deployed == offered && recovered == offered,
		        "only explicitly offered jobs dock, deploy real teams and recover completed work");
		require(!offered || continued, "offered job exercised a saved physical docking approach");
		require(detached == cover, "Only combat-cover jobs separate during deployed work and redock for recovery");
		require(pulsed == offered, "only an admitted docking approach activates its delegated impulse");
	}
}
