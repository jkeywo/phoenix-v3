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
		bool observed_fore = false, observed_aft = false;
		Json latest_aft;
		const auto observed = *world.observe(102);
		for (const auto &record : observed.gameplay.at("records"))
			if (record.at("schema") == "phoenix.team.inspection" && record.at("fields").at("repair") == true)
			{
				const auto &fields = record.at("fields");
				observed_fore |= fields.at("compartment") == "fore";
				if (fields.at("compartment") == "aft")
				{
					observed_aft = true;
					latest_aft = record;
				}
			}
		require(observed_fore && observed_aft == report,
		        "local inspection reports cannot inspect an unvisited compartment");
		if (report)
			require(latest_aft.at("fields").at("fault_count") == 0 &&
			            Json::parse(latest_aft.at("fields").at("faults").get<std::string>()).empty(),
			        "a complete empty local snapshot supersedes the repaired faults after departure");
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
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["gun"]["compartment"] = "aft";
		ship["systems"]["gun"]["faults"] = Json::array({{{"id", "retained"},
		                                                 {"symptom", "Feed jam"},
		                                                 {"diagnosis", "Feed obstructed"},
		                                                 {"effect", "disabled"},
		                                                 {"repair_work", 1000000}}});
		ship["systems"]["team-b"]["compartment"] = "aft";
		fit_scored_repair_policy(source, {"team-a"});
		auto automated = ship["control_layout"]["profiles"][0];
		automated["id"] = "reported";
		auto &seat = automated["consoles"]["observer"];
		auto &human = seat["human"];
		human.erase(std::remove(human.begin(), human.end(), "team-a"), human.end());
		seat["ai"] = {"team-a"};
		ship["control_layout"]["profiles"].push_back(automated);
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		auto active = world.save();
		active["bodies"][0]["combat"]["faults"]["gun"][0]["active"] = true;
		world.restore(catalog, active);
		advance(world, 1);
		const auto instrument_console = *world.observe(101);
		require(instrument_console.gameplay.at("combat").at("faults").empty(),
		        "an equipment feed cannot reveal another Console's local inspection without a team feed or report");
		const auto repair_console = *world.observe(102);
		require(!repair_console.gameplay.at("combat").at("faults").empty(),
		        "the authored repair Console receives the team's full current local inspection");
		{
			auto shared_source = source;
			auto &feeds = shared_source["entities"][0]["stations"][0]["consoles"][0]["feeds"];
			feeds.push_back("team-b");
			feeds.erase(std::remove(feeds.begin(), feeds.end(), "gun"), feeds.end());
			const auto shared_catalog = load(shared_source);
			World shared;
			shared.load(shared_catalog, "test.map");
			claim(shared);
			auto shared_state = shared.save();
			shared_state["bodies"][0]["combat"]["faults"]["gun"][0]["active"] = true;
			shared.restore(shared_catalog, shared_state);
			advance(shared, 1);
			const auto received = *shared.observe(101);
			require(!received.gameplay.at("combat").at("faults").empty(),
			        "a declared team feed supplies every local fault even without a feed for the faulty equipment");
		}

		submit(world, 102, "team-b", "assignment", 1);
		advance(world, 12);
		require(state(world)["systems"]["team-b"]["location"] == 0,
		        "inspection scout physically leaves the reported compartment");
		const auto before = *world.observe(102);
		require(before.gameplay.at("combat").at("faults").empty() &&
		            !before.gameplay.at("combat").at("compartments")[1].at("damaged").get<bool>(),
		        "remote physical faults are absent after the local observer leaves");
		const auto checkpoint = world.save();
		World unreported;
		auto without = checkpoint;
		const auto schema = compile_gameplay_knowledge(catalog).knowledge.schema("phoenix.team.inspection");
		auto &records = without["knowledge"]["runtime"]["records"];
		records.erase(std::remove_if(records.begin(), records.end(),
		                             [&](const Json &record)
		                             {
			                             return record.at("value").at("schema") == schema &&
			                                    record.at("value").at("subject") == 1028;
		                             }),
		              records.end());
		unreported.restore(catalog, without);
		require(fixture_native_submit(world, 102, {1, "preset", 1}) &&
		            fixture_native_submit(unreported, 102, {1, "preset", 1}),
		        "delegate repair after inspection");
		advance(world, 2);
		advance(unreported, 2);
		require(state(world)["systems"]["team-a"]["destination"] == 1 &&
		            state(unreported)["systems"]["team-a"]["destination"] == 0,
		        "a retained permitted inspection supplies actionable repair work after its observer leaves");
		World resumed;
		resumed.restore(catalog, world.save());
		advance(world, 15);
		advance(resumed, 15);
		require(world.save() == resumed.save(), "retained repair evidence and physical travel resume identically");
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
