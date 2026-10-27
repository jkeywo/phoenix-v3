// Included in the process integration fixture's private namespace.
Json mixed_funding_and_rollback()
{
	auto source = fixture();
	auto &definition = effect(source, "test.process.tick");
	definition["shortage"] = "proportional";
	definition["rate"] = {{"fact", "owner.property"}, {"property", "test.rate"}};
	definition["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
	definition["upkeep_costs"] = Json::array(
	    {{{"payer", "owner"}, {"property", "test.fuel"}, {"per_second", 1}},
	     {{"payer", "owner"}, {"store", "energy:cell"}, {"resource", "fixture.resource.energy"}, {"per_second", 1}}});
	definition["work_costs"] = Json::array(
	    {{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 2}},
	     {{"payer", "owner"}, {"store", "energy:cell"}, {"resource", "fixture.resource.energy"}, {"per_work", 1}}});
	source["entities"][0]["properties"]["test.rate"] = 500;
	source["entities"][0]["processes"] = {{"mixed", {{"process", "test.process.tick"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 3);
	require(state(world, "mixed").at("work_remainder") == 500 && !state(world, "mixed").at("work_credits").empty(),
	        "Fractional effect work and mixed payment credits have authoritative owners");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 17);
	ticks(resumed, 17);
	require(property(world, "test.work") == 10000 && property(world, "test.fuel") == 76 && energy(world) == 989,
	        "Half-rate completed work pays exact mixed per-work and per-second costs with no fractional loss");
	require(world.save() == resumed.save(), "Mixed idle/work credits and partial work continue exactly");
	auto modified = fixture();
	auto &scaled = effect(modified, "test.process.tick");
	scaled["rate"] = {{"fact", "owner.property"}, {"property", "test.rate"}};
	scaled["work_scale"] = {{"fact", "owner.property"}, {"property", "test.scale"}};
	scaled["work_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
	modified["effects"].push_back({{"id", "test.process.rate-price"},
	                               {"kind", "modifier"},
	                               {"contributions",
	                                {{"rate", {{"property", "test.rate"}, {"delta_permille", 1000}}},
	                                 {"price", {{"property", "test.scale"}, {"delta_permille", 1000}}}}}});
	effect(modified, "test.process.start")["operations"].push_back(
	    {{"kind", "apply"}, {"target", "owner"}, {"effect", "test.process.rate-price"}, {"grant", "rate-price"}});
	modified["entities"][0]["processes"] = {{"scaled", {{"process", "test.process.tick"}}}};
	const auto modified_catalog = load(modified);
	World accelerated;
	accelerated.load(modified_catalog, "test.map");
	ticks(accelerated, 3);
	const auto work = property(accelerated, "test.work").get<std::int64_t>(),
	           fuel = property(accelerated, "test.fuel").get<std::int64_t>();
	ticks(accelerated);
	require(property(accelerated, "test.work") == work + 2000 && property(accelerated, "test.fuel") == fuel - 4,
	        "Existing custom modifiers scale process throughput and work price independently without replacing native "
	        "balances");
	Json refused = Json::array();
	for (bool entry : {true, false})
	{
		auto rejected = fixture();
		rejected["effects"].push_back({{"id", "test.process.impossible"},
		                               {"kind", "operations"},
		                               {"operations", Json::array({{{"kind", "property_add"},
		                                                            {"target", "owner"},
		                                                            {"property", entry ? "test.events" : "test.work"},
		                                                            {"amount", 1}},
		                                                           {{"kind", "consume"},
		                                                            {"target", "owner"},
		                                                            {"store", "energy:cell"},
		                                                            {"resource", "fixture.resource.energy"},
		                                                            {"amount", 2000}}})}});
		auto &process = effect(rejected, "test.process.tick");
		process["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
		if (entry)
			process["start_effect"] = "test.process.impossible";
		else
		{
			process["work_effect"] = "test.process.impossible";
			process.erase("work_values");
			process["work_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}},
			                                     {{"payer", "owner"},
			                                      {"store", "energy:cell"},
			                                      {"resource", "fixture.resource.energy"},
			                                      {"per_work", 1}}});
		}
		rejected["entities"][0]["processes"] = {{"refused", {{"process", "test.process.tick"}}}};
		const auto rejected_catalog = load(rejected);
		World failed;
		failed.load(rejected_catalog, "test.map");
		ticks(failed, 3);
		require(property(failed, "test.work") == 0 && energy(failed) == 1000 &&
		            property(failed, "test.fuel") == (entry ? 100 : 97) &&
		            property(failed, "test.events") == (entry ? 0 : 1),
		        "Failed entry/start or work/cost batch commits no partial effects or payments and does not repay "
		        "admitted entry");
		World continued;
		continued.restore(rejected_catalog, failed.save());
		ticks(failed, 2);
		ticks(continued, 2);
		require(failed.save() == continued.save(),
		        "Rejected prepared process work resumes without hidden claims or duplicate charges");
		refused.push_back(portable(failed.save()));
	}
	return Json::array({portable(world.save()), portable(accelerated.save()), refused});
}
Json temporary_grants()
{
	auto source = fixture();
	source["effects"].push_back({{"id", "test.process.independent"},
	                             {"kind", "modifier"},
	                             {"duration_ticks", 50},
	                             {"contributions", {{"range", {{"property", "sensors"}, {"delta_permille", 100}}}}}});
	source["effects"].push_back({{"id", "test.process.dependent"},
	                             {"kind", "modifier"},
	                             {"contributions", {{"range", {{"property", "sensors"}, {"delta_permille", 200}}}}}});
	source["effects"].push_back({{"id", "test.process.dependent-op"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "target"},
	                                                          {"equipment", "$target"},
	                                                          {"effect", "test.process.dependent"},
	                                                          {"grant", "dependent"}}})}});
	source["effects"].push_back({{"id", "test.process.maintain"},
	                             {"kind", "rule"},
	                             {"trigger", "maintain"},
	                             {"phase", "maintained"},
	                             {"effect", "test.process.dependent-op"}});
	source["effects"].push_back({{"id", "test.process.grant"},
	                             {"kind", "process_grant"},
	                             {"process", "test.process.tick"},
	                             {"duration_ticks", 6}});
	source["effects"].push_back(
	    {{"id", "test.process.grant-op"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "apply"}, {"target", "owner"}, {"effect", "test.process.grant"}, {"grant", "temporary"}}})}});
	source["effects"].push_back(Json::parse(
	    R"json({"id":"test.process.refresh","kind":"rule","trigger":"tick","phase":"periodic","period_ticks":2,"effect":"test.process.grant-op","when":{"op":"lt","args":[{"fact":"tick"},{"value":7,"unit":"ticks"}]}})json"));
	auto &definition = effect(source, "test.process.tick");
	definition["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
	definition["rules"] =
	    Json::array({{{"id", "dependent"}, {"rule", "test.process.maintain"}, {"equipment", "radar"}}});
	effect(source, "test.process.grant")["rules"] =
	    Json::array({{{"id", "grant-dependent"}, {"rule", "test.process.maintain"}, {"equipment", "radar"}}});
	effect(source, "test.process.start")["operations"].push_back({{"kind", "apply"},
	                                                              {"target", "owner"},
	                                                              {"equipment", "radar"},
	                                                              {"effect", "test.process.independent"},
	                                                              {"grant", "independent"}});
	source["entities"][0]["rules"] = Json::array({{{"id", "refresh"}, {"rule", "test.process.refresh"}}});
	controls(
	    source,
	    {{"temporary",
	      {{"source", "entity"}, {"process", "temporary"}, {"controller", "processes"}, {"enabled", "temporary"}}}});

	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim process and grant rule observation Console");
	ticks(world, 3);
	const auto first = owner(world).at("processes");
	require(first.size() == 1, "Repeated grant source installs one temporary process");
	const auto identity = first.begin().key();
	// A periodic effect may install after this tick's process admission boundary.
	// Compare refresh against an admitted run, not its legitimate pending grant.
	for (int wait = 0; wait < 3 && !owner(world).at("processes").at(identity).at("admitted").get<bool>(); ++wait)
		ticks(world);
	const auto initial = owner(world).at("processes").at(identity);
	require(initial.at("admitted") == true,
	        "Temporary grant begins its ordinary process run before refresh comparison");
	ticks(world, 3);
	const auto refreshed = owner(world).at("processes").at(identity);
	if (!(refreshed.at("generation") == initial.at("generation") && refreshed.at("run") == initial.at("run") &&
	      refreshed.at("expiry").get<std::int64_t>() > initial.at("expiry").get<std::int64_t>() &&
	      property(world, "test.fuel") == 97))
		throw std::runtime_error(
		    "Refresh extends temporary expiry while preserving run, progress and once-only entry payment: initial=" +
		    initial.dump() + " refreshed=" + refreshed.dump() + " fuel=" + property(world, "test.fuel").dump());
	const auto observed = world.observe(101);
	std::size_t process_rules = 0;
	for (const auto &row : observed->gameplay.at("rules"))
		process_rules += row.at("source_kind") == "process" && row.at("rule") == "test.process.maintain";
	require(process_rules == 2 &&
	            network::bounded_gameplay(observed->gameplay).at("rules") == observed->gameplay.at("rules"),
	        "Rules authored on a process and its grant retain permitted status through bounded observation projection");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 9);
	ticks(resumed, 9);
	require(owner(world).at("processes").empty() && property(world, "test.fuel") == 97,
	        "Expiry retires the temporary process without refund or another start");
	const auto applications = modifier_applications(world.save(), 1);
	require(applications.size() == 1 && applications[0].at("definition") == "test.process.independent",
	        "Process retirement withdraws its dependent maintained contribution while independent committed "
	        "consequence survives");
	require(world.save() == resumed.save(), "Refreshed grant expiry and native dependent cleanup continue exactly");
	auto removed = fixture();
	removed["effects"].push_back({{"id", "test.process.grant"},
	                              {"kind", "process_grant"},
	                              {"process", "test.process.tick"},
	                              {"duration_ticks", 20}});
	removed["effects"].push_back(
	    {{"id", "test.process.remove"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "apply"}, {"target", "protected"}, {"effect", "test.process.grant"}, {"grant", "removed"}},
	           {{"kind", "remove"}, {"target", "protected"}, {"grant", "removed"}}})}});
	scenario(removed, {{1, "test.process.remove"}});
	const auto removal_catalog = load(removed);
	World gone;
	gone.load(removal_catalog, "test.map");
	ticks(gone, 4);
	require(owner(gone).at("processes").empty() && property(gone, "test.events") == 0 &&
	            property(gone, "test.work") == 0 &&
	            gone.save().at("scenario").at("receipts")[0].at("status") ==
	                static_cast<unsigned>(ScenarioActionStatus::Succeeded),
	        "Atomic apply/remove dispatch resolves a process grant and cannot start a removed lifetime");
	return Json::array({portable(world.save()), portable(gone.save())});
}

Json competing_and_invalid_grants()
{
	auto source = fixture();
	source["entities"][0]["properties"]["test.fuel"] = 1;
	source["effects"].push_back(
	    {{"id", "test.process.failed-work"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array({{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.work"}, {"amount", 10}},
	                   {{"kind", "consume"},
	                    {"target", "owner"},
	                    {"store", "energy:cell"},
	                    {"resource", "fixture.resource.energy"},
	                    {"amount", 2000}}})}});
	auto failed = effect(source, "test.process.tick");
	failed["id"] = "test.process.failed";
	failed["work_effect"] = "test.process.failed-work";
	failed.erase("work_values");
	failed["work_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
	source["effects"].push_back(failed);
	effect(source, "test.process.tick")["work_costs"] =
	    Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
	source["entities"][0]["processes"] = {{"a-failed", {{"process", "test.process.failed"}}},
	                                      {"b-good", {{"process", "test.process.tick"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world);
	if (!(property(world, "test.fuel") == 0 && property(world, "test.work") == 1000 && energy(world) == 1000))
		throw std::runtime_error(
		    "Rejected provisional work reservations release exact scarce input for a competing valid process: fuel=" +
		    property(world, "test.fuel").dump() + " work=" + property(world, "test.work").dump() +
		    " energy=" + std::to_string(energy(world)) + " processes=" + owner(world).at("processes").dump());
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 3);
	ticks(resumed, 3);
	require(property(world, "test.work") == 1000 && world.save() == resumed.save(),
	        "Competing consumers cannot overspend exhausted quantity and resume without leaking reservations");
	auto invalid = fixture();
	invalid["effects"].push_back(
	    {{"id", "test.native.granted"}, {"kind", "process"}, {"native", "generation"}, {"label", "Invalid generator"}});
	invalid["effects"].push_back({{"id", "test.native.grant"},
	                              {"kind", "process_grant"},
	                              {"process", "test.native.granted"},
	                              {"duration_ticks", 20}});
	invalid["effects"].push_back(
	    {{"id", "test.native.apply"},
	     {"kind", "operations"},
	     {"costs", Json::array({{{"payer", "protected"}, {"property", "test.fuel"}, {"amount", 5}}})},
	     {"operations",
	      Json::array({{{"kind", "property_add"}, {"target", "protected"}, {"property", "test.events"}, {"amount", 1}},
	                   {{"kind", "apply"},
	                    {"target", "protected"},
	                    {"equipment", "radar"},
	                    {"effect", "test.native.grant"},
	                    {"grant", "invalid"}}})}});
	scenario(invalid, {{1, "test.native.apply"}});
	const auto invalid_catalog = load(invalid);
	World refused;
	refused.load(invalid_catalog, "test.map");
	ticks(refused, 4);
	require(property(refused, "test.fuel") == 100 && property(refused, "test.events") == 0 && energy(refused) == 1000 &&
	            refused.save().at("scenario").at("receipts")[0].at("status") ==
	                static_cast<unsigned>(ScenarioActionStatus::Failed),
	        "A native process grant on incompatible equipment rolls back its complete paid effect bundle");
	return Json::array({portable(world.save()), portable(refused.save())});
}
Json active_process_removal()
{
	auto source = fixture();
	source["effects"].push_back({{"id", "test.process.grant"},
	                             {"kind", "process_grant"},
	                             {"process", "test.process.tick"},
	                             {"duration_ticks", 100}});
	source["effects"].push_back(
	    {{"id", "test.process.grant-once"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "apply"}, {"target", "protected"}, {"effect", "test.process.grant"}, {"grant", "active"}}})}});
	source["effects"].push_back(
	    {{"id", "test.process.remove-owner"},
	     {"kind", "operations"},
	     {"operations", Json::array({{{"kind", "remove"}, {"target", "owner"}, {"scope", "owner"}}})}});
	source["effects"].push_back(Json::parse(
	    R"json({"id":"test.process.remove-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.process.remove-owner","when":{"op":"ge","args":[{"fact":"tick"},{"value":7,"unit":"ticks"}]}})json"));
	effect(source, "test.process.tick")["rules"] =
	    Json::array({{{"id", "self-remove"}, {"rule", "test.process.remove-rule"}}});
	scenario(source, {{1, "test.process.grant-once"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 5);
	require(owner(world).at("processes").size() == 1 && property(world, "test.work").get<std::int64_t>() > 0,
	        "Temporary process is active before authored owner removal");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 6);
	ticks(resumed, 6);
	require(
	    owner(world).at("processes").empty() && world.save() == resumed.save(),
	    "Trusted owner-scoped effect removes the exact active process lifetime before expiry and continues exactly");
	return portable(world.save());
}

Json bounded_process_prices()
{
	auto source = fixture();
	auto &definition = effect(source, "test.process.tick");
	definition["work_scale"] = {{"value", 1000000}, {"unit", "permille"}};
	definition["work_costs"] =
	    Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1000000000000LL}}});
	source["entities"][0]["processes"] = {{"expensive", {{"process", "test.process.tick"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 3);
	require(property(world, "test.work") == 0 && property(world, "test.fuel") == 100,
	        "A validated but unpayable scaled price pauses its own process without overflowing or stopping the World");
	return portable(world.save());
}

Json granted_owner_selectors()
{
	auto source = fixture();
	source["effects"].push_back({{"id", "test.process.grant"},
	                             {"kind", "process_grant"},
	                             {"process", "test.process.tick"},
	                             {"duration_ticks", 5}});
	source["effects"].push_back(
	    {{"id", "test.process.grant-owners"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "apply"}, {"target", "protected"}, {"effect", "test.process.grant"}, {"grant", "actor"}},
	           {{"kind", "apply"},
	            {"target", "protected"},
	            {"equipment", "radar"},
	            {"effect", "test.process.grant"},
	            {"grant", "equipment"}},
	           {{"kind", "apply"},
	            {"target", "protected"},
	            {"officer", "a"},
	            {"effect", "test.process.grant"},
	            {"grant", "officer"}},
	           {{"kind", "apply"},
	            {"target", "protected"},
	            {"compartment", "fore"},
	            {"effect", "test.process.grant"},
	            {"grant", "compartment"}}})}});
	scenario(source, {{1, "test.process.grant-owners"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 4);
	require(
	    owner(world).at("processes").size() == 1 && owner(world, "radar").at("processes").size() == 1 &&
	        owner(world, {}, "a").at("processes").size() == 1 && owner(world, {}, {}, 0).at("processes").size() == 1 &&
	        property(world, "test.work", {}, {}, 0).get<std::int64_t>() > 0,
	    "Temporary effect-only processes resolve explicit actor, equipment, officer and compartment grant selectors");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 5);
	ticks(resumed, 5);
	require(owner(world).at("processes").empty() && owner(world, "radar").at("processes").empty() &&
	            owner(world, {}, "a").at("processes").empty() && owner(world, {}, {}, 0).at("processes").empty() &&
	            world.save() == resumed.save(),
	        "Every native owner expires only its own temporary process and preserves exact continuation");
	return portable(world.save());
}

Json maintained_process_grants()
{
	auto source = fixture();
	source["effects"].push_back({{"id", "test.parent.grant"},
	                             {"kind", "process_grant"},
	                             {"process", "test.process.tick"},
	                             {"duration_ticks", 100}});
	source["effects"].push_back(
	    {{"id", "test.parent.apply"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "apply"}, {"target", "owner"}, {"effect", "test.parent.grant"}, {"grant", "dependent"}}})}});
	source["effects"].push_back(Json::parse(R"json({"id":"test.parent.rule","kind":"rule",
	    "trigger":"maintain","phase":"maintained","effect":"test.parent.apply",
	    "when":{"fact":"owner.property","property":"test.allowed","view":"effective"}})json"));
	source["entities"][0]["rules"] = Json::array({{{"id", "parent"}, {"rule", "test.parent.rule"}}});
	source["effects"].push_back({{"id", "test.parent.independent"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"effect", "test.parent.grant"},
	                                                          {"grant", "independent"}}})}});
	source["effects"].push_back(
	    {{"id", "test.parent.off"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_set"}, {"target", "protected"}, {"property", "test.allowed"}, {"value", false}}})}});
	scenario(source, {{1, "test.parent.independent"}, {7, "test.parent.off"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 5);
	require(owner(world).at("processes").size() == 2,
	        "Maintained parent and committed independent effect grant separate running process lifetimes");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 5);
	ticks(resumed, 5);
	const auto processes = owner(world).at("processes");
	require(processes.size() == 1 && processes.begin().value().at("slot") == "independent" &&
	            processes.begin().value().at("admitted") == true,
	        "Parent withdrawal retires only its dependent process while a committed independent grant survives");
	require(world.save() == resumed.save(), "Maintained process grant withdrawal continues exactly");
	return portable(world.save());
}

Json unpaid_exit_consequences()
{
	Json trace = Json::array();
	for (bool stop : {false, true})
	{
		auto source = fixture();
		source["effects"].push_back(
		    {{"id", "test.process.unpaid-exit"},
		     {"kind", "operations"},
		     {"costs", Json::array({{{"payer", "owner"},
		                             {"store", "energy:cell"},
		                             {"resource", "fixture.resource.energy"},
		                             {"amount", 2000}}})},
		     {"operations",
		      Json::array(
		          {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 5000}}})}});
		auto &process = effect(source, "test.process.tick");
		process[stop ? "stop_effect" : "pause_effect"] = "test.process.unpaid-exit";
		process["requirements"] = {{"fact", "owner.property"}, {"property", "test.allowed"}};
		source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
		controls(
		    source,
		    {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}}});
		if (!stop)
		{
			source["effects"].push_back({{"id", "test.process.block-exit"},
			                             {"kind", "operations"},
			                             {"operations", Json::array({{{"kind", "property_set"},
			                                                          {"target", "protected"},
			                                                          {"property", "test.allowed"},
			                                                          {"value", false}}})}});
			scenario(source, {{4, "test.process.block-exit"}});
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive"), "Claim mandatory exit fixture");
		ticks(world, 3);
		if (stop)
			command(world, "processes", "main", 0);
		ticks(world, 3);
		require(state(world, "main").at("phase") == (stop ? "off" : "paused") &&
		            state(world, "main").at("requested") == !stop && state(world, "main").at("admitted") == !stop &&
		            property(world, "test.events") == 1 && energy(world) == 1000,
		        "Unpayable exit consequence cannot prevent mandatory pause or stop and commits no fee or effect");
		const auto settled = world.save();
		const auto work = property(world, "test.work");
		World resumed;
		resumed.restore(catalog, settled);
		ticks(world, 3);
		ticks(resumed, 3);
		require(property(world, "test.work") == work &&
		            world.save().at("rules").at("next_job") == settled.at("rules").at("next_job") &&
		            world.save() == resumed.save(),
		        "Failed exit consequence is recorded once rather than retried every paused tick and continues exactly");
		trace.push_back(portable(world.save()));
	}
	return trace;
}

Json process_fractional_rate_lifetime()
{
	auto source = fixture();
	source["effects"].push_back(
	    {{"id", "test.process.fractional-rate"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_rate"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 1}}})}});
	auto &process = effect(source, "test.process.tick");
	process.erase("start_effect");
	process.erase("work_values");
	process["work_effect"] = "test.process.fractional-rate";
	process["requirements"] = {{"fact", "owner.property"}, {"property", "test.allowed"}};
	source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
	source["effects"].push_back({{"id", "test.process.unrelated"},
	                             {"kind", "modifier"},
	                             {"duration_ticks", 3},
	                             {"contributions", {{"range", {{"property", "sensors"}, {"delta_permille", 100}}}}}});
	source["effects"].push_back({{"id", "test.process.unrelated-apply"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"equipment", "radar"},
	                                                          {"effect", "test.process.unrelated"},
	                                                          {"grant", "unrelated"}}})}});
	for (const auto &[id, value] :
	     std::vector<std::pair<std::string, bool>>{{"test.process.rate-off", false}, {"test.process.rate-on", true}})
		source["effects"].push_back({{"id", id},
		                             {"kind", "operations"},
		                             {"operations", Json::array({{{"kind", "property_set"},
		                                                          {"target", "protected"},
		                                                          {"property", "test.allowed"},
		                                                          {"value", value}}})}});
	scenario(source, {{1, "test.process.unrelated-apply"}, {8, "test.process.rate-off"}, {12, "test.process.rate-on"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	int work_ticks = 0;
	for (int tick = 0; tick < 7; ++tick)
	{
		ticks(world);
		work_ticks += state(world, "main").at("actual_work") == 1000;
	}
	require(work_ticks == 7 && property(world, "test.events") == 0 && modifier_applications(world.save(), 1).empty(),
	        "Fractional process rate remains pending after an unrelated finite modifier retires");
	World resumed;
	resumed.restore(catalog, world.save());
	bool paused = false;
	for (int tick = 0; tick < 40 && work_ticks < World::ticks_per_second; ++tick)
	{
		ticks(world);
		ticks(resumed);
		work_ticks += state(world, "main").at("actual_work") == 1000;
		paused |= state(world, "main").at("phase") == "paused";
	}
	require(paused && work_ticks == World::ticks_per_second && property(world, "test.events") == 1 &&
	            world.save() == resumed.save(),
	        "Process without attached rules retains fractional property work across unrelated retirement, pause and "
	        "restore");
	return portable(world.save());
}
