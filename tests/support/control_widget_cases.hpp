#pragma once

// Synthetic authored channels exercise the actual pack loader, Controller
// compiler, authority admission, tree interpreter and physical engine path.
inline void control_widget_cases()
{
	auto source = controller_fixture();
	source["control_widgets"] = Json::parse(R"json([{
      "id":"test.widget.drive","schema":1,"tree":"test.widget.policy",
      "equipment":{"engines":{"multiple":true}},
      "commands":[
        {"id":"speed","action":"speed_mm_s","initial":0,"consumers":["$engines"]},
        {"id":"choice","action":"intent_integer","minimum":0,"maximum":1,"initial":1,"consumers":[]},
        {"id":"auxiliary","action":"intent_integer","minimum":0,"maximum":2,"initial":0,"consumers":[]}],
      "data":{"choice":{"binding":"command.choice","label":"Selected option","placement":"main"},
              "speed":{"binding":"command.speed","label":"Requested speed","placement":"details"}},
      "parts":["main","left","right"],
      "levels":[{"id":"automated","human":[]},
                {"id":"assisted","human":["choice"]},
                {"id":"detailed","human":["choice","speed"],"data":{"speed":"main"}}]
    }])json");
	source["trees"].push_back(Json::parse(R"json({
      "id":"test.widget.policy","context":"controller",
      "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
      "memory":[{"name":"selected","type":"boolean","initial":false}],
      "initial":"operate","states":[{"id":"operate","running":[
        {"attempt":[
          {"tasks":[{"action":"write.auxiliary","arguments":[2]},
                    {"action":"write.choice","arguments":[0]},
                    {"action":"write.speed","arguments":[6000]}]},
          {"tasks":[{"action":"write.choice","arguments":[1]},
                    {"action":"write.speed","arguments":[3000]}]}
        ],"result":"selected"}
      ]}]
    })json"));
	auto &layout = source["entities"][0]["control_layout"];
	layout["schema"] = 2;
	layout["controllers"] = Json::array({{{"id", "propulsion"},
	                                      {"console", "drive"},
	                                      {"widget", "test.widget.drive"},
	                                      {"equipment", {{"engines", {"port", "starboard"}}}},
	                                      {"star_levels", {{"1", "assisted"}, {"2", "assisted"}, {"3", "detailed"}}},
	                                      {"data_placement", {{"2", {{"choice", "details"}}}}}}});
	layout["profiles"][0]["stars"] = 1;
	layout["profiles"][1]["stars"] = 3;
	for (auto &profile : layout["profiles"])
		profile.erase("consoles");
	{
		// Native work uses the same admitted-attempt path as scalar settings.
		auto native = combat_fixture();
		auto &controls = native["entities"][0]["control_layout"];
		controls["schema"] = 2;
		controls["profiles"] = Json::array({{{"id", "direct"}, {"stars", 1}}});
		native["control_widgets"] = Json::array();
		for (auto &instance : controls["controllers"])
		{
			const auto identity = instance.at("id").get<std::string>();
			const auto name = "test.native." + identity;
			auto commands = instance.at("commands");
			Json human = Json::array();
			for (auto &command : commands)
			{
				command["consumers"] = {"$subject"};
				human.push_back(command.at("id"));
			}
			Json definition{{"id", name},
			                {"schema", 1},
			                {"equipment", {{"subject", Json::object()}}},
			                {"commands", commands},
			                {"data", Json::object()},
			                {"parts", {"main"}},
			                {"levels", Json::array({{{"id", "automated"}, {"human", Json::array()}},
			                                        {{"id", "manual"}, {"human", human}}})}};
			if (identity == "team-a")
			{
				definition["operations"] = {{"team_assignment", {"$subject"}}, {"team_work", {"$subject"}}};
				definition["levels"][1]["human"].push_back("team_assignment");
				definition["tree"] = "test.native.team";
			}
			native["control_widgets"].push_back(definition);
			instance = {{"id", identity},
			            {"console", instance.at("console")},
			            {"widget", name},
			            {"equipment", {{"subject", identity}}},
			            {"star_levels", {{"1", "manual"}, {"2", "manual"}, {"3", "manual"}}}};
		}
		native["trees"].push_back(Json::parse(R"json({"id":"test.native.team","context":"controller",
		 "team_commands":true,"command_bindings":[{"channel":"enabled","action":"system_enabled","type":"boolean"}],
		 "initial":"run","states":[{"id":"run","running":[{"attempt":[
		  {"tasks":[{"action":"team.order","arguments":["team-a","auto/aft",1]}]},
		  {"tasks":[{"action":"write.enabled","arguments":[false]}]}]}]}]})json"));
		{
			auto overlapping = native;
			for (auto &definition : overlapping["control_widgets"])
				if (definition["id"] == "test.native.team-b")
				{
					definition["equipment"]["other"] = Json::object();
					definition["operations"] = {{"team_assignment", {"$other"}}};
				}
			for (auto &instance : overlapping["entities"][0]["control_layout"]["controllers"])
				if (instance["id"] == "team-b")
					instance["equipment"]["other"] = "team-a";
			rejected(
			    [&]
			    {
				    (void)load(overlapping);
			    },
			    "overlapping widget owners");
		}
		const auto native_catalog = load(native);
		World world;
		world.load(native_catalog, "test.map");
		require(world.claim(102, 1, "observer"), "claim scoped native team widget");
		world.tick();
		const auto team = controller(world, 102, "team-a");
		const auto supported = controller(world, 102, "team-b").at("actions");
		require(std::any_of(supported.begin(), supported.end(),
		                    [](const auto &action)
		                    {
			                    return action.at("id") == "officer_support";
		                    }),
		        "shared widget bindings retain the existing officer support capability");
		require(team.at("commands").at("enabled").at("value") == 1,
		        "refused native assignment cannot bypass a human command during fallback");
		for (const auto &row : world.observe(102)->gameplay.at("teams"))
			if (row.at("id") == "team-a")
				require(row.at("assigned_compartment") != 1,
				        "native preflight never moves a human-owned team destination");
		world.release(102);
		world.tick();
		require(world.claim(102, 1, "observer"), "reclaim native widget after unattended assignment");
		for (const auto &row : world.observe(102)->gameplay.at("teams"))
			if (row.at("id") == "team-a")
				require(row.at("assigned_compartment") == 1,
				        "unattended native widget admits the same tree's physical team assignment");
		World resumed;
		resumed.restore(native_catalog, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "native widget effects preserve checkpoint continuation");
	}
	{
		auto shared = source;
		auto &definition = shared["control_widgets"][0];
		definition.erase("tree");
		definition["equipment"]["engines"]["multiple"] = false;
		definition["policy"] = Json::parse(R"json({"initial":"run",
		 "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
		 "system_bindings":[{"name":"enabled","system":"$engines","field":"enabled","type":"boolean"}],
		 "states":[{"id":"run","running":[{"attempt":[
		  {"conditions":[{"left":{"observation":"enabled"},"op":"eq","right":true}],
		   "tasks":[{"action":"write.choice","arguments":[0]},{"action":"write.speed","arguments":[7000]}]},
		  {"tasks":[{"action":"write.choice","arguments":[1]},{"action":"write.speed","arguments":[3000]}]}]}]}]})json");
		auto routine = definition.at("policy");
		routine["id"] = "test.shared-widget-routine";
		routine["context"] = "controller";
		routine["system_bindings"][0]["system"] = "port";
		shared["trees"].push_back(routine);
		definition["policy"] = {
		    {"extends", "test.shared-widget-routine"},
		    {"system_bindings",
		     Json::array({{{"name", "enabled"}, {"system", "$engines"}, {"field", "enabled"}, {"type", "boolean"}}})}};
		shared["entities"][0]["control_layout"]["controllers"][0]["equipment"]["engines"] = "port";
		auto second = shared["entities"][0];
		second["id"] = "test.other-widget-ship";
		second["systems"]["starboard"]["enabled"] = false;
		second["control_layout"]["controllers"][0]["equipment"]["engines"] = "starboard";
		shared["entities"].push_back(second);
		shared["maps"][0]["entities"].push_back(
		    {{"id", 2}, {"definition", "test.other-widget-ship"}, {"x_mm", 100000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		const auto shared_catalog = load(shared);
		require(
		    shared_catalog.entities.at("test.controllable").at("control_layout").at("controllers")[0].at("widget") ==
		        "test.widget.drive",
		    "policy compilation preserves authored shared widget identities");
		World world;
		world.load(shared_catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(201, 2, "drive"),
		        "claim shared widget on different ship fittings");
		world.tick();
		require(controller(world, 101, "propulsion").at("commands").at("speed").at("value") == 3000 &&
		            controller(world, 201, "propulsion").at("commands").at("speed").at("value") == 3000,
		        "shared role-bound policy preserves the human-setting fallback on both ships");
		require(world.release(101) && world.release(201), "release both shared widget instances");
		world.tick();
		auto saved = world.save();
		require(saved["bodies"][0]["console_controllers"]["controllers"][0]["commands"][0]["value"] == 7000 &&
		            saved["bodies"][1]["console_controllers"]["controllers"][0]["commands"][0]["value"] == 3000,
		        "one shared policy reads its explicitly bound equipment on each ship");
		World resumed;
		resumed.restore(shared_catalog, saved);
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "shared role-bound policy resumes deterministically");
		auto edited = shared_catalog;
		edited.trees.at(
		    "test.shared-widget-routine")["states"][0]["running"][0]["attempt"][0]["tasks"][1]["arguments"][0] = 8000;
		World recompiled;
		recompiled.load(prepared_catalog(edited), "test.map");
		recompiled.tick();
		require(
		    recompiled.save()["bodies"][0]["console_controllers"]["controllers"][0]["commands"][0]["value"] == 8000,
		    "recompilation resolves edited inherited policies from authored bindings rather than a frozen expansion");
	}
	{
		auto repeated = source;
		auto &definition = repeated["control_widgets"][0];
		definition.erase("tree");
		definition["repeat"] = "engines";
		definition["member_policy"] = Json::parse(R"json({"initial":"run",
		 "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
		 "system_bindings":[{"name":"enabled","system":"$engines","field":"enabled","type":"boolean"}],
		 "states":[{"id":"run","running":[{"attempt":[
		  {"conditions":[{"left":{"observation":"enabled"},"op":"eq","right":true}],
		   "tasks":[{"action":"write.choice","arguments":[0]},{"action":"write.speed","arguments":[7000]}]},
		  {"tasks":[{"action":"write.choice","arguments":[1]},{"action":"write.speed","arguments":[3000]}]}]}]}]})json");
		repeated["entities"][0]["systems"]["starboard"]["enabled"] = false;
		const auto repeated_catalog = load(repeated);
		World world;
		world.load(repeated_catalog, "test.map");
		require(world.claim(101, 1, "drive"), "claim one repeated equipment-set widget");
		world.tick();
		const auto state = controller(world, 101, "propulsion");
		require(state.at("commands").size() == 6 && state.at("commands").at("m0.speed").at("value") == 3000 &&
		            state.at("commands").at("m1.speed").at("value") == 3000,
		        "each member has separate channels while the same human-setting refusal drives fallback");
		world.release(101);
		world.tick();
		auto saved = world.save();
		const auto &commands = saved["bodies"][0]["console_controllers"]["controllers"][0]["commands"];
		require(commands[0]["value"] == 7000 && commands[3]["value"] == 3000,
		        "one Controller evaluates each member against its own bound physical observations");
		World resumed;
		resumed.restore(repeated_catalog, saved);
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "independent member policy continuations survive restore");
	}
	{
		auto bridge = source;
		bridge["entities"][0]["bridge"] = {{"initial", "routine"},
		                                   {"alert_console", "drive"},
		                                   {"conditions", Json::array({{{"id", "routine"}, {"label", "Routine"}},
		                                                               {{"id", "danger"}, {"label", "Danger"}}})}};
		bridge["control_widgets"].push_back(Json::parse(R"json({
		 "id":"test.widget.bridge","schema":1,"equipment":{},"commands":[],
		 "operations":{"alert":[],"stance":[]},"tree":"test.widget.bridge-ai",
		 "parts":["main"],"data":{"alert":{"binding":"board:/alert/label","label":"Alert","placement":"main"}},
		 "levels":[{"id":"automated","human":[]},{"id":"manual","human":["alert","stance"]}]})json"));
		bridge["trees"].push_back(Json::parse(R"json({
		 "id":"test.widget.bridge-ai","context":"controller","initial":"run",
		 "states":[{"id":"run","running":[{"attempt":[
		  {"tasks":[{"action":"alert.set","arguments":["",1,0]}]},
		  {"tasks":[]}]}]}]})json"));
		bridge["entities"][0]["control_layout"]["controllers"].push_back(
		    {{"id", "bridge"},
		     {"console", "drive"},
		     {"widget", "test.widget.bridge"},
		     {"equipment", Json::object()},
		     {"star_levels", {{"1", "manual"}, {"2", "manual"}, {"3", "automated"}}}});
		{
			auto duplicate = bridge;
			auto owner = duplicate["entities"][0]["control_layout"]["controllers"].back();
			owner["id"] = "second-bridge";
			duplicate["entities"][0]["control_layout"]["controllers"].push_back(owner);
			rejected(
			    [&]
			    {
				    (void)load(duplicate);
			    },
			    "native operation has overlapping widget owners");
		}
		const auto bridge_catalog = load(bridge);
		World world;
		world.load(bridge_catalog, "test.map");
		require(world.claim(101, 1, "drive"), "claim bridge widget");
		world.tick();
		require(world.observe(101)->gameplay.at("alert").at("id") == "routine",
		        "shared tree cannot change a human-owned alert operation");
		require(!world.submit(101, {1, "alert.set", 1}), "bridge operation rejects missing widget authority");
		const auto view = network::read_observation(network::observation_json(*world.observe(101)), bridge_catalog);
		OperatingCommand alert;
		bool found = false;
		for (const auto &button : view.gameplay.at("controls"))
			if (button.at("action") == "alert.set" && button.at("value") == 1)
			{
				alert = decode_operating_command(button);
				alert.target = 1;
				found = true;
			}
		require(found && alert.controller == "bridge", "remote descriptor carries bridge owner");
		require(eligible_console_command(view, Json{{"widgets", Json::array()}}, Json::object(), alert).has_value(),
		        "remote advisory admits the projected bridge descriptor");
		require(world.submit(101, alert), "bridge admits current operator and domain revisions");
		world.tick();
		require(world.observe(101)->gameplay.at("alert").at("id") == "danger",
		        "bridge widget uses existing alert mechanism");
		World resumed;
		resumed.restore(bridge_catalog, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "bridge widget authority and continuation survive restore");
	}
	const auto catalog = load(source);
	const auto widget = compile_control_widget(catalog, "test.widget.drive");
	{
		auto native_only = source;
		native_only["consoles"][0]["objectives"] = true;
		auto &definition = native_only["control_widgets"][0];
		definition["operations"] = {{"logistics", {"$engines"}}};
		definition["levels"][2]["human"] = {"speed", "choice", "auxiliary"};
		World world;
		world.load(load(native_only), "test.map");
		require(world.prepare_console_profile({1, "drive"}, "automatic") && world.claim(101, 1, "drive"),
		        "claim scalar-manual widget with delegated native work");
		require(world.observe(101)->gameplay.at("objective_board").at("can_select").get<bool>(),
		        "delegated native operations count as AI work for ship Command");
	}
	{
		auto native_source = source;
		auto &definition = native_source["control_widgets"][0];
		definition["operations"] = {{"logistics", {"$engines"}}};
		definition["levels"][2]["human"].push_back("logistics");
		const auto native_catalog = load(native_source);
		World native;
		native.load(native_catalog, "test.map");
		require(native.claim(101, 1, "drive"), "claim widget with native operation responsibility");
		auto before = controller(native, 101, "propulsion");
		require(before.at("operations").at("logistics").at("human") == false &&
		            before.at("operations").at("logistics").at("consumers") == Json::array({"port", "starboard"}),
		        "native operation binds the same explicit equipment roles and starts delegated");
		require(native.submit(101, {1, "preset", 1, {}}), "select native operation workload");
		native.tick();
		const auto after = controller(native, 101, "propulsion");
		require(after.at("operations").at("logistics").at("human") == true &&
		            after.at("generation") != before.at("generation"),
		        "widget level changes native authority through the common operator generation");
		const auto projected =
		    network::read_observation(network::observation_json(*native.observe(101)), native_catalog);
		const auto &remote = projected.gameplay.at("controllers")[0];
		require(console_operation_human(remote, "logistics", "port") &&
		            !console_operation_human(remote, "logistics", "foreign") &&
		            !console_operation_human(remote, "medical", "port"),
		        "remote widget admission preserves operation scope without granting another equipment set or workflow");
		World restored;
		restored.restore(native_catalog, native.save());
		require(native.save() == restored.save(), "native operation ownership survives a real checkpoint restore");
		require(native.release(101), "release native operation widget");
		const auto checkpoint = native.save();
		const auto &saved = checkpoint.at("bodies")[0].at("console_controllers").at("controllers")[0];
		require(saved.at("human_operations").empty(), "absence returns every native operation to AI");
	}
	{
		auto staged_source = source;
		staged_source["trees"].back()["states"][0]["running"] = Json::parse(R"json([
          {"action":"write.auxiliary","arguments":[{"observation":"tick"}]},
          {"attempt":[
            {"conditions":[{"left":{"observation":"command.auxiliary.pending"},"op":"eq","right":true},
                           {"left":{"observation":"command.auxiliary.revision"},"op":"gt","right":{"observation":"command.auxiliary.consumed"}}],
             "tasks":[{"action":"write.speed","arguments":[4321]}]},
            {"tasks":[{"action":"write.speed","arguments":[0]}]}
          ]}
        ])json");
		World staged;
		staged.load(load(staged_source), "test.map");
		require(staged.claim(101, 1, "drive"), "claim shared widget with same-evaluation metadata guards");
		staged.tick();
		staged.tick();
		require(requested(staged, "port") == 4321,
		        "fallback guards read staged command receipts and revisions alongside staged values");
	}
	{
		auto bridge_source = source;
		auto command = bridge_source["consoles"][0];
		command["id"] = "test.widget.command";
		command["command"] = true;
		command["human_seeking"] = false;
		bridge_source["consoles"].push_back(command);
		bridge_source["entities"][0]["stations"][0]["consoles"][1]["definition"] = "test.widget.command";
		bridge_source["consoles"][0]["stances"] =
		    Json::array({{{"id", "routine"}, {"label", "Routine"}}, {{"id", "hold"}, {"label", "Hold"}}});
		bridge_source["control_widgets"].push_back(Json::parse(
		    R"json({"id":"test.widget.command-owner","schema":1,"commands":[],"equipment":{},"operations":{"stance":[]},"levels":[{"id":"automated","human":[]},{"id":"manual","human":["stance"]}]})json"));
		bridge_source["entities"][0]["control_layout"]["controllers"].push_back(
		    {{"id", "command"},
		     {"console", "observer"},
		     {"widget", "test.widget.command-owner"},
		     {"equipment", Json::object()},
		     {"star_levels", {{"1", "manual"}, {"2", "manual"}, {"3", "manual"}}}});

		World bridge;
		bridge.load(load(bridge_source), "test.map");
		require(bridge.claim(101, 1, "drive") && bridge.claim(31, 1, "observer"),
		        "claim partial widget and bridge Command");
		OperatingCommand stance{1, "stance.set", 1, "drive"};
		bool offered = false;
		const auto command_view = bridge.observe(31);
		for (const auto &control : command_view->gameplay.at("controls"))
			if (control.value("action", "") == "stance.set" && control.value("system", "") == "drive")
			{
				stance.generation = control.at("generation");
				stance.controller = control.at("controller");
				stance.arguments = control.at("arguments").get<std::vector<std::int64_t>>();
				offered = true;
			}
		if (!offered)
			throw std::runtime_error("Command did not offer stance: " + command_view->gameplay.at("controls").dump() +
			                         " / " + controller(bridge, 101, "propulsion").dump());
		require(bridge.submit(31, stance), "Command can direct a delegated part of an interactive widget");
		bridge.tick();
		require(bridge.observe(101)->gameplay.at("stance").at("id") == "hold" &&
		            controller(bridge, 101, "propulsion")["commands"]["choice"]["human"] == true,
		        "AI stance selection preserves the human command allocation in the same widget");
	}
	{
		auto action_source = source;
		auto action = action_source["trees"].back();
		action["id"] = "test.widget.work";
		action["controller_action"] = {{"label", "Operate drive"}, {"inputs", Json::array()}};
		action_source["trees"].back() = Json::parse(R"json({
          "id":"test.widget.policy","context":"controller",
          "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
          "action_bindings":[{"id":"operate","tree":"test.widget.work"}],
          "decision_candidates":[{"id":"operate","description":"Perform the shared drive policy",
            "tasks":[{"action":"select.operate","arguments":["model-duty"]}]}],
          "initial":"operate","states":[{"id":"operate","running":[
            {"action":"select.operate","arguments":["standing-duty"]}]}]
        })json");
		action_source["trees"].push_back(action);
		World acting;
		acting.load(load(action_source), "test.map");
		require(acting.claim(101, 1, "drive"), "claim partially controlled action widget");
		acting.tick();
		const auto state = controller(acting, 101, "propulsion");
		require(
		    state["commands"]["speed"]["value"] == 3000 && state["actions"][0]["available"] == true,
		    "shared widget action selection keeps running under partial ownership and uses admitted branch fallback");
		require(acting.configure_controller_provider({1, "drive", "propulsion"}, "anyjev"),
		        "configure model provider on a partially controlled widget");
		const auto snapshot = acting.controller_decision_snapshot({1, "drive", "propulsion"});
		require(!snapshot.is_null() && acting.controller_ai(1)[0]["human"] == true &&
		            acting.controller_ai(1)[0]["ai_running"] == true &&
		            acting.submit_controller_decision({{"snapshot", snapshot}, {"candidate", "operate"}}),
		        "partial human ownership permits model decisions through the same admitted executor");
		acting.tick();
		require(controller(acting, 101, "propulsion")["commands"]["choice"]["value"] == 1 &&
		            controller(acting, 101, "propulsion")["commands"]["speed"]["value"] == 3000,
		        "model-selected widget action cannot overwrite human settings and keeps authored fallback");
		require(acting.submit(101, {1, "preset", 1, {}}), "change partial ownership while model input is outstanding");
		acting.tick();
		require(!acting.submit_controller_decision({{"snapshot", snapshot}, {"candidate", "operate"}}),
		        "ownership changes invalidate provider decisions as well as human command descriptors");
	}
	{
		WorkshopDocument draft;
		draft.load_pack(archive(source));
		require(draft.preview_definition(WorkshopDefinitionKind::ControlWidget, "test.widget.drive").at("commands") ==
		                source["control_widgets"][0].at("commands") &&
		            draft.preview_ui()->control_widgets.contains("test.widget.drive") &&
		            draft.preview_ui()->entities.contains("test.controllable") &&
		            draft.preview_equipment().control_widgets.contains("test.widget.drive"),
		        "Workshop retains shared widget definitions and ship binding context in authoring previews");
		draft.definitions()["entities"][0]["control_layout"]["controllers"][0]["data_placement"]["3"]["choice"] =
		    "details";
		WorkshopDocument reopened;
		reopened.load_pack(draft.export_pack());
		const auto exported = reopened.verify();
		const auto layout = compile_controllable_layout(exported, "test.controllable");
		require(layout.controllers[0].data_placement.at(3).at("choice") == WidgetDataDestination::Details,
		        "Workshop export and reopen preserve independent per-star data placement and shared bindings");
	}
	{
		auto placed = source;
		auto console = placed["consoles"][0];
		console["id"] = "test.widget.console";
		console["layout"] = Json::parse(R"json({"schema":1,
          "root":{"id":"wide","kind":"row","min_width":700,"children":[
            {"id":"left","kind":"component","component":"control/propulsion/left"},
            {"id":"right","kind":"component","component":"control/propulsion/right"}]},
          "compact":{"id":"small","kind":"component","component":"control/propulsion/complete"}})json");
		placed["consoles"].push_back(console);
		placed["entities"][0]["stations"][0]["consoles"][0]["definition"] = "test.widget.console";
		placed["control_widgets"][0]["part_bindings"] = {
		    {"left", {{"commands", {"choice"}}, {"data", {"choice"}}, {"details", true}}},
		    {"right", {{"commands", {"speed"}}, {"data", {"speed"}}}}};
		const auto bound = load(placed);
		const auto program = compile_ui_layout(bound, bound.consoles.at("test.widget.console"));
		require(resolve_ui_layout(program, {0, 0, 1200, 700}, 1).placements.size() == 2 &&
		            resolve_ui_layout(program, {0, 0, 360, 700}, 1).placements[0].component ==
		                "control/propulsion/complete",
		        "one logical widget has split wide parts and a reunited compact presentation");
		{
			auto invalid = placed;
			invalid["control_widgets"][0]["part_bindings"]["right"]["values"]["speed"] = {18000};
			rejected(
			    [&]
			    {
				    (void)load(invalid);
			    },
			    "part value exceeds bound command");
			invalid = placed;
			invalid["control_widgets"][0]["commands"][1]["choices"] =
			    Json::array({{{"value", 0}, {"label_key", "Off"}}, {{"value", 1}, {"label_key", "On"}}});
			invalid["control_widgets"][0]["commands"][1]["maximum"] = 2;
			invalid["control_widgets"][0]["part_bindings"]["left"]["values"]["choice"] = {2};
			rejected(
			    [&]
			    {
				    (void)load(invalid);
			    },
			    "part value exceeds bound command");
			invalid = placed;
			invalid["control_widgets"][0]["data"]["choice"]["label"] = "";
			rejected(
			    [&]
			    {
				    (void)load(invalid);
			    },
			    "localisable label");
		}
		{
			auto conditional = placed;
			conditional["consoles"].back()["layout"]["root"]["children"][1]["when"] = {{"min_stars", 3}};
			(void)load(conditional);
			conditional["consoles"].back()["layout"]["root"]["children"][0]["when"] = {{"min_stars", 2}};
			rejected(
			    [&]
			    {
				    (void)load(conditional);
			    },
			    "human command has no placed part");
			auto missing_data = placed;
			missing_data["control_widgets"][0]["part_bindings"]["right"]["data"] = Json::array();
			rejected(
			    [&]
			    {
				    (void)load(missing_data);
			    },
			    "main data item has no placed part");
			missing_data = placed;
			missing_data["control_widgets"][0]["part_bindings"]["left"]["details"] = false;
			rejected(
			    [&]
			    {
				    (void)load(missing_data);
			    },
			    "detail data has no placed entry point");
			missing_data = placed;
			missing_data["control_widgets"][0]["part_bindings"]["right"]["commands"] = Json::array();
			rejected(
			    [&]
			    {
				    (void)load(missing_data);
			    },
			    "human command has no placed part");
		}
		auto missing = placed;
		missing["consoles"].back()["layout"]["root"]["children"][0]["component"] = "control/unknown/left";
		missing["consoles"].back()["layout"]["root"]["children"][1]["component"] = "control/unknown/right";
		missing["consoles"].back()["layout"]["compact"]["component"] = "control/unknown/complete";
		rejected(
		    [&]
		    {
			    (void)load(missing);
		    },
		    "unbound control widget");
		missing = placed;
		missing["consoles"].back()["layout"]["root"] = {
		    {"id", "omitted"}, {"kind", "component"}, {"component", "target-info"}};
		missing["consoles"].back()["layout"]["compact"] = missing["consoles"].back()["layout"]["root"];
		rejected(
		    [&]
		    {
			    (void)load(missing);
		    },
		    "human command has no placed part");
		missing = placed;
		missing["control_widgets"][0]["part_bindings"]["right"]["commands"] = {"unknown"};
		rejected(
		    [&]
		    {
			    (void)load(missing);
		    },
		    "unknown or duplicate part member");
		missing = placed;
		missing["consoles"].back()["layout"]["root"]["children"].push_back(
		    {{"id", "duplicate"}, {"kind", "component"}, {"component", "control/propulsion/complete"}});
		rejected(
		    [&]
		    {
			    (void)load(missing);
		    },
		    "complete widget or its named parts");
	}
	{
		auto invalid = source;
		invalid["entities"][0]["control_layout"]["schema"] = 1;
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "schema 2 shared widgets required");
		invalid = source;
		invalid["entities"][0]["control_layout"]["controllers"][0]["star_levels"] = {
		    {"1", "detailed"}, {"2", "assisted"}, {"3", "automated"}};
		const auto retired = load(invalid);
		World replaced;
		replaced.load(retired, "test.map");
		require(replaced.claim(101, 1, "drive"), "claim non-monotonic widget responsibility mapping");
		require(controller(replaced, 101, "propulsion")["commands"]["speed"]["human"],
		        "first star may select detailed responsibility on an assisted widget");
		require(replaced.submit(101, {1, "preset", 1, {}}), "select automated higher-star replacement");
		replaced.tick();
		require(!controller(replaced, 101, "propulsion")["human"],
		        "higher-star replacement can automate a retired assisted widget");
		invalid = source;
		invalid["control_widgets"][0]["levels"][2]["human"] = {"speed"};
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "without explicit replacement");
		invalid = source;
		invalid["entities"][0]["control_layout"]["controllers"][0]["data_placement"]["2"]["missing"] = "main";
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "unknown data item");
		invalid = source;
		invalid["control_widgets"][0]["data"]["speed"]["binding"] = "system.undeclared.hidden";
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "unresolved command or equipment data binding");
		invalid = source;
		invalid["control_widgets"][0]["part_bindings"] = {{"main", {{"commands", {"speed"}}, {"ranges", {"speed"}}}}};
		invalid["control_widgets"][0]["commands"][0]["action"] = "impulse";
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "ranges require numeric settings");
		invalid = source;
		invalid["control_widgets"][0]["commands"][0]["consumers"] = {"port"};
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "shared command consumers must use declared equipment roles");
		invalid = source;
		invalid["entities"][0]["control_layout"]["controllers"][0]["policy"] = "combat";
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "instance cannot override shared commands or tree");
	}
	require(control_widget_data_placement(widget, "assisted", 1).at("choice") == WidgetDataDestination::Main &&
	            control_widget_data_placement(widget, "assisted", 2, {{"2", {{"choice", "details"}}}}).at("choice") ==
	                WidgetDataDestination::Details,
	        "same responsibility level supports independently authored data placement per star");
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim shared control widget");
	const auto before = controller(world, 101, "propulsion");
	require(before["commands"]["choice"]["human"] == true && before["commands"]["speed"]["human"] == false,
	        "widget level compiles exclusive command ownership inside one Controller");
	require(!world.submit(101, speed(world, 101, "propulsion", 9000)),
	        "human cannot write a delegated channel of an otherwise interactive Controller");
	{
		auto command = speed(world, 101, "propulsion", 9000);
		command.action = "speed_mm_s";
		require(!eligible_console_command(*world.observe(101), catalog.consoles.at("test.console"), Json::object(),
		                                  command),
		        "presentation admission checks channel ownership inside an interactive widget");
	}
	world.tick();
	const auto after = controller(world, 101, "propulsion");
	const auto presented = console_control_widgets(catalog, *world.observe(101));
	{
		auto projected = *world.observe(101);
		projected.gameplay["controls"].push_back(
		    {{"id", "legacy.native"}, {"controller", "propulsion"}, {"action", "ftl.depart"}});
		const auto widgets = console_control_widgets(catalog, projected);
		require(std::none_of(widgets[0].controls.begin(), widgets[0].controls.end(),
		                     [](const auto &control)
		                     {
			                     return control.at("id") == "legacy.native";
		                     }),
		        "widget host only exposes allocated channel commands, not unrelated legacy native operations");
	}
	const auto client =
	    resolve_packs({console_assets(catalog, "test.controllable", std::vector<std::string>{"drive"})});
	require(client.control_widgets.contains("test.widget.drive") &&
	            !client.control_widgets.at("test.widget.drive").contains("tree") && client.trees.empty() &&
	            console_control_widgets(client, *world.observe(101))[0].data.size() == presented[0].data.size(),
	        "restricted client bundle preserves widget data presentation without AI policy or private continuation");
	require(presented.size() == 1 && presented[0].data.size() == 2 &&
	            presented[0].data[0].destination == WidgetDataDestination::Main && presented[0].data[0].text == "1" &&
	            presented[0].data[1].destination == WidgetDataDestination::Details,
	        "permitted widget model distinguishes main data from details without changing command ownership");
	require(
	    after["commands"]["choice"]["value"] == 1 &&
	        after["commands"]["choice"]["revision"] == before["commands"]["choice"]["revision"] &&
	        after["commands"]["speed"]["value"] == 3000 && after["commands"]["auxiliary"]["value"] == 2,
	    "same tick refusal falls back to matching human intent without mutation, retaining earlier admitted writes");
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "partial Controller ownership and attempt continuation survive restore");
	{
		const auto stale = speed(world, 101, "propulsion", 8000);
		require(world.submit(101, {1, "preset", 1, {}}), "select detailed widget workload");
		world.tick();
		require(!world.submit(101, stale) && world.submit(101, speed(world, 101, "propulsion", 8000)),
		        "authority change invalidates old proposals and admits the newly human-owned channel");
		world.tick();
		require(requested(world, "port") == 8000, "same tree cannot overwrite the detailed human flight setting");
		const auto detailed = console_control_widgets(catalog, *world.observe(101));
		require(detailed[0].data[1].destination == WidgetDataDestination::Main,
		        "higher-level data defaults move speed from details onto main independently of layout geometry");
		require(world.submit(101, {1, "away", 1, {}}), "delegate the complete widget while Away");
		world.tick();
		require(requested(world, "port") == 6000 &&
		            !controller(world, 101, "propulsion")["commands"]["choice"]["human"],
		        "Away restores full automation without replacing the shared tree");
	}
	{
		auto failing = source;
		failing["trees"].back()["budget"] = 5;
		const auto failing_catalog = load(failing);
		World interrupted;
		interrupted.load(failing_catalog, "test.map");
		require(interrupted.claim(101, 1, "drive"), "claim bounded-evaluation fixture");
		const auto checkpoint = interrupted.save()["bodies"][0]["console_controllers"]["controllers"][0];
		rejected(
		    [&]
		    {
			    interrupted.tick();
		    },
		    "budget exhausted");
		const auto unchanged = interrupted.save()["bodies"][0]["console_controllers"]["controllers"][0];
		require(checkpoint["commands"] == unchanged["commands"] && checkpoint["brain"] == unchanged["brain"],
		        "failed evaluation commits neither staged command changes nor continuation");
	}
	World automatic;
	automatic.load(catalog, "test.map");
	automatic.tick();
	require(automatic.save()["bodies"][0]["console_controllers"]["controllers"][0]["commands"][0]["value"] == 6000,
	        "same shared tree chooses its preferred branch when all commands are automated");
	{
		auto referenced = source;
		referenced["trees"].back()["states"][0]["running"][0]["attempt"][0]["tasks"] =
		    Json::array({{{"call", "test.widget.preferred"}}});
		referenced["trees"].push_back(Json::parse(R"json({
          "id":"test.widget.preferred","context":"controller",
          "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
          "initial":"choose","states":[
            {"id":"choose","initial":"operate","entry":[{"action":"write.choice","arguments":[0]}]},
            {"id":"operate","parent":"choose","running":[{"action":"write.speed","arguments":[6000]}]}
          ]})json"));
		const auto referenced_catalog = load(referenced);
		World original;
		original.load(referenced_catalog, "test.map");
		require(original.claim(101, 1, "drive"), "claim referenced-attempt widget");
		original.tick();
		require(requested(original, "port") == 3000,
		        "refused reference entry propagates failure before its dependent child behaviour");
		World continued;
		continued.restore(referenced_catalog, original.save());
		original.tick();
		continued.tick();
		require(original.save() == continued.save() && requested(continued, "port") == 3000,
		        "pending referenced entry retries consistently after save and resume");
	}
	{
		auto split = source;
		auto &instances = split["entities"][0]["control_layout"]["controllers"];
		instances[0]["equipment"]["engines"] = {"port"};
		auto second = instances[0];
		second["id"] = "starboard";
		second["equipment"]["engines"] = {"starboard"};
		second["star_levels"] = {{"1", "detailed"}, {"2", "detailed"}, {"3", "detailed"}};
		instances.push_back(second);
		const auto split_catalog = load(split);
		World independent;
		independent.load(split_catalog, "test.map");
		require(independent.claim(101, 1, "drive"), "claim independently bound widgets");
		require(independent.submit(101, speed(independent, 101, "starboard", 7000)),
		        "second shared widget uses its independently mapped detailed workload");
		independent.tick();
		require(requested(independent, "port") == 3000 && requested(independent, "starboard") == 7000,
		        "reused widget instances retain separate equipment and responsibility mappings");
		split["entities"][0]["control_layout"]["controllers"][1]["equipment"]["engines"] = {"port"};
		rejected(
		    [&]
		    {
			    (void)load(split);
		    },
		    "physical");
	}
}
