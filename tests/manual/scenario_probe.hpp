#pragma once

// Explicit, opt-in authored-content smoke. Never runs as part of CTest.
int scenario_probe(const char *archive, const char *mission, int ticks, const char *save_path = nullptr)
{
	using namespace phoenix;
	const auto catalog = resolve_packs({read_pack(read_file(archive))});
	const auto &map = catalog.maps.at(mission);
	std::map<std::string, std::string> ships;
	for (const auto &slot : map.at("ship_slots"))
		ships.emplace(slot.at("id").get<std::string>(), slot.at("allowed_ships")[0].get<std::string>());
	World world;
	world.load(catalog, mission, ships);
	if (std::string(mission) == "test.courier-patrol")
	{
		World controls;
		controls.restore(catalog, world.save());
		const auto player = controls.inspect().back().id;
		if (!controls.claim(100, player, "helm") || !controls.claim(101, player, "engineering") ||
		    !controls.submit(100, {player, "preset", 2, {}}) || !controls.submit(101, {player, "preset", 2, {}}) ||
		    !controls.submit(100, {player, "speed_mm_s", 1000, "drive.engine"}) ||
		    !controls.submit(101, {player, "generator_output", 1000, "core.generator"}) ||
		    controls.submit(100, {player, "generator_output", 0, "core.generator"}) ||
		    controls.submit(101, {player, "speed_mm_s", 0, "drive.engine"}))
			throw std::runtime_error("patrol Helm/Power ownership smoke failed");
		controls.tick();
		std::cout << "{\"independent_helm_power_commands\":true}\n";
	}
	std::int64_t minimum_separation_squared = 1000000000000000000LL;
	std::int64_t minimum_terrain_squared = 1000000000000000000LL;
	for (int tick = 0; tick < ticks; ++tick)
	{
		world.tick();
		const auto bodies = world.inspect();
		for (const auto &a : bodies)
			if (a.id <= 2)
				for (const auto &b : bodies)
				{
					const auto dx = a.x - b.x, dy = a.y - b.y;
					const auto squared = dx * dx + dy * dy;
					if (a.id == 1 && b.id == 2)
						minimum_separation_squared = std::min(minimum_separation_squared, squared);
					if (b.id >= 10 && b.id <= 21)
						minimum_terrain_squared = std::min(minimum_terrain_squared, squared);
				}
		if (tick % 400 == 0 || tick + 1 == ticks)
		{
			Json row{{"tick", world.ticks()}, {"bodies", Json::array()}};
			for (const auto &body : world.inspect())
				if (body.id <= 2)
					row["bodies"].push_back({{"id", body.id},
					                         {"x", body.x},
					                         {"y", body.y},
					                         {"vx", body.vx},
					                         {"vy", body.vy},
					                         {"heading", body.heading}});
			const auto saved = world.save();
			if (saved.contains("scenario"))
				row["objectives"] = saved.at("scenario").at("runtime").at("objectives");
			row["minimum_separation_squared"] = minimum_separation_squared;
			row["minimum_terrain_squared"] = minimum_terrain_squared;
			std::cout << row.dump() << '\n';
		}
	}
	if (save_path)
	{
		auto session = make_host_world();
		session->add_pack(read_file(archive));
		const auto slot = map.at("ship_slots")[0].at("id").get<std::string>();
		const auto console = resolve_consoles(catalog, ships.at(slot)).front().id;
		session->start_server({mission, ships, slot, console});
		session->authoritative_world().restore(catalog, world.save());
		write_file(save_path, session->save());
	}
	return 0;
}
