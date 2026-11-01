void scan_request_contract(Json source)
{
	auto &ship = source["entities"][0];
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"survey","behavior":"scan","tier":"directive_other","title_key":"Known survey work"},
	 {"id":"ready","behavior":"ready","tier":"directive_other","title_key":"Other useful work"}])json");
	ship["control_layout"]["controllers"].push_back(
	    Json::parse(R"json({"id":"requester","console":"drive","tree":"test.scan.requester",
	 "commands":[{"id":"other","action":"intent_integer","consumers":[]}]})json"));
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"].push_back("requester");
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.scan.ask","context":"controller","command_bindings":[{"channel":"other"}],
	  "controller_action":{"label":"Request a disclosed scan"},"initial":"ask","states":[{"id":"ask","running":[
	   {"action":"write.other","arguments":[0]},
	   {"action":"coordination.request_scan","arguments":["drive","scanner",2,"compartment_condition",2,300]},
	   {"action":"action.succeed","arguments":["requested"]}]}]},
	 {"id":"test.scan.other","context":"controller","command_bindings":[{"channel":"other"}],
	  "controller_action":{"label":"Other useful work"},"initial":"work","states":[{"id":"work","running":[
	   {"action":"write.other","arguments":[1]},{"action":"action.succeed","arguments":["other_work"]}]}]},
	 {"id":"test.scan.reply","context":"controller","controller_action":{"label":"Reply to a scan request","inputs":[
	   {"name":"request","type":"integer"},{"name":"status","type":"text"}]},"initial":"reply","states":[{"id":"reply","running":[
	   {"attempt":[{"tasks":[{"action":"coordination.respond_to","arguments":[{"observation":"input.request"},{"observation":"input.status"},1,"scoped_scan"]},
	     {"action":"action.succeed","arguments":["reply_admitted"]}]},{"tasks":[{"action":"action.fail","arguments":["reply_refused"]}]}]}]}]},
	 {"id":"test.scan.requester","context":"controller","command_bindings":[{"channel":"other"}],
	  "action_bindings":[{"id":"ask","tree":"test.scan.ask"},{"id":"other","tree":"test.scan.other"}],
	  "objective_bindings":[{"id":"ask","behavior":"ready","action":"ask","available":[
	   {"left":{"observation":"tick"},"op":"ge","right":2},
	   {"left":{"observation":"coordination.reply.scan.status"},"op":"eq","right":"none"}],"factors":{"progress":1000}},
	   {"id":"other","behavior":"ready","action":"other","factors":{"progress":1}}],"initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.scored-scanner")
		{
			tree["request_observations"] = true;
			tree["action_bindings"].push_back({{"id", "reply"}, {"tree", "test.scan.reply"}});
			auto requested = tree["objective_bindings"][0];
			requested["id"] = "requested";
			requested["available"].push_back(
			    {{"left", {{"observation", "scan_candidate.requested"}}}, {"op", "eq"}, {"right", true}});
			requested["factors"]["progress"] = 3000;
			requested["factors"]["travel"] = 0;
			tree["objective_bindings"].push_back(requested);
			for (const auto *status : {"requested", "accepted"})
				tree["objective_bindings"].push_back(
				    {{"id", std::string("reply-") + status},
				     {"behavior", "scan"},
				     {"candidate_source", "requests"},
				     {"action", "reply"},
				     {"available",
				      Json::array({{{"left", {{"observation", "request.operation"}}}, {"op", "eq"}, {"right", "scan"}},
				                   {{"left", {{"observation", "request.status"}}}, {"op", "eq"}, {"right", status}},
				                   {{"left", {{"observation", "request.feasible"}}}, {"op", "eq"}, {"right", true}}})},
				     {"target", {{"observation", "request.target"}}},
				     {"arguments", Json::array({{{"observation", "request.id"}},
				                                std::string(status) == "requested" ? "accepted" : "completed"})},
				     {"factors", {{"progress", 4000}}}});
		}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim typed scan request fixture");
	const auto saved_controller = [](const World &value, const std::string &id)
	{
		const auto saved = value.save();
		for (const auto &body : saved.at("bodies"))
			if (body.at("body").at("id") == 1)
				for (const auto &controller : body.at("console_controllers").at("controllers"))
					if (controller.at("id") == id)
						return controller;
		throw std::runtime_error("missing scan protocol Controller");
	};
	ticks(world, 6);
	const auto sender = saved_controller(world, "requester");
	require(sender.at("coordination")[1].size() == 1 &&
	            sender.at("coordination")[1][0].at("capability") == "compartment_condition" &&
	            sender.at("commands")[0].at("value") == 1,
	        "one typed scan request retains capability and leaves its sender free for other work");
	const auto receiver = saved_controller(world, "scanner");
	require(receiver.at("coordination")[0].size() == 1 && receiver.at("coordination")[0][0].at("status") == 1,
	        "acceptance acknowledges desired scan work before evidence proves completion");
	require(!world.submit(101, OperatingCommand{1, {}, 2, {}, "requester", "target", sender.at("generation")}) &&
	            !world.submit(101, OperatingCommand{1, {}, 2, {}, "scanner", "target", receiver.at("generation")}),
	        "requesting a scan transfers no scanner actuator authority or recipient ownership");
	bool matching_reply = false;
	const auto observed = *world.observe(101);
	for (const auto &message : observed.gameplay.at("messages"))
		if (message.at("parameters").value("operation", std::string{}) == "scan" &&
		    message.at("payload").at("fields").at("status") == "accepted")
			matching_reply |= message.at("payload").at("fields").at("capability") == "compartment_condition" &&
			                  message.at("parameters").at("capability") == "compartment condition" &&
			                  message.at("body_key").get<std::string>().find("{capability}") != std::string::npos;
	require(matching_reply, "scan acceptance prose and typed payload describe the same capability and target");
	for (const auto mode : {"unknown_capability", "premature_completion"})
	{
		auto invalid = world.save();
		for (auto &body : invalid["bodies"])
			if (body.at("body").at("id") == 1)
				for (auto &controller : body["console_controllers"]["controllers"])
					if (controller.at("id") == "scanner")
					{
						auto &request = controller["coordination"][0][0];
						if (std::string(mode) == "unknown_capability")
							request["capability"] = "hidden_ship_definition";
						else
						{
							request["status"] = 3;
							request["evidence_revision"] = 0;
						}
					}
		rejects(
		    [&]
		    {
			    World malformed;
			    malformed.restore(catalog, invalid);
		    },
		    "invalid capability or unproven scan completion cannot enter a checkpoint");
	}
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 40);
	ticks(resumed, 40);
	const auto final = saved_controller(world, "requester");
	require(final.at("coordination")[1].size() == 1 && final.at("coordination")[1][0].at("status") == 3 &&
	            final.at("coordination")[1][0].at("evidence_revision") >=
	                final.at("coordination")[1][0].at("request_revision") &&
	            world.save() == resumed.save(),
	        "scan completion requires fresh permitted evidence and resumes without duplicate requests");
}
// Scoped objectives and observed targets, with no lookup of undisclosed findings.
void scan_candidate_contract()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["standing_directives"] = Json::parse(R"json([
	 {"id":"specified","behavior":"scan","tier":"objective_other","target":2,"system":"scanner","title_key":"Inspect specified contact"},
	 {"id":"survey","behavior":"scan","tier":"directive_other","title_key":"Survey known contacts"}])json");
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "scanner")
		{
			controller.erase("fallback");
			controller["tree"] = "test.scored-scanner";
		}
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["ai"] = {"scanner"};
	auto &human = ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"];
	human.erase(std::remove(human.begin(), human.end(), "scanner"), human.end());
	source["maps"][0]["entities"][1]["x_mm"] = 90000;
	source["maps"][0]["entities"][2]["y_mm"] = 40000;
	for (const auto &tree : Json::parse(R"json([
	 {"id":"test.scan-selected","context":"controller","command_bindings":[{"channel":"target","action":"sensor_target"}],
	  "controller_action":{"label":"Inspect the selected permitted contact","inputs":[{"name":"target","type":"integer"}]},
	  "initial":"scan","states":[{"id":"scan","running":[{"action":"write.target","arguments":[{"observation":"input.target"}]}],
	    "exit":[{"action":"write.target","arguments":[0]}]}]},
	 {"id":"test.scored-scanner","context":"controller","scan_observations":true,
	  "command_bindings":[{"channel":"target","action":"sensor_target"}],
	  "action_bindings":[{"id":"scan","tree":"test.scan-selected"}],
	  "objective_bindings":[{"id":"scan","behavior":"scan","candidate_source":"scans","action":"scan",
	   "available":[{"left":{"observation":"scan_candidate.available"},"op":"eq","right":true}],
	   "target":{"observation":"scan_candidate.target"},"arguments":[{"observation":"scan_candidate.target"}],
	   "commitment":{"observation":"scan_candidate.continuing"},
	   "factors":{"progress":1000,"travel":{"observation":"scan_candidate.distance"}}}],
	  "initial":"observe","states":[{"id":"observe"}]}
	])json"))
		source["trees"].push_back(tree);
	{
		auto bearing_source = source;
		bearing_source["entities"][0]["systems"]["radar"]["range_mm"] = 30000;
		bearing_source["entities"][0]["systems"]["radar"]["long_range_mm"] = 100000;
		World bearing;
		bearing.load(load(bearing_source), "test.map");
		require(bearing.claim(101, 1, "drive"), "claim bearing-only scanner fixture");
		ticks(bearing, 3);
		const auto observation = *bearing.observe(101);
		bool selected = false;
		for (const auto &controller : observation.gameplay.at("controllers"))
			if (controller.at("id") == "scanner")
				selected = controller.at("commands").at("target").at("value") == 2;
		require(!observation.bearing_contacts.empty() && selected,
		        "a scoped bearing-only target can initiate ordinary active acquisition without hidden range");
	}
	scan_request_contract(source);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim scored scanner Console");
	ticks(world, 3);
	const auto scanner = [](const World &value)
	{
		const auto view = *value.observe(101);
		for (const auto &controller : view.gameplay.at("controllers"))
			if (controller.at("id") == "scanner")
				return controller;
		throw std::runtime_error("missing scored scanner");
	};
	require(scanner(world).at("commands").at("target").at("value") == 2,
	        "a scoped scan objective outranks a nearer survey contact through common arbitration");
	auto changed = source;
	changed["maps"][0]["sensor_findings"][0]["target_entity"] = 3;
	World alternate;
	alternate.load(load(changed), "test.map");
	require(alternate.claim(101, 1, "drive"), "claim alternate hidden finding fixture");
	ticks(alternate, 3);
	require(scanner(world).at("commands") == scanner(alternate).at("commands") &&
	            scanner(world).at("objective_candidates") == scanner(alternate).at("objective_candidates"),
	        "undisclosed finding declarations cannot choose scan targets or alter utility");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 20);
	ticks(resumed, 20);
	require(world.save() == resumed.save(), "physical scoped scanning resumes identically");
	const auto view = *world.observe(101);
	bool acquired = false;
	for (const auto &contact : view.gameplay.at("sensors").at("contacts"))
		if (contact.at("id") == 2)
			acquired |= contact.at("findings").contains("shield_sectors");
	require(acquired, "ordinary scored scan orders acquire useful permitted evidence");
	const auto wire = network::bounded_gameplay(view.gameplay);
	require(wire.at("sensors").at("scanners")[0].contains("powered") &&
	            wire.at("sensors").at("scanners")[0].contains("capabilities"),
	        "native and browser projections preserve scoped scanner capability facts");
	auto hidden = world.save();
	for (auto &body : hidden["bodies"])
		if (body.at("body").at("id") == 2)
			body["body"]["position"]["x"] = 900000;
	world.restore(catalog, hidden);
	ticks(world, 3);
	require(scanner(world).at("commands").at("target").at("value") == 3,
	        "an unavailable scoped scan leaves its objective active and falls through to known survey work");
}
