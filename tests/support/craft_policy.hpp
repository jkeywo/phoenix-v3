#pragma once
namespace phoenix::test
{
// Synthetic authored policy; independent of development content and ship tuning.
inline Json craft_test_policy(const Json &tree)
{
	const auto id = tree.at("id").get<std::string>();
	const auto bindings = tree.at("command_bindings");
	const auto obs = [](const std::string &name)
	{
		return Json{{"observation", name}};
	};
	const auto condition = [&](const std::string &name, const std::string &op, Json value)
	{
		return Json{{"left", obs(name)}, {"op", op}, {"right", value}};
	};
	using Rule = std::pair<std::string, Json>;
	std::vector<Rule> rules;
	rules.emplace_back(
	    "recall", Json::array({condition("craft.reserve_low", "eq", true), condition("craft.order", "ne", "return"),
	                           condition("craft.order", "ne", "pickup")}));
	for (const auto *fact : {"craft.armed", "craft.target_present"})
		rules.emplace_back("recall", Json::array({condition("craft.order", "eq", "bomb"),
		                                          condition(fact, "eq", false),
		                                          {{"left", {{"elapsed", true}}}, {"op", "ge"}, {"right", 4}}}));
	for (const auto *order : {"return", "pickup"})
		rules.emplace_back("returning", Json::array({condition("craft.order", "eq", order)}));
	rules.emplace_back(
	    "bomb",
	    Json::array({condition("craft.order", "eq", "bomb"), condition("craft.armed", "eq", true),
	                 condition("craft.target_present", "eq", true), condition("craft.reserve_low", "eq", false)}));
	for (const auto *order : {"escort", "defend"})
	{
		const auto base =
		    Json::array({condition("craft.order", "eq", order), condition("craft.reserve_low", "eq", false)});
		auto intercept = base;
		intercept.push_back(condition("tactical.intercept_target", "gt", 0));
		rules.emplace_back("intercept", intercept);
		auto attack = base;
		attack.push_back(condition("tactical.intercept_target", "eq", 0));
		attack.push_back(condition("tactical.ship_target", "gt", 0));
		rules.emplace_back("attack", attack);
		auto guard = base;
		guard.push_back(condition("tactical.intercept_target", "eq", 0));
		guard.push_back(condition("tactical.ship_target", "eq", 0));
		rules.emplace_back("guard", guard);
	}
	auto ordinary = Json::array({condition("craft.reserve_low", "eq", false)});
	for (const auto *order : {"bomb", "return", "pickup", "escort", "defend"})
		ordinary.push_back(condition("craft.order", "ne", order));
	rules.emplace_back("navigate", ordinary);
	Json transitions = Json::array();
	for (const auto &[target, conditions] : rules)
	{
		auto guarded = conditions;
		guarded.push_back({{"left", {{"memory", "mode"}}}, {"op", "ne"}, {"right", target}});
		transitions.push_back({{"target", target}, {"conditions", guarded}});
	}
	Json states = Json::array({{{"id", "operate"}, {"initial", "navigate"}, {"transitions", transitions}}});
	for (const auto *name : {"navigate", "recall", "returning", "bomb", "guard", "intercept", "attack"})
	{
		const std::string state = name;
		Json work = Json::array();
		for (const auto &binding : bindings)
		{
			const auto channel = binding.at("channel").get<std::string>();
			Json value;
			if (channel == "speed")
				value = obs(state == "attack" ? "tactical.pursuit_speed" : "craft.speed");
			else if (channel == "heading")
				value = obs(state == "attack" ? "tactical.pursuit_heading" : "craft.heading");
			else if (channel == "height")
				value = obs(state == "attack" ? "tactical.pursuit_altitude" : "craft.altitude");
			else if (channel == "target")
				value = state == "bomb"     ? obs("craft.target")
				        : state == "attack" ? obs("tactical.ship_target")
				                            : Json(0);
			else if (channel == "mode")
				value = state == "attack" ? 2 : state == "intercept" || state == "guard" ? 1 : 0;
			else
				continue;
			work.push_back({{"action", "write." + channel}, {"arguments", Json::array({value})}});
		}
		Json row{{"id", state},
		         {"parent", "operate"},
		         {"running", work},
		         {"entry", Json::array({{{"memory", "mode"}, {"value", state}}})}};
		if (state == "recall")
			row["entry"].push_back({{"action", "craft.return_home"}});
		states.push_back(row);
	}
	return Json::array(
	    {{{"id", id + ".execute"},
	      {"context", "controller"},
	      {"controller_action", {{"label", "Craft automation"}, {"inputs", Json::array()}}},
	      {"command_bindings", bindings},
	      {"initial", "operate"},
	      {"memory", Json::array({{{"name", "mode"}, {"type", "text"}, {"initial", "navigate"}}})},
	      {"states", states}},
	     {{"id", id},
	      {"context", "controller"},
	      {"command_bindings", bindings},
	      {"action_bindings", Json::array({{{"id", "sortie"}, {"tree", id + ".execute"}}})},
	      {"initial", "orders"},
	      {"states", Json::array({{{"id", "orders"},
	                               {"running", Json::array({{{"action", "select.sortie"},
	                                                         {"arguments", {"assigned-order"}}}})}}})}}});
}
} // namespace phoenix::test
