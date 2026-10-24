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
		const auto before = world.save().at("payload_effects");
		require(!before.at("results").empty(), "Native payload reached its deferred transaction");
		if (removing)
			require(!before.at("active").empty(), "Removal rollback starts with a live periodic instance");
		else
			require(before.at("active").empty() && before.at("next_instance") == 1,
			        "A failed group rolls back its staged periodic instance and identity allocation");
		World resumed;
		resumed.restore(catalog, world.save());
		for (int i = 0; i < 6; ++i)
		{
			world.tick();
			resumed.tick();
		}
		const auto after = world.save().at("payload_effects");
		require(after.at("active") == before.at("active") && after.at("next_instance") == before.at("next_instance"),
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
{"id":"hit","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":1},{"type":"entity","id":2},"test.start-chain"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.chain.tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	for (int i = 0; i < 10 && world.save().at("shield_reactions").at("pending").empty(); ++i)
		world.tick();
	require(world.save().at("shield_reactions").at("pending").size() == 2,
	        "Native damage stages a batch of deferred shield reactions");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 5; ++i)
	{
		const auto before = world.save().at("shield_reactions");
		world.tick();
		resumed.tick();
		const auto after = world.save().at("shield_reactions");
		require(after.at("pending").size() == 2 && after.at("results").size() == before.at("results").size() + 2,
		        "Deferred callbacks retain their next-tick jobs without executing them in the draining batch");
		require(world.save() == resumed.save(),
		        "A chain of native reactions continues deterministically after restore");
	}
	return world.save();
}
