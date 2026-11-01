#pragma once
// Synthetic authored support policy. It exercises production scoring and admission.
inline void fit_scored_supply(phoenix::Json &source)
{
	using phoenix::Json;
	auto obs = [](const std::string &n)
	{
		return Json{{"observation", n}};
	};
	auto condition = [&](const std::string &n, const std::string &op, Json right)
	{
		return Json{{"left", obs(n)}, {"op", op}, {"right", right}};
	};
	for (auto &entity : source["entities"])
	{
		if (!entity.contains("control_layout"))
			continue;
		for (auto &owner : entity["control_layout"]["controllers"])
		{
			if (!owner.contains("commands") || owner.contains("tree"))
				continue;
			Json channels = Json::array();
			for (const auto &command : owner["commands"])
				if (command.at("action") == "generator_output" || command.at("action") == "cooling_allocation")
					channels.push_back(command);
			if (channels.empty())
				continue;
			const auto id =
			    "test.scored-supply." + entity.at("id").get<std::string>() + "." + owner.at("id").get<std::string>();
			Json policy{{"id", id},
			            {"context", "controller"},
			            {"request_observations", true},
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
			policy["command_bindings"] = Json::array();
			for (const auto &command : channels)
				policy["command_bindings"].push_back({{"channel", command.at("id")}, {"action", command.at("action")}});
			for (const auto *operation : {"power", "cooling"})
			{
				Json commands = Json::array(), writes = Json::array();
				for (const auto &c : channels)
					if (c.at("action") ==
					    std::string(operation == std::string("power") ? "generator_output" : "cooling_allocation"))
					{
						commands.push_back({{"channel", c.at("id")}, {"action", c.at("action")}});
						writes.push_back({{"action", "write." + c.at("id").get<std::string>()},
						                  {"arguments", Json::array({obs("input.limit")})}});
					}
				if (commands.empty())
					continue;
				for (const auto *stage : {"accept", "execute", "complete", "refuse"})
				{
					auto action_id = id + "." + operation + "." + stage;
					Json tasks = Json::array();
					if (std::string(stage) == "execute")
						tasks = writes;
					else
						tasks.push_back(
						    {{"action", "coordination.respond_to"},
						     {"arguments", Json::array({obs("input.request"),
						                                std::string(stage) == "accept"     ? "accepted"
						                                : std::string(stage) == "complete" ? "completed"
						                                                                   : "refused",
						                                obs("input.limit"),
						                                std::string(stage) == "complete" ? "observed_supply"
						                                : std::string(stage) == "accept" ? "support_committed"
						                                                                 : "orders_or_capacity"})}});
					tasks.push_back({{"action", "action.succeed"}, {"arguments", {std::string(stage)}}});
					source["trees"].push_back({{"id", action_id},
					                           {"context", "controller"},
					                           {"command_bindings", commands},
					                           {"controller_action",
					                            {{"label", "Addressed supply work"},
					                             {"inputs", Json::array({{{"name", "request"}, {"type", "integer"}},
					                                                     {{"name", "limit"}, {"type", "integer"}}})}}},
					                           {"initial", "work"},
					                           {"states", Json::array({{{"id", "work"}, {"running", tasks}}})}});
					const auto binding = std::string(operation) + "-" + stage;
					policy["action_bindings"].push_back({{"id", binding}, {"tree", action_id}});
					Json available = Json::array(
					    {condition("request.id", "gt", 0), condition("request.operation", "eq", operation)});
					auto eq = [&](const std::string &field, Json v)
					{
						available.push_back(condition(field, "eq", v));
					};
					if (std::string(stage) == "refuse")
					{
						eq("request.status", "requested");
						eq("request.blocked", true);
					}
					else
					{
						eq("request.feasible", true);
						eq("request.status", std::string(stage) == "accept" ? "requested" : "accepted");
						if (std::string(stage) == "execute")
							eq("request.command_needed", true);
						if (std::string(stage) == "complete")
							eq("request.done", true);
						if (std::string(stage) == "accept")
							available.push_back(condition("request.conflict_priority", "lt", obs("request.priority")));
					}
					policy["objective_bindings"].push_back(
					    {{"id", binding},
					     {"behavior", "ready"},
					     {"candidate_source", "requests"},
					     {"action", binding},
					     {"available", available},
					     {"target", obs("request.id")},
					     {"arguments", Json::array({obs("request.id"), obs("request.limit")})},
					     {"commitment", obs("request.continuing")},
					     {"factors",
					      {{"urgency", obs("request.priority")},
					       {"progress", std::string(stage) == "complete" ? 3000
					                    : std::string(stage) == "accept" ? 2000
					                                                     : 1000}}}});
				}
			}
			source["trees"].push_back(policy);
			owner.erase("fallback");
			owner["tree"] = id;
			if (!entity.contains("standing_directives"))
				entity["standing_directives"] = Json::array();
			if (std::none_of(entity["standing_directives"].begin(), entity["standing_directives"].end(),
			                 [](const auto &r)
			                 {
				                 return r.at("behavior") == "ready";
			                 }))
				entity["standing_directives"].push_back({{"id", "supply-readiness"},
				                                         {"behavior", "ready"},
				                                         {"tier", "directive_other"},
				                                         {"title_key", "Maintain readiness"}});
		}
	}
}
