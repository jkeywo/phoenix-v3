// Included after the FTL system fixture's native command helpers.
Json finite_ftl_process(const World &world)
{
	const auto saved = world.save();
	for (const auto &owner : saved.at("processes"))
		if (owner.at("entity") == 1 && owner.at("equipment") == "jump")
			for (const auto &process : owner.at("state").at("processes"))
				if (process.at("native_id") == "jump")
					return process;
	throw std::runtime_error("Missing finite FTL process");
}
void finite_ftl_cases()
{
	auto source = ftl_fixture();
	source["properties"] = Json::array({{{"id", "test.ftl-fuel"},
	                                     {"kind", "quantity"},
	                                     {"unit", "units"},
	                                     {"minimum", 0},
	                                     {"maximum", 100000},
	                                     {"default", 100000}}});
	source["effects"] = Json::array(
	    {{{"id", "test.finite-ftl"},
	      {"kind", "process"},
	      {"native", "ftl"},
	      {"upkeep_costs", Json::array({{{"payer", "owner"}, {"property", "test.ftl-fuel"}, {"per_second", 1}}})},
	      {"work_costs", Json::array({{{"payer", "owner"}, {"property", "test.ftl-fuel"}, {"per_work", 1}}})}}});
	source["entities"][0]["systems"]["jump"]["properties"] = {{"test.ftl-fuel", 100000}};
	source["entities"][0]["systems"]["jump"]["processes"] = {{"finite", {{"process", "test.finite-ftl"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Finite FTL fixture claimed");
	require(world.submit(101, command(world, "system_enabled", 1)), "Begin finite FTL preparation");
	for (int i = 0; i < 1000 && !battery(world).at("ready").get<bool>(); ++i)
		world.tick();
	const auto completed = finite_ftl_process(world);
	require(completed.at("completed") && completed.at("completion_delivered") && completed.at("ready") &&
	            completed.at("progress") == battery(world).at("energy") &&
	            completed.at("required_work") == battery(world).at("capacity"),
	        "Finite FTL completion publishes physical reserve readiness");
	const auto run = completed.at("run");
	ticks(world, 40);
	require(finite_ftl_process(world).at("run") == run && finite_ftl_process(world).at("completed") &&
	            battery(world).at("ready") && battery(world).at("power_draw") > 0,
	        "Ready maintenance retains one completion episode and continues paying native upkeep");
	World restored;
	restored.restore(catalog, world.save());
	ticks(world, 17);
	ticks(restored, 17);
	require(world.save() == restored.save(), "Completed finite FTL episode continues without replay");
	require(!finite_ftl_process(world).at("idle_credits").empty() &&
	            !finite_ftl_process(world).at("work_credits").empty(),
	        "Ready FTL fixture retains fractional process funding credits");
	require(world.submit(101, isolate(world, 1)), "Isolate ready reserve to exercise depletion boundary");
	world.tick();
	require(!finite_ftl_process(world).at("completed").get<bool>() &&
	            !finite_ftl_process(world).at("admitted").get<bool>() &&
	            finite_ftl_process(world).at("idle_credits").empty() &&
	            finite_ftl_process(world).at("work_credits").empty(),
	        "Genuine reserve depletion retires the completed episode and its paid credits");
	restored.restore(catalog, world.save());
	ticks(world, 3);
	ticks(restored, 3);
	require(world.save() == restored.save(), "Ready-to-depleted funding boundary restores exactly");
	require(world.submit(101, isolate(world, 0)), "Restore finite preparation supply");
	for (int i = 0; i < 1000 && !battery(world).at("ready").get<bool>(); ++i)
		world.tick();
	require(world.submit(101, command(world, "system_enabled", 0)), "Stop finite FTL preparation");
	ticks(world, 12);
	require(!finite_ftl_process(world).at("ready").get<bool>() &&
	            finite_ftl_process(world).at("progress") == battery(world).at("energy"),
	        "Physical off-state discharge removes finite readiness");
	require(world.submit(101, command(world, "system_enabled", 1)), "Restart depleted FTL preparation");
	for (int i = 0; i < 1000 && !battery(world).at("ready").get<bool>(); ++i)
		world.tick();
	require(finite_ftl_process(world).at("run").get<std::uint64_t>() > run.get<std::uint64_t>() &&
	            finite_ftl_process(world).at("completed"),
	        "A replenished reserve completes a fresh native preparation episode");
	auto starved = world.save();
	for (auto &owner : starved["custom_properties"])
		if (owner.at("entity") == 1 && owner.at("equipment") == "jump")
			owner["values"]["test.ftl-fuel"] = 0;
	world.restore(catalog, starved);
	ticks(world, 40);
	require(!battery(world).at("ready").get<bool>() && battery(world).at("power_draw") == 0 &&
	            battery(world).at("energy") < battery(world).at("capacity"),
	        "Unfunded FTL work disables native operation and cannot retain free departure readiness");
	restored.restore(catalog, world.save());
	ticks(world, 3);
	ticks(restored, 3);
	require(world.save() == restored.save(), "Unfunded FTL reserve and retained intent continue exactly");
	for (auto &effect : source["effects"])
		if (effect.at("id") == "test.finite-ftl")
			effect["shortage"] = "stop";
	const auto stop_catalog = load(source);
	World stopped;
	stopped.load(stop_catalog, "test.map");
	require(stopped.claim(101, 1, "drive") && stopped.submit(101, command(stopped, "system_enabled", 1)),
	        "Enable finite FTL with terminal shortage policy");
	for (int i = 0; i < 1000 && !battery(stopped).at("ready").get<bool>(); ++i)
		stopped.tick();
	require(finite_ftl_process(stopped).at("completed"), "Stop-policy fixture reaches completed FTL readiness");
	auto no_fuel = stopped.save();
	for (auto &owner : no_fuel["custom_properties"])
		if (owner.at("entity") == 1 && owner.at("equipment") == "jump")
			owner["values"]["test.ftl-fuel"] = 0;
	stopped.restore(stop_catalog, no_fuel);
	for (int i = 0; i < 40 && finite_ftl_process(stopped).at("requested").get<bool>(); ++i)
		stopped.tick();
	const auto terminal = finite_ftl_process(stopped);
	require(terminal.at("phase") == "off" && !terminal.at("requested").get<bool>() &&
	            !terminal.at("admitted").get<bool>() && !terminal.at("completed").get<bool>() &&
	            !terminal.at("completion_delivered").get<bool>() && !terminal.at("ready").get<bool>(),
	        "Terminal FTL funding shortage clears completion and readiness at the stop boundary");
	World stop_restored;
	stop_restored.restore(stop_catalog, stopped.save());
	require(stop_restored.save() == stopped.save(), "Completed-to-stopped FTL restores at the exact terminal boundary");
	ticks(stopped, 3);
	ticks(stop_restored, 3);
	require(stop_restored.save() == stopped.save(),
	        "Terminal FTL shortage continues without replay or automatic restart");
	for (auto &effect : source["effects"])
		if (effect.at("id") == "test.finite-ftl")
		{
			effect["shortage"] = "pause";
			effect["completion_effect"] = "test.ftl-ready-fee";
		}
	source["effects"].push_back(
	    {{"id", "test.ftl-ready-fee"},
	     {"kind", "operations"},
	     {"costs", Json::array({{{"payer", "owner"},
	                             {"store", "energy:jump-cell"},
	                             {"resource", "fixture.resource.energy"},
	                             {"amount", 1}}})},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.ftl-fuel"}, {"amount", 0}}})}});
	const auto completion_catalog = load(source);
	World completion;
	completion.load(completion_catalog, "test.map");
	require(completion.claim(101, 1, "drive") && completion.submit(101, command(completion, "system_enabled", 1)),
	        "Enable finite FTL with reserve-spending completion consequence");
	completion.tick();
	for (int i = 0; i < 1000 && !finite_ftl_process(completion).at("completed").get<bool>(); ++i)
		completion.tick();
	const auto charged_completion = finite_ftl_process(completion);
	require(charged_completion.at("completed") && charged_completion.at("completion_delivered") &&
	            !charged_completion.at("ready").get<bool>() &&
	            charged_completion.at("progress") == battery(completion).at("energy") &&
	            charged_completion.at("progress") < charged_completion.at("required_work") &&
	            !battery(completion).at("ready").get<bool>() &&
	            !completion.submit(101, command(completion, "ftl.depart")),
	        "FTL completion republishes live reserve facts after its consequence spends energy");
	World completion_restored;
	completion_restored.restore(completion_catalog, completion.save());
	require(completion_restored.save() == completion.save(),
	        "FTL completion consequence boundary restores without stale readiness");
}
