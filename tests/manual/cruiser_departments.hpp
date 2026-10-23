// Manual exercise operator: send eligible patients to the production clinical orders.
void cruiser_combat_medical(phoenix::World &world, phoenix::EntityId ship, const phoenix::Catalog &catalog,
                            phoenix::Json &receipt)
{
	using namespace phoenix;
	if (!world.claim(905, ship, "science"))
		return;
	const auto view = *world.observe(905);
	std::vector<std::string> assigned;
	bool issued = false;
	for (const auto &team : view.gameplay.at("teams"))
	{
		if (!team.at("id").get<std::string>().starts_with("medical-") || team.at("order") != "")
			continue;
		Json selected;
		int priority = 0;
		for (const auto &patient : team.at("patients"))
		{
			const auto id = patient.at("id").get<std::string>();
			if (patient.at("facility") != "" || std::find(assigned.begin(), assigned.end(), id) != assigned.end())
				continue;
			const int rank = patient.at("condition") == "severe" ? (patient.at("stabilised") == true ? 1 : 3)
			                 : patient.at("condition") == "minor" || patient.at("condition") == "injured" ? 2
			                                                                                              : 0;
			if (rank > priority)
			{
				selected = patient;
				priority = rank;
			}
		}
		if (!priority)
			continue;
		const auto compartments = compile_personnel(catalog, "base.cruiser").compartments;
		const auto id = team.at("id").get<std::string>();
		const std::string action =
		    selected.at("condition") == "severe" && selected.at("stabilised") == true ? "recover" : "treat";
		OperatingCommand order{ship, "team.order", team.at("revision"), id};
		order.console = "science";
		order.controller = id;
		order.text = action + "/" + compartments.at(selected.at("location").get<std::size_t>()) + "/" +
		             selected.at("id").get<std::string>();
		for (const auto &control : view.gameplay.at("controllers"))
			if (control.at("id") == id)
				order.generation = control.at("generation");
		const bool accepted = world.submit(905, order);
		receipt["medical_orders"].push_back(
		    {{"tick", world.ticks()}, {"team", id}, {"order", order.text}, {"accepted", accepted}});
		if (accepted)
		{
			assigned.push_back(selected.at("id"));
			issued = true;
		}
	}
	if (issued)
		world.tick();
	world.release(905);
}

// Explicit production-fit clinical exercise; injuries are seeded only in this private copy.
void cruiser_medical(phoenix::Catalog catalog)
{
	using namespace phoenix;
	auto &crew = catalog.entities.at("base.cruiser")["personnel"]["officers"];
	crew[1]["condition"] = "minor";
	crew[6]["condition"] = "severe";
	World world;
	world.load(catalog, "base.free-flight", {{"base.player", "base.cruiser"}});
	const auto ship = *world.ship_for_slot("base.player");
	if (!world.claim(903, ship, "science"))
		throw std::runtime_error("Clinical Science claim refused");
	auto state = [&](const std::string &officer)
	{
		const auto saved = world.save();
		const auto &names = catalog.entities.at("base.cruiser")["personnel"]["officers"];
		for (const auto &body : saved["bodies"])
			if (body["body"]["id"] == ship)
				for (std::size_t i = 0; i < names.size(); ++i)
					if (names[i]["id"] == officer)
						return body["personnel"]["officers"][i];
		throw std::runtime_error("Clinical officer missing");
	};
	auto order = [&](const std::string &team, const std::string &text)
	{
		const auto view = *world.observe(903);
		OperatingCommand command{ship, "team.order", 0, team};
		command.console = "science";
		command.controller = team;
		command.text = text;
		for (const auto &row : view.gameplay.at("teams"))
			if (row["id"] == team)
				command.value = row.at("revision");
		for (const auto &control : view.gameplay.at("controllers"))
			if (control["id"] == team)
				command.generation = control.at("generation");
		if (!world.submit(903, command))
			throw std::runtime_error("Clinical order refused: " + text);
		world.tick();
	};
	order("medical-1", "treat/engines/officer-2");
	order("medical-2", "treat/sensors/officer-7");
	for (int i = 0; i < 100; ++i)
		world.tick();
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 40; ++i)
	{
		world.tick();
		resumed.tick();
	}
	if (world.save() != resumed.save())
		throw std::runtime_error("Cruiser clinical continuation differs");
	for (int i = 0;
	     i < 1200 && (state("officer-2")["condition"] != "healthy" || state("officer-7")["stabilised"] != true); ++i)
		world.tick();
	if (state("officer-2")["condition"] != "healthy" || state("officer-7")["stabilised"] != true)
		throw std::runtime_error("Cruiser field treatment did not complete");
	order("medical-2", "recover/sensors/officer-7");
	for (int i = 0; i < 2500 && state("officer-7")["condition"] != "healthy"; ++i)
		world.tick();
	if (state("officer-7")["condition"] != "healthy")
		throw std::runtime_error("Cruiser infirmary recovery did not complete");
	auto saved = world.save();
	for (auto &body : saved["bodies"])
		if (body["body"]["id"] == ship)
			body["combat"]["systems"]["security-1"]["condition"] = "injured";
	world.restore(catalog, saved);
	order("medical-1", "treat/hangar/team:security-1");
	bool healthy = false;
	for (int i = 0; i < 1500 && !healthy; ++i)
	{
		world.tick();
		const auto current = world.save();
		for (const auto &body : current.at("bodies"))
			if (body["body"]["id"] == ship)
				healthy = body["combat"]["systems"]["security-1"]["condition"] == "healthy";
	}
	if (!healthy)
		throw std::runtime_error("Cruiser Medical did not heal the injured Security team");
	std::ofstream("build/cruiser-medical.json") << Json{
	    {"continuation_equal", true},
	    {"minor", state("officer-2")},
	    {"severe", state("officer-7")},
	    {"security_healed", healthy},
	    {"tick", world.ticks()}}.dump(2);
}

// Existing boarding exercise, with a bounded private encounter variant for UI/contract evidence.
void cruiser_boarding(phoenix::Catalog catalog)
{
	using namespace phoenix;
	for (auto &encounter : catalog.entities.at("base.boarding-target")["boarding_missions"][0]["encounters"])
	{
		encounter["success_permille"] = 1000;
		encounter["defender_penalty_permille"] = 0;
		encounter["loss_permille"] = encounter["injury_permille"] = 0;
	}
	Json receipt = Json::array();
	for (const std::string objective : {"reactor", "bridge"})
	{
		World world;
		world.load(catalog, "base.boarding-action", {{"courier", "base.cruiser"}});
		const auto ship = *world.ship_for_slot("courier");
		for (const auto &[claim, console] : std::vector<std::pair<int, std::string>>{
		         {901, "comms"}, {902, "engineering"}, {903, "science"}, {904, "tactical"}})
			if (!world.claim(claim, ship, console))
				throw std::runtime_error("Cruiser boarding claim refused");
		for (const auto claim : {902, 904})
			if (!world.submit(claim, {ship, "preset", 2, {}}))
				throw std::runtime_error("Boarding detailed workload refused");
		world.tick();
		for (const auto &definition : catalog.entities.at("base.cruiser").at("control_layout").at("controllers"))
			for (const auto &channel : definition.at("commands"))
				if (channel.at("action") == "system_enabled" &&
				    std::any_of(channel.at("consumers").begin(), channel.at("consumers").end(),
				                [&](const Json &id)
				                {
					                return catalog.entities.at("base.cruiser")
					                           .at("systems")
					                           .at(id.get<std::string>())
					                           .value("kind", "") == "weapon";
				                }))
				{
					const auto id = definition.at("id").get<std::string>();
					OperatingCommand off{ship, "system_enabled", 0, "", id, channel.at("id")};
					off.console = "tactical";
					const auto view = *world.observe(904);
					for (const auto &control : view.gameplay.at("controllers"))
						if (control["id"] == id)
							off.generation = control.at("generation");
					if (!world.submit(904, off))
						throw std::runtime_error("Boarding exercise weapon isolation refused: " + id);
				}
		auto mission = [&]() -> Json
		{
			const auto view = *world.observe(901);
			for (const auto &m : view.gameplay.at("away_missions"))
				if (m.at("id").get<std::string>().starts_with("board."))
					return m;
			throw std::runtime_error("Cruiser did not discover the target's boarding operation");
		};
		for (int i = 0; i < 40; ++i)
			world.tick();
		auto command =
		    [&](int claim, const std::string &controller, const std::string &action, const std::string &text = "")
		{
			const auto view = *world.observe(claim);
			const auto m = mission();
			OperatingCommand c{ship, "away." + action, m.at("revision"), m.at("id")};
			c.console = view.console;
			c.controller = controller;
			c.text = text;
			for (const auto &control : view.gameplay.at("controllers"))
				if (control["id"] == controller)
					c.generation = control.at("generation");
			return c;
		};
		auto order =
		    [&](int claim, const std::string &controller, const std::string &action, const std::string &text = "")
		{
			if (!world.submit(claim, command(claim, controller, action, text)))
				throw std::runtime_error("Cruiser boarding refused " + action + ": " + mission().dump());
			world.tick();
		};
		order(904, "dispatch", "open");
		order(904, "security-1", "assign", "security/team:security-1");
		order(903, "medical-1", "assign", "medical/team:medical-1");
		std::string craft;
		const auto tactical = *world.observe(904);
		for (const auto &row : tactical.gameplay.at("craft"))
			if (row["role"] == "shuttle")
			{
				craft = row.at("id");
				break;
			}
		order(902, "utility-bay", "transport", "craft:" + craft);
		for (int i = 0; i < 200; ++i)
			world.tick();
		if (world.can_submit(904, command(904, "dispatch", "send")))
			throw std::runtime_error("Cruiser shuttle bypassed boarding hull gate");
		auto damaged = world.save();
		for (auto &body : damaged["bodies"])
			if (body["body"]["id"] == 1)
				for (auto &room : body["combat"]["compartments"])
					room["hp"] = room.at("hp").get<std::int64_t>() * 49 / 100;
		world.restore(catalog, damaged);
		order(904, "dispatch", "send");
		for (int i = 0; i < 3000 && mission()["phase"] != "deployed"; i += 10)
			for (int j = 0; j < 10; ++j)
				world.tick();
		if (mission()["phase"] != "deployed")
			throw std::runtime_error("Cruiser shuttle boarding delivery timed out: " + mission().dump());
		bool answered = false;
		for (int i = 0; i < 500 && !answered; ++i)
		{
			const auto view = *world.observe(901);
			for (const auto &thread : view.gameplay.at("conversations").at("threads"))
				for (const auto &choice : thread.at("choices"))
					if (choice.at("id") == objective)
					{
						OperatingCommand c{ship, "comms.answer", thread.at("id")};
						c.console = "comms";
						c.controller = "external-comms";
						c.text = objective;
						c.request_id = thread.at("prompt");
						for (const auto &control : view.gameplay.at("controllers"))
							if (control["id"] == c.controller)
								c.generation = control.at("generation");
						answered = world.submit(901, c);
					}
			world.tick();
		}
		if (!answered)
			throw std::runtime_error("Cruiser Comms boarding objective was not answered");
		World resumed;
		resumed.restore(catalog, world.save());
		for (int i = 0; i < 40; ++i)
		{
			world.tick();
			resumed.tick();
		}
		if (world.save() != resumed.save())
			throw std::runtime_error("Cruiser boarding continuation differs");
		const auto mission_id = mission().at("id");
		auto outcome = [&]() -> std::string
		{
			const auto saved = world.save();
			for (const auto &m : saved.at("away_missions"))
				if (m.at("id") == mission_id)
					return m.at("outcome");
			throw std::runtime_error("Boarding continuation missing");
		};
		for (int i = 0; i < 2000 && outcome() == ""; i += 10)
		{
			if (i % 200 == 0)
			{
				std::ofstream("build/cruiser-boarding-progress.json") << world.save().dump(2);
				std::cout << "Boarding " << objective << " tick " << world.ticks() << ": " << mission().dump() << '\n'
				          << std::flush;
			}
			for (int j = 0; j < 10; ++j)
				world.tick();
		}
		if (!outcome().starts_with("complete:"))
			throw std::runtime_error("Cruiser boarding did not complete: " + mission().dump());
		Json record{{"objective", objective},
		            {"mission", mission()},
		            {"outcome", outcome()},
		            {"continuation_equal", true},
		            {"tick", world.ticks()}};
		if (objective == "reactor")
		{
			order(904, "dispatch", "recall");
			for (int i = 0; i < 3000 && mission()["phase"] != "recovered"; i += 10)
				for (int j = 0; j < 10; ++j)
					world.tick();
			if (mission()["phase"] != "recovered")
				throw std::runtime_error("Cruiser boarding extraction timed out");
			record["recovered"] = mission();
		}
		else
		{
			const auto fleet = world.operational_fleet(ship);
			if (std::find(fleet.begin(), fleet.end(), 1) == fleet.end())
				throw std::runtime_error("Cruiser did not gain the prize");
			record["prize_fleet"] = fleet;
		}
		receipt.push_back(record);
		std::ofstream("build/cruiser-boarding.json") << receipt.dump(2);
	}
}
