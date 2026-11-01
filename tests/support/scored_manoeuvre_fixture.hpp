#pragma once
inline void fit_scored_manoeuvre(phoenix::Json &source, const std::string &owner_id)
{
	using phoenix::Json;
	auto obs = [](const std::string &n)
	{
		return Json{{"observation", n}};
	};
	auto eq = [&](const std::string &n, Json v)
	{
		return Json{{"left", obs(n)}, {"op", "eq"}, {"right", v}};
	};
	auto &ship = source["entities"][0];
	for (auto &owner : ship["control_layout"]["controllers"])
	{
		if (owner.at("id") != owner_id)
			continue;
		const auto id = "test.scored-manoeuvre." + owner_id;
		Json commands = Json::array(), writes = Json::array(), stops = Json::array();
		for (const auto &c : owner["commands"])
			if (c.at("action") == "speed_mm_s" || c.at("action") == "heading_mdeg")
			{
				commands.push_back({{"channel", c.at("id")}, {"action", c.at("action")}});
				writes.push_back({{"action", "write." + c.at("id").get<std::string>()},
				                  {"arguments", Json::array({obs(c.at("action") == "speed_mm_s" ? "input.speed"
				                                                                                : "input.bearing")})}});
				if (c.at("action") == "speed_mm_s")
					stops.push_back({{"action", "write." + c.at("id").get<std::string>()}, {"arguments", {0}}});
			}
		Json policy{{"id", id},
		            {"context", "controller"},
		            {"request_observations", true},
		            {"command_bindings", commands},
		            {"objective_scoring",
		             {{"default",
		               {{"urgency", 1000000},
		                {"progress", 1},
		                {"risk", 1},
		                {"travel", 1},
		                {"resources", 1},
		                {"commitment", 1000}}}}},
		            {"initial", "observe"},
		            {"states", Json::array({{{"id", "observe"}}})},
		            {"action_bindings", Json::array()},
		            {"objective_bindings", Json::array()}};
		for (const auto *stage : {"accept", "execute", "complete", "refuse"})
		{
			Json tasks = Json::array();
			if (std::string(stage) == "execute")
				tasks = writes;
			else
			{
				tasks.push_back(
				    {{"action", "coordination.respond_to"},
				     {"arguments", Json::array({obs("input.request"),
				                                std::string(stage) == "accept"     ? "accepted"
				                                : std::string(stage) == "complete" ? "completed"
				                                                                   : "refused",
				                                obs("input.limit"),
				                                std::string(stage) == "complete" ? "observed_range"
				                                : std::string(stage) == "accept" ? "manoeuvre_committed"
				                                                                 : "navigation_unavailable"})}});
				tasks.push_back({{"action", "action.succeed"}, {"arguments", {stage}}});
			}
			const auto name = std::string(stage), aid = id + "." + name;
			source["trees"].push_back(
			    {{"id", aid},
			     {"context", "controller"},
			     {"command_bindings", commands},
			     {"controller_action",
			      {{"label", "Addressed manoeuvre work"},
			       {"inputs", Json::array({{{"name", "request"}, {"type", "integer"}},
			                               {{"name", "limit"}, {"type", "integer"}},
			                               {{"name", "bearing"}, {"type", "integer"}},
			                               {{"name", "speed"}, {"type", "integer"}}})}}},
			     {"initial", "work"},
			     {"states", Json::array({{{"id", "work"},
			                              {"running", tasks},
			                              {"exit", std::string(stage) == "execute" ? stops : Json::array()}}})}});
			policy["action_bindings"].push_back({{"id", name}, {"tree", aid}});
			Json available =
			    Json::array({eq("request.operation", "manoeuvre"),
			                 eq("request.status", name == "accept" || name == "refuse" ? "requested" : "accepted")});
			available.push_back(eq(name == "refuse" ? "request.blocked" : "request.feasible", true));
			if (name == "execute")
				available.push_back(eq("request.done", false));
			if (name == "complete")
				available.push_back(eq("request.done", true));
			policy["objective_bindings"].push_back(
			    {{"id", name},
			     {"behavior", "ready"},
			     {"candidate_source", "requests"},
			     {"request_operation", "manoeuvre"},
			     {"action", name},
			     {"available", available},
			     {"target", obs("request.id")},
			     {"arguments",
			      Json::array({obs("request.id"), obs("request.limit"), obs("request.bearing"), obs("request.speed")})},
			     {"factors",
			      {{"urgency", obs("request.priority")},
			       {"progress", name == "complete" ? 3000
			                    : name == "accept" ? 2000
			                                       : 1000}}}});
		}
		source["trees"].push_back(policy);
		owner.erase("fallback");
		owner.erase("policy");
		owner.erase("ai");
		owner["tree"] = id;
	}
	if (!ship.contains("standing_directives"))
		ship["standing_directives"] = Json::array();
	if (std::none_of(ship["standing_directives"].begin(), ship["standing_directives"].end(),
	                 [](const auto &r)
	                 {
		                 return r.at("behavior") == "ready";
	                 }))
		ship["standing_directives"].push_back({{"id", "manoeuvre-readiness"},
		                                       {"behavior", "ready"},
		                                       {"tier", "directive_other"},
		                                       {"title_key", "Attend departmental requests"}});
}
