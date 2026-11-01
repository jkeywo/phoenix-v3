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
		fit_scored_repair_policy(source, {"team-a"});
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
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		auto sealed = ship["combat"]["decks"][0]["compartments"][0];
		sealed["id"] = "sealed";
		sealed["x_mm"] = 20000;
		ship["combat"]["decks"][0]["compartments"].push_back(sealed);
		ship["systems"]["team-a"]["compartment"] = "aft";
		ship["systems"]["team-a"]["home_compartment"] = "aft";
		ship["systems"]["gun"]["compartment"] = "sealed";
		ship["systems"]["gun"]["faults"] = Json::array({{{"id", "hidden"},
		                                                 {"symptom", "Feed unavailable"},
		                                                 {"diagnosis", "Feed damaged"},
		                                                 {"effect", "disabled"},
		                                                 {"repair_work", 5}}});
		fit_scored_repair_policy(source, {"team-a"});
		ship["standing_directives"].push_back({{"id", "isolated"},
		                                       {"behavior", "repair"},
		                                       {"tier", "scripted_high"},
		                                       {"title_key", "Attend sealed room"},
		                                       {"compartment", "sealed"}});
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller["id"] == "team-b")
				controller["commands"][0]["initial"] = 0;
		auto &seat = ship["control_layout"]["profiles"][0]["consoles"]["observer"];
		auto &human = seat["human"];
		human.erase(std::remove(human.begin(), human.end(), "team-a"), human.end());
		seat["ai"] = {"team-a"};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		auto saved = world.save();
		saved["bodies"][0]["combat"]["compartments"][0]["hp"] = 50;
		saved["bodies"][0]["combat"]["compartments"][2]["hp"] = 0;
		saved["bodies"][0]["combat"]["compartments"][2]["disabled"] = true;
		world.restore(catalog, saved);
		World hidden;
		saved["bodies"][0]["combat"]["faults"]["gun"][0]["active"] = true;
		hidden.restore(catalog, saved);
		advance(world, 3);
		advance(hidden, 3);
		bool blocked = false, useful = false;
		Json decisions = Json::array(), hidden_decisions = Json::array();
		const auto observed = *world.observe(102), hidden_observed = *hidden.observe(102);
		for (const auto &controller : observed.gameplay.at("controllers"))
			if (controller.at("id") == "team-a")
			{
				decisions = controller.at("objective_candidates");
				for (const auto &candidate : decisions)
				{
					blocked |=
					    candidate.at("reason") == "known_route_blocked" && candidate.at("tier") == "scripted_high";
					useful |= candidate.at("selected").get<bool>() && candidate.at("target") == 1;
				}
			}
		for (const auto &controller : hidden_observed.gameplay.at("controllers"))
			if (controller.at("id") == "team-a")
				hidden_decisions = controller.at("objective_candidates");
		require(blocked && useful, "inaccessible higher-tier repair falls through to reachable observed damage");
		require(decisions == hidden_decisions,
		        "hidden remote faults cannot change repair eligibility, scoring or selection");
		World resumed;
		resumed.restore(catalog, world.save());
		advance(world, 100);
		advance(resumed, 100);
		require(world.save() == resumed.save(), "scored repair travel, local work and home return resume identically");
		const auto repaired = state(world);
		require(repaired["compartments"][0]["hp"] == 100 && repaired["compartments"][2]["hp"] == 0 &&
		            repaired["systems"]["team-a"]["location"] == 1,
		        "repair team restores accessible damage and returns home without searching an isolated compartment");
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
