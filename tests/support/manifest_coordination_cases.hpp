// Independent missions and policies, loaded through the ordinary production path.
Json manifest_protocol_fixture(const std::string &mode)
{
	auto source = away_fixture();
	append_scored_away_policy(source, "test.manifest.preparation");
	auto &ship = source["entities"][0];
	ship["standing_directives"] = Json::parse(R"json([
        {"id":"prepare","behavior":"away","mission":"job","tier":"scripted_high","title_key":"Prepare mission"},
        {"id":"other","behavior":"ready","tier":"directive_other","title_key":"Other useful work"},
        {"id":"withdraw","behavior":"withdraw","mission":"job","tier":"reaction","title_key":"Withdraw request"}])json");
	auto &layout = ship["control_layout"];
	layout["station_complexity"] = true;
	layout["manual_workloads"]["drive"]["transport"] = 1;
	layout["profiles"][0]["stars"] = 3;
	auto automatic = layout["profiles"][0];
	automatic["id"] = "protocol";
	automatic["stars"] = 1;
	auto &drive = automatic["consoles"]["drive"];
	drive["human"].erase(std::remove(drive["human"].begin(), drive["human"].end(), Json("dispatch")),
	                     drive["human"].end());
	drive["ai"].push_back("dispatch");
	automatic["consoles"]["observer"] = {{"human", Json::array()}, {"ai", {"team"}}};
	layout["profiles"].push_back(automatic);
	layout["default_profile"] = "protocol";
	for (auto &controller : layout["controllers"])
		if (controller["id"] == "dispatch" || controller["id"] == "team")
		{
			controller.erase("fallback");
			controller["tree"] = controller["id"] == "dispatch" ? "test.manifest.sender" : "test.manifest.receiver";
			controller["commands"].push_back(
			    {{"id", "other"}, {"action", "intent_integer"}, {"initial", 0}, {"consumers", Json::array()}});
		}
	source["maps"][0]["away_missions"][0]["ai_dispatch"] = true;
	const auto obs = [](const char *name)
	{
		return Json{{"observation", name}};
	};
	const auto condition = [&](const char *name, Json value, const char *op = "eq")
	{
		return Json{{"left", obs(name)}, {"op", op}, {"right", value}};
	};
	const auto action = [&](const std::string &id, const Json &inputs, const Json &operation)
	{
		const Json success{{"action", "action.succeed"}, {"arguments", {"admitted"}}};
		const Json failure{{"action", "action.fail"}, {"arguments", {"refused"}}};
		const Json attempt{{"attempt", Json::array({{{"tasks", Json::array({operation, success})}},
		                                            {{"tasks", Json::array({failure})}}})}};
		Json tree{{"id", id},
		          {"context", "controller"},
		          {"controller_action", {{"label", "Coordinate mission"}, {"inputs", inputs}}},
		          {"initial", "act"},
		          {"states", Json::array({{{"id", "act"}, {"running", Json::array({attempt})}}})}};
		if (operation.at("action") == "write.other")
			tree["command_bindings"] = Json::array({{{"channel", "other"}, {"action", "intent_integer"}}});
		source["trees"].push_back(std::move(tree));
	};
	action("test.manifest.request", Json::array(),
	       {{"action", "coordination.request_manifest"}, {"arguments", {"observer", "team", "job", 2, 1, 60}}});
	action("test.manifest.other", Json::array(), {{"action", "write.other"}, {"arguments", {1}}});
	action("test.manifest.withdraw", Json::array(), {{"action", "coordination.withdraw"}, {"arguments", {"manifest"}}});
	action("test.manifest.open", Json::array({{{"name", "revision"}, {"type", "integer"}}}),
	       {{"action", "away.open"}, {"arguments", Json::array({"job", "", obs("input.revision")})}});
	for (const auto *status : {"accepted", "completed"})
		action("test.manifest." + std::string(status), Json::array({{{"name", "request"}, {"type", "integer"}}}),
		       {{"action", "coordination.respond_to"},
		        {"arguments", Json::array({obs("input.request"), status, 1, "department_preparation"})}});
	const Json other{{"id", "other"},
	                 {"behavior", "ready"},
	                 {"action", "other"},
	                 {"target", 0},
	                 {"available", Json::array({condition("command.other", 0)})},
	                 {"arguments", Json::array()},
	                 {"factors", {{"progress", 1}}}};
	Json sender{{"id", "test.manifest.sender"},
	            {"context", "controller"},
	            {"workflow_observations", true},
	            {"command_bindings", Json::array({{{"channel", "other"}, {"action", "intent_integer"}}})},
	            {"action_bindings", Json::array({{{"id", "request"}, {"tree", "test.manifest.request"}},
	                                             {{"id", "other"}, {"tree", "test.manifest.other"}},
	                                             {{"id", "withdraw"}, {"tree", "test.manifest.withdraw"}},
	                                             {{"id", "open"}, {"tree", "test.manifest.open"}}})},
	            {"objective_bindings",
	             Json::array({{{"id", "request"},
	                           {"behavior", "away"},
	                           {"action", "request"},
	                           {"retry_ticks", 2},
	                           {"available", Json::array({condition("coordination.reply.manifest.status", "none"),
	                                                      condition("workflow.phase", "available")})},
	                           {"arguments", Json::array()}},
	                          other})},
	            {"initial", "observe"},
	            {"states", Json::array({{{"id", "observe"}}})}};
	if (mode == "complete")
		sender["objective_bindings"].push_back(
		    {{"id", "open"},
		     {"behavior", "away"},
		     {"action", "open"},
		     {"available", Json::array({condition("coordination.reply.manifest.status", "accepted"),
		                                condition("workflow.phase", "available"), condition("tick", 30, "ge")})},
		     {"arguments", Json::array({obs("workflow.mission_revision")})}});
	if (mode == "withdraw")
		sender["objective_bindings"].push_back(
		    {{"id", "withdraw"},
		     {"behavior", "withdraw"},
		     {"action", "withdraw"},
		     {"available",
		      Json::array({condition("coordination.reply.manifest.status", "accepted"), condition("tick", 20, "ge")})},
		     {"arguments", Json::array()}});
	source["trees"].push_back(sender);
	Json receiver{{"id", "test.manifest.receiver"},
	              {"context", "controller"},
	              {"request_observations", true},
	              {"command_bindings", Json::array({{{"channel", "other"}, {"action", "intent_integer"}}})},
	              {"action_bindings", Json::array({{{"id", "accept"}, {"tree", "test.manifest.accepted"}},
	                                               {{"id", "complete"}, {"tree", "test.manifest.completed"}},
	                                               {{"id", "other"}, {"tree", "test.manifest.other"}}})},
	              {"objective_bindings", Json::array({other})},
	              {"initial", "observe"},
	              {"states", Json::array({{{"id", "observe"}}})}};
	receiver["objective_bindings"].push_back(
	    {{"id", "accept"},
	     {"behavior", "away"},
	     {"candidate_source", "requests"},
	     {"action", mode == "premature" ? "complete" : "accept"},
	     {"retry_ticks", 20},
	     {"target", obs("request.id")},
	     {"available", Json::array({condition("request.operation", "manifest"),
	                                condition("request.status", "requested"), condition("request.feasible", true)})},
	     {"arguments", Json::array({obs("request.id")})},
	     {"factors", {{"urgency", obs("request.priority")}}}});
	if (mode != "premature")
		receiver["objective_bindings"].push_back(
		    {{"id", "complete"},
		     {"behavior", "away"},
		     {"candidate_source", "requests"},
		     {"action", "complete"},
		     {"target", obs("request.id")},
		     {"available", Json::array({condition("request.status", "accepted"), condition("request.done", true)})},
		     {"arguments", Json::array({obs("request.id")})}});
	receiver["away_preparation_observations"] = true;
	for (const auto &tree : source["trees"])
		if (tree.at("id") == "test.manifest.preparation")
		{
			for (const auto &binding : tree.at("action_bindings"))
				if (binding.at("id") == "contribute" || binding.at("id") == "assign")
					receiver["action_bindings"].push_back(binding);
			for (const auto &binding : tree.at("objective_bindings"))
				if (binding.at("id") == "contribute" || binding.at("id") == "assign")
					receiver["objective_bindings"].push_back(binding);
		}
	source["trees"].push_back(receiver);
	return source;
}
Json manifest_controller(const World &world, const std::string &id)
{
	const auto saved = world.save();
	for (const auto &body : saved.at("bodies"))
		if (body.at("body").at("id") == 2)
			for (const auto &controller : body.at("console_controllers").at("controllers"))
				if (controller.at("id") == id)
					return controller;
	throw std::runtime_error("Missing manifest Controller");
}
void manifest_coordination_cases()
{
	for (const auto *mode : {"complete", "expire", "withdraw", "premature"})
	{
		const auto catalog = load(manifest_protocol_fixture(mode));
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		require(world.claim(101, 2, "drive") && world.claim(102, 2, "observer"), "Claim manifest protocol watches");
		ticks(world, 15);
		const auto sender = manifest_controller(world, "dispatch"), receiver = manifest_controller(world, "team");
		require(sender.at("coordination")[1].size() == 1 && receiver.at("coordination")[0].size() == 1,
		        "One addressed manifest request reaches its named recipient");
		const auto &requested = sender.at("coordination")[1][0];
		require(requested.at("mission") == "job" && requested.at("request_revision").get<std::uint64_t>() > 0,
		        "Mission identity and revision survive typed request delivery");
		require(board(world)[0].at("phase") == "available" && board(world, 102)[0].at("contributing") == false,
		        "A delivered preparation request does not transfer department authority or operate the mission");
		require(sender.at("commands").back().at("value") == 1 && receiver.at("commands").back().at("value") == 1,
		        "Outstanding preparation replies leave other feasible Controller work available");
		if (std::string(mode) == "premature")
		{
			require(requested.at("status") == 0 && receiver.at("coordination")[0][0].at("status") == 0,
			        "Completion without a permitted preparation receipt is refused");
			continue;
		}
		require(requested.at("status") == 1 && requested.at("amount") == 1 && requested.at("requested_amount") == 2,
		        "Partial acceptance remains distinct from preparation completion");
		bool partial_reply = false;
		const auto sender_view = world.observe(101), receiver_view = world.observe(102);
		for (const auto &message : sender_view->gameplay.at("messages"))
			if (message.at("parameters").value("operation", std::string{}) == "manifest")
			{
				const auto &fields = message.at("payload").at("fields");
				const auto &parameters = message.at("parameters");
				partial_reply |=
				    fields.at("status") == "accepted" && fields.at("amount") == 1 &&
				    fields.at("requested_amount") == 2 && fields.at("mission") == "job" &&
				    parameters.at("amount") == "1" && parameters.at("requested_amount") == "2" &&
				    parameters.at("mission") == "job" &&
				    message.at("body_key").get<std::string>().find("{requested_amount}") != std::string::npos;
			}
		require(partial_reply, "Partial acceptance prose and payload describe the same addressed commitment");
		for (const auto &message : receiver_view->gameplay.at("messages"))
			require(message.at("parameters").value("status", std::string{}) != "accepted",
			        "A private reply does not silently become the other Console's evidence");
		World resumed;
		resumed.restore(catalog, world.save());
		require((resumed.observe(101).has_value() || resumed.claim(101, 2, "drive")) &&
		            (resumed.observe(102).has_value() || resumed.claim(102, 2, "observer")),
		        "Resume protocol watches");
		if (std::string(mode) == "complete")
		{
			ticks(world, 20);
			ticks(resumed, 20);
			require(board(world)[0].at("phase") == "open", "Dispatch admits mission opening after acceptance");
			for (auto *copy : {&world, &resumed})
				require(fixture_native_submit(*copy, 101, order(*copy, "transport", "transport")),
				        "Admit physical transport after the mission opens");
			ticks(world, 20);
			ticks(resumed, 20);
			const auto complete = manifest_controller(world, "dispatch").at("coordination")[1][0];
			require(complete.at("status") == 3 && complete.at("evidence_revision").get<std::uint64_t>() >=
			                                          complete.at("request_revision").get<std::uint64_t>(),
			        "Observed departmental preparation produces a revisioned completion receipt");
		}
		else
		{
			const auto duration = std::string(mode) == "withdraw" ? 10 : 60;
			ticks(world, duration);
			ticks(resumed, duration);
			const auto expected = std::string(mode) == "withdraw" ? 5 : 4;
			require(manifest_controller(world, "dispatch").at("coordination")[1][0].at("status") == expected &&
			            manifest_controller(world, "team").at("coordination")[0][0].at("status") == expected,
			        "Withdrawal and expiry end the addressed commitment");
		}
		require(world.save() == resumed.save(), "Mission-scoped requests and receipts continue deterministically");
	}
	auto unknown = manifest_protocol_fixture("expire");
	for (auto &tree : unknown["trees"])
		if (tree.at("id") == "test.manifest.request")
			tree["states"][0]["running"][0]["attempt"][0]["tasks"][0]["arguments"][2] = "undisclosed-job";
	World isolated;
	const auto isolated_catalog = load(unknown);
	isolated.load(isolated_catalog, "test.map", {{"crew", "test.away"}});
	require(isolated.claim(101, 2, "drive") && isolated.claim(102, 2, "observer"), "Claim isolated mission watches");
	ticks(isolated, 15);
	const auto isolated_sender = manifest_controller(isolated, "dispatch");
	require((!isolated_sender.contains("coordination") || isolated_sender.at("coordination")[1].empty()) &&
	            isolated_sender.at("commands").back().at("value") == 1,
	        "An undisclosed mission cannot be requested and refusal permits other useful work");
}
