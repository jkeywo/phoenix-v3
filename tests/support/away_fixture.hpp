#pragma once
#include "controller_fixture.hpp"
namespace phoenix::test
{
inline Json away_fixture()
{
	auto source = controller_fixture();
	auto &s = source["entities"][0];
	s["id"] = "test.away";
	s["systems"] = {{"radio",
	                 {{"kind", "comms"},
	                  {"range_mm", 100000},
	                  {"energy_per_second", 20},
	                  {"power_store", "cell"},
	                  {"compartment", "bridge"}}},
	                {"transport",
	                 {{"kind", "displacement"},
	                  {"range_mm", 20000},
	                  {"capacity", 4},
	                  {"transfer_ticks", 3},
	                  {"energy_per_second", 20},
	                  {"power_store", "cell"},
	                  {"compartment", "bridge"}}},
	                {"dispatch", {{"kind", "dispatch"}, {"compartment", "bridge"}}},
	                {"team", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "workshop"}}}};
	s["systems"]["engine"] = {
	    {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}, {"compartment", "bridge"}};
	s["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}, {"compartment", "bridge"}};
	s["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                           {"capacity", 100000},
	                                           {"initial", 100000},
	                                           {"charge_rate", 10000},
	                                           {"discharge_rate", 10000}}})}};
	s["combat"] = {{"repair_console", "observer"},
	               {"decks", Json::array({{{"id", "deck"},
	                                       {"weight", 1},
	                                       {"compartments", Json::array({{{"id", "bridge"},
	                                                                      {"x_mm", 1000},
	                                                                      {"y_mm", 0},
	                                                                      {"half_width_mm", 1000},
	                                                                      {"half_length_mm", 1000},
	                                                                      {"hp", 100},
	                                                                      {"resistance_per_m", 1},
	                                                                      {"recovery_hp", 10}},
	                                                                     {{"id", "workshop"},
	                                                                      {"x_mm", -1000},
	                                                                      {"y_mm", 0},
	                                                                      {"half_width_mm", 1000},
	                                                                      {"half_length_mm", 1000},
	                                                                      {"hp", 100},
	                                                                      {"resistance_per_m", 1},
	                                                                      {"recovery_hp", 10}}})}}})},
	               {"connections", Json::array({{{"from", "bridge"}, {"to", "workshop"}, {"travel_ticks", 3}}})}};
	s["combat"]["destruction"] = {{"visual", "test.away"},
	                              {"sound", "test-core:assets/pixel.png"},
	                              {"growth_ticks", 5},
	                              {"fade_ticks", 10},
	                              {"size_mm", 1000}};
	s["stations"] = Json::array(
	    {{{"id", "helm"},
	      {"consoles", Json::array({{{"id", "drive"},
	                                 {"definition", "test.console"},
	                                 {"systems", Json::array()},
	                                 {"feeds", {"radio", "transport", "dispatch", "team", "engine", "steering"}}}})}},
	     {{"id", "engineering"},
	      {"consoles", Json::array({{{"id", "observer"},
	                                 {"definition", "test.console"},
	                                 {"systems", Json::array()},
	                                 {"feeds", {"team"}}}})}}});
	Json controllers = Json::array();
	for (const auto &key : {"radio", "transport", "dispatch", "team", "engine", "steering"})
		controllers.push_back(
		    {{"id", key},
		     {"console", std::string(key) == "team" ? "observer" : "drive"},
		     {"fallback", "hold"},
		     {"commands",
		      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {key}}}})}});
	s["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles", Json::array({{{"id", "direct"},
	                               {"consoles",
	                                {{"drive", {{"human", {"radio", "transport", "dispatch", "engine", "steering"}}}},
	                                 {"observer", {{"human", {"team"}}}}}}}})}};
	s["personnel"] = {
	    {"departments", Json::array({{{"id", "navigation"}, {"label", "Navigation"}, {"station", "helm"}},
	                                 {{"id", "engineering"}, {"label", "Engineering"}, {"station", "engineering"}}})},
	    {"traits", Json::array({{{"id", "navigator"}, {"label", "Navigator"}, {"bonuses", Json::object()}},
	                            {{"id", "repair"}, {"label", "Engineer"}, {"bonuses", {{"repair", 250}}}}})},
	    {"duties", Json::array()},
	    {"officers", Json::array({{{"id", "nav"},
	                               {"name", "Navigator"},
	                               {"department", "navigation"},
	                               {"compartment", "bridge"},
	                               {"duty", "transport"},
	                               {"traits", {"navigator"}}},
	                              {{"id", "chief"},
	                               {"name", "Chief"},
	                               {"department", "engineering"},
	                               {"compartment", "workshop"},
	                               {"duty", "team-duty"},
	                               {"traits", {"repair"}}}})}};
	for (const auto &key : {"radio", "transport", "dispatch", "team", "engine", "steering"})
		s["personnel"]["duties"].push_back({{"id", key},
		                                    {"label", key},
		                                    {"controller", key},
		                                    {"console", std::string(key) == "team" ? "observer" : "drive"},
		                                    {"department", std::string(key) == "team" ? "engineering" : "navigation"},
		                                    {"compartment", "bridge"},
		                                    {"effects", Json::object()}});
	s["personnel"]["duties"][1]["required_trait"] = "navigator";
	s["personnel"]["duties"].push_back({{"id", "team-duty"},
	                                    {"label", "Team"},
	                                    {"team", "team"},
	                                    {"department", "engineering"},
	                                    {"compartment", "workshop"},
	                                    {"effects", {{"team", {"repair"}}}}});
	s["comms"] = {{"characters", Json::array({{{"id", "leader"}, {"name_key", "Remote leader"}, {"public", true}}})}};
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.dialogue","context":"conversation","initial":"start","lines":{"intro":{"speaker":"team","text_key":"Mission available"},"done":{"speaker":"team","text_key":"Work complete"}},"prompts":{"choice":{"speaker":"team","text_key":"Proceed?","timeout_ticks":30,"default_choice":"yes","choices":[{"id":"yes","text_key":"Proceed"}]}},"states":[{"id":"start","entry":[{"action":"say","arguments":["intro"]}],"transitions":[{"target":"ask","conditions":[{"left":{"observation":"event"},"op":"eq","right":"arrived"}]}]},{"id":"ask","entry":[{"action":"offer","arguments":["choice"]}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"event"},"op":"eq","right":"done"}]}]},{"id":"done","entry":[{"action":"say","arguments":["done"]}]}]})"));
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.away-tree","context":"away","initial":"wait","states":[{"id":"wait","transitions":[{"target":"work","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},{"id":"work","entry":[{"action":"checkpoint","arguments":["repair"]},{"action":"report","arguments":["arrived"]},{"action":"work","arguments":[40]}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true},{"left":{"observation":"answer"},"op":"eq","right":"yes"}]}]},{"id":"done","entry":[{"action":"report","arguments":["done"]},{"action":"complete","arguments":["repaired"]}]}]})"));
	auto &map = source["maps"][0];
	map["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.away"}, {"x_mm", 10000}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	map["ship_slots"] = Json::array(
	    {{{"id", "crew"}, {"allowed_ships", {"test.away"}}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	map["conversations"] =
	    Json::array({{{"id", "channel"},
	                  {"tree", "test.dialogue"},
	                  {"bindings", {{"ship", {{"slot", "crew"}}}, {"team", {{"entity", 1}, {"character", "leader"}}}}},
	                  {"members", {"ship", "team"}}}});
	map["away_missions"] =
	    Json::array({{{"id", "job"},
	                  {"title_key", "Repair job"},
	                  {"ship_slot", "crew"},
	                  {"target_entity", 1},
	                  {"conversation", "channel"},
	                  {"reveal_event", "Mission available"},
	                  {"tree", "test.away-tree"},
	                  {"slots", Json::array({{{"id", "repair"}, {"kind", "team"}, {"required", true}}})}}});
	return source;
}
} // namespace phoenix::test
