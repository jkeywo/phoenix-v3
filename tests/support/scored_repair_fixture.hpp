#pragma once
// Synthetic policies; independent of production ships, scoring and balance packs.
namespace phoenix::test
{
inline void fit_scored_repair_policy(Json &source, const std::vector<std::string> &owners)
{
	const auto obs = [](const std::string &name)
	{
		return Json{{"observation", name}};
	};
	const auto eq = [&](const std::string &name, Json value)
	{
		return Json{{"left", obs(name)}, {"op", "eq"}, {"right", value}};
	};
	const auto action = [&](const std::string &name, Json inputs, Json tasks)
	{
		source["trees"].push_back({{"id", "test.repair." + name},
		                           {"context", "controller"},
		                           {"controller_action", {{"label", "Known repair work"}, {"inputs", inputs}}},
		                           {"team_commands", true},
		                           {"initial", "act"},
		                           {"states", Json::array({{{"id", "act"}, {"running", tasks}}})}});
	};
	const auto attempt = [&](Json command)
	{
		Json tasks = Json::array();
		tasks.push_back(command);
		tasks.push_back({{"action", "action.succeed"}, {"arguments", {"admitted"}}});
		Json failure{{"action", "action.fail"}, {"arguments", {"refused"}}};
		Json branches = Json::array();
		branches.push_back({{"tasks", tasks}});
		branches.push_back({{"tasks", Json::array({failure})}});
		return Json::array({Json{{"attempt", branches}}});
	};
	action("assign",
	       Json::array({{{"name", "team"}, {"type", "text"}},
	                    {{"name", "destination"}, {"type", "text"}},
	                    {{"name", "revision"}, {"type", "integer"}}}),
	       attempt(
	           Json{{"action", "team.order"},
	                {"arguments", Json::array({obs("input.team"), obs("input.destination"), obs("input.revision")})}}));
	action("continue", Json::array({{{"name", "team"}, {"type", "text"}}, {{"name", "target"}, {"type", "integer"}}}),
	       Json::array());
	Json actions = Json::array(
	    {{{"id", "assign"}, {"tree", "test.repair.assign"}}, {{"id", "continue"}, {"tree", "test.repair.continue"}}});
	Json bindings = Json::array();
	for (const auto *kind : {"work", "home", "disabled", "requested"})
		for (const bool continuing : {false, true})
		{
			const bool home = std::string(kind) == "home";
			Json available = Json::array({eq("repair_candidate.eligible", true), eq("repair_candidate.home", home),
			                              eq("repair_candidate.continuing", continuing)});
			if (std::string(kind) == "disabled" || std::string(kind) == "requested")
				available.push_back(eq(std::string("repair_candidate.") + kind, true));
			bindings.push_back(
			    {{"id", std::string(kind) + (continuing ? "-continue" : "-assign")},
			     {"behavior", home ? "home" : "repair"},
			     {"candidate_source", "repair"},
			     {"action", continuing ? "continue" : "assign"},
			     {"available", available},
			     {"target", obs("repair_candidate.target")},
			     {"commitment", obs("repair_candidate.continuing")},
			     {"arguments", continuing
			                       ? Json::array({obs("repair_candidate.team"), obs("repair_candidate.target")})
			                       : Json::array({obs("repair_candidate.team"), obs("repair_candidate.destination"),
			                                      obs("repair_candidate.revision")})},
			     {"factors",
			      {{"urgency", std::string(kind) == "disabled"    ? 1000
			                   : std::string(kind) == "requested" ? 500
			                                                      : 0},
			       {"progress", home ? Json(1) : obs("repair_candidate.deficit")},
			       {"travel", obs("repair_candidate.travel")}}}});
		}
	for (const auto *status : {"accepted", "completed"})
	{
		action(status,
		       Json::array({{{"name", "request"}, {"type", "integer"}}, {{"name", "amount"}, {"type", "integer"}}}),
		       attempt(Json{{"action", "coordination.respond_to"},
		                    {"arguments",
		                     Json::array({obs("input.request"), status, obs("input.amount"), "observed_repair"})}}));
		actions.push_back({{"id", status}, {"tree", std::string("test.repair.") + status}});
		Json available =
		    Json::array({eq("request.operation", "repair"), eq("request.feasible", true),
		                 eq("request.status", std::string(status) == "accepted" ? "requested" : "accepted")});
		if (std::string(status) == "completed")
			available.push_back(eq("request.done", true));
		bindings.push_back({{"id", status},
		                    {"behavior", "repair"},
		                    {"candidate_source", "requests"},
		                    {"action", status},
		                    {"available", available},
		                    {"target", obs("request.id")},
		                    {"arguments", Json::array({obs("request.id"), obs("request.amount")})},
		                    {"factors", {{"urgency", 1000000}}}});
	}
	source["trees"].push_back(
	    {{"id", "test.scored-repair"},
	     {"context", "controller"},
	     {"repair_observations", true},
	     {"request_observations", true},
	     {"team_commands", true},
	     {"action_bindings", actions},
	     {"objective_bindings", bindings},
	     {"objective_scoring", {{"default", {{"urgency", 1}, {"progress", 1}, {"travel", 1}, {"commitment", 10}}}}},
	     {"initial", "observe"},
	     {"states", Json::array({{{"id", "observe"}}})}});
	auto &ship = source["entities"][0];
	if (!ship.contains("standing_directives"))
		ship["standing_directives"] = Json::array();
	for (const auto *behavior : {"repair", "home"})
		ship["standing_directives"].push_back({{"id", std::string("test-") + behavior},
		                                       {"behavior", behavior},
		                                       {"tier", "directive_other"},
		                                       {"title_key", "Known assignment"}});
	for (auto &owner : ship["control_layout"]["controllers"])
		if (std::find(owners.begin(), owners.end(), owner.at("id").get<std::string>()) != owners.end())
		{
			owner.erase("fallback");
			owner.erase("policy");
			owner.erase("ai");
			owner["tree"] = "test.scored-repair";
			// The scored assignment must use the admitted team operation, without
			// depending on a legacy scalar repair-destination channel.
			std::erase_if(owner["commands"].get_ref<Json::array_t &>(),
			              [](const auto &command)
			              {
				              return command.at("action") == "repair_compartment";
			              });
		}
}

} // namespace phoenix::test
