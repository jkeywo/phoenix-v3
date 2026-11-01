#pragma once
namespace phoenix::test
{
inline void fit_interdiction_goal_policies(Json &source)
{
	for (const auto &tree : Json::parse(R"json([
  {"id":"test.encounter.plan","context":"controller","command_bindings":[{"channel":"course","action":"destination_intent"}],
   "controller_action":{"label":"Coordinate a permitted encounter course","inputs":[{"name":"goal","type":"integer"},{"name":"x","type":"integer"},{"name":"y","type":"integer"},{"name":"revision","type":"integer"}]},
   "initial":"plan","states":[{"id":"plan","running":[{"action":"write.course","arguments":[{"observation":"input.goal"},{"observation":"input.x"},{"observation":"input.y"},{"observation":"input.revision"}]},{"action":"action.succeed","arguments":["intent_admitted"]}]}]},
  {"id":"test.encounter.move","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"},{"channel":"speed","action":"speed_mm_s"}],
   "controller_action":{"label":"Advance the accepted course","inputs":[{"name":"x","type":"integer"},{"name":"y","type":"integer"},{"name":"speed","type":"integer"}]},
   "initial":"move","states":[{"id":"move","running":[{"action":"write.destination","arguments":[true,{"observation":"input.x"},{"observation":"input.y"}]},{"action":"write.speed","arguments":[{"observation":"input.speed"}]},{"action":"action.succeed","arguments":["course_admitted"]}]}]},
  {"id":"test.encounter.idle","context":"controller","command_bindings":[{"channel":"destination","action":"destination","type":"boolean"},{"channel":"speed","action":"speed_mm_s"}],
   "controller_action":{"label":"Stop idle flight"},"initial":"idle","states":[{"id":"idle","running":[{"action":"write.destination","arguments":[false,0,0]},{"action":"write.speed","arguments":[0]},{"action":"action.succeed","arguments":["idle"]}]}]},
  {"id":"test.encounter.wait","context":"controller","controller_action":{"label":"Maintain accepted physical work"},"initial":"wait","states":[{"id":"wait","running":[{"action":"action.succeed","arguments":["maintained"]}]}]},
  {"id":"test.encounter.dock","context":"controller","docking_commands":true,"controller_action":{"label":"Admit selected docking work","inputs":[{"name":"port","type":"text"},{"name":"remote","type":"text"},{"name":"revision","type":"integer"}]},
   "initial":"dock","states":[{"id":"dock","running":[{"action":"dock.assist","arguments":[{"observation":"input.port"},{"observation":"input.remote"},{"observation":"input.revision"}]},{"action":"action.succeed","arguments":["dock_admitted"]}]}]},
  {"id":"test.encounter.charge","context":"controller","command_bindings":[{"channel":"enabled","type":"boolean"}],"controller_action":{"label":"Prepare departure reserve"},"initial":"charge","states":[{"id":"charge","running":[{"action":"write.enabled","arguments":[true]},{"action":"action.succeed","arguments":["charge_admitted"]}]}]},
  {"id":"test.encounter.depart","context":"controller","ftl_commands":true,"controller_action":{"label":"Admit a ready departure"},"initial":"depart","states":[{"id":"depart","running":[{"action":"ftl.depart","arguments":["jump"]},{"action":"action.succeed","arguments":["departure_admitted"]}]}]},
  {"id":"test.encounter.ftl","context":"controller","ftl_system":"jump","command_bindings":[{"channel":"enabled","type":"boolean"}],
   "action_bindings":[{"id":"charge","tree":"test.encounter.charge"},{"id":"depart","tree":"test.encounter.depart"},{"id":"wait","tree":"test.encounter.wait"}],
   "objective_bindings":[{"id":"charge","behavior":"depart","action":"charge","available":[{"left":{"observation":"ftl.ready"},"op":"eq","right":false},{"left":{"observation":"command.enabled"},"op":"eq","right":false}]},
    {"id":"depart","behavior":"depart","action":"depart","available":[{"left":{"observation":"ftl.ready"},"op":"eq","right":true}]},
    {"id":"wait","behavior":"ready","action":"wait"}],"initial":"observe","states":[{"id":"observe"}]}
 ])json"))
		source["trees"].push_back(tree);
	const auto observation = [](const std::string &name)
	{
		return Json{{"observation", name}};
	};
	const auto condition = [&](const std::string &name, const Json &value, const std::string &op = "eq")
	{
		return Json{{"left", observation(name)}, {"op", op}, {"right", value}};
	};
	Json planner = {{"id", "test.encounter.planner"},
	                {"context", "controller"},
	                {"contact_observations", true},
	                {"command_bindings", Json::array({{{"channel", "course"}, {"action", "destination_intent"}}})},
	                {"action_bindings", Json::array({{{"id", "plan"}, {"tree", "test.encounter.plan"}}})},
	                {"objective_bindings", Json::array()},
	                {"initial", "observe"},
	                {"states", Json::array({{{"id", "observe"}}})}};
	for (const auto *behavior : {"navigate", "depart", "withdraw", "board", "formation", "engage"})
	{
		const bool contact = std::string(behavior) == "board" || std::string(behavior) == "formation" ||
		                     std::string(behavior) == "engage";
		Json binding = {
		    {"id", behavior},
		    {"behavior", behavior},
		    {"action", "plan"},
		    {"candidate_source", contact ? "contacts" : "single"},
		    {"navigation", {{"mode", contact ? "approach" : "destination"}, {"distance", contact ? 5000 : 0}}},
		    {"arguments", Json::array({observation("objective.record.id"), observation("navigation.x"),
		                               observation("navigation.y"), observation("objective.record.revision")})},
		    {"available", Json::array()},
		    {"factors", {{"progress", 1000}}}};
		if (contact)
			binding["available"].push_back(condition("contact_candidate.available", true));
		if (std::string(behavior) == "engage")
		{
			binding["available"].push_back(condition("contact_candidate.hostile", true));
			binding["available"].push_back(condition("objective.record.operation", "engage"));
			binding["navigation"]["distance_parameter"] = "approach_distance";
			auto defend = binding;
			defend["id"] = "defend";
			defend["available"].back() = condition("objective.record.operation", "defend");
			defend["available"].push_back(
			    condition("contact_candidate.distance", observation("objective.record.radius"), "le"));
			planner["objective_bindings"].push_back(defend);
		}
		if (std::string(behavior) == "formation")
			binding["navigation"]["distance_parameter"] = "radius";
		planner["objective_bindings"].push_back(binding);
	}
	source["trees"].push_back(planner);
	Json drive = Json::parse(
	    R"json({"id":"test.encounter.drive","context":"controller","docking_commands":true,"docking_observations":true,
  "command_bindings":[{"channel":"destination","action":"destination","type":"boolean"},{"channel":"speed","action":"speed_mm_s"}],
  "command_reads":[{"name":"course.id","controller":"course","channel":"course"},{"name":"course.revision","controller":"course","channel":"course","field":"objective_revision"},
   {"name":"course.x","controller":"course","channel":"course","field":"x"},{"name":"course.y","controller":"course","channel":"course","field":"y"},{"name":"course.accepted","controller":"course","channel":"course","field":"accepted"}],
  "action_bindings":[{"id":"move","tree":"test.encounter.move"},{"id":"idle","tree":"test.encounter.idle"},{"id":"wait","tree":"test.encounter.wait"},{"id":"dock","tree":"test.encounter.dock"}],
  "objective_bindings":[],"initial":"observe","states":[{"id":"observe"}]})json");
	const auto accepted =
	    Json::array({condition("course.accepted", true), condition("course.id", observation("objective.record.id")),
	                 condition("course.revision", observation("objective.record.revision"))});
	const auto facts = [&](bool follower)
	{
		auto result = accepted;
		result.push_back(condition("docking.available", true));
		result.push_back(condition("docking.follower", follower));
		return result;
	};
	for (const auto *behavior : {"navigate", "depart", "withdraw", "board", "formation", "engage"})
		drive["objective_bindings"].push_back(
		    {{"id", behavior},
		     {"behavior", behavior},
		     {"action", "move"},
		     {"available", facts(false)},
		     {"arguments",
		      Json::array({observation("course.x"), observation("course.y"), observation("drive.cruise_speed")})},
		     {"factors", {{"progress", 1000}}}});
	auto docking = facts(false);
	docking.push_back(condition("docking_candidate.available", true));
	drive["objective_bindings"].push_back(
	    {{"id", "board-dock"},
	     {"behavior", "board"},
	     {"action", "dock"},
	     {"candidate_source", "docks"},
	     {"docking_mode", "assist"},
	     {"available", docking},
	     {"arguments", Json::array({observation("docking_candidate.port"), observation("docking_candidate.remote"),
	                                observation("docking_candidate.revision")})},
	     {"factors", {{"progress", 2000}, {"travel", observation("docking_candidate.travel")}}}});
	for (const auto *mode : {"cancel", "undock"})
	{
		auto action = *std::find_if(source["trees"].begin(), source["trees"].end(),
		                            [](const auto &tree)
		                            {
			                            return tree["id"] == "test.encounter.dock";
		                            });
		action["id"] = std::string("test.encounter.") + mode;
		action["states"][0]["running"][0]["action"] = std::string("dock.") + mode;
		source["trees"].push_back(action);
		drive["action_bindings"].push_back({{"id", mode}, {"tree", std::string("test.encounter.") + mode}});
		for (const auto *behavior : {"navigate", "depart", "withdraw", "formation", "engage"})
		{
			auto eligibility = facts(true);
			eligibility.push_back(condition("docking_candidate.available", true));
			drive["objective_bindings"].push_back(
			    {{"id", std::string(behavior) + "-" + mode},
			     {"behavior", behavior},
			     {"action", mode},
			     {"candidate_source", "docks"},
			     {"docking_mode", mode},
			     {"available", eligibility},
			     {"arguments",
			      Json::array({observation("docking_candidate.port"), "", observation("docking_candidate.revision")})},
			     {"factors", {{"progress", 2000}}}});
		}
	}
	for (const bool follower : {false, true})
		drive["objective_bindings"].push_back({{"id", follower ? "maintain" : "idle"},
		                                       {"behavior", "ready"},
		                                       {"action", follower ? "wait" : "idle"},
		                                       {"available", Json::array({condition("docking.available", true),
		                                                                  condition("docking.follower", follower)})},
		                                       {"factors", {{"progress", 1}}}});
	source["trees"].push_back(drive);
	for (auto &ship : source["entities"])
		if (ship["id"] == "test.target" || ship["id"] == "test.pirate")
		{
			if (!ship.contains("standing_directives"))
				ship["standing_directives"] = Json::array({{{"id", "ready"},
				                                            {"behavior", "ready"},
				                                            {"tier", "directive_other"},
				                                            {"title_key", "Readiness"}}});
			for (auto &owner : ship["control_layout"]["controllers"])
			{
				if (owner["id"] == "engine")
				{
					owner.erase("fallback");
					owner["tree"] = "test.encounter.drive";
					if (std::none_of(owner["commands"].begin(), owner["commands"].end(),
					                 [](const auto &cmd)
					                 {
						                 return cmd["id"] == "destination";
					                 }))
						owner["commands"].push_back({{"id", "destination"},
						                             {"action", "destination"},
						                             {"arguments", {0, 0}},
						                             {"consumers", {"engine", "steering"}}});
				}
				if (owner["id"] == "jump")
				{
					owner.erase("fallback");
					owner["tree"] = "test.encounter.ftl";
				}
			}
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "course"},
			     {"console", "drive"},
			     {"tree", "test.encounter.planner"},
			     {"commands", Json::array({{{"id", "course"},
			                                {"action", "destination_intent"},
			                                {"arguments", {0, 0, 0}},
			                                {"consumers", Json::array()}}})}});
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"].push_back("course");
			ship["personnel"]["duties"].push_back({{"id", "course"},
			                                       {"label", "Course planning"},
			                                       {"controller", "course"},
			                                       {"department", "navigation"},
			                                       {"compartment", "bridge"}});
		}
}
} // namespace phoenix::test
