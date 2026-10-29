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
	const auto catalog = load(source);
	const auto widget = compile_control_widget(catalog, "test.widget.drive");
	{
		auto action_source = source;
		auto action = action_source["trees"].back();
		action["id"] = "test.widget.work";
		action["controller_action"] = {{"label", "Operate drive"}, {"inputs", Json::array()}};
		action_source["trees"].back() = Json::parse(R"json({
          "id":"test.widget.policy","context":"controller",
          "command_bindings":[{"channel":"speed"},{"channel":"choice"},{"channel":"auxiliary"}],
          "action_bindings":[{"id":"operate","tree":"test.widget.work"}],
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
		invalid["entities"][0]["control_layout"]["controllers"][0]["star_levels"]["1"] = "detailed";
		rejected(
		    [&]
		    {
			    (void)load(invalid);
		    },
		    "must not decrease");
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
	world.tick();
	const auto after = controller(world, 101, "propulsion");
	const auto presented = console_control_widgets(catalog, *world.observe(101));
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
