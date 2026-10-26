// Synthetic production-path regressions for lazy staging and deferred callbacks.
Json periodic_transaction_rollback()
{
	Json trace = Json::array();
	for (bool removing : {false, true})
	{
		auto source = directed_fixture();
		directed_burning(source, 50, 100);
		const auto resource = source["entities"][0]["systems"]["mag"]["stock"]["selection"];
		const Json failure = {{"kind", "consume"}, {"store", "missing.store"}, {"resource", resource}, {"amount", 1}};
		if (!removing)
			directed_effect(source, "test.burn.apply")["operations"].push_back(failure);
		else
		{
			source["effects"].push_back(
			    {{"id", "test.failed-removal"},
			     {"kind", "operations"},
			     {"operations",
			      Json::array({{{"kind", "remove"}, {"effect", "test.burn"}, {"all_sources", true}}, failure})}});
			source["trees"].push_back(
			    Json::parse(R"json({"id":"test.failed-removal.tree","context":"scenario","initial":"wait",
"states":[{"id":"wait","transitions":[{"target":"remove","conditions":[{"left":{"observation":"tick"},"op":"ge","right":10}]}]},
{"id":"remove","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":2},{"type":"entity","id":1},"test.failed-removal"]}]}]})json"));
			source["maps"][0]["scenario"] = {{"tree", "test.failed-removal.tree"}};
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive"), "Claim periodic transaction fixture");
		world.tick();
		command(world, "target", 2);
		command(world, "operation", 1);
		ticks(world, 6);
		command(world, "enabled", 0);
		world.tick();
		const auto before = world.save().at("active_effects");
		require(!world.save().at("rules").at("results").empty(), "Native payload reached its deferred transaction");
		if (removing)
			require(!before.at("conditions").empty(), "Removal rollback starts with a live periodic instance");
		else
			require(before.at("conditions").empty() && before.at("next_condition") == 1,
			        "A failed group rolls back its staged periodic instance and identity allocation");
		World resumed;
		resumed.restore(catalog, world.save());
		for (int i = 0; i < 6; ++i)
		{
			world.tick();
			resumed.tick();
		}
		const auto after = world.save().at("active_effects");
		const auto native_roster = [](Json active)
		{
			for (auto &effect : active)
				for (auto &[key, clock] : effect["clocks"].items())
				{
					// Current phase facts advance even while a future periodic deadline is unchanged.
					clock["context"].erase("tick");
					clock["context"].erase("elapsed");
				}
			return active;
		};
		require(native_roster(after.at("conditions")) == native_roster(before.at("conditions")) &&
		            after.at("next_condition") == before.at("next_condition"),
		        "Failed periodic removal preserves the committed roster and next identity");
		require(world.save() == resumed.save(), "Failed periodic transactions continue identically after restore");
		trace.push_back(world.save());
	}
	return trace;
}
Json deferred_callback_continuation()
{
	auto source = directed_fixture();
	source["effects"].push_back(
	    Json::parse(R"json({"id":"test.chained-shield","kind":"shield","contributions":{"main":{
"capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,"sustain_energy_per_second":0,
"warmup_ticks":1,"post_damage":1}}})json"));
	source["effects"].push_back(Json::parse(R"json({"id":"test.start-chain","kind":"operations","operations":[
{"kind":"damage","damage":2},{"kind":"damage","damage":2}]})json"));
	source["trees"].push_back(
	    Json::parse(R"json({"id":"test.chain.tree","context":"scenario","initial":"shield","states":[
{"id":"shield","entry":[{"action":"apply_shield_effect","arguments":[{"type":"entity","id":1},"test.chained-shield","cover"]}],
"transitions":[{"target":"hit","conditions":[{"left":{"observation":"tick"},"op":"ge","right":4}]}]},
{"id":"hit","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":1},{"type":"entity","id":1},"test.start-chain"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.chain.tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	for (int i = 0; i < 10 && world.save().at("rules").at("pending").empty(); ++i)
		world.tick();
	require(world.save().at("rules").at("pending").size() == 2,
	        "Native damage stages a batch of deferred shield reactions");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 5; ++i)
	{
		const auto before = world.save().at("rules");
		world.tick();
		resumed.tick();
		const auto after = world.save().at("rules");
		require(
		    after.at("pending").size() == 2 && after.at("results").size() == before.at("results").size() + 2,
		    ("Deferred callbacks retain their next-tick jobs without executing them in the draining batch: pending=" +
		     std::to_string(after.at("pending").size()) + " results=" + std::to_string(after.at("results").size()))
		        .c_str());
		require(world.save() == resumed.save(),
		        "A chain of native reactions continues deterministically after restore");
	}
	return world.save();
}

Json periodic_ordered_removal()
{
	auto source = directed_fixture();
	for (const auto &effect : Json::parse(R"json([
 {"id":"test.first","kind":"periodic","duration_ticks":2,"period_ticks":1,"effect":"test.first.tick"},
 {"id":"test.later","kind":"periodic","duration_ticks":10,"period_ticks":1,"effect":"test.later.tick"},
 {"id":"test.replacement","kind":"modifier","duration_ticks":8,"contributions":{"range":{"property":"sensors","delta_permille":50}}},
 {"id":"test.first.tick","kind":"operations","operations":[
   {"kind":"remove","target":"protected","effect":"test.later","all_sources":true},
   {"kind":"apply","target":"protected","effect":"test.replacement","equipment":"radar","grant":"replacement"}]},
 {"id":"test.later.tick","kind":"operations","operations":[{"kind":"damage","target":"protected","damage":5}]},
 {"id":"test.periodic.start","kind":"operations","operations":[
   {"kind":"apply","target":"protected","effect":"test.first","grant":"first"},
   {"kind":"apply","target":"protected","effect":"test.later","grant":"later"}]}
 ])json"))
		source["effects"].push_back(effect);
	source["trees"].push_back(
	    Json::parse(R"json({"id":"test.periodic-order.tree","context":"scenario","initial":"start",
"states":[{"id":"start","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":2},{"type":"entity","id":1},"test.periodic.start"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.periodic-order.tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	world.tick();
	const auto before = world.save();
	require(before.at("active_effects").at("conditions").size() == 2,
	        "Two periodic applications are due in stable order");
	const auto initial_hp = hp(world);
	World resumed;
	resumed.restore(catalog, before);
	world.tick();
	resumed.tick();
	require(hp(world) == initial_hp - 5 && world.save() == resumed.save(),
	        "Removal preserves the shared scheduler's already frozen batch while creating another effect entity");
	const auto after = world.save().at("active_effects").at("conditions");
	require(std::none_of(after.begin(), after.end(),
	                     [](const auto &effect)
	                     {
		                     return effect.at("definition") == "test.later";
	                     }) &&
	            world.save().at("active_effects").at("modifiers").size() == 1,
	        "A removed periodic instance cannot revive when another effect is created");
	world.tick();
	resumed.tick();
	require(hp(world) == initial_hp - 5 && world.save() == resumed.save(),
	        "Retired instances cannot schedule another tick after the frozen batch completes");
	return world.save();
}
