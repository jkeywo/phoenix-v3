// Synthetic finite jobs run through production loading, funding and continuation.
Json finite_effect_only_jobs()
{
	auto source = fixture();
	auto &job = effect(source, "test.process.tick");
	job["required_work"] = 2500;
	job["completion_effect"] = "test.process.pause";
	job["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
	job["work_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 2}}});
	source["entities"][0]["processes"] = {{"finite", {{"process", "test.process.tick"}}}};
	controls(source,
	         {{"finite",
	           {{"source", "entity"}, {"process", "finite"}, {"controller", "processes"}, {"enabled", "finite"}}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim finite job fixture");
	ticks(world, 2);
	require(state(world, "finite").at("progress") == 2000 && !state(world, "finite").at("completed").get<bool>(),
	        "Finite job retains partial committed progress");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 5);
	ticks(resumed, 5);
	require(state(world, "finite").at("progress") == 2500 && state(world, "finite").at("phase") == "completed" &&
	            state(world, "finite").at("completion_delivered") == true && property(world, "test.work") == 2500 &&
	            property(world, "test.events") == 11 && property(world, "test.fuel") == 92,
	        "Finite job caps the final fractional work, funds exact costs and emits completion once");
	require(world.save() == resumed.save(), "Finite completion continues without duplicate work or consequences");
	auto contradictory = world.save();
	bool completion_alias_changed = false;
	for (auto &row : contradictory["processes"])
		if (row.at("entity") == 1 && row.at("equipment") == "" && row.at("officer") == "" &&
		    row.at("compartment") == -1 && row.at("state").at("processes").contains("finite"))
		{
			row["state"]["processes"]["finite"]["completion_delivered"] = false;
			completion_alias_changed = true;
		}
	require(completion_alias_changed, "Synthetic completed process exposes its schema-31 completion alias");
	bool contradictory_alias_refused = false;
	try
	{
		World invalid;
		invalid.restore(catalog, contradictory);
	}
	catch (const std::exception &)
	{
		contradictory_alias_refused = true;
	}
	require(contradictory_alias_refused,
	        "Schema-31 restore rejects a completion alias contradicting its authoritative value");
	command(world, "processes", "finite", 0);
	ticks(world);
	command(world, "processes", "finite", 1);
	ticks(world);
	require(state(world, "finite").at("completed") == false && state(world, "finite").at("progress") == 1000,
	        "Explicit restart begins a fresh finite run");
	command(world, "processes", "finite", 0);
	ticks(world, 3);
	require(state(world, "finite").at("phase") == "off" && property(world, "test.work") == 3500,
	        "Cancellation stops further work without undoing previous committed consequences");
	auto failure = fixture();
	auto &failed = effect(failure, "test.process.tick");
	failed["required_work"] = 1000;
	failed["completion_effect"] = "test.finite.unpayable";
	failure["effects"].push_back(
	    {{"id", "test.finite.unpayable"},
	     {"kind", "operations"},
	     {"costs", Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 200}}})},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 10}}})}});
	failure["entities"][0]["processes"] = {{"finite", {{"process", "test.process.tick"}}}};
	const auto rejected = load(failure);
	World completed;
	completed.load(rejected, "test.map");
	ticks(completed, 3);
	require(state(completed, "finite").at("completed") == true &&
	            state(completed, "finite").at("completion_delivered") == true &&
	            property(completed, "test.work") == 1000 && property(completed, "test.events") == 1 &&
	            property(completed, "test.fuel") == 100 &&
	            !state(completed, "finite").at("blocker").get<std::string>().empty(),
	        "Failed completion consequences neither roll back native work nor partially charge or apply");
	World restored;
	restored.restore(rejected, completed.save());
	ticks(completed, 4);
	ticks(restored, 4);
	require(completed.save() == restored.save() && property(completed, "test.events") == 1,
	        "A failed completion remains attempted exactly once through restore");
	auto paused_source = fixture();
	auto &pausable = effect(paused_source, "test.process.tick");
	pausable["required_work"] = 20000;
	pausable["requirements"] = {{"fact", "owner.property"}, {"property", "test.allowed"}};
	pausable["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
	paused_source["entities"][0]["processes"] = {{"finite", {{"process", "test.process.tick"}}}};
	for (const auto &[name, allowed] : std::vector<std::pair<std::string, bool>>{{"block", false}, {"unblock", true}})
		paused_source["effects"].push_back({{"id", "test.finite." + name},
		                                    {"kind", "operations"},
		                                    {"operations", Json::array({{{"kind", "property_set"},
		                                                                 {"target", "owner"},
		                                                                 {"property", "test.allowed"},
		                                                                 {"value", allowed}}})}});
	scenario(paused_source, {{3, "test.finite.block"}, {8, "test.finite.unblock"}});
	const auto paused_catalog = load(paused_source);
	World paused;
	paused.load(paused_catalog, "test.map");
	ticks(paused, 5);
	const auto checkpoint = state(paused, "finite");
	require(checkpoint.at("phase") == "paused" && checkpoint.at("progress").get<std::int64_t>() > 0,
	        "Finite job pauses with committed target progress");
	World continued;
	continued.restore(paused_catalog, paused.save());
	ticks(paused, 2);
	ticks(continued, 2);
	require(state(paused, "finite").at("progress") == checkpoint.at("progress"), "Blocked finite job commits no work");
	ticks(paused, 5);
	ticks(continued, 5);
	require(state(paused, "finite").at("phase") == "running" &&
	            state(paused, "finite").at("run") == checkpoint.at("run") && property(paused, "test.fuel") == 97 &&
	            paused.save() == continued.save(),
	        "Finite job resumes the same paid run and exact saved fractional state");
	auto invalid = fixture();
	effect(invalid, "test.process.tick")["required_work"] = 0;
	bool refused = false;
	try
	{
		(void)load(invalid);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused, "Finite effect jobs reject nonpositive work requirements");
	return Json::array({portable(world.save()), portable(completed.save()), portable(paused.save())});
}

Json finite_stop_restarts()
{
	auto self_source = fixture();
	auto &self_job = effect(self_source, "test.process.tick");
	self_job["required_work"] = 1000;
	self_job["completion_effect"] = "test.finite.self-remove";
	self_source["effects"].push_back(
	    {{"id", "test.finite.self-remove"},
	     {"kind", "operations"},
	     {"operations", Json::array({{{"kind", "remove"}, {"target", "owner"}, {"scope", "owner"}}})}});
	self_source["entities"][0]["processes"] = {{"finite", {{"process", "test.process.tick"}}}};
	controls(self_source,
	         {{"finite",
	           {{"source", "entity"}, {"process", "finite"}, {"controller", "processes"}, {"enabled", "finite"}}}});
	const auto self_catalog = load(self_source);
	World self;
	self.load(self_catalog, "test.map");
	require(self.claim(101, 1, "drive"), "Claim self-removing finite job");
	self.tick();
	require(state(self, "finite").at("phase") == "off" && state(self, "finite").at("requested") == false &&
	            state(self, "finite").at("progress") == 0 && state(self, "finite").at("completed") == false &&
	            property(self, "test.work") == 1000,
	        "Completion owner-removal ends a finite job with reusable valid run state");
	World self_restored;
	self_restored.restore(self_catalog, self.save());
	const auto old_run = state(self, "finite").at("run");
	for (auto *current : {&self, &self_restored})
	{
		command(*current, "processes", "finite", 1);
		current->tick();
	}
	require(property(self, "test.work") == 2000 && state(self, "finite").at("run") != old_run &&
	            state(self, "finite").at("phase") == "off" && self.save() == self_restored.save(),
	        "Explicit restart after completion self-removal performs fresh work exactly once through restore");

	auto stopped_source = fixture();
	auto &stopped_job = effect(stopped_source, "test.process.tick");
	stopped_job["required_work"] = 3000;
	stopped_job["shortage"] = "stop";
	stopped_job["work_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
	stopped_source["entities"][0]["properties"]["test.fuel"] = 1;
	stopped_source["entities"][0]["processes"] = {{"finite", {{"process", "test.process.tick"}}}};
	controls(stopped_source,
	         {{"finite",
	           {{"source", "entity"}, {"process", "finite"}, {"controller", "processes"}, {"enabled", "finite"}}}});
	const auto stopped_catalog = load(stopped_source);
	World stopped;
	stopped.load(stopped_catalog, "test.map");
	require(stopped.claim(101, 1, "drive"), "Claim shortage-stop finite job");
	stopped.tick();
	require(state(stopped, "finite").at("progress") == 1000 && property(stopped, "test.fuel") == 0,
	        "Finite job commits partial work before exhausting funding");
	stopped.tick();
	require(state(stopped, "finite").at("phase") == "off" && state(stopped, "finite").at("requested") == false &&
	            state(stopped, "finite").at("progress") == 0 && state(stopped, "finite").at("completed") == false,
	        "Funding stop resets finite run state while retaining committed world consequences");
	auto refilled = stopped.save();
	for (auto &owner : refilled["custom_properties"])
		if (owner["entity"] == 1 && owner["equipment"] == "" && owner["officer"] == "" && owner["compartment"] == -1)
			owner["values"]["test.fuel"] = 3;
	stopped.restore(stopped_catalog, refilled);
	World stopped_restored;
	stopped_restored.restore(stopped_catalog, refilled);
	for (auto *current : {&stopped, &stopped_restored})
	{
		command(*current, "processes", "finite", 1);
		current->tick();
	}
	require(state(stopped, "finite").at("progress") == 1000 && property(stopped, "test.work") == 2000 &&
	            stopped.save() == stopped_restored.save(),
	        "A stopped finite job restarts from a fresh run after resupply, without replaying prior work");
	return Json::array({portable(self.save()), portable(stopped.save())});
}
