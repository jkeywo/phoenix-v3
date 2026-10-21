
int officer_journey(int argc, char **argv)
{
	const bool snapshots = std::string(argv[1]) == "--officer-snapshots";
	if (argc < (snapshots ? 3 : 7))
		throw std::runtime_error("officer journey needs pack, token, service, seconds, helm|engineering|six|tree");
	const auto catalog = resolve_packs({read_pack(read_file(argv[2]))});
	World world;
	world.load(catalog, "base.courier-race", {{"racer-a", "base.race-courier"}, {"racer-b", "base.race-courier"}});
	const auto ship = *world.ship_for_slot("racer-a");
	const bool solo = !snapshots && std::string(argv[6]).ends_with("-solo");
	if (solo)
	{
		const auto other = *world.ship_for_slot("racer-b");
		if (!world.prepare_console_profile({other, "helm"}, "detailed") || !world.claim(901, other, "helm"))
			throw std::runtime_error("Cannot hold the comparison ship's Helm for the solo journey");
	}
	world.tick();
	std::string mode = snapshots ? "six" : argv[6];
	if (solo)
		mode.erase(mode.size() - 5);
	std::vector<ControllerAddress> addresses;
	for (const auto &row : world.controller_ai(ship))
	{
		const auto id = row.at("controller").get<std::string>();
		const auto console = row.at("console").get<std::string>();
		const bool helm = id == "drive.engine" || id == "drive.steering";
		const bool engineering = id == "core.generator" || id == "core.distribution" || id == "core.reactor-cooling" ||
		                         id == "drive.engine-supply";
		if ((mode == "helm"          ? helm
		     : mode == "engineering" ? engineering
		                             : helm || engineering) &&
		    row.at("eligible").get<bool>())
		{
			addresses.push_back({ship, console, id});
			if (mode != "tree")
				world.configure_controller_provider(addresses.back(), "anyjev");
		}
	}
	Json initial = Json::array();
	for (const auto &address : addresses)
		initial.push_back(world.controller_decision_snapshot(address));
	if (snapshots)
	{
		std::cout << initial.dump() << '\n';
		return 0;
	}
	InferenceHost host;
	if (mode != "tree")
		host.connection = std::make_shared<InferenceConnection>(argv[3], argv[4]);
	const auto start = std::chrono::steady_clock::now();
	const auto end = start + std::chrono::seconds(std::stoi(argv[5]));
	auto next_tick = start;
	Json events = Json::array(), samples = Json::array();
	while (std::chrono::steady_clock::now() < end)
	{
		host.poll(world, ship,
		          [&](const Json &frame)
		          {
			          const auto &d = frame.at("decision"), &s = d.at("snapshot");
			          const bool accepted = world.submit_controller_decision(d);
			          events.push_back({{"wall_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
			                                            std::chrono::steady_clock::now() - start)
			                                            .count()},
			                            {"tick", world.ticks()},
			                            {"requested_tick", s.at("tick")},
			                            {"controller", s.at("controller")},
			                            {"candidate", d.value("candidate", std::string{})},
			                            {"failed", d.value("failed", false)},
			                            {"accepted", accepted}});
			          return accepted;
		          });
		if (std::chrono::steady_clock::now() >= next_tick)
		{
			world.tick();
			next_tick += std::chrono::milliseconds(50);
			if (world.ticks() % 20 == 0)
			{
				for (const auto &body : world.inspect())
					if (body.id == ship)
						samples.push_back(
						    {{"tick", world.ticks()}, {"x", body.x}, {"y", body.y}, {"vx", body.vx}, {"vy", body.vy}});
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	Json final_snapshots = Json::array();
	for (const auto &address : addresses)
		final_snapshots.push_back(world.controller_decision_snapshot(address));
	Json state;
	const auto saved = world.save();
	for (const auto &body : saved.at("bodies"))
		if (body.at("body").at("id") == ship)
			state = body;
	std::cout << Json{{"mode", mode},     {"snapshots", final_snapshots}, {"controllers", world.controller_ai(ship)},
	                  {"events", events}, {"samples", samples},           {"board", world.public_board(ship)},
	                  {"body", state}}
	                 .dump()
	          << '\n';
	return 0;
}
