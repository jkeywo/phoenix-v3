#pragma once
inline void fit_scored_personnel_policy(phoenix::Json &source)
{
	source["trees"].push_back(phoenix::Json::parse(
	    R"json({"id":"test.personnel.assign","context":"controller","personnel_commands":true,"controller_action":{"label":"Restore an unfilled departmental duty","inputs":[{"name":"officer","type":"text"},{"name":"duty","type":"text"},{"name":"revision","type":"integer"}]},"initial":"assign","states":[{"id":"assign","running":[{"attempt":[{"tasks":[{"action":"personnel.assign","arguments":[{"observation":"input.officer"},{"observation":"input.duty"},{"observation":"input.revision"}]},{"action":"action.succeed","arguments":["officer_assignment_admitted"]}]},{"tasks":[{"action":"action.fail","arguments":["officer_assignment_refused"]}]}]}]}]})json"));
	source["trees"].push_back(phoenix::Json::parse(
	    R"json({"id":"test.personnel.policy","context":"controller","personnel_observations":true,"personnel_commands":true,"action_bindings":[{"id":"personnel-assignment","tree":"test.personnel.assign"}],"objective_bindings":[{"id":"personnel-assignment","behavior":"ready","candidate_source":"personnel","action":"personnel-assignment","available":[{"left":{"observation":"personnel_candidate.eligible"},"op":"eq","right":true}],"arguments":[{"observation":"personnel_candidate.officer"},{"observation":"personnel_candidate.duty"},{"observation":"personnel_candidate.revision"}],"factors":{"progress":{"observation":"personnel_candidate.fit"},"travel":{"observation":"personnel_candidate.travel"}}}],"initial":"watch","states":[{"id":"watch"}]})json"));
	auto &ship = source["entities"][0];
	ship["standing_directives"] = phoenix::Json::array({{{"id", "readiness"},
	                                                     {"behavior", "ready"},
	                                                     {"tier", "directive_other"},
	                                                     {"title_key", "Restore readiness"}}});
	auto &owner = ship["control_layout"]["controllers"][0];
	owner.erase("fallback");
	owner["tree"] = "test.personnel.policy";
}
