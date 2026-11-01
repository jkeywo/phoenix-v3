#pragma once
inline void fit_scored_combat_target(phoenix::Json &source, bool repair_reports = false)
{
	using phoenix::Json;
	for (const auto &tree : Json::parse(R"json([
 {"id":"test.scored-target.aim","context":"controller","command_bindings":[{"channel":"target","action":"weapon_target"}],
  "controller_action":{"label":"Target a known hostile","inputs":[{"name":"target","type":"integer"}]},
  "initial":"write","states":[{"id":"write","running":[{"action":"write.target","arguments":[{"observation":"input.target"}]},{"action":"action.succeed","arguments":["target_admitted"]}]}]},
 {"id":"test.scored-target.report","context":"controller","controller_action":{"label":"Request known equipment repair","inputs":[{"name":"compartment","type":"integer"}]},
  "initial":"report","states":[{"id":"report","running":[{"attempt":[{"tasks":[
   {"action":"coordination.maintain","arguments":["repair","observer","team-a",{"observation":"input.compartment"},1,2,1200,40]},
   {"action":"action.succeed","arguments":["new_repair_request"]}]},{"tasks":[{"action":"action.fail","arguments":["repair_request_pending_or_refused"]}]}]}]}]},
 {"id":"test.scored-target","context":"controller","contact_observations":true,"command_bindings":[{"channel":"target","action":"weapon_target"}],
  "action_bindings":[{"id":"aim","tree":"test.scored-target.aim"},{"id":"report","tree":"test.scored-target.report"}],
  "objective_scoring":{"default":{"urgency":1,"progress":1,"risk":1,"travel":1,"resources":1,"commitment":1000000}},
  "objective_bindings":[{"id":"aim","behavior":"engage","candidate_source":"contacts","action":"aim",
   "available":[{"left":{"observation":"contact_candidate.current"},"op":"eq","right":true},{"left":{"observation":"contact_candidate.hostile"},"op":"eq","right":true},{"left":{"observation":"contact_candidate.category"},"op":"eq","right":"ship"}],
   "target":{"observation":"contact_candidate.id"},"arguments":[{"observation":"contact_candidate.id"}],"commitment":{"observation":"contact_candidate.continuing"},"factors":{"progress":1000,"travel":{"observation":"contact_candidate.distance"}}},
   {"id":"clear","behavior":"ready","action":"aim","arguments":[0],"factors":{"progress":1}}],
  "initial":"observe","states":[{"id":"observe"}]}
 ])json"))
		source["trees"].push_back(tree);
	if (repair_reports)
		source["trees"].back()["objective_bindings"].push_back(
		    Json::parse(R"json({"id":"report-repair","behavior":"repair","action":"report","retry_ticks":40,
 "available":[{"left":{"observation":"repair.compartment"},"op":"gt","right":0}],"arguments":[{"observation":"repair.compartment"}],"factors":{"progress":1000}})json"));
	auto &ship = source["entities"][0];
	if (!ship.contains("standing_directives"))
		ship["standing_directives"] = Json::array();
	for (const auto *behavior : {"engage", "ready"})
		if (std::none_of(ship["standing_directives"].begin(), ship["standing_directives"].end(),
		                 [&](const auto &row)
		                 {
			                 return row.at("behavior") == behavior;
		                 }))
			ship["standing_directives"].push_back(
			    {{"id", std::string("combat-") + behavior},
			     {"behavior", behavior},
			     {"tier", std::string(behavior) == "engage" ? "directive_combat" : "directive_other"},
			     {"title_key", "Combat duty"}});
	for (auto &owner : ship["control_layout"]["controllers"])
		if (owner["id"] == "gun")
		{
			owner.erase("fallback");
			owner.erase("policy");
			owner.erase("ai");
			owner["tree"] = "test.scored-target";
		}
}
