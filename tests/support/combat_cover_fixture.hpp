#pragma once
#include "interdiction_goal_policies.hpp"

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
	phoenix::test::fit_interdiction_goal_policies(source, {"test.away"});
	for (auto &tree : source["trees"])
	{
		if (tree["id"] == "test.encounter.planner")
			for (auto &binding : tree["objective_bindings"])
				if (binding["id"] == "engage")
				{
					binding["navigation"] = {{"mode", "relative"}, {"forward", -60000}, {"lateral", 0}};
					for (auto &condition : binding["available"])
						if (condition["left"].value("observation", std::string{}) == "objective.record.operation")
							condition["right"] = "cover";
				}
	}
	// Steering follows the same admitted course as propulsion. No native manoeuvre facts.
	for (const auto &tree : Json::parse(R"json([
 {"id":"test.cover-face","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"}],
  "controller_action":{"label":"Follow the admitted course","inputs":[{"name":"x","type":"integer"},{"name":"y","type":"integer"}]},
  "initial":"write","states":[{"id":"write","running":[{"action":"write.destination","arguments":[true,{"observation":"input.x"},{"observation":"input.y"}]},{"action":"action.succeed","arguments":["course_admitted"]}]}]},
 {"id":"test.cover-steering","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"}],
  "command_reads":[{"name":"course.id","controller":"course","channel":"course"},{"name":"course.revision","controller":"course","channel":"course","field":"objective_revision"},
   {"name":"course.x","controller":"course","channel":"course","field":"x"},{"name":"course.y","controller":"course","channel":"course","field":"y"},{"name":"course.accepted","controller":"course","channel":"course","field":"accepted"}],
  "action_bindings":[{"id":"face","tree":"test.cover-face"}],"objective_bindings":[],"initial":"watch","states":[{"id":"watch"}]},
 {"id":"test.cover-shoot","context":"controller","command_bindings":[{"channel":"target","action":"weapon_target"}],
  "controller_action":{"label":"Target an observed hostile","inputs":[{"name":"target","type":"integer"}]},
  "initial":"write","states":[{"id":"write","running":[{"action":"write.target","arguments":[{"observation":"input.target"}]},{"action":"action.succeed","arguments":["target_admitted"]}]}]},
 {"id":"test.cover-fire","context":"controller","contact_observations":true,"command_bindings":[{"channel":"target","action":"weapon_target"}],
  "action_bindings":[{"id":"shoot","tree":"test.cover-shoot"}],"objective_bindings":[{"id":"shoot","behavior":"engage","candidate_source":"contacts","action":"shoot",
   "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true},{"left":{"observation":"contact_candidate.hostile"},"op":"eq","right":true}],
   "target":{"observation":"contact_candidate.id"},"arguments":[{"observation":"contact_candidate.id"}],"factors":{"progress":1000,"travel":{"observation":"contact_candidate.distance"}}}],"initial":"watch","states":[{"id":"watch"}]}
 ])json"))
		source["trees"].push_back(tree);
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.cover-steering")
			for (const auto *behavior : {"board", "engage"})
				tree["objective_bindings"].push_back(
				    Json::parse(R"json({"id":"course","behavior":"board","action":"face",
 "available":[{"left":{"observation":"course.accepted"},"op":"eq","right":true},
 {"left":{"observation":"course.id"},"op":"eq","right":{"observation":"objective.record.id"}},
 {"left":{"observation":"course.revision"},"op":"eq","right":{"observation":"objective.record.revision"}}],
 "arguments":[{"observation":"course.x"},{"observation":"course.y"}],"factors":{"progress":1000}})json"));
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.cover-steering")
		{
			tree["objective_bindings"][1]["id"] = "cover";
			tree["objective_bindings"][1]["behavior"] = "engage";
		}
	// Explicit scenario publications, independent of the mission's native work executor.
	source["trees"].push_back(Json::parse(R"json({"id":"test.cover-objectives","context":"scenario","initial":"board",
 "states":[{"id":"board","entry":[{"action":"add_objective","arguments":["board",{"observation":"self.entity"},"board"]}],
 "transitions":[{"target":"cover","conditions":[{"left":{"observation":"event.name"},"op":"eq","right":"party.deployed"}]}]},
 {"id":"cover","entry":[{"action":"add_objective","arguments":["cover",{"observation":"self.entity"},"cover"]}],
 "transitions":[{"target":"return","conditions":[{"left":{"observation":"event.name"},"op":"eq","right":"party.complete"}]}]},
 {"id":"return","entry":[{"action":"add_objective","arguments":["return",{"observation":"self.entity"},"return"]}]}]})json"));
	source["maps"][0]["scenario"] =
	    Json::parse(R"json({"tree":"test.cover-objectives","bindings":{"actor":{"slot":"crew"}},
 "events":["party.deployed","party.complete"],"event_bindings":[
 {"kind":"away.deployed","subject":{"entity":1},"operation":"job","event":"party.deployed"},
 {"kind":"away.completed","subject":{"entity":1},"operation":"job","event":"party.complete"}],
 "fact_bindings":[{"id":"deployed","kind":"away","subject":{"slot":"crew"},"operation":"job","field":"ever_deployed"},
 {"id":"complete","kind":"event","event":"party.complete"},{"id":"recovered","kind":"away","subject":{"slot":"crew"},"operation":"job","field":"recovered"}],
 "objectives":[
 {"id":"board","type":"condition","behavior":"board","tier":"scripted_high","behavior_target":{"entity":1},"title_key":"Board the offered job","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"deployed","value":true}},
 {"id":"cover","type":"condition","behavior":"engage","operation":"cover","tier":"objective_combat","title_key":"Cover the deployed party","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"complete","value":true}},
 {"id":"return","type":"condition","behavior":"board","tier":"scripted_high","behavior_target":{"entity":1},"title_key":"Recover the party","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"recovered","value":true}}]})json");
}
