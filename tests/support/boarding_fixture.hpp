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
} // namespace phoenix::test
