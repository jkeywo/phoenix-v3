#pragma once
#include "controller_fixture.hpp"
namespace phoenix::test
{
inline Json combat_fixture()
{
	auto source = controller_fixture();
	auto ship = source["entities"][0];
	ship["id"] = "test.combat";
	ship.erase("motion_model");
	ship["tags"] = {"ship"};
	ship["render"]["radar_icon"] = "ship";
	ship["render"]["sprite"] = {
	    {"albedo", "test-core:assets/pixel.png"},
	    {"radar_icon", "ship"},
	    {"frames",
	     Json::array({{{"name", "single"},
	                   {"rect", {0, 0, 1, 1}},
	                   {"pivot", {0, 0}},
	                   {"azimuth", 0},
	                   {"depth_scale", 0},
	                   {"depth_offset", 0},
	                   {"attachments", {{"mount", {{"x", 0}, {"y", 0}, {"layer", 1}, {"depth_offset", 0}}}}}}})}};
	ship["render"]["images"] = Json::array();
	ship["render"]["mode"] = "camera";
	ship["collision"] = {{"shape", "box"}, {"mass_grams", 1000000}, {"half_width_mm", 10000}, {"half_length_mm", 5000}};
	const Json projectile = {{"visual", "test.visual"}, {"speed_mm_s", 100000}, {"lifetime_ticks", 50},
	                         {"penetration", 1000},     {"damage", 40},         {"damage_per_m", 2}};
	ship["systems"] = {
	    {"radar", {{"kind", "radar"}, {"range_mm", 200000}, {"shows", {"ship", "asteroid"}}, {"compartment", "fore"}}},
	    {"mount",
	     {{"kind", "turret"},
	      {"arc_mdeg", 270000},
	      {"traverse_mdeg_s", 90000},
	      {"marker", "mount"},
	      {"visual", "test.visual"},
	      {"compartment", "fore"}}},
	    {"mag",
	     {{"kind", "magazine"},
	      {"capacity", 3},
	      {"reload", {{"strategy", "infinite"}, {"ticks", 10}, {"amount", 2}}},
	      {"compartment", "fore"}}},
	    {"gun",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"second",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"team-a", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}},
	    {"team-b", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}}};
	ship["combat"] = {{"repair_console", "observer"},
	                  {"decks", Json::array({{{"id", "deck"},
	                                          {"weight", 1},
	                                          {"compartments", Json::array({{{"id", "fore"},
	                                                                         {"x_mm", 5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}},
	                                                                        {{"id", "aft"},
	                                                                         {"x_mm", -5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}}})}}})},
	                  {"connections", Json::array({{{"from", "fore"}, {"to", "aft"}, {"travel_ticks", 10}}})},
	                  {"destruction",
	                   {{"visual", "test.visual"},
	                    {"sound", "test-core:assets/pixel.png"},
	                    {"growth_ticks", 5},
	                    {"fade_ticks", 10},
	                    {"size_mm", 10000}}}};
	Json controllers = Json::array(), drive = Json::array(), support = Json::array();
	for (auto it = ship["systems"].begin(); it != ship["systems"].end(); ++it)
	{
		const auto kind = it.value().at("kind");
		const std::string console = kind == "repair_team" ? "observer" : "drive";
		Json commands =
		    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {it.key()}}}});
		if (kind == "weapon")
			commands.push_back({{"id", "target"}, {"action", "weapon_target"}, {"consumers", {it.key()}}});
		if (kind == "repair_team")
			commands.push_back({{"id", "assignment"}, {"action", "repair_compartment"}, {"consumers", {it.key()}}});
		controllers.push_back({{"id", it.key()}, {"console", console}, {"fallback", "hold"}, {"commands", commands}});
		(console == "drive" ? drive : support).push_back(it.key());
	}
	ship["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles",
	     Json::array(
	         {{{"id", "direct"}, {"consoles", {{"drive", {{"human", drive}}}, {"observer", {{"human", support}}}}}}})}};
	ship["stations"][0]["consoles"][0]["feeds"] = {"gun", "second", "mount", "mag", "radar"};
	ship["stations"][0]["consoles"][1]["feeds"] = {"team-a", "team-b", "radar"};
	Json visual = source["entities"][0];
	visual["id"] = "test.visual";
	visual.erase("control_layout");
	visual.erase("stations");
	visual["systems"] = Json::object();
	visual["collision"] = {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}};
	source["entities"] = Json::array({ship, visual});
	source["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 2}, {"definition", "test.combat"}, {"x_mm", 60000}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 3}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 60000}, {"heading_mdeg", 0}}});
	return source;
}
} // namespace phoenix::test
