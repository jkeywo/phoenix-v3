#pragma once
namespace phoenix::test
{
inline void fit_test_bay_policy(Json &source)
{
	const auto obs = [](const char *name)
	{
		return Json{{"observation", name}};
	};
	const auto condition = [&](const char *name, Json value, const char *op = "eq")
	{
		return Json{{"left", obs(name)}, {"op", op}, {"right", value}};
	};
	Json bindings = Json::array(), actions = Json::array();
	for (const auto *operation : {"defend", "bomb", "recall"})
	{
		const auto name = std::string(operation);
		actions.push_back({{"id", name}, {"tree", "test.bay." + name}});

		Json execution{{"id", "test.bay." + name}, {"context", "controller"}, {"initial", "submit"}};
		execution["controller_action"] = {{"label", "Submit craft order"},
		                                  {"inputs", Json::array({{{"name", "craft"}, {"type", "text"}},
		                                                          {{"name", "target"}, {"type", "text"}},
		                                                          {{"name", "revision"}, {"type", "integer"}}})}};
		const Json submit{{"action", "craft." + name},
		                  {"arguments", Json::array({obs("input.craft"), obs("input.target"), obs("input.revision")})}};
		const Json accepted{{"action", "action.succeed"}, {"arguments", {"order_accepted"}}};
		const Json refused{{"action", "action.fail"}, {"arguments", {"order_refused"}}};
		const Json attempt{{"attempt", Json::array({{{"tasks", Json::array({submit, accepted})}},
		                                            {{"tasks", Json::array({refused})}}})}};
		execution["states"] = Json::array({{{"id", "submit"}, {"running", Json::array({attempt})}}});
		source["trees"].push_back(std::move(execution));
	}

	for (const auto *operation : {"defend", "bomb", "recall"})
	{
		const bool recall = std::string(operation) == "recall", bomb = std::string(operation) == "bomb";
		auto available =
		    Json::array({condition(recall ? "bay.recall_available" : "bay.dispatch_available", true),
		                 recall ? condition("bay.pending_order", "return", "ne") : condition("bay.pending", false),
		                 condition("bay.order", recall ? "return" : operation, "ne")});
		if (!recall)
		{
			available.push_back(condition("bay.role", bomb ? "bomber" : "interceptor"));
			available.push_back(condition(bomb ? "bay.target_present" : "bay.threat_present", true));
			available.push_back(condition("bay.armed", true));
			available.push_back(condition("bay.reserve_low", false));
			available.push_back(condition("bay.report_age", 120, "le"));
		}
		else
			available.push_back(condition("bay.threat_present", false));
		bindings.push_back({{"id", operation},
		                    {"behavior", recall ? "ready" : "engage"},
		                    {"candidate_source", "craft"},
		                    {"action", operation},
		                    {"available", available},
		                    {"target", obs("bay.entity")},
		                    {"arguments", Json::array({obs("bay.craft"), bomb ? obs("bay.attack_text") : Json(""),
		                                               obs("bay.revision")})}});
	}
	bindings.push_back(
	    {{"id", "departure-recall"},
	     {"behavior", "ready"},
	     {"candidate_source", "craft"},
	     {"action", "recall"},
	     {"target", obs("bay.entity")},
	     {"available",
	      Json::array({condition("bay.recall_available", true), condition("bay.ftl_preparing", true),
	                   condition("bay.pending_order", "return", "ne"), condition("bay.order", "return", "ne")})},
	     {"arguments", Json::array({obs("bay.craft"), "", obs("bay.revision")})},
	     {"factors", {{"urgency", 1000}, {"progress", 1000}}}});
	source["trees"].push_back({{"id", "test.bay.policy"},
	                           {"context", "controller"},
	                           {"craft_dispatch_observations", true},
	                           {"action_bindings", actions},
	                           {"objective_bindings", bindings},
	                           {"initial", "observe"},
	                           {"states", Json::array({{{"id", "observe"}}})}});
	auto &carrier = source["entities"][0];
	carrier["standing_directives"] = Json::array(
	    {{{"id", "engage"},
	      {"behavior", "engage"},
	      {"tier", "directive_combat"},
	      {"title_key", "Engage observed threats"}},
	     {{"id", "ready"}, {"behavior", "ready"}, {"tier", "directive_other"}, {"title_key", "Restore readiness"}}});
	for (auto &controller : carrier["control_layout"]["controllers"])
		if (controller["id"] == "bay")
		{
			controller.erase("fallback");
			controller["tree"] = "test.bay.policy";
		}
}
} // namespace phoenix::test
