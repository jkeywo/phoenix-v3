#pragma once
namespace phoenix::test
{
// Synthetic scored policy, independent of development hulls and shipped tuning.
inline Json craft_test_policy(const Json &tree)
{
	const auto id = tree.at("id").get<std::string>();
	const auto bindings = tree.at("command_bindings");
	const auto obs = [](const std::string &n)
	{
		return Json{{"observation", n}};
	};
	const auto condition = [&](const std::string &n, const std::string &op, Json v)
	{
		return Json{{"left", obs(n)}, {"op", op}, {"right", v}};
	};
	bool armed = false, interceptor = false;
	for (const auto &b : bindings)
	{
		armed |= b.at("channel") == "target";
		interceptor |= b.at("channel") == "mode";
	}
	Json result = Json::array();
	Json root{{"id", id},
	          {"context", "controller"},
	          {"contact_observations", true},
	          {"command_bindings", bindings},
	          {"initial", "orders"},
	          {"states", Json::array({{{"id", "orders"}}})},
	          {"objective_scoring",
	           {{"default",
	             {{"urgency", 1},
	              {"progress", 1},
	              {"risk", 1000000},
	              {"travel", 1},
	              {"resources", 1},
	              {"commitment", 1000}}}}},
	          {"action_bindings", Json::array()},
	          {"objective_bindings", Json::array()}};
	for (const auto *mode : {"navigate", "attack", "intercept"})
	{
		const std::string action = mode;
		if (action == "attack" && !armed || action == "intercept" && !interceptor)
			continue;
		Json tasks = Json::array();
		for (const auto &b : bindings)
		{
			const auto channel = b.at("channel").get<std::string>();
			Json value;
			if (channel == "speed")
				value = obs("craft.speed");
			else if (channel == "heading")
				value = obs("craft.heading");
			else if (channel == "height")
				value = obs("craft.altitude");
			else if (channel == "target")
				value = action == "navigate" ? Json(0) : obs("input.target");
			else if (channel == "mode")
				value = action == "attack" ? 2 : action == "intercept" ? 1 : 0;
			else
				continue;
			tasks.push_back({{"action", "write." + channel}, {"arguments", Json::array({value})}});
		}
		const auto aid = id + (action == "navigate" ? ".execute" : "." + action);
		result.push_back(
		    {{"id", aid},
		     {"context", "controller"},
		     {"command_bindings", bindings},
		     {"controller_action",
		      {{"label", "Execute accepted craft guidance"},
		       {"inputs",
		        action == "navigate" ? Json::array() : Json::array({{{"name", "target"}, {"type", "integer"}}})}}},
		     {"initial", "work"},
		     {"states", Json::array({{{"id", "work"}, {"running", tasks}}})}});
		root["action_bindings"].push_back({{"id", action}, {"tree", aid}});
		if (action == "navigate")
		{
			root["objective_bindings"].push_back(
			    {{"id", action}, {"behavior", "ready"}, {"action", action}, {"factors", {{"progress", 1}}}});
			continue;
		}
		for (const auto *order : {"bomb", "defend", "escort"})
		{
			if (action == "intercept" && std::string(order) == "bomb")
				continue;
			Json available =
			    Json::array({condition("craft.order", "eq", order), condition("craft.reserve_low", "eq", false),
			                 condition("contact_candidate.current", "eq", true),
			                 condition("contact_candidate.category", "eq", action == "intercept" ? "missile" : "ship"),
			                 condition("objective.record.tier", action == "intercept" ? "eq" : "ne", "reaction")});
			if (action == "attack")
				available.push_back(condition("craft.armed", "eq", true));
			else
				available.push_back(condition("contact_candidate.closing", "eq", true));
			available.push_back(
			    std::string(order) == "bomb"
			        ? condition("contact_candidate.id", "eq", obs("craft.target"))
			        : condition("contact_candidate.order_distance", "le", obs("contact_candidate.order_range")));
			root["objective_bindings"].push_back(
			    {{"id", action + "-" + order},
			     {"behavior", "engage"},
			     {"candidate_source", "contacts"},
			     {"action", action},
			     {"available", available},
			     {"arguments", Json::array({obs("contact_candidate.id")})},
			     {"commitment", obs("contact_candidate.continuing")},
			     {"factors",
			      {{"progress", action == "intercept" ? 2000 : 1000},
			       {"risk", action == "intercept" ? obs("contact_candidate.closest_approach") : Json(0)},
			       {"travel", obs(action == "intercept" ? "contact_candidate.time_to_approach"
			                                            : "contact_candidate.distance")}}}});
		}
	}
	result.push_back(root);
	return result;
}
} // namespace phoenix::test
