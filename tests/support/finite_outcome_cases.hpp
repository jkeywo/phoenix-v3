// Synthetic direct native outcomes exercise production effect transactions.
Json finite_direct_outcomes()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	for (auto &controller : ship["control_layout"]["controllers"])
		for (auto &channel : controller["commands"])
			if (channel.value("action", std::string{}) == "system_enabled")
				channel["initial"] = 0;
	for (auto &room : ship["combat"]["decks"][0]["compartments"])
		room["initial_hp"] = 40;
	ship["systems"]["radar"]["faults"] = Json::array({{{"id", "broken"},
	                                                   {"effect", "disabled"},
	                                                   {"symptom", "Failure"},
	                                                   {"diagnosis", "Failed component"},
	                                                   {"chance_permille", 0},
	                                                   {"damage_threshold", 1},
	                                                   {"repair_work", 100000}}});
	ship["personnel"]["officers"][0]["condition"] = "minor";
	for (const auto *id : {"b", "c"})
	{
		auto patient = ship["personnel"]["officers"][0];
		patient["id"] = id;
		patient["name"] = id;
		patient["condition"] = "severe";
		ship["personnel"]["officers"].push_back(patient);
	}
	source["effects"].push_back(Json::parse(R"json({"id":"test.native.success","kind":"operations",
 "operations":[
 {"kind":"structural_repair","target":"owner","compartment":"fore","amount":1000},
 {"kind":"fault_repair","target":"owner","equipment":"radar","fault":"broken"},
 {"kind":"treat","target":"owner","officer":"a"},
 {"kind":"stabilise","target":"owner","officer":"b"},
 {"kind":"recover","target":"owner","officer":"b"},
 {"kind":"treat","target":"owner","equipment":"team-a"}]})json"));
	source["effects"].push_back(Json::parse(R"json({"id":"test.native.refused","kind":"operations",
 "costs":[{"payer":"owner","property":"test.fuel","amount":3}],
 "operations":[
 {"kind":"structural_repair","target":"owner","compartment":"aft","amount":50},
 {"kind":"property_add","target":"owner","property":"test.events","amount":7},
 {"kind":"recover","target":"owner","officer":"c"}]})json"));
	scenario(source, {{3, "test.native.success"}, {9, "test.native.refused"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	auto initial = world.save();
	for (auto &row : initial["bodies"])
		if (row["body"]["id"] == 1)
		{
			row["combat"]["faults"]["radar"][0]["active"] = true;
			row["combat"]["systems"]["team-a"]["condition"] = "injured";
			auto &deceased = row["personnel"]["officers"][2];
			deceased["condition"] = "dead";
			deceased["duty"] = "";
			deceased["present"] = false;
			deceased["edge_to"] = -1;
			deceased["remaining"] = 0;
			deceased["escort"] = "";
			deceased["facility"] = "";
			deceased["stabilised"] = false;
		}
	world.restore(catalog, initial);
	ticks(world, 6);
	const auto healed = body(world);
	require(healed.at("combat").at("compartments")[0].at("hp") == 100 &&
	            healed.at("combat").at("compartments")[1].at("hp") == 40 &&
	            healed.at("combat").at("faults").at("radar")[0].at("active") == false,
	        "Direct repair clamps structural HP and removes only the named native fault");
	require(healed.at("personnel").at("officers")[0].at("condition") == "healthy" &&
	            healed.at("personnel").at("officers")[1].at("condition") == "healthy" &&
	            healed.at("combat").at("systems").at("team-a").at("condition") == "healthy",
	        "Direct native outcomes treat officers and teams and chain stabilisation with recovery");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 8);
	ticks(resumed, 8);
	const auto after = body(world);
	require(after.at("combat").at("compartments")[1].at("hp") == 40 &&
	            after.at("personnel").at("officers")[2].at("condition") == "dead" &&
	            property(world, "test.events") == 0 && property(world, "test.fuel") == 100,
	        "An invalid clinical transition rolls back repair, properties and fees and never revives the dead");
	require(world.save() == resumed.save(), "Native outcomes and failed transactions resume without replay");
	return portable(world.save());
}
