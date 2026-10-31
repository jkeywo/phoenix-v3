#pragma once

void fit_combat_cover_manoeuvre(Json &source)
{
	auto &hull = source["entities"][0];
	hull["render"]["radar_icon"] = "ship";
	hull["systems"]["scan"]["range_mm"] = 1000000;
	hull["systems"]["engine"]["max_speed_mm_s"] = 20000;
	hull["systems"]["engine"]["acceleration_mm_s2"] = 20000;
	hull["systems"]["engine"]["deceleration_mm_s2"] = 20000;
	hull["systems"]["steering"]["turn_mdeg_s"] = 120000;
	hull["systems"]["cover-mag"] = {{"kind", "magazine"}, {"compartment", "bridge"}, {"capacity", 100}};
	hull["systems"]["cover-gun"] = {{"kind", "weapon"},
	                                {"compartment", "bridge"},
	                                {"magazine", "cover-mag"},
	                                {"range_mm", 100000},
	                                {"arc_mdeg", 360000},
	                                {"fire_ticks", 20},
	                                {"projectile",
	                                 {{"visual", "test.away"},
	                                  {"speed_mm_s", 100000},
	                                  {"lifetime_ticks", 20},
	                                  {"penetration", 1},
	                                  {"damage", 1},
	                                  {"damage_per_m", 1}}}};
	for (auto &controller : hull["control_layout"]["controllers"])
		if (controller["id"] == "engine" || controller["id"] == "steering")
		{
			controller["commands"].push_back({{"id", "destination"},
			                                  {"action", "destination"},
			                                  {"arguments", {0, 0}},
			                                  {"consumers", {controller["id"]}}});
			controller["tree"] = controller["id"] == "engine" ? "test.cover-propulsion" : "test.cover-steering";
		}
	hull["control_layout"]["controllers"].push_back(
	    {{"id", "cover-gun"},
	     {"console", "drive"},
	     {"tree", "test.cover-fire"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"cover-gun"}}},
	                   {{"id", "target"}, {"action", "weapon_target"}, {"consumers", {"cover-gun"}}}})}});
	for (const auto *id : {"engine", "steering", "cover-mag", "cover-gun"})
	{
		auto &feeds = hull["stations"][0]["consoles"][0]["feeds"];
		if (std::find(feeds.begin(), feeds.end(), id) == feeds.end())
			feeds.push_back(id);
	}
	for (auto &profile : hull["control_layout"]["profiles"])
		profile["consoles"]["drive"]["ai"].push_back("cover-gun");
	hull["personnel"]["duties"].push_back({{"id", "cover-gun"},
	                                       {"label", "Cover gun"},
	                                       {"controller", "cover-gun"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"}});
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.cover-propulsion","context":"controller","workflow_observations":true,"docking_commands":true,
 "command_bindings":[{"channel":"destination","action":"destination","type":"boolean"},{"channel":"speed","action":"speed_mm_s"}],"initial":"dock",
 "states":[{"id":"dock","entry":[{"action":"write.destination","arguments":[false,0,0]},{"action":"write.speed","arguments":[0]}],
 "running":[{"attempt":[{"tasks":[{"action":"dock.assist","arguments":[{"observation":"workflow.port"},{"observation":"workflow.dock_target"},{"observation":"workflow.dock_revision"}]}]},{"tasks":[]}]}],
 "transitions":[{"target":"release","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":true},{"left":{"observation":"workflow.dock_phase"},"op":"eq","right":"docked"}]}]},
 {"id":"release","entry":[{"attempt":[{"tasks":[{"action":"dock.undock","arguments":[{"observation":"workflow.port"},"",{"observation":"workflow.dock_revision"}]}]},{"tasks":[]}]}],"transitions":[{"target":"clear"}]},
 {"id":"clear","transitions":[{"target":"cover","conditions":[{"left":{"observation":"workflow.dock_phase"},"op":"eq","right":"free"}]}]},
 {"id":"cover","running":[{"action":"write.destination","arguments":[{"observation":"workflow.cover_moving"},{"observation":"workflow.cover_x"},{"observation":"workflow.cover_y"}]},{"action":"write.speed","arguments":[{"observation":"workflow.cover_speed"}]}],
 "transitions":[{"target":"dock","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":false}]}]}]})json"));
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.cover-steering","context":"controller","workflow_observations":true,
 "command_bindings":[{"channel":"destination","action":"destination","type":"boolean"},{"channel":"heading","action":"heading_mdeg"}],"initial":"monitor",
 "states":[{"id":"monitor","transitions":[{"target":"move","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":true},{"left":{"observation":"workflow.dock_phase"},"op":"eq","right":"free"}]}]},
 {"id":"move","running":[{"action":"write.destination","arguments":[{"observation":"workflow.cover_moving"},{"observation":"workflow.cover_x"},{"observation":"workflow.cover_y"}]}],
 "transitions":[{"target":"monitor","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":false}]},{"target":"face","conditions":[{"left":{"observation":"workflow.cover_moving"},"op":"eq","right":false}]}]},
 {"id":"face","running":[{"action":"write.destination","arguments":[false,0,0]},{"action":"write.heading","arguments":[{"observation":"workflow.cover_heading"}]}],
 "transitions":[{"target":"monitor","conditions":[{"left":{"observation":"workflow.detach"},"op":"eq","right":false}]},{"target":"move","conditions":[{"left":{"observation":"workflow.cover_moving"},"op":"eq","right":true}]}]}]})json"));
	source["trees"].push_back(Json::parse(R"json({"id":"test.cover-fire","context":"controller",
 "command_bindings":[{"channel":"target","action":"weapon_target"}],"initial":"fire",
 "states":[{"id":"fire","running":[{"action":"write.target","arguments":[{"observation":"tactical.target"}]}]}]})json"));
	Json enemy;
	for (const auto &entity : source["entities"])
		if (entity["id"] == "test.receiving-ship")
			enemy = entity;
	enemy["id"] = "test.cover-enemy";
	enemy["render"]["radar_icon"] = "ship";
	enemy["faction"] = "opponent";
	for (auto &controller : enemy["control_layout"]["controllers"])
		controller.erase("tree");
	source["entities"].push_back(enemy);
	source["maps"][0]["factions"].push_back({{"id", "opponent"}, {"label", "Opponent"}});
	source["maps"][0]["relationships"] = Json::array({{{"a", "workers"}, {"b", "opponent"}, {"relation", "hostile"}}});
	source["maps"][0]["entities"].push_back(
	    {{"id", 90}, {"definition", "test.cover-enemy"}, {"x_mm", 0}, {"y_mm", -200000}, {"heading_mdeg", 90000}});
}
