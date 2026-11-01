#pragma once
// Explicitly declared navigation feeds must be the only source of craft target updates.
void craft_guidance_evidence_cases()
{
	for (const bool share_observer : {false, true})
	{
		auto source = armed_craft_fixture();
		for (auto &definition : source["entities"])
			if (definition.at("id") == "test.interceptor")
			{
				auto &flight = definition["control_layout"]["controllers"][0];
				Json radar_command;
				for (const auto &command : flight["commands"])
					if (command.at("action") == "radar_enabled")
						radar_command = command;
				std::erase_if(flight["commands"].get_ref<Json::array_t &>(),
				              [](const auto &command)
				              {
					              return command.at("action") == "radar_enabled";
				              });
				require(!radar_command.is_null(), "scoped fixture owns an explicit sensor command");
				definition["control_layout"]["controllers"].push_back({{"id", "observer-sensor"},
				                                                       {"console", "observer"},
				                                                       {"fallback", "hold"},
				                                                       {"commands", Json::array({radar_command})}});
				definition["control_layout"]["profiles"][0]["consoles"]["observer"] = {{"ai", {"observer-sensor"}}};
				auto &consoles = definition["stations"][0]["consoles"];
				std::erase(consoles[0]["feeds"].get_ref<phoenix::Json::array_t &>(), phoenix::Json("radar"));
				consoles[1]["feeds"] = {"radar"};
				std::swap(consoles[0], consoles[1]);
				if (share_observer)
					definition["craft"]["guidance_console"] = "observer";
			}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto home = *world.ship_for_slot("crew");
		require(world.claim(101, home, "drive"), "claim explicit guidance fixture carrier");
		ticks(world, 4);
		std::string craft;
		const auto fleet = world.observe(101);
		for (const auto &row : fleet->gameplay.at("craft"))
			if (row.at("role") == "interceptor")
				craft = row.at("id");
		require(!craft.empty() && world.submit(101, command(world, "craft", "escort", craft, "1")),
		        "bind the selected craft to a currently observed escort rendezvous");
		ticks(world, 6);
		const auto checkpoint = world.save();
		auto moved = checkpoint;
		for (auto &row : moved["bodies"])
			if (row["body"]["id"] == 1)
				row["body"]["position"]["x"] = row["body"]["position"]["x"].get<std::int64_t>() + 50000;
		World hidden;
		hidden.restore(catalog, moved);
		world.tick();
		hidden.tick();
		const auto bound = [&](const World &w)
		{
			const auto saved = w.save();
			for (const auto &row : saved.at("craft").at("entities"))
				if (std::to_string(row.at("id").get<std::uint64_t>()) == craft)
					return row.at("state").at("target_position");
			throw std::runtime_error("missing scoped guidance craft");
		};
		if ((bound(world) == bound(hidden)) != !share_observer)
			std::cerr << "guidance shared=" << share_observer << " baseline=" << bound(world)
			          << " moved=" << bound(hidden) << "\n";
		require((bound(world) == bound(hidden)) == !share_observer,
		        "hidden motion changes guidance only through the declared Console feed, irrespective of Console order");
		World resumed;
		resumed.restore(catalog, hidden.save());
		for (int i = 0; i < 5; ++i)
		{
			hidden.tick();
			resumed.tick();
		}
		require(hidden.save() == resumed.save(), "scoped accepted craft guidance resumes exactly");
	}
}
