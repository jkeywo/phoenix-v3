// Included after the craft integration's production-path helpers.
void autonomous_bay_cases()
{
	auto source = armed_craft_fixture();
	auto &carrier = source["entities"][0];
	auto &layout = carrier["control_layout"];
	for (auto &profile : layout["profiles"])
	{
		auto &human = profile["consoles"]["drive"]["human"];
		human.erase(std::remove(human.begin(), human.end(), Json("bay")), human.end());
		human.push_back("sortie-target");
		profile["consoles"]["drive"]["ai"].push_back("bay");
	}
	layout["controllers"].push_back({{"id", "sortie-target"},
	                                 {"console", "drive"},
	                                 {"fallback", "hold"},
	                                 {"commands", Json::array({{{"id", "target"},
	                                                            {"action", "intent_integer"},
	                                                            {"initial", 1},
	                                                            {"consumers", Json::array()},
	                                                            {"label_key", "An unrelated display label"}}})}});
	carrier["personnel"]["duties"].push_back({{"id", "sortie-target"},
	                                          {"label", "Sortie intent"},
	                                          {"controller", "sortie-target"},
	                                          {"console", "drive"},
	                                          {"department", "navigation"},
	                                          {"compartment", "bridge"},
	                                          {"effects", Json::object()}});
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.bay.policy")
			tree["command_reads"] = Json::array({{{"name", "bay.accepted_target"},
			                                      {"controller", "sortie-target"},
			                                      {"channel", "target"},
			                                      {"type", "integer"}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	require(world.claim(101, 2, "drive"), "Claim autonomous carrier intent Console");
	bool defended = false, bombed = false;
	for (int tick = 0; tick < 20; ++tick)
	{
		world.tick();
		const auto observed = *world.observe(101);
		for (const auto &craft : observed.gameplay.at("craft"))
		{
			defended |=
			    craft.at("role") == "interceptor" && craft.at("order") == "defend" && craft.at("phase") != "docked";
			bombed |= craft.at("role") == "bomber" && craft.at("order") == "bomb" && craft.at("phase") != "docked";
		}
	}
	require(defended && bombed,
	        "Bay Controller launches defensive and explicitly bound attack sorties through ordinary admission");
	World resumed;
	resumed.restore(catalog, world.save());
	require(resumed.observe(101).has_value() || resumed.claim(101, 2, "drive"), "Resume carrier intent Console");
	ticks(world, 20);
	ticks(resumed, 20);
	require(world.save() == resumed.save(),
	        "Scored craft dispatch and physical sorties retain checkpoint continuation");

	const auto selection = [](const World &copy)
	{
		const auto observed = *copy.observe(101);
		for (const auto &controller : observed.gameplay.at("controllers"))
			if (controller.at("id") == "bay")
				return Json{{"action", controller.at("action")}, {"candidates", controller.at("objective_candidates")}};
		throw std::runtime_error("Missing bay Controller diagnostics");
	};
	auto hidden = source, changed = source;
	hidden["maps"][0]["entities"][0]["x_mm"] = 800000;
	changed["maps"][0]["entities"][0]["x_mm"] = 1000000;
	World first, second;
	first.load(load(hidden), "test.map", {{"crew", "test.away"}});
	second.load(load(changed), "test.map", {{"crew", "test.away"}});
	require(first.claim(101, 2, "drive") && second.claim(101, 2, "drive"), "Claim hidden-contact isolation carriers");
	ticks(first, 5);
	ticks(second, 5);
	require(selection(first) == selection(second), "Undisclosed enemy position cannot change bay dispatch selection");
}
