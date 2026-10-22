#pragma once
#include "away_fixture.hpp"
#include "craft_policy.hpp"
namespace phoenix::test
{
inline Json craft_fixture()
{
	auto source = away_fixture();
	auto craft = source["entities"][0];
	craft["id"] = "test.shuttle";
	craft.erase("personnel");
	craft.erase("comms");
	craft["craft"] = {{"role", "shuttle"}, {"cruise_altitude_mm", 1000}, {"return_reserve_permille", 0}};
	craft["half_height_mm"] = 500;
	craft["collision"] = {{"shape", "circle"}, {"radius_mm", 500}, {"mass_grams", 1000}};
	craft["transport"] = {
	    {"capacity", 4}, {"capabilities", {"shelter", "tools"}}, {"capability_systems", {{"tools", {"radio"}}}}};
	const auto carrier_systems = craft["systems"];
	craft["systems"] = Json::object();
	for (const auto *key : {"engine", "steering", "radio"})
		craft["systems"][key] = carrier_systems.at(key);
	craft["systems"]["engine"]["max_speed_mm_s"] = 40000;
	craft["systems"]["engine"]["acceleration_mm_s2"] = 20000;
	craft["systems"]["engine"]["power_store"] = "cell";
	craft["systems"]["engine"]["energy_per_second"] = 20;
	craft["resources"]["stores"][0]["initial"] = 50000;
	craft["combat"]["decks"][0]["compartments"] = Json::array({craft["combat"]["decks"][0]["compartments"][0]});
	craft["combat"]["connections"] = Json::array();
	craft["stations"] = Json::array(
	    {{{"id", "operations"},
	      {"consoles",
	       Json::array({{{"id", "drive"},
	                     {"definition", "test.console"},
	                     {"systems", Json::array()},
	                     {"feeds", {"engine", "steering", "radio"}}},
	                    {{"id", "observer"}, {"definition", "test.console"}, {"systems", Json::array()}}})}}});
	Json commands = Json::array();
	for (const auto &[id, action, consumer] :
	     std::vector<std::tuple<std::string, std::string, std::string>>{{"speed", "speed_mm_s", "engine"},
	                                                                    {"heading", "heading_mdeg", "steering"},
	                                                                    {"height", "altitude_mm", "engine"}})
		commands.push_back({{"id", id}, {"action", action}, {"initial", 0}, {"consumers", {consumer}}});
	craft["control_layout"] = {
	    {"schema", 1},
	    {"controllers",
	     Json::array(
	         {{{"id", "flight"}, {"console", "drive"}, {"tree", "test.craft-flight"}, {"commands", commands}}})},
	    {"default_profile", "automatic"},
	    {"profiles", Json::array({{{"id", "automatic"}, {"consoles", {{"drive", {{"ai", {"flight"}}}}}}}})}};
	Json tree{
	    {"id", "test.craft-flight"},
	    {"context", "controller"},
	    {"commands", commands},
	    {"initial", "fly"},
	    {"states",
	     Json::array(
	         {{{"id", "fly"},
	           {"running",
	            Json::array(
	                {{{"action", "write.speed"}, {"arguments", Json::array({{{"observation", "craft.speed"}}})}},
	                 {{"action", "write.heading"}, {"arguments", Json::array({{{"observation", "craft.heading"}}})}},
	                 {{"action", "write.height"},
	                  {"arguments", Json::array({{{"observation", "craft.altitude"}}})}}})}}})}};
	auto &bindings = tree["command_bindings"] = Json::array();
	for (const auto &c : commands)
		bindings.push_back({{"channel", c.at("id")}, {"action", c.at("action")}});
	for (const auto &definition : craft_test_policy(tree))
		source["trees"].push_back(definition);
	craft["control_layout"]["controllers"][0]["commands"].push_back(
	    {{"id", "radio"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"radio"}}});
	source["entities"].push_back(craft);
	auto &carrier = source["entities"][0];
	carrier["systems"]["bay"] = {{"kind", "shuttle_bay"},       {"slots", 2},
	                             {"compartment", "bridge"},     {"initial", {"test.shuttle", "test.shuttle"}},
	                             {"transfer_ticks", 2},         {"docking_range_mm", 12000},
	                             {"docking_speed_mm_s", 15000}, {"power_store", "cell"},
	                             {"charge_per_second", 200}};
	carrier["control_layout"]["controllers"].push_back(
	    {{"id", "bay"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"bay"}}}})}});
	carrier["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("bay");
	carrier["personnel"]["duties"].push_back({{"id", "bay-duty"},
	                                          {"label", "Flight operations"},
	                                          {"controller", "bay"},
	                                          {"console", "drive"},
	                                          {"department", "navigation"},
	                                          {"compartment", "bridge"},
	                                          {"effects", Json::object()}});
	carrier["stations"][0]["consoles"][0]["feeds"].push_back("bay");
	carrier["transport"] = {{"compartment", "bridge"}, {"capacity", 8}, {"capabilities", {"shelter"}}};
	source["maps"][0]["away_missions"][0]["deployment_range_mm"] = 15000;
	source["maps"][0]["away_missions"][0]["deployment_ticks"] = 2;
	return source;
}
inline Json armed_craft_fixture()
{
	auto source = craft_fixture();
	Json radar{{"kind", "radar"},
	           {"range_mm", 300000},
	           {"shows", {"ship", "missile"}},
	           {"capabilities", {{"basic_identity", 0}}},
	           {"compartment", "bridge"}};
	auto &carrier = source["entities"][0];
	carrier["tags"] = {"ship"};
	carrier["systems"]["radar"] = radar;
	carrier["stations"][0]["consoles"][0]["feeds"].push_back("radar");
	carrier["control_layout"]["controllers"][0]["commands"].push_back(
	    {{"id", "radar"}, {"action", "radar_enabled"}, {"initial", 1}, {"consumers", {"radar"}}});
	carrier["systems"]["supply"] = {{"kind", "magazine"},
	                                {"capacity", 30},
	                                {"initial", 20},
	                                {"ammunition_type", "test.missile"},
	                                {"compartment", "bridge"}};
	carrier["control_layout"]["controllers"][0]["commands"].push_back(
	    {{"id", "supply"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"supply"}}});
	carrier["systems"]["bay"]["slots"] = 4;
	carrier["systems"]["bay"]["initial"].push_back("test.interceptor");
	carrier["systems"]["bay"]["initial"].push_back("test.bomber");
	carrier["systems"]["bay"]["ammunition"] =
	    Json::array({{{"source", "supply"}, {"destination", "magazine"}, {"rounds_per_second", 20}}});
	const auto shuttle = source["entities"].back();
	for (const auto *role : {"interceptor", "bomber"})
	{
		auto craft = shuttle;
		craft["id"] = "test." + std::string(role);
		craft["tags"] = {"ship"};
		craft["craft"]["role"] = role;
		craft["systems"]["radar"] = radar;
		craft["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "radar"}, {"action", "radar_enabled"}, {"initial", 1}, {"consumers", {"radar"}}});
		craft["stations"][0]["consoles"][0]["feeds"].push_back("radar");
		craft["systems"]["magazine"] = {{"kind", "magazine"},
		                                {"capacity", 3},
		                                {"initial", 2},
		                                {"ammunition_type", "test.missile"},
		                                {"compartment", "bridge"}};
		Json payload{
		    {"visual", "test.shuttle"}, {"speed_mm_s", 40000}, {"lifetime_ticks", 100}, {"damage", 1},
		    {"penetration", 1},         {"damage_per_m", 1},   {"durability", 5},       {"guidance", "homing"}};
		craft["systems"]["weapon"] = {{"kind", "weapon"}, {"magazine", "magazine"}, {"projectile", payload},
		                              {"fire_ticks", 2},  {"range_mm", 100000},     {"compartment", "bridge"}};
		craft["stations"][0]["consoles"][0]["feeds"].push_back("weapon");
		craft["stations"][0]["consoles"][0]["feeds"].push_back("magazine");
		if (std::string(role) == "interceptor")
		{
			craft["systems"]["weapon"]["point_defence"] = true;
			craft["systems"]["weapon"]["power_store"] = "cell";
			craft["systems"]["weapon"]["auto_energy_per_second"] = 20;
		}
		auto &commands = craft["control_layout"]["controllers"][0]["commands"];
		commands.push_back({{"id", "target"}, {"action", "weapon_target"}, {"initial", 0}, {"consumers", {"weapon"}}});
		auto tree = source["trees"].back();
		tree["id"] = "test." + std::string(role) + "-flight";
		// Find the ordinary flight tree, independently of insertion order.
		for (const auto &t : source["trees"])
			if (t.at("id") == "test.craft-flight")
				tree = t;
		tree["id"] = "test." + std::string(role) + "-flight";
		tree["command_bindings"].push_back({{"channel", "target"}});
		tree["states"][0]["running"].push_back(
		    {{"action", "write.target"}, {"arguments", Json::array({{{"observation", "craft.target"}}})}});
		if (std::string(role) == "interceptor")
		{
			commands.push_back({{"id", "mode"}, {"action", "weapon_mode"}, {"initial", 1}, {"consumers", {"weapon"}}});
			tree["command_bindings"].push_back({{"channel", "mode"}});
		}
		craft["control_layout"]["controllers"][0]["tree"] = tree["id"];
		for (const auto &definition : craft_test_policy(tree))
			source["trees"].push_back(definition);
		source["entities"].push_back(craft);
	}
	for (auto &entity : source["entities"])
	{
		const std::string system = entity["systems"].contains("supply") ? "supply" : "magazine";
		if (!entity["systems"].contains(system))
			continue;
		const auto &mag = entity["systems"][system];
		entity["logistics"] = {{"controller", system == "supply" ? "engine" : "flight"},
		                       {"units_per_second", 100},
		                       {"materials", Json::array({{{"id", "test.missile"}}})},
		                       {"stores", Json::array({{{"id", "ammunition"},
		                                                {"system", system},
		                                                {"compartment", "bridge"},
		                                                {"capacity", mag.at("capacity")},
		                                                {"accepts", {"test.missile"}}}})}};
	}
	auto &map = source["maps"][0];
	map["factions"] = Json::array({{{"id", "crew"}, {"label", "Crew"}}, {{"id", "enemy"}, {"label", "Enemy"}}});
	map["relationships"] = Json::array({{{"a", "crew"}, {"b", "enemy"}, {"relation", "hostile"}}});
	map["entities"][0]["faction"] = "enemy";
	map["entities"][0]["x_mm"] = 50000;
	map["ship_slots"][0]["faction"] = "crew";
	auto &attacker = source["entities"][0];
	attacker["systems"]["enemy-mag"] = {{"kind", "magazine"}, {"capacity", 6}, {"compartment", "bridge"}};
	attacker["systems"]["enemy-weapon"] = {{"kind", "weapon"},
	                                       {"magazine", "enemy-mag"},
	                                       {"range_mm", 200000},
	                                       {"fire_ticks", 10},
	                                       {"compartment", "bridge"},
	                                       {"projectile",
	                                        {{"visual", "test.shuttle"},
	                                         {"speed_mm_s", 20000},
	                                         {"lifetime_ticks", 150},
	                                         {"durability", 5},
	                                         {"guidance", "homing"},
	                                         {"damage", 1},
	                                         {"penetration", 1},
	                                         {"damage_per_m", 1}}}};
	attacker["control_layout"]["controllers"].push_back(
	    {{"id", "enemy"},
	     {"console", "drive"},
	     {"tree", "test.enemy-missiles"},
	     {"commands",
	      Json::array(
	          {{{"id", "target"}, {"action", "weapon_target"}, {"initial", 0}, {"consumers", {"enemy-weapon"}}}})}});
	attacker["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"] = {"enemy"};
	attacker["personnel"]["duties"].push_back({{"id", "enemy"},
	                                           {"label", "Missile test"},
	                                           {"controller", "enemy"},
	                                           {"console", "drive"},
	                                           {"department", "navigation"},
	                                           {"compartment", "bridge"},
	                                           {"effects", Json::object()}});
	source["trees"].push_back(
	    {{"id", "test.enemy-missiles"},
	     {"context", "controller"},
	     {"command_bindings", Json::array({{{"channel", "target"}}})},
	     {"initial", "fire"},
	     {"states", Json::array({{{"id", "fire"},
	                              {"running", Json::array({{{"action", "write.target"}, {"arguments", {2}}}})}}})}});
	return source;
}
} // namespace phoenix::test
