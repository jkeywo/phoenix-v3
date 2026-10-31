// Synthetic equipment reports, local diagnosis and home return through World.
void repair_evidence_contract()
{
	for (const bool report : {false, true})
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["gun"]["compartment"] = "aft";
		ship["systems"]["gun"]["faults"] = Json::array({{{"id", "jam"},
		                                                 {"symptom", "Weapon feed unavailable"},
		                                                 {"diagnosis", "Feed jam"},
		                                                 {"effect", "disabled"},
		                                                 {"repair_work", 5}}});
		ship["systems"]["team-a"]["home_compartment"] = "fore";
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller["id"] == "team-a" || (report && controller["id"] == "gun"))
			{
				controller.erase("fallback");
				controller["policy"] = "combat";
				if (controller["id"] == "gun")
					controller["ai"]["requests"] = Json::array({{{"operation", "repair"},
					                                             {"console", "observer"},
					                                             {"controller", "team-a"},
					                                             {"priority", 2},
					                                             {"retry_ticks", 40}}});
			}
		for (auto &profile : ship["control_layout"]["profiles"])
			for (const auto &[console, id] :
			     std::vector<std::pair<std::string, std::string>>{{"observer", "team-a"}, {"drive", "gun"}})
				if (id == "team-a" || report)
				{
					auto &human = profile["consoles"][console]["human"];
					human.erase(std::remove(human.begin(), human.end(), id), human.end());
					profile["consoles"][console]["ai"].push_back(id);
				}
		for (auto &relationship : source["maps"][0]["relationships"])
			relationship["relation"] = "neutral";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		auto saved = world.save();
		saved["bodies"][0]["combat"]["faults"]["gun"][0]["active"] = true;
		world.restore(catalog, saved);
		advance(world, 70);
		const auto repaired = state(world);
		require(repaired["systems"]["team-a"]["location"] == 0,
		        "idle repair Controller returns its team to the authored home");
		require(repaired["faults"]["gun"][0]["active"].get<bool>() == !report,
		        "hidden remote faults require a delivered equipment repair request");
		if (!report)
			require(repaired["faults"]["gun"][0]["work"] == 0 && repaired["systems"]["team-a"]["destination"] == 0,
			        "automatic local work never searches remote physical fault state");
		World restored;
		restored.restore(catalog, world.save());
		advance(world, 8);
		advance(restored, 8);
		require(world.save() == restored.save(), "reported repair, inspection and home return resume identically");
	}
	auto invalid = fixture();
	invalid["entities"][0]["systems"]["team-a"]["home_compartment"] = "missing";
	rejected(
	    [&]
	    {
		    (void)load(invalid);
	    },
	    "unknown home compartment");
}
