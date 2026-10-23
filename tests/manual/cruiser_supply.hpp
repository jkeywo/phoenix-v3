// Explicit production-fit exercise; deliberate quiet combat-map variant, never a balance test.
void cruiser_supply(phoenix::Catalog catalog)
{
	using namespace phoenix;
	catalog.maps.at("base.cruiser-combat").erase("scenario");
	catalog.scenarios.erase("base.cruiser-combat");
	for (auto &entity : catalog.maps.at("base.cruiser-combat")["entities"])
		if (entity["id"] == 90)
		{
			entity["x_mm"] = 600000;
			entity["y_mm"] = 0;
		}
	for (auto &store : catalog.entities.at("base.cruiser")["logistics"]["stores"])
		if (store["id"] == "main-hold")
		{
			store["initial"]["parts"] = 0;
			store["initial"]["medical"] = 0;
		}
	World world;
	world.load(catalog, "base.cruiser-combat", {{"cruiser", "base.cruiser"}});
	const auto ship = *world.ship_for_slot("cruiser");
	for (const auto &[claim, console] : std::vector<std::pair<int, std::string>>{
	         {901, "comms"}, {902, "engineering"}, {903, "tactical"}, {904, "helm"}})
		if (!world.claim(claim, ship, console))
			throw std::runtime_error("Supply watch claim refused");
	auto ticks = [&](int n)
	{
		while (n--)
			world.tick();
	};
	auto controller = [&](int claimant, const std::string &id)
	{
		const auto view = *world.observe(claimant);
		for (const auto &c : view.gameplay.at("controllers"))
			if (c.at("id") == id)
				return c;
		throw std::runtime_error("Supply Controller missing: " + id);
	};
	ticks(100);
	OperatingCommand hail{ship, "comms.hail", 0, {}};
	hail.controller = "external-comms";
	hail.generation = controller(901, hail.controller).at("generation");
	hail.text = "ship:90/quartermaster";
	if (!world.submit(901, hail))
	{
		std::ofstream("build/cruiser-supply-hail.json")
		    << Json{{"observation", world.observe(901)->gameplay}, {"save", world.save()}}.dump(2);
		throw std::runtime_error("Depot hail refused");
	}
	ticks(20);
	bool answered = false;
	for (int i = 0; i < 200 && !answered; ++i)
	{
		const auto view = *world.observe(901);
		for (const auto &thread : view.gameplay.at("conversations").at("threads"))
			if (thread.at("prompt") != 0)
			{
				OperatingCommand c{ship, "comms.answer", thread.at("id"), {}};
				c.controller = "external-comms";
				c.generation = controller(901, c.controller).at("generation");
				c.request_id = thread.at("prompt");
				c.text = "both";
				answered = world.submit(901, c);
				if (answered)
					break;
			}
		world.tick();
	}
	if (!answered)
		throw std::runtime_error("Depot conversation choice unavailable");
	ticks(10);
	Json receipt{{"variant", "Quiet Cruiser combat map; nearby depot; initial parts/medical deliberately depleted"},
	             {"missions", Json::array()}};
	auto row = [&](const std::string &id)
	{
		const auto view = *world.observe(902);
		for (const auto &r : view.gameplay.at("away_missions"))
			if (r.at("id") == id)
				return r;
		throw std::runtime_error("Supply offer was not disclosed: " + id);
	};
	for (const auto *mission : {"depot-gift", "depot-trade"})
	{
		auto issue =
		    [&](int claim, const std::string &owner, const std::string &verb, const std::string &text = std::string{})
		{
			OperatingCommand c{ship, "away." + verb, row(mission).at("revision"), mission};
			c.controller = owner;
			c.generation = controller(claim, owner).at("generation");
			c.text = text;
			if (!world.submit(claim, c))
				throw std::runtime_error("Supply command refused: " + verb + " " + row(mission).dump());
			world.tick();
		};
		issue(902, "engineering-dispatch", "open");
		issue(903, "combat-bay", "contribute", "1");
		for (int i = 0; i < 2000 && row(mission).at("blocker") != ""; ++i)
		{
			world.tick();
			if (i % 200 == 0)
				std::cout << mission << " preparation " << world.ticks() << ": " << row(mission).dump() << std::endl;
		}
		issue(902, "engineering-dispatch", "send");
		std::set<std::string> phases;
		for (int i = 0; i < 10000 && row(mission).at("phase") != "recovered"; i += 20)
		{
			ticks(20);
			if (i % 500 == 0)
			{
				std::ofstream("build/cruiser-supply-progress.json") << world.save().dump(2);
				std::cout << mission << " flight " << world.ticks() << ": " << row(mission).dump() << std::endl;
			}
			if (i % 20 == 0)
			{
				const auto r = row(mission);
				const auto phase = r.at("supply").at("phase").get<std::string>();
				if (phases.insert(phase).second)
				{
					World restored;
					restored.restore(catalog, world.save());
					world.tick();
					restored.tick();
					if (world.save() != restored.save())
						throw std::runtime_error("Supply continuation diverged: " + phase);
				}
			}
		}
		const auto result = row(mission);
		if (result.at("result") != "complete:Supplies delivered")
			throw std::runtime_error("Supply recovery incomplete: " + result.dump());
		receipt["missions"].push_back(result);
	}
	std::ofstream("build/cruiser-supply-receipt.json") << receipt.dump(2);
	std::cout << "PASS Cruiser one-star Comms disclosure, Engineering collection/barter, Tactical escort and phase "
	             "recovery\n";
}
