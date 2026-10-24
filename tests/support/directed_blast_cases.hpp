// Included inside the directed system test's anonymous namespace, after helpers.
Json directed_blast_cases()
{
	Json trace = Json::array();
	for (int variant = 0; variant < 3; ++variant)
	{
		auto source = directed_fixture();
		source["maps"][0]["entities"][2]["x_mm"] = 60000;
		source["maps"][0]["entities"][2]["y_mm"] = 60000;
		source["resources"].push_back({{"id", "test.blast-marker"},
		                               {"label", "Blast provenance"},
		                               {"tags", {"energy"}},
		                               {"sizes", {{"energy", 1}}}});
		source["entities"][0]["resources"] = {{"stores", Json::array({{{"id", "marker"},
		                                                               {"resource", "test.blast-marker"},
		                                                               {"capacity", 10000},
		                                                               {"initial", 0},
		                                                               {"charge_rate", 10000},
		                                                               {"discharge_rate", 10000}}})}};
		for (const auto &[name, value] :
		     std::vector<std::pair<std::string, int>>{{"impact", 1}, {"shotdown", 10}, {"ship", 100}})
		{
			source["effects"].push_back({{"id", "test.blast." + name + ".group"},
			                             {"kind", "operations"},
			                             {"operations", Json::array({{{"kind", "add"},
			                                                          {"target", "protected"},
			                                                          {"store", "energy:marker"},
			                                                          {"resource", "test.blast-marker"},
			                                                          {"amount", value}}})}});
			source["effects"].push_back({{"id", "test.blast." + name},
			                             {"kind", "payload"},
			                             {"bindings", Json::array({{{"id", "mark"},
			                                                        {"outcome", "entity"},
			                                                        {"positive_damage", false},
			                                                        {"effect", "test.blast." + name + ".group"}}})}});
		}
		const auto fire = source["entities"][0]["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
		auto &mode = directed_effect(source, fire);
		mode["fire_ticks"] = 1000;
		mode["payloads"] = {"test.blast.impact"};
		mode["projectile"] = {{"visual", "test.visual"},
		                      {"speed_mm_s", 20000},
		                      {"lifetime_ticks", 200},
		                      {"penetration", 1000},
		                      {"damage", 100},
		                      {"damage_per_m", 10},
		                      {"durability", 5},
		                      {"radius_mm", 1000},
		                      {"blast_radius_mm", 20000},
		                      {"shield_bypass_permille", 1000},
		                      {"destruction",
		                       {{"chance_permille", 1000},
		                        {"damage", 40},
		                        {"radius_mm", 100000},
		                        {"affects_missiles", false},
		                        {"payloads", {"test.blast.shotdown"}}}}};
		auto defender = source["entities"][0];
		defender["id"] = "test.blast.defender";
		if (variant == 1)
		{
			auto defence_mode = mode;
			defence_mode["id"] = "test.blast.defence-mode";
			defence_mode["payloads"] = Json::array();
			defence_mode["projectile"] = {{"visual", "test.visual"}, {"speed_mm_s", 1000000}, {"lifetime_ticks", 20},
			                              {"penetration", 1},        {"damage", 10},          {"damage_per_m", 1000000},
			                              {"radius_mm", 100}};
			source["effects"].push_back(std::move(defence_mode));
			defender["systems"]["gun"]["directed"]["modes"][1] = "test.blast.defence-mode";
		}
		if (variant == 2)
		{
			auto compartment = defender["combat"]["decks"][0]["compartments"][0];
			compartment["hp"] = 2;
			compartment["recovery_hp"] = 1;
			defender["combat"]["decks"][0]["compartments"] = Json::array({compartment});
			defender["combat"]["connections"] = Json::array();
			defender["combat"]["destruction"] = {
			    {"chance_permille", 1000}, {"damage", 0}, {"radius_mm", 100000}, {"payloads", {"test.blast.ship"}}};
		}
		source["entities"].push_back(std::move(defender));
		source["maps"][0]["entities"][1]["definition"] = "test.blast.defender";
		if (variant == 1)
			source["maps"][0]["entities"][1]["heading_mdeg"] = 180000;
		if (variant != 2)
		{
			source["effects"].push_back(Json::parse(R"json({"id":"test.blast.shield","kind":"shield","contributions":{
"cover":{"capacity":1000,"initial":1000,"warmup_ticks":1,"sustain_energy_per_second":0,"recharge_per_second":0,"off_recharge_per_second":0}}})json"));
			source["trees"].push_back(
			    Json::parse(R"json({"id":"test.blast.shield.tree","context":"scenario","initial":"apply",
"states":[{"id":"apply","entry":[{"action":"apply_shield_effect","arguments":[{"type":"entity","id":2},"test.blast.shield","cover"]}]}]})json"));
			source["maps"][0]["scenario"] = {{"tree", "test.blast.shield.tree"}};
		}
		const auto content = load(source);
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive"), "Claim blast payload source");
		if (variant == 1)
			require(world.claim(202, 2, "drive"), "Claim physical missile shootdown gun");
		ticks(world, 1);
		command(world, "target", 2);
		command(world, "operation", 1);
		world.tick();
		for (int n = 0; n < 12 && weapon(world).at("shots") == 0; ++n)
			world.tick();
		require(weapon(world).at("shots") == 1, "One committed missile leaves the native launcher");
		command(world, "operation", 0);
		if (variant == 1)
		{
			ticks(world, 15); // Let the target missile clear its own hull before interception.
			command(world, "target", 1, "gun", 2, 202);
			command(world, "operation", 1, "gun", 2, 202);
			world.tick();
			for (int n = 0; n < 12 && weapon(world, "gun", 2).at("shots") == 0; ++n)
				world.tick();
			require(weapon(world, "gun", 2).at("shots") == 1, "A real projectile intercepts the missile in flight");
			command(world, "operation", 0, "gun", 2, 202);
		}
		ticks(world, 80);
		const auto marker = [&](EntityId id)
		{
			return body(world, id)
			    .at("resources")
			    .at("network")
			    .at("stores")[0]
			    .at("stock")
			    .at("quantities")
			    .value("test.blast-marker", std::int64_t{0});
		};
		if (variant == 0)
		{
			require(marker(2) == 1 && marker(1) == 0,
			        "Impact executes only the captured impact payload against actual blast targets");
			require(hp(world, 2) < 20000, "Impact warhead retains its authored shield bypass");
		}
		else if (variant == 1)
		{
			require(marker(2) == 10 && marker(1) == 10, "Physical shootdown executes the separate destruction payload "
			                                            "exactly once, without its impact payload");
			require(hp(world, 2) == 20000,
			        "Missile destruction damage retains its independent no-bypass shielding rules");
		}
		else
			require(
			    hp(world, 2) == 0 && marker(1) == 100,
			    "Once-only ship destruction delivers a separately authored effect-only blast to collateral targets");
		const auto before = world.save();
		World resumed;
		resumed.restore(content, before);
		ticks(world, 4);
		ticks(resumed, 4);
		require(world.save() == resumed.save() && marker(1) == (variant == 0   ? 0
		                                                        : variant == 1 ? 10
		                                                                       : 100),
		        "Blast receipts and destruction do not replay after restore");
		trace.push_back(world.save());
	}
	return trace;
}

Json directed_inflight_source_loss()
{
	auto source = directed_fixture();
	directed_burning(source, 3, 80);
	const auto fire = source["entities"][0]["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["fire_ticks"] = 1000;
	directed_effect(source, fire)["projectile"]["speed_mm_s"] = 20000;
	directed_effect(source, fire)["projectile"]["lifetime_ticks"] = 200;
	auto fragile = source["entities"][0];
	fragile["id"] = "test.inflight.fragile";
	for (auto &compartment : fragile["combat"]["decks"][0]["compartments"])
	{
		compartment["hp"] = 2;
		compartment["recovery_hp"] = 1;
	}
	source["entities"].push_back(std::move(fragile));
	source["maps"][0]["entities"][0]["definition"] = "test.inflight.fragile";
	source["maps"][0]["entities"][1]["heading_mdeg"] = 180000;
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(202, 2, "drive"),
	        "Claim real combatants for independent in-flight work");
	world.tick();
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	for (int n = 0; n < 12 && weapon(world).at("shots") == 0; ++n)
		world.tick();
	require(weapon(world).at("shots") == 1, "Slow projectile commits its ammunition and payload before source loss");
	command(world, "enabled", 0);
	command(world, "target", 1, "second", 2, 202);
	world.tick();
	for (int n = 0; n < 35 && hp(world, 1) > 0; ++n)
		world.tick();
	require(hp(world, 1) == 0, "Native counterfire destroys the original gun's ship before impact");
	command(world, "enabled", 0, "second", 2, 202);
	world.tick();
	const auto in_flight = world.save();
	require(
	    std::any_of(in_flight.at("projectiles").begin(), in_flight.at("projectiles").end(),
	                [](const auto &projectile)
	                {
		                return projectile.at("source") == 1 && projectile.at("weapon") == "gun" &&
		                       !projectile.at("payloads").empty();
	                }),
	    "The committed projectile retains original gun identity and captured payload after native source destruction");
	require(hp(world, 2) == 20000, "The victim has not yet been reached at the destroyed-source flight checkpoint");
	for (std::size_t i = 0; i < in_flight.at("projectiles").size(); ++i)
	{
		const auto &payloads = in_flight.at("projectiles")[i].at("payloads");
		for (const auto &payload : payloads)
			require(payload.is_string(), "Captured payloads save catalogue IDs rather than immutable definition trees");
		if (payloads.empty())
			continue;
		for (bool duplicate : {false, true})
		{
			auto invalid = in_flight;
			auto &captured = invalid["projectiles"][i]["payloads"];
			if (duplicate)
				captured.push_back(payloads[0]);
			else
				captured[0] = "test.missing-payload";
			rejects(
			    [&]
			    {
				    World rejected;
				    rejected.restore(content, invalid);
			    },
			    "Restore rejects duplicate or unresolved captured payload IDs");
		}
		break;
	}

	World resumed;
	resumed.restore(content, in_flight);
	bool inflicted = false;
	for (int n = 0; n < 70; ++n)
	{
		world.tick();
		resumed.tick();
		const auto snapshot = world.save();
		require(snapshot == resumed.save(), "In-flight captured work continues identically without a live source");
		for (const auto &effect : snapshot.at("payload_effects").at("active"))
			if (effect.at("original_source") == 1 && effect.at("original_equipment") == "gun" &&
			    effect.at("context").at("target") == 2)
				inflicted = true;
		if (inflicted)
			break;
	}
	require(inflicted,
	        "The eventual native hit applies the loaded resource's Burning payload after source destruction");
	const auto before = hp(world, 2);
	ticks(world, 7);
	ticks(resumed, 7);
	require(hp(world, 2) < before && world.save() == resumed.save(),
	        "The late inflicted condition performs independent periodic damage and retains exact continuation");
	return world.save();
}
