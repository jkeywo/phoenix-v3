// Included in the process integration fixture's private namespace.
Json permitted_process_information()
{
	Json trace = Json::array();
	for (int mode = 0; mode < 3; ++mode)
	{
		auto source = fixture();
		auto &definition = effect(source, "test.process.tick");
		definition["observations"] = {"test.rate"};
		source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
		controls(
		    source,
		    {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}}});
		source["effects"].push_back(
		    {{"id", "test.process.observed-rate"},
		     {"kind", "modifier"},
		     {"contributions", {{"rate", {{"property", "test.rate"}, {"delta_permille", 1000}}}}}});
		effect(source, "test.process.start")["operations"].push_back(
		    {{"kind", "apply"}, {"target", "owner"}, {"effect", "test.process.observed-rate"}, {"grant", "observed"}});
		if (mode == 1)
			effect(source, "test.process.tick")["observations"] = Json::array();
		if (mode == 2)
			source["entities"][0]["properties"].erase("test.rate");
		source["trees"].push_back(Json::parse(R"json({"id":"test.process.reader","context":"controller",
   "command_bindings":[{"channel":"main","type":"boolean"}],
   "process_property_bindings":[{"endpoint":"main","property":"test.rate"}],
   "initial":"waiting","states":[{"id":"waiting","transitions":[{"target":"stop","conditions":[{"left":{"observation":"process.main.property.test.rate"},"op":"eq","right":2000}]}]},
   {"id":"stop","entry":[{"action":"write.main","arguments":[false]}]}]})json"));
		if (mode)
			source["trees"].back()["states"][0]["transitions"][0]["conditions"][0]["right"] = 0;
		auto &ship = source["entities"][0];
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller.at("id") == "processes")
			{
				controller.erase("fallback");
				controller["tree"] = "test.process.reader";
			}
		for (auto &profile : ship["control_layout"]["profiles"])
		{
			std::erase_if(profile["consoles"]["drive"]["human"].get_ref<Json::array_t &>(),
			              [](const auto &id)
			              {
				              return id == "processes";
			              });
			profile["consoles"]["drive"]["ai"] = Json::array({"processes"});
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"),
		        "Claim permitted and unfed process Consoles");
		ticks(world, 4);
		const auto permitted = world.observe(101);
		bool found = false;
		Json observed;
		for (const auto &row : permitted->gameplay.at("processes"))
			if (row.at("control") == "main")
			{
				found = true;
				for (const auto &property : row.at("properties"))
					if (property.at("id") == "test.rate")
						observed = property;
			}
		require(found, "Named process status is visible on its owning Console");
		if (!mode)
		{
			require(state(world, "main").at("requested") == false && !observed.is_null() &&
			            observed.at("value") == 2000 && property(world, "test.rate") == 1000,
			        "Authorised effective process property drives ordinary AI action and matches human information "
			        "without exposing its base");
			require(network::bounded_gameplay(permitted->gameplay).at("processes") ==
			            permitted->gameplay.at("processes"),
			        "Typed process observations retain their values through bounded network projection");
			auto invalid = permitted->gameplay;
			for (auto &row : invalid["processes"])
				if (row.at("control") == "main")
					row["properties"][0]["value"] = "invalid-number";
			bool refused = false;
			try
			{
				(void)network::bounded_gameplay(invalid);
			}
			catch (const std::exception &)
			{
				refused = true;
			}
			require(refused, "Malformed typed process information is refused by the existing projection boundary");
		}
		else
			require(state(world, "main").at("requested") == true && observed.is_null(),
			        "Unobserved or undeclared process property remains Missing rather than a fabricated zero available "
			        "to AI");
		const auto unfed = world.observe(102);
		bool leaked = false;
		for (const auto &row : unfed->gameplay.value("processes", Json::array()))
			leaked |= row.value("control", std::string{}) == "main";
		require(!leaked, "Other Consoles do not receive a private named process endpoint or its properties");
		trace.push_back(portable(world.save()));
	}
	auto source = fixture();
	source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
	controls(source,
	         {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim stale process command fixture");
	ticks(world);
	const auto view = world.observe(101);
	for (const auto &controller : view->gameplay.at("controllers"))
		if (controller.at("id") == "processes")
		{
			OperatingCommand request{1, {}, 0, {}, "processes", "main", controller.at("generation")};
			request.arguments = {state(world, "main").at("generation").get<std::int64_t>() + 1};
			require(!world.can_submit(101, request), "A stale process grant identity cannot control its replacement");
		}
	return trace;
}

Json process_rule_visibility()
{
	auto source = fixture();
	source["effects"].push_back(
	    {{"id", "test.process.noop"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array({{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 0}}})}});
	source["effects"].push_back({{"id", "test.process.status-rule"},
	                             {"kind", "rule"},
	                             {"trigger", "tick"},
	                             {"phase", "periodic"},
	                             {"effect", "test.process.noop"}});
	source["effects"].push_back({{"id", "test.process.status"},
	                             {"kind", "process"},
	                             {"native", "effects"},
	                             {"label", "Observed execution"},
	                             {"rules", Json::array({{{"id", "status"}, {"rule", "test.process.status-rule"}}})}});
	source["entities"][0]["processes"] = {{"main", {{"process", "test.process.status"}}},
	                                      {"hidden", {{"process", "test.process.status"}}}};
	source["entities"][0]["combat"]["decks"][0]["compartments"][0]["processes"] = {
	    {"room", {{"process", "test.process.status"}}}};
	controls(source,
	         {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}},
	          {"room",
	           {{"source", "compartment"},
	            {"owner", "fore"},
	            {"process", "room"},
	            {"controller", "processes"},
	            {"enabled", "room"}}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "Claim process rule visibility Consoles");
	ticks(world, 3);
	const auto permitted = world.observe(101)->gameplay;
	const auto unpermitted = world.observe(102)->gameplay;
	const auto count = [](const Json &gameplay)
	{
		std::size_t rows = 0;
		for (const auto &rule : gameplay.value("rules", Json::array()))
			rows += rule.at("source_kind") == "process" && rule.at("rule") == "test.process.status-rule";
		return rows;
	};
	require(
	    count(permitted) == 2 && count(unpermitted) == 0,
	    "Entity and compartment rules require the matching Console endpoint and do not reveal an unbound peer slot");
	require(network::bounded_gameplay(permitted).value("rules", Json::array()) ==
	                permitted.value("rules", Json::array()) &&
	            network::bounded_gameplay(unpermitted).value("rules", Json::array()) ==
	                unpermitted.value("rules", Json::array()),
	        "Permitted and withheld process rule status survive the bounded projection boundary");
	return portable(world.save());
}

Json finite_process_information()
{
	auto source = fixture();
	effect(source, "test.process.tick")["required_work"] = 2500;
	effect(source, "test.process.tick")["completion_effect"] = "test.process.pause";
	source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
	controls(source,
	         {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}}});
	WorkshopDocument document;
	document.load_pack(archive(source));
	for (auto &definition : document.definitions()["effects"])
		if (definition.at("id") == "test.process.tick")
			definition["required_work"] = 3500;
	const auto exported = resolve_packs({read_pack(document.export_pack(PackEncoding::Dat))});
	const auto edited = compile_process(exported, "test.process.tick");
	require(edited.required_work == 3500 && edited.completion_effect.has_value(),
	        "Workshop editing and DAT export retain finite work and its completion-effect dependency");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "Claim finite-job information Consoles");
	ticks(world, 4);
	const auto observed = world.observe(101)->gameplay;
	bool found = false;
	for (const auto &row : observed.at("processes"))
		if (row.at("control") == "main")
		{
			found = true;
			require(row.at("finite") == true && row.at("details_available") == true && row.at("progress") == 2500 &&
			            row.at("required_work") == 2500 && row.at("completed") == true &&
			            row.at("completion_delivered") == true,
			        "A permitted finite job publishes exact bounded progress and once-only completion");
		}
	require(found, "Finite-job endpoint is present on its owning Console");
	require(network::bounded_gameplay(observed).at("processes") == observed.at("processes"),
	        "Finite-job facts survive the bounded network projection");
	auto invalid = observed;
	invalid["processes"][0]["progress"] = -1;
	bool refused = false;
	try
	{
		(void)network::bounded_gameplay(invalid);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused, "Malformed finite progress is rejected at the projection boundary");
	for (const auto &row : world.observe(102)->gameplay.value("processes", Json::array()))
		require(row.value("control", std::string{}) != "main", "An unfed Console cannot inspect a finite job");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 2);
	ticks(resumed, 2);
	require(portable(world.save()) == portable(resumed.save()),
	        "Completed job information survives exact continuation");
	source["trees"].push_back(Json::parse(R"json({"id":"test.process.finite-reader","context":"controller",
 "command_bindings":[{"channel":"main","type":"boolean"}],"process_bindings":["main"],
 "initial":"waiting","states":[{"id":"waiting","transitions":[{"target":"finished","conditions":[{"left":{"observation":"process.main.completed"},"op":"eq","right":true}]}]},
 {"id":"finished","entry":[{"action":"write.main","arguments":[false]}]}]})json"));
	for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "processes")
		{
			controller.erase("fallback");
			controller["tree"] = "test.process.finite-reader";
		}
	for (auto &profile : source["entities"][0]["control_layout"]["profiles"])
	{
		std::erase_if(profile["consoles"]["drive"]["human"].get_ref<Json::array_t &>(),
		              [](const auto &id)
		              {
			              return id == "processes";
		              });
		profile["consoles"]["drive"]["ai"] = Json::array({"processes"});
	}
	World automated;
	automated.load(load(source), "test.map");
	require(automated.claim(101, 1, "drive"), "Claim finite Controller information fixture");
	ticks(automated, 7);
	require(state(automated, "main").at("requested") == false,
	        "AI observes the same permitted completion and acts through the ordinary process command");
	return Json::array({portable(world.save()), portable(automated.save())});
}
