#pragma once
#include "away_fixture.hpp"
namespace phoenix::test
{
inline Json boarding_fixture()
{
	auto s = away_fixture();
	s["trees"].push_back(Json::parse(
	    R"({"id":"test.clinical","context":"team","initial":"work","states":[{"id":"work","entry":[{"action":"treat"}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},{"id":"done","entry":[{"action":"complete"}]}]})"));
	auto &ship = s["entities"][0];
	for (const auto *id : {"security", "medical"})
	{
		ship["systems"][id] = {{"kind", "team"}, {"type", id}, {"capabilities", {id}}, {"compartment", "workshop"}};
		ship["control_layout"]["controllers"].push_back(
		    {{"id", id},
		     {"console", "observer"},
		     {"fallback", "hold"},
		     {"commands",
		      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {id}}}})}});
		ship["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back(id);
		ship["stations"][1]["consoles"][0]["feeds"].push_back(id);
		ship["personnel"]["duties"].push_back({{"id", std::string("control-") + id},
		                                       {"label", id},
		                                       {"controller", id},
		                                       {"console", "observer"},
		                                       {"department", "engineering"},
		                                       {"compartment", "workshop"},
		                                       {"effects", Json::object()}});
		ship["personnel"]["duties"].push_back({{"id", std::string("team-") + id},
		                                       {"label", id},
		                                       {"team", id},
		                                       {"department", "engineering"},
		                                       {"compartment", "workshop"},
		                                       {"effects", Json::object()}});
	}
	ship["systems"]["medical"]["treatment_ticks"] = 8;
	ship["systems"]["medical"]["orders"] = Json::array({{{"id", "treat"},
	                                                     {"label", "Treat"},
	                                                     {"target", "patients"},
	                                                     {"capability", "medical"},
	                                                     {"patient_states", {"minor", "severe"}},
	                                                     {"tree", "test.clinical"}}});
	s["maps"][0]["away_missions"][0]["slots"].push_back(
	    {{"id", "security"}, {"kind", "team"}, {"capability", "security"}, {"required", false}});
	return s;
}
inline Json boarding_mission_fixture()
{
	auto s = boarding_fixture();
	auto &ship = s["entities"][0];
	ship["tags"] = {"ship"};
	ship["systems"]["scan"] = {{"kind", "radar"}, {"range_mm", 100000}, {"shows", {"ship"}}, {"compartment", "bridge"}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "scan"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"scan"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("scan");
	ship["stations"][0]["consoles"][0]["feeds"].push_back("scan");
	ship["personnel"]["duties"].push_back({{"id", "scan"},
	                                       {"label", "Scan"},
	                                       {"controller", "scan"},
	                                       {"console", "drive"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"},
	                                       {"effects", Json::object()}});
	s["trees"].push_back(Json::parse(
	    R"({"id":"test.boarding-dialogue","context":"conversation","initial":"wait","lines":{"intro":{"speaker":"party","text_key":"Boarding party ready"},"assessment":{"speaker":"party","text_key":"Defenders encountered. Assessment available."}},"prompts":{"objective":{"speaker":"party","text_key":"Choose a boarding objective","choices":[{"id":"bridge","text_key":"Seize the bridge"},{"id":"reactor","text_key":"Shut down the reactor"}],"timeout_ticks":8,"default_choice":"bridge"}},"states":[{"id":"wait","entry":[{"action":"say","arguments":["intro"]}],"transitions":[{"target":"choose","conditions":[{"left":{"observation":"event"},"op":"eq","right":"arrived"}]}]},{"id":"choose","entry":[{"action":"offer","arguments":["objective"]}]}]})"));
	s["trees"].push_back(Json::parse(
	    R"({"id":"test.boarding","context":"away","boarding":true,"initial":"wait","states":[{"id":"wait","transitions":[{"target":"choose","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},{"id":"choose","entry":[{"action":"report","arguments":["arrived"]}],"transitions":[{"target":"bridge","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"bridge"}]},{"target":"reactor","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"reactor"}]}]},{"id":"bridge","entry":[{"action":"encounter","arguments":["guards"]}],"transitions":[{"target":"seize","conditions":[{"left":{"observation":"encounter_done"},"op":"eq","right":true},{"left":{"observation":"encounter_success"},"op":"eq","right":true}]},{"target":"failed","conditions":[{"left":{"observation":"encounter_done"},"op":"eq","right":true},{"left":{"observation":"encounter_success"},"op":"eq","right":false}]}]},{"id":"seize","entry":[{"action":"secure","arguments":["bridge"]},{"action":"capture","arguments":["bridge"]},{"action":"complete","arguments":["captured"]}]},{"id":"reactor","entry":[{"action":"move_to","arguments":["workshop"]}],"transitions":[{"target":"shutdown","conditions":[{"left":{"observation":"arrived"},"op":"eq","right":true}]}]},{"id":"shutdown","entry":[{"action":"secure","arguments":["reactor"]},{"action":"complete","arguments":["reactor_stopped"]}]},{"id":"failed","entry":[{"action":"fail","arguments":["repelled"]}]}]})"));
	auto target = ship;
	target["id"] = "test.boardable";
	// A supported real generator, sharing the ship's existing cell.
	target["resources"]["generators"] = Json::array({{{"id", "reactor"},
	                                                  {"store", "cell"},
	                                                  {"maximum_rate", 1000},
	                                                  {"initial_output", 1000},
	                                                  {"response_rate", 1000}}});
	target["systems"]["reactor"] = {{"kind", "generator"}, {"generator", "reactor"}, {"compartment", "workshop"}};
	target["stations"][1]["consoles"][0]["feeds"].push_back("reactor");
	target["control_layout"]["controllers"].push_back(
	    {{"id", "reactor"},
	     {"console", "observer"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "output"}, {"action", "generator_output"}, {"initial", 1000}, {"consumers", {"reactor"}}}})}});
	target["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back("reactor");
	target["personnel"]["duties"].push_back({{"id", "reactor"},
	                                         {"label", "Reactor"},
	                                         {"controller", "reactor"},
	                                         {"console", "observer"},
	                                         {"department", "engineering"},
	                                         {"compartment", "workshop"},
	                                         {"effects", Json::object()}});
	target["boarding_missions"] = Json::array(
	    {{{"id", "seize"},
	      {"title_key", "Board the target"},
	      {"entry", "bridge"},
	      {"tree", "test.boarding"},
	      {"dialogue", "test.boarding-dialogue"},
	      {"slots",
	       Json::array({{{"id", "security"}, {"kind", "team"}, {"capability", "security"}, {"required", true}},
	                    {{"id", "medical"}, {"kind", "team"}, {"capability", "medical"}, {"required", false}}})},
	      {"encounters", Json::array({{{"id", "guards"},
	                                   {"compartment", "bridge"},
	                                   {"defenders", {"security"}},
	                                   {"success_permille", 1000},
	                                   {"defender_penalty_permille", 0},
	                                   {"injury_permille", 1000},
	                                   {"loss_permille", 0},
	                                   {"medical_reduction_permille", 1000},
	                                   {"assessment_ticks", 2},
	                                   {"report", "assessment"}}})},
	      {"objectives",
	       {{"bridge", {{"kind", "bridge"}, {"compartment", "bridge"}, {"prize_crew", true}}},
	        {"reactor", {{"kind", "hold"}, {"compartment", "workshop"}, {"system", "reactor"}}}}}}});
	auto reactor_guard = target["boarding_missions"][0]["encounters"][0];
	reactor_guard["id"] = "reactor-guards";
	reactor_guard["compartment"] = "workshop";
	target["boarding_missions"][0]["encounters"].push_back(reactor_guard);
	for (auto &state : s["trees"].back()["states"])
		if (state["id"] == "reactor")
		{
			state["entry"] = Json::array({{{"action", "encounter"}, {"arguments", {"reactor-guards"}}}});
			state["transitions"][0]["conditions"] =
			    Json::array({{{"left", {{"observation", "encounter_done"}}}, {"op", "eq"}, {"right", true}},
			                 {{"left", {{"observation", "encounter_success"}}}, {"op", "eq"}, {"right", true}}});
		}
	s["entities"].push_back(target);
	s["maps"][0]["entities"][0]["definition"] = "test.boardable";
	return s;
}
inline Json boarding_craft_fixture(const std::string &mode)
{
	auto s = boarding_mission_fixture();
	s["trees"].push_back({{"id", "test.craft-idle"},
	                      {"context", "controller"},
	                      {"initial", "idle"},
	                      {"states", Json::array({{{"id", "idle"}}})}});
	auto craft = s["entities"][0];
	craft["id"] = "test.craft";
	craft["motion_model"] = "inertial";
	craft["tags"] = {"craft"};
	craft.erase("personnel");
	craft.erase("comms");
	craft.erase("combat");
	craft["collision"] = {{"shape", "circle"}, {"radius_mm", 100}, {"mass_grams", 1000}};
	auto systems = Json::object();
	for (const auto *id : {"engine", "steering", "scan"})
		systems[id] = craft["systems"][id];
	systems["engine"]["acceleration_mm_s2"] = 10000;
	craft["systems"] = systems;
	craft["stations"] = Json::array({{{"id", "flight"},
	                                  {"consoles", Json::array({{{"id", "drive"},
	                                                             {"definition", "test.console"},
	                                                             {"systems", Json::array()},
	                                                             {"feeds", {"engine", "steering", "scan"}}}})}}});
	craft["control_layout"] = {
	    {"schema", 1},
	    {"default_profile", "automatic"},
	    {"controllers",
	     Json::array(
	         {{{"id", "guidance"},
	           {"console", "drive"},
	           {"tree", "test.craft-idle"},
	           {"commands", Json::array({{{"id", "follow"},
	                                      {"action", "follow_contact"},
	                                      {"initial", 0},
	                                      {"arguments", {0, 1500, 5000}},
	                                      {"consumers", {"engine", "steering"}}}})}},
	          {{"id", "scan"},
	           {"console", "drive"},
	           {"fallback", "hold"},
	           {"commands",
	            Json::array(
	                {{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"scan"}}}})}}})},
	    {"profiles", Json::array({{{"id", "automatic"}, {"consoles", {{"drive", {{"ai", {"guidance", "scan"}}}}}}}})}};
	s["entities"].push_back(craft);
	auto &source = s["entities"][0];
	source["systems"]["rescue"] = source["systems"]["transport"];
	auto rescue_control = source["control_layout"]["controllers"][1];
	rescue_control["id"] = "rescue";
	rescue_control["commands"][0]["consumers"] = {"rescue"};
	source["control_layout"]["controllers"].push_back(rescue_control);
	source["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("rescue");
	source["stations"][0]["consoles"][0]["feeds"].push_back("rescue");
	auto duty = source["personnel"]["duties"][1];
	duty["id"] = "rescue";
	duty["controller"] = "rescue";
	source["personnel"]["duties"].push_back(duty);
	auto relief = source["personnel"]["officers"][0];
	relief["id"] = "relief";
	relief["duty"] = "rescue";
	source["personnel"]["officers"].push_back(relief);
	auto &bay = s["entities"][0]["systems"]["transport"];
	bay["kind"] = "transport";
	bay["mode"] = mode;
	bay["craft"] = "test.craft";
	bay["count"] = 1;
	bay["arrival_distance_mm"] = 3000;
	bay["arrival_speed_mm_s"] = 1000;
	bay["cruise_speed_mm_s"] = 5000;
	return s;
}
} // namespace phoenix::test
