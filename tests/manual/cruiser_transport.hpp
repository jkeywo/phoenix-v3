// Explicit fit exercise, using the existing rescue operation at Cruiser clearance.
void cruiser_transport(phoenix::Catalog catalog)
{
	using namespace phoenix;
	auto &map = catalog.maps.at("base.rescue-under-threat");
	map["ship_slots"][0]["allowed_ships"] = Json::array({"base.cruiser"});
	map["entities"][0]["x_mm"] = 350000;
	map["entities"][0]["y_mm"] = 0;
	Json receipt = Json::array();
	for (const bool shuttle : {true, false})
	{
		World world;
		world.load(catalog, "base.rescue-under-threat", {{"courier", "base.cruiser"}});
		const auto ship = *world.ship_for_slot("courier");
		// Allow the exercise's ordinary AI Comms exchange to reveal the operation.
		for (int i = 0; i < 40; ++i)
			world.tick();
		for (const auto &[claim, console] : std::vector<std::pair<int, std::string>>{
		         {901, "comms"}, {902, "engineering"}, {903, "science"}, {904, "tactical"}})
			if (!world.claim(claim, ship, console))
				throw std::runtime_error("Transport station claim refused: " + console);
		auto advance = [&](int ticks)
		{
			while (ticks-- > 0)
				world.tick();
		};
		auto mission = [&]()
		{
			return world.observe(901)->gameplay.at("away_missions").at(0);
		};
		auto order =
		    [&](int claim, const std::string &controller, const std::string &action, const std::string &text = "")
		{
			const auto view = *world.observe(claim);
			OperatingCommand command{ship, "away." + action, mission().at("revision"), "repairs"};
			command.console = view.console;
			command.controller = controller;
			command.text = text;
			for (const auto &control : view.gameplay.at("controllers"))
				if (control.at("id") == controller)
					command.generation = control.at("generation");
			if (!world.submit(claim, command))
				throw std::runtime_error("Cruiser transport refused " + action + ": " + mission().dump());
			advance(1);
		};
		auto until = [&](const std::string &phase)
		{
			for (int i = 0; i < 3000 && mission().at("phase") != phase; ++i)
			{
				world.tick();
				if (i % 200 == 0)
				{
					std::ofstream("build/cruiser-transport-progress.json") << world.save().dump(2);
					std::cout << "Transport " << (shuttle ? "shuttle" : "displacement") << " tick " << world.ticks()
					          << " awaiting " << phase << ": " << mission().dump() << '\n'
					          << std::flush;
				}
			}
			if (mission().at("phase") != phase)
				throw std::runtime_error("Cruiser transport timed out awaiting " + phase + ": " + mission().dump());
		};
		until("available");
		order(901, "dispatch", "open");
		order(902, "repair-1", "assign", "repair-team/team:repair-1");
		for (const auto *shield : {"shield-fore", "shield-port", "shield-aft", "shield-starboard"})
		{
			const auto view = *world.observe(903);
			OperatingCommand command{ship, "system_enabled", 0, "", shield, "system_enabled"};
			command.console = "science";
			for (const auto &control : view.gameplay.at("controllers"))
				if (control.at("id") == shield)
					command.generation = control.at("generation");
			if (!world.submit(903, command))
				throw std::runtime_error("Shield lowering refused");
		}
		advance(1);
		std::string craft;
		const auto tactical = *world.observe(904);
		for (const auto &row : tactical.gameplay.at("craft"))
			if (row.at("role") == "shuttle")
			{
				craft = row.at("id");
				break;
			}
		std::ofstream("build/cruiser-transport-preparation.json") << Json{
		    {"tactical", tactical.gameplay},
		    {"science", world.observe(903)->gameplay},
		    {"selected_craft", craft}}.dump(2);
		order(shuttle ? 904 : 903, shuttle ? "utility-bay" : "displacement", "transport",
		      shuttle ? "craft:" + craft : "displacement");
		advance(160);
		order(901, "dispatch", "send");
		until("deployed");
		const auto deployed = mission();
		order(901, "dispatch", "recall");
		until("recovered");
		receipt.push_back({{"transport", shuttle ? "shuttle" : "displacement"},
		                   {"deployed", deployed},
		                   {"recovered", mission()},
		                   {"tick", world.ticks()},
		                   {"craft", world.observe(904)->gameplay.at("craft")}});
		std::ofstream("build/cruiser-transport.json") << receipt.dump(2);
	}
}
