#pragma once
#include "boarding_fixture.hpp"
#include "ftl_fixture.hpp"
namespace phoenix::test
{
inline Json interdiction_fixture(int ships = 1)
{
	auto f = boarding_fixture();
	fit_ftl(f, "helm", "observer");
	auto &e = f["entities"][0];
	for (auto &d : e["personnel"]["duties"])
		if (d["id"] == "jump-feed")
			d["department"] = "engineering";
	e["faction"] = "crew";
	e["systems"]["radio"]["range_mm"] = 1000000;
	e["tags"] = {"ship"};
	e["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 1000000}, {"shows", {"ship"}}, {"compartment", "bridge"}};
	e["systems"]["magazine"] = {{"kind", "magazine"}, {"capacity", 10}, {"compartment", "bridge"}};
	e["systems"]["gun"] = {{"kind", "weapon"},
	                       {"magazine", "magazine"},
	                       {"arc_mdeg", 360000},
	                       {"fire_ticks", 1},
	                       {"compartment", "bridge"},
	                       {"projectile",
	                        {{"visual", "test.away"},
	                         {"speed_mm_s", 10000},
	                         {"lifetime_ticks", 1},
	                         {"penetration", 1},
	                         {"damage", 1},
	                         {"damage_per_m", 1}}}};
	for (const auto *id : {"radar", "magazine", "gun"})
	{
		Json commands =
		    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {id}}}});
		if (std::string(id) == "gun")
			commands.push_back({{"id", "target"}, {"action", "weapon_target"}, {"consumers", {id}}});
		e["control_layout"]["controllers"].push_back(
		    {{"id", id}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
		e["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back(id);
		e["stations"][0]["consoles"][0]["feeds"].push_back(id);
		e["personnel"]["duties"].push_back(
		    {{"id", id}, {"label", id}, {"controller", id}, {"department", "navigation"}, {"compartment", "bridge"}});
	}
	for (auto &c : e["control_layout"]["controllers"])
		if (c["id"] == "engine")
		{
			c["commands"].push_back({{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"engine"}}});
			c["commands"].push_back({{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"steering"}}});
		}
	e["docking"] = Json::parse(R"({"ports":[
 {"id":"fore","controller":"engine","compartment":"bridge","compatibility":"test","x_mm":5000,"heading_mdeg":0,"mating_ticks":3,"separation_ticks":3,"auto_accept_allies":true,"supply":true},
 {"id":"aft","controller":"engine","compartment":"bridge","compatibility":"test","x_mm":-5000,"heading_mdeg":180000,"mating_ticks":3,"separation_ticks":3,"auto_accept_allies":true,"supply":true}]})");
	auto target = e;
	target["id"] = "test.target";
	target["systems"]["occupation"] = {{"kind", "team"},
	                                   {"type", "security"},
	                                   {"capabilities", {"security"}},
	                                   {"compartment", "workshop"},
	                                   {"enabled", false}};
	auto occupation = target["control_layout"]["controllers"].back();
	occupation["id"] = "occupation";
	occupation["console"] = "observer";
	occupation["commands"] =
	    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 0}, {"consumers", {"occupation"}}}});
	target["control_layout"]["controllers"].push_back(occupation);
	target["control_layout"]["profiles"][0]["consoles"]["observer"]["ai"] = {"occupation"};
	target["stations"][1]["consoles"][0]["feeds"].push_back("occupation");
	target["personnel"]["duties"].push_back({{"id", "occupation"},
	                                         {"label", "Occupation"},
	                                         {"controller", "occupation"},
	                                         {"department", "engineering"},
	                                         {"compartment", "workshop"}});
	target["personnel"]["duties"].push_back({{"id", "occupation-team"},
	                                         {"label", "Occupation team"},
	                                         {"team", "occupation"},
	                                         {"department", "engineering"},
	                                         {"compartment", "workshop"}});
	auto pirate = e;
	pirate["id"] = "test.pirate";
	pirate["faction"] = "pirates";
	f["entities"].push_back(target);
	f["entities"].push_back(pirate);
	auto &map = f["maps"][0];
	map["factions"] = Json::array({{{"id", "crew"}, {"label", "Crew"}}, {{"id", "pirates"}, {"label", "Pirates"}}});
	map["relationships"] = Json::array({{{"a", "crew"}, {"b", "pirates"}, {"relation", "hostile"}}});
	map["entities"][0]["definition"] = "test.target";
	map["entities"][0]["x_mm"] = 30000;
	map["ship_slots"][0]["x_mm"] = -200000;
	f["trees"].push_back(Json::parse(R"({"id":"test.demand","context":"conversation","initial":"idle",
 "lines":{"warning":{"speaker":"pirate","text_key":"Last warning"},"plea":{"speaker":"engineer","text_key":"Help us"}},
 "prompts":{"demand":{"speaker":"pirate","text_key":"Leave the target","choices":[{"id":"withdraw","text_key":"Accept"},{"id":"refuse","text_key":"Refuse"}],"ai_choice":"refuse","ai_delay_ticks":1000}},"states":[{"id":"idle"}]})"));
	f["trees"].push_back(
	    Json::parse(R"({"id":"test.interdiction","context":"scenario","interdiction":true,"initial":"wait","states":[
 {"id":"wait","transitions":[{"target":"demand","conditions":[{"left":{"observation":"started"},"op":"eq","right":true}]}]},
 {"id":"demand","entry":[{"action":"spawn","arguments":[""]},{"action":"offer","arguments":["demand"]}],"transitions":[
 {"target":"hostile","conditions":[{"left":{"observation":"attacked"},"op":"eq","right":true}]},
 {"target":"hostile","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"refuse"}]},
 {"target":"truce","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"withdraw"}]},
 {"target":"warning","conditions":[{"left":{"elapsed":true},"op":"ge","right":5}]}]},
 {"id":"warning","entry":[{"action":"say","arguments":["warning"]}],"transitions":[
 {"target":"hostile","conditions":[{"left":{"observation":"attacked"},"op":"eq","right":true}]},
 {"target":"hostile","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"refuse"}]},
 {"target":"truce","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"withdraw"}]},
 {"target":"hostile","conditions":[{"left":{"elapsed":true},"op":"ge","right":5}]}]},
 {"id":"truce","entry":[{"action":"posture","arguments":["truce"]}],"transitions":[
 {"target":"hostile","conditions":[{"left":{"observation":"attacked"},"op":"eq","right":true}]},
 {"target":"hostile","conditions":[{"left":{"observation":"reinforced"},"op":"eq","right":true}]},
 {"target":"taken","conditions":[{"left":{"observation":"takeover_ready"},"op":"eq","right":true}]}]},
 {"id":"hostile","entry":[{"action":"posture","arguments":["hostile"]}],"transitions":[{"target":"taken","conditions":[{"left":{"observation":"takeover_ready"},"op":"eq","right":true}]}]},
 {"id":"taken","entry":[{"action":"takeover","arguments":[""]}]}]})"));
	Json fleet = Json::array();
	for (int n = 0; n < ships; ++n)
		fleet.push_back({{"definition", "test.pirate"},
		                 {"x_mm", 60000 + n * 40000},
		                 {"y_mm", n * 40000},
		                 {"heading_mdeg", 180000}});
	map["interdictions"] =
	    Json::array({{{"id", "takeover"},
	                  {"player_slot", "crew"},
	                  {"target_entity", 1},
	                  {"target_character", "leader"},
	                  {"start_event", ""},
	                  {"tree", "test.interdiction"},
	                  {"dialogue", "test.demand"},
	                  {"threat_radius_mm", 10000},
	                  {"clearance_radius_mm", 15000},
	                  {"engagement_radius_mm", 500000},
	                  {"formation_radius_mm", 40000},
	                  {"takeover_ticks", 30},
	                  {"occupation_teams", {"occupation"}},
	                  {"pleas", Json::array({{{"ticks", 10}, {"line", "plea"}}, {{"ticks", 20}, {"line", "plea"}}})},
	                  {"variants", {{"default", fleet}}}}});
	return f;
}
} // namespace phoenix::test
