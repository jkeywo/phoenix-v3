#pragma once
void typed_dialogue_finding_cases()
{
	auto source = fixture();
	auto &profile = source["entities"][0]["control_layout"]["profiles"][0]["consoles"]["drive"];
	profile["human"] = {"propulsion"};
	profile["ai"] = {"comms"};
	// The fixture DSL grants native operations over every attached fitting, so
	// propulsion also owns a scoped conversation operation. Use an unrelated
	// Controller with no fitting to exercise addressed-record isolation.
	source["entities"][0]["control_layout"]["controllers"].push_back(
	    {{"id", "private_reader"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "marker"}, {"action", "intent_integer"}, {"initial", 0}, {"consumers", Json::array()}}})}});
	profile["ai"].push_back("private_reader");
	source["entities"][0]["control_layout"]["profiles"][1]["consoles"]["drive"]["ai"].push_back("private_reader");
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.dialogue")
		{
			tree["prompts"]["question"]["ai_choice"] = "no";
			tree["prompts"]["question"]["ai_rules"] =
			    Json::array({{{"received_tag", "reactor_critical"}, {"choice", "yes"}}});
		}
	source["maps"][0]["conversations"] = Json::parse(
	    R"([{"id":"typed-tags","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]}])");
	const auto catalog = load(source);
	bool unrelated_reader = false;
	for (const auto &controller : catalog.entities.at("test.controllable").at("control_layout").at("controllers"))
		if (controller.at("id") == "private_reader")
			unrelated_reader = !catalog.control_widgets.at(controller.at("widget").get<std::string>())
			                        .at("operations")
			                        .contains("conversation");
	require(unrelated_reader, "private finding recipient has no authored conversation responsibility");
	World initial;
	initial.load(catalog, "test.map");
	require(initial.claim(1, 1, "drive"), "claim synthetic dialogue Console");
	for (int n = 0; n < 12; ++n)
		initial.tick();
	const auto checkpoint = initial.save();
	for (const auto *mode : {"tag-only", "malformed", "non-array", "non-string", "withdrawn", "claim", "expired",
	                         "other-controller", "other-console"})
	{
		GameplayKnowledge knowledge;
		knowledge.configure(catalog);
		knowledge.restore(checkpoint.at("knowledge"));
		const auto &definitions = knowledge.definition();
		const auto schema = definitions.schema("phoenix.sensor.findings");
		KnowledgeRecordInput record;
		record.schema = schema;
		record.producer = definitions.role("phoenix.scenario");
		record.subject = 2;
		record.provenance = {knowledge.system_id(1, "radio"), initial.ticks(), 1, 0, EvidenceKind::Fact};
		const auto payload = definitions.field(schema, "payload");
		record.fields[payload] = std::string(R"([{"id":"old","observed":12,"tag":"reactor_critical"}])");
		const std::string variant = mode;
		const ConsoleAddress recipient{1, variant == "other-console" ? "observer" : "drive"};
		require(knowledge.publish_scenario_console(recipient, record, initial.ticks()),
		        "production ingestion admits earlier tagged finding");
		record.provenance.revision = 2;
		record.fields[payload] = std::string(R"([{"tag":"reactor_critical"}])");
		if (variant == "malformed")
			record.fields[payload] = std::string("{");
		else if (variant == "non-array")
			record.fields[payload] = std::string(R"({"tag":"reactor_critical"})");
		else if (variant == "non-string")
			record.fields[payload] = std::string(R"([{"tag":42}])");
		else if (variant == "withdrawn")
			record.withdrawn = true;
		else if (variant == "claim")
			record.provenance.kind = EvidenceKind::Claim;
		require(knowledge.publish_scenario_console(recipient, record, initial.ticks()),
		        "production ingestion admits replacement finding without changing its stream");
		if (variant == "tag-only")
		{
			const auto projection = knowledge.project(recipient);
			const auto *cached = projection.decision_payload(projection.records.back());
			const auto *finding = cached ? std::get_if<SensorReportDecision>(cached) : nullptr;
			require(finding && !finding->valid && finding->finding_tags == std::vector<std::string>{"reactor_critical"},
			        "tag-only admission preserves dialogue tags without validating detailed scanner evidence");
		}
		auto state = checkpoint;
		state["knowledge"] = knowledge.save();
		for (auto &row : state["knowledge"]["runtime"]["records"])
			if (row.at("value").at("schema") == schema)
			{
				if (variant == "other-controller")
					row["recipient_actor"] = knowledge.controller_id({1, "drive", "private_reader"});
				if (variant == "expired" && row.at("value").at("revision") == 2)
					row["expires"] = initial.ticks() + 1;
			}
		World world, restored;
		world.restore(catalog, state);
		restored.restore(catalog, world.save());
		for (int n = 0; n < 25; ++n)
		{
			world.tick();
			restored.tick();
		}
		require(world.save() == restored.save(),
		        "typed finding admission resumes without changing dialogue continuation");
		require(world.save().at("conversations").at("threads")[0].at("prompt").at("answer") ==
		            (variant == "tag-only" ? "yes" : "no"),
		        ("dialogue respects latest typed finding, freshness and recipient scope: " + variant).c_str());
	}
}

void received_information_cases()
{
	for (const bool scanner_human : {false, true})
		for (const bool comms_human : {false, true})
			for (const bool acquire : {false, true})
			{
				auto source = fixture();
				auto &ship = source["entities"][0];
				ship["systems"]["radar"]["capabilities"] = {{"findings", 0}, {"basic_identity", 0}};
				ship["control_layout"]["controllers"].push_back(
				    {{"id", "scanner"},
				     {"console", "drive"},
				     {"fallback", "hold"},
				     {"commands", Json::array({{{"id", "target"},
				                                {"action", "sensor_target"},
				                                {"initial", 0},
				                                {"consumers", {"radar"}}}})}});
				auto &profile = ship["control_layout"]["profiles"][0]["consoles"]["drive"];
				profile["human"] = Json::array({"propulsion"});
				profile["ai"] = Json::array();
				profile[scanner_human ? "human" : "ai"].push_back("scanner");
				profile[comms_human ? "human" : "ai"].push_back("comms");
				ship["control_layout"]["profiles"][1]["consoles"]["drive"]["ai"].push_back("scanner");
				// Only the observing ship has a scanner; the casualty cannot target itself.
				auto target = ship;
				target["id"] = "test.remote";
				target["control_layout"]["controllers"].back()["commands"][0]["initial"] = 0;
				source["entities"].push_back(target);
				if (acquire && !scanner_human)
				{
					source["entities"][0]["control_layout"]["controllers"].back()["tree"] = "test.scan-policy";
					for (const auto &tree : Json::parse(R"json([
 {"id":"test.scan-action","context":"controller","command_bindings":[{"channel":"target","action":"sensor_target"}],
 "controller_action":{"label":"Scan a disclosed objective","inputs":[{"name":"target","type":"integer"}]},
 "initial":"scan","states":[{"id":"scan","running":[{"action":"write.target","arguments":[{"observation":"input.target"}]},{"action":"action.succeed","arguments":["scan_admitted"]}]}]},
 {"id":"test.scan-policy","context":"controller","scan_observations":true,"command_bindings":[{"channel":"target","action":"sensor_target"}],
 "action_bindings":[{"id":"scan","tree":"test.scan-action"}],"objective_bindings":[{"behavior":"scan","candidate_source":"scans","action":"scan",
 "available":[{"left":{"observation":"scan_candidate.available"},"op":"eq","right":true}],
 "target":{"observation":"scan_candidate.target"},"arguments":[{"observation":"scan_candidate.target"}]}],
 "initial":"observe","states":[{"id":"observe"}]}])json"))
						source["trees"].push_back(tree);
				}
				source["maps"][0]["entities"][1]["definition"] = "test.remote";
				source["maps"][0]["sensor_findings"] = Json::array({{{"id", "warning"},
				                                                     {"target_entity", 2},
				                                                     {"text_key", "Containment warning"},
				                                                     {"scan_ticks", 3},
				                                                     {"requires", {"findings"}},
				                                                     {"requires_event", "test.objective"},
				                                                     {"tag", "reactor_critical"},
				                                                     {"destination_station", "drive"}}});
				source["maps"][0]["scenario"] = {{"tree", "test.objective"},
				                                 {"bindings", {{"actor", {{"entity", 1}}}}},
				                                 {"events", {"test.objective"}}};
				source["maps"][0]["scenario"]["objectives"] =
				    Json::array({{{"id", "scan"},
				                  {"type", "condition"},
				                  {"behavior", "scan"},
				                  {"behavior_target", {{"entity", 2}}},
				                  {"complete_when", {{"fact", "never"}, {"value", true}}},
				                  {"actor", {{"entity", 1}}},
				                  {"recipients", {{"ship", {{"entity", 1}}}}},
				                  {"title_key", "Inspect the disclosed casualty"}}});
				source["maps"][0]["scenario"]["events"].push_back("test.never");
				source["maps"][0]["scenario"]["fact_bindings"] =
				    Json::array({{{"id", "never"}, {"kind", "event"}, {"event", "test.never"}}});
				source["trees"].push_back(Json::parse(
				    R"({"id":"test.objective","context":"scenario","initial":"start","states":[{"id":"start","entry":[{"action":"emit_scenario_event","arguments":["test.objective",0,false,""]},{"action":"add_objective","arguments":["scan",{"observation":"self.entity"},"scan"]}]}]})"));
				source["maps"][0]["conversations"] = Json::parse(
				    R"([{"id":"decision","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]}])");
				for (auto &tree : source["trees"])
					if (tree["id"] == "test.dialogue")
					{
						tree["prompts"]["question"]["ai_choice"] = "no";
						tree["prompts"]["question"]["ai_rules"] =
						    Json::array({{{"received_tag", "reactor_critical"}, {"choice", "yes"}}});
					}
				const auto catalog = load(source);
				World world;
				world.load(catalog, "test.map");
				require(world.claim(1, 1, "drive"), "information console claimed");
				world.tick();
				if (acquire && scanner_human)
				{
					const auto observed = world.observe(1);
					for (const auto &controller : observed->gameplay.at("controllers"))
						if (controller.at("id") == "scanner")
							require(world.submit(1,
							                     OperatingCommand{
							                         1, {}, 2, {}, "scanner", "target", controller.at("generation")}),
							        "human scan ordered");
				}
				for (int n = 0; n < 35; ++n)
					world.tick();
				const auto saved = world.save();
				const auto &prompt = saved.at("conversations").at("threads")[0].at("prompt");
				require(prompt.at("answer") == (comms_human ? ""
				                                : acquire   ? "yes"
				                                            : "no"),
				        ("AI uses only received tags; human retains decision: " + std::to_string(scanner_human) + "/" +
				         std::to_string(comms_human) + "/" + std::to_string(acquire) + " " + prompt.dump())
				            .c_str());
				int messages = 0;
				const auto observation = world.observe(1);
				const auto knowledge = observation->gameplay;
				for (const auto &message : knowledge.at("messages"))
					if (message.at("body_key") == "Containment warning")
						++messages;
				require(messages == 0, "routine findings reach the permitted board without a forwarding announcement");
				World restored;
				restored.restore(catalog, saved);
				for (int n = 0; n < 5; ++n)
				{
					world.tick();
					restored.tick();
				}
				require(world.save() == restored.save(), "received information and replies resume without duplicates");
			}
	{
		auto source = fixture();
		for (auto &tree : source["trees"])
			if (tree.at("id") == "test.dialogue")
			{
				tree["lines"]["hello"]["received_tag"] = "delivered-line";
				tree["lines"]["unspoken"] = tree["lines"]["hello"];
				tree["lines"]["unspoken"]["received_tag"] = "unspoken-line";
				tree["prompts"]["question"]["ai_rules"] =
				    Json::array({{{"received_tag", "unspoken-line"}, {"choice", "yes"}},
				                 {{"received_tag", "delivered-line"}, {"choice", "no"}}});
			}
		source["maps"][0]["conversations"] = Json::parse(R"json([
{"id":"claimed-line","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]}
])json");
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		for (int i = 0; i < 15; ++i)
			world.tick();
		const auto checkpoint = world.save();
		require(checkpoint.at("conversations").at("threads")[0].at("messages")[0].at("received_tag") ==
		            "delivered-line",
		        "a delivered message persists its exact authored claim");
		World restored;
		restored.restore(catalog, checkpoint);
		for (int i = 0; i < 25; ++i)
		{
			world.tick();
			restored.tick();
		}
		require(world.save() == restored.save(),
		        "received line claims resume without consulting other line definitions");
		require(world.save().at("conversations").at("threads")[0].at("prompt").at("answer") == "no",
		        "an unspoken line sharing the same prose key supplies no decision evidence");
	}
}
