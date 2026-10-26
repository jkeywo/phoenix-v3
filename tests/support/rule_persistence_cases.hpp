// Included inside the rules system fixture namespace after its common helpers.
Json rule_persistence_cases()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["mag"]["capacity"] = 100;
	ship["systems"]["mag"]["initial"] = 100;
	ship["systems"]["mag"]["ammunition_type"] = "test.runtime.supply";
	ship["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	ship["systems"]["radar"]["rules"] = Json::array({{{"id", "paid"}, {"rule", "test.runtime.maintain"}}});
	definition(source, "test.runtime.maintain")["funding"] = {
	    {"shortage", "pause"},
	    {"costs", Json::array({{{"payer", "target"},
	                            {"store", "energy:cell"},
	                            {"resource", "fixture.resource.energy"},
	                            {"per_second", {{"value", 3}, {"unit", "resource:fixture.resource.energy"}}}},
	                           {{"payer", "target"},
	                            {"store", "mag"},
	                            {"resource", "test.runtime.supply"},
	                            {"per_second", {{"value", 7}, {"unit", "resource:test.runtime.supply"}}}}})}};
	source["effects"].push_back({{"id", "test.persistence.mark"},
	                             {"kind", "rule"},
	                             {"trigger", "post_entity_damage"},
	                             {"when", {{"fact", "source.exists"}}},
	                             {"effect", "test.runtime.enter-op"}});
	auto independent_modifier = definition(source, "test.runtime.mod");
	independent_modifier["id"] = "test.persistence.other-mod";
	source["effects"].push_back(independent_modifier);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 7);
	const auto saved = world.save();
	const auto &executions = saved.at("rules").at("ships")[0].at("state").at("executions");
	require(executions.size() == 1, "persistence fixture has one independently owned paid rule clock");
	const auto key = executions.begin().key();
	const auto &state = executions.at(key);
	require(state.at("lease_tick") == saved.at("tick").get<std::int64_t>() && state.at("lease_permille") == 1000 &&
	            std::any_of(state.at("credits").begin(), state.at("credits").end(),
	                        [](const auto &credit)
	                        {
		                        return credit.at("resource") == "test.runtime.supply" && credit.at("prepaid") > 0;
	                        }),
	        "snapshot crosses a paid work lease with material prepaid credit");
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "restoring rules neither repays upkeep nor rebuilds a new output identity");
	advance(world, 13);
	advance(resumed, 13);
	require(world.save() == resumed.save(),
	        "prepaid materials and native power continue exactly after a mid-credit save");
	const auto reject = [&](const Json &snapshot, const auto &mutate)
	{
		auto broken = snapshot;
		mutate(broken);
		World victim;
		victim.restore(catalog, snapshot);
		const auto before = victim.save();
		rejects(
		    [&]
		    {
			    victim.restore(catalog, broken);
		    },
		    "malformed shared rule snapshot was accepted");
		require(victim.save() == before, "rejected shared rule restore preserves the previous complete world");
	};
	reject(saved,
	       [&](Json &broken)
	       {
		       auto duplicate = broken["rules"]["ships"][0]["state"]["executions"][key];
		       broken["rules"]["ships"][0]["state"]["executions"]["duplicate.clock"] = duplicate;
	       });
	reject(saved,
	       [&](Json &broken)
	       {
		       broken["rules"]["ships"][0]["state"]["executions"][key]["credits"][0]["store"] = "missing.paid.store";
	       });
	reject(saved,
	       [&](Json &broken)
	       {
		       broken["rules"]["ships"][0]["state"]["executions"][key]["credits"][0]["prepaid"] = -1;
	       });
	reject(saved,
	       [&](Json &broken)
	       {
		       broken["rules"]["ships"][0]["state"]["executions"][key]["lease_tick"] =
		           broken["tick"].get<std::int64_t>() + 2;
	       });
	reject(saved,
	       [](Json &broken)
	       {
		       broken["active_effects"]["modifiers"][0]["source"] = "independent.persistence";
	       });
	reject(saved,
	       [](Json &broken)
	       {
		       broken["active_effects"]["modifiers"][0]["source"] = "rule." + std::string(64, 'a') + ".range";
	       });
	reject(saved,
	       [](Json &broken)
	       {
		       broken["active_effects"]["modifiers"][0]["definition"] = "test.persistence.other-mod";
	       });
	reject(saved,
	       [&](Json &broken)
	       {
		       broken["rules"]["ships"][0]["state"]["executions"][key]["applied_context"]["target"]["entity"] = 424242;
	       });
	reject(saved,
	       [](Json &broken)
	       {
		       broken["active_effects"]["modifiers"][0]["maintained"] = false;
	       });
	// A pending delivery carries a captured origin independently of a live firing
	// ship. Build that legitimate saved delivery using the compiled fact layout.
	auto orphan = saved;
	auto context = state.at("context");
	const auto absent = std::uint64_t{424242};
	context["source"]["entity"] = absent;
	context["instigator"]["entity"] = absent;
	context["capture_owner"] = true;
	context["captured"] = Json::array();
	const auto compiled = compile_rule_catalog(catalog).at("test.persistence.mark");
	require(compiled.facts.size() == 1, "orphan fixture captures its source existence exactly once");
	const auto &fact = compiled.facts.front();
	context["captured"].push_back({{"role", fact.role},
	                               {"kind", fact.kind},
	                               {"key", fact.key},
	                               {"aux", fact.aux},
	                               {"unit", fact.unit},
	                               {"type", fact.type},
	                               {"value_type", state_tree::Type::Boolean},
	                               {"number", 1},
	                               {"text", ""}});
	const auto job = orphan["rules"]["next_job"].get<std::uint64_t>();
	orphan["rules"]["next_job"] = job + 1;
	orphan["rules"]["pending"].push_back({{"id", job},
	                                      {"due", orphan["tick"].get<std::int64_t>() + 1},
	                                      {"phase", 0},
	                                      {"rule", "test.persistence.mark"},
	                                      {"source_key", key},
	                                      {"damage", 0},
	                                      {"context", context}});
	World orphan_world;
	orphan_world.restore(catalog, orphan);
	require(orphan_world.save() == orphan, "captured launch origin survives without a current source entity");
	World orphan_resumed;
	orphan_resumed.restore(catalog, orphan_world.save());
	const auto marker_before = quantity(orphan_world, 1);
	advance(orphan_world, 3);
	advance(orphan_resumed, 3);
	require(orphan_world.save() == orphan_resumed.save() && quantity(orphan_world, 1) == marker_before + 1,
	        "orphan delivery executes its frozen source facts once without replaying work payment");
	reject(orphan,
	       [](Json &broken)
	       {
		       broken["rules"]["pending"].push_back(broken["rules"]["pending"][0]);
	       });
	reject(orphan,
	       [](Json &broken)
	       {
		       broken["rules"]["pending"][0]["due"] = broken["tick"].get<std::int64_t>() + 1000;
	       });
	reject(orphan,
	       [](Json &broken)
	       {
		       broken["rules"]["pending"][0]["context"]["captured"][0]["value_type"] = state_tree::Type::Integer;
	       });
	reject(orphan,
	       [](Json &broken)
	       {
		       broken["rules"]["pending"][0]["context"]["captured"][0]["key"] = "unrecognised.native.fact";
	       });
	// Hull destruction has a deliberate unprocessed frontier. Personnel data and
	// native rule identities remain intact until the next closure phase.
	auto retiring_source = runtime_fixture();
	retiring_source["entities"][0]["personnel"] = Json::parse(R"json({
	  "departments":[{"id":"operations","label":"Operations","station":"operations"}],
	  "compartments":["fore"],"connections":[],
	  "traits":[{"id":"watch","label":"Watch","bonuses":{"sensors":250}}],
	  "duties":[{"id":"watch","label":"Watch","controller":"radar","department":"operations",
	             "compartment":"fore","effects":{"radar":["sensors"]}}],
	  "officers":[{"id":"chief","name":"Chief","department":"operations","compartment":"fore",
	               "duty":"watch","traits":["watch"]}]
	})json");
	for (const auto &controller : retiring_source["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") != "radar")
			retiring_source["entities"][0]["personnel"]["duties"].push_back({{"id", controller.at("id")},
			                                                                 {"label", controller.at("id")},
			                                                                 {"controller", controller.at("id")},
			                                                                 {"department", "operations"},
			                                                                 {"compartment", "fore"}});
	for (const auto &[id, system] : retiring_source["entities"][0]["systems"].items())
		if (system.at("kind") == "repair_team")
			retiring_source["entities"][0]["personnel"]["duties"].push_back({{"id", "crew-" + id},
			                                                                 {"label", id},
			                                                                 {"team", id},
			                                                                 {"department", "operations"},
			                                                                 {"compartment", "fore"}});
	const auto retiring_catalog = load(retiring_source);
	World retiring;
	retiring.load(retiring_catalog, "test.map");
	advance(retiring, 2);
	auto death = retiring.save();
	require(!modifier_applications(death, 1).empty(),
	        "pending-death fixture starts with an authored officer trait contribution");
	death["bodies"][0]["destructible"]["durability"] = 0;
	death["bodies"][0]["destructible"]["destroyed_tick"] = death["tick"];
	death["bodies"][0]["destructible"]["processed"] = false;
	for (auto &compartment : death["bodies"][0]["combat"]["compartments"])
	{
		compartment["hp"] = 0;
		compartment["disabled"] = true;
	}
	World closure;
	closure.restore(retiring_catalog, death);
	require(closure.save() == death, "unprocessed native hull death retains its exact pre-closure rule identities");
	World closure_resumed;
	closure_resumed.restore(retiring_catalog, closure.save());
	advance(closure);
	advance(closure_resumed);
	require(closure.save() == closure_resumed.save() && modifier_applications(closure.save(), 1).empty(),
	        "first native closure withdraws pending-death officer work without replaying activation");
	auto already_processed = death;
	already_processed["bodies"][0]["destructible"]["processed"] = true;
	already_processed["bodies"][0]["destructible"]["rule_notified"] = true;
	rejects(
	    [&]
	    {
		    World invalid;
		    invalid.restore(retiring_catalog, already_processed);
	    },
	    "already processed death must not authorize orphan trait clocks");
	auto unknown_binding = death;
	unknown_binding["rules"]["ships"][0]["state"]["executions"].begin().value()["source_key"] =
	    "trait:1:chief:invented";
	rejects(
	    [&]
	    {
		    World invalid;
		    invalid.restore(retiring_catalog, unknown_binding);
	    },
	    "pending death must not authorize a revoked or invented native trait source");
	return portable(orphan_world.save());
}
