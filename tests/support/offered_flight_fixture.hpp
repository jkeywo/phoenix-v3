#pragma once
#include "interdiction_goal_policies.hpp"

// Synthetic disclosed boarding and recovery work. Native mission execution
// owns the physical party lifecycle; it does not select the ship's course.
void fit_offered_job_flight(Json &source, bool offered, bool cover)
{
	phoenix::test::fit_interdiction_goal_policies(source, {"test.away"});
	source["entities"][0]["standing_directives"].push_back({{"id", "flight-ready"},
	                                                        {"behavior", "ready"},
	                                                        {"tier", "directive_other"},
	                                                        {"title_key", "Flight readiness"}});
	if (!offered)
		return;
	source["trees"].push_back(Json::parse(R"json({"id":"test.offered-flight","context":"scenario","initial":"board",
 "states":[{"id":"board","entry":[{"action":"add_objective","arguments":["board",{"observation":"self.entity"},"board"]}],
 "transitions":[{"target":"work","conditions":[{"left":{"observation":"event.name"},"op":"eq","right":"party.deployed"}]}]},
 {"id":"work","entry":[],"transitions":[{"target":"return","conditions":[{"left":{"observation":"event.name"},"op":"eq","right":"party.complete"}]}]},
 {"id":"return","entry":[{"action":"add_objective","arguments":["return",{"observation":"self.entity"},"return"]}]}]})json"));
	if (cover)
		source["trees"].back()["states"][1]["entry"].push_back(
		    {{"action", "add_objective"},
		     {"arguments", Json::array({"stand-off", {{"observation", "self.entity"}}, "stand-off"})}});
	source["maps"][0]["regions"] = Json::array(
	    {{{"id", "stand-off"}, {"shape", "circle"}, {"x_mm", 20000}, {"y_mm", -40000}, {"radius_mm", 1000}}});
	source["maps"][0]["scenario"] =
	    Json::parse(R"json({"tree":"test.offered-flight","bindings":{"actor":{"slot":"crew"}},
 "events":["party.deployed","party.complete"],"event_bindings":[
 {"kind":"away.deployed","subject":{"entity":1},"operation":"job","event":"party.deployed"},
 {"kind":"away.completed","subject":{"entity":1},"operation":"job","event":"party.complete"}],
 "fact_bindings":[{"id":"deployed","kind":"away","subject":{"slot":"crew"},"operation":"job","field":"ever_deployed"},
 {"id":"complete","kind":"event","event":"party.complete"},{"id":"recovered","kind":"away","subject":{"slot":"crew"},"operation":"job","field":"recovered"}],
 "objectives":[
 {"id":"board","type":"condition","behavior":"board","tier":"scripted_high","behavior_target":{"entity":1},"title_key":"Board the offered job","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"recovered","value":true}},
 {"id":"stand-off","type":"condition","behavior":"navigate","tier":"scripted_low","region":"stand-off","title_key":"Stand clear during work","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"complete","value":true}},
 {"id":"return","type":"condition","behavior":"board","tier":"scripted_high","behavior_target":{"entity":1},"title_key":"Recover the party","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"recovered","value":true}}]})json");
	if (cover)
		source["maps"][0]["scenario"]["objectives"][0]["complete_when"]["fact"] = "deployed";
}
