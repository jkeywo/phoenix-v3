// Included after the existing Medical fixture helpers.
namespace
{
std::int64_t finite_clinical_budget(const World &world, EntityId entity, const std::string &equipment = {})
{
	const auto saved = world.save();
	for (const auto &row : saved.at("custom_properties"))
		if (row.at("entity") == entity && row.at("equipment") == equipment && row.at("officer") == "" &&
		    row.at("compartment") == -1)
			return row.at("values").at("test.native-budget").get<std::int64_t>();
	throw std::runtime_error("Missing finite clinical budget");
}
void finite_structural_commitments()
{
	auto source = away_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["team"]["hp_per_second"] = 1;
	ship["logistics"] = Json::parse(R"({"controller":"team","materials":[{"id":"spares"}],
	 "stores":[{"id":"kit","system":"team","compartment":"workshop","capacity":1,"accepts":["spares"],"initial":{"spares":1}}],
	 "consumers":[{"system":"team","store":"kit","costs":{"structural":{"spares":1}}}]})");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	auto saved = world.save();
	auto &compartment = saved_body(saved, 2)["combat"]["compartments"][1];
	compartment["hp"] = 99;
	compartment["work"] = 19000;
	world.restore(catalog, saved);
	world.tick();
	saved = world.save();
	require(saved_body(saved, 2)["combat"]["compartments"][1]["hp"] == 100,
	        "Structural finite work completes using the target-owned native fraction");
	const auto carry = saved_body(saved, 2)["inventory"]["work_credit"]["team"].get<std::int64_t>();
	require(carry > 0, "Structural source retains purchased but unused native work");
	saved_body(saved, 2)["combat"]["compartments"][1]["hp"] = 99;
	saved_body(saved, 2)["combat"]["compartments"][1]["work"] = 19000;
	world.restore(catalog, saved);
	World resumed;
	resumed.restore(catalog, saved);
	world.tick();
	resumed.tick();
	const auto completed = world.save();
	auto copy = completed;
	require(saved_body(copy, 2)["combat"]["compartments"][1]["hp"] == 100 &&
	            saved_body(copy, 2)["inventory"]["work_credit"]["team"].get<std::int64_t>() < carry &&
	            world.save() == resumed.save(),
	        "A new repair episode consumes existing native prepaid work without another kit across recovery");
}

void finite_clinical_electrical_funding()
{
	for (const auto stop : {false, true})
	{
		auto source = medical_fixture();
		source["resources"] =
		    Json::parse(R"([{"id":"test.native-energy","label":"Energy","tags":["energy"],"sizes":{"energy":1}}])");
		source["properties"] = Json::parse(R"([
		 {"id":"test.native-budget","kind":"quantity","unit":"points","default":10000,"maximum":10000},
		 {"id":"test.native-starts","kind":"quantity","unit":"points","default":0,"maximum":100},
		 {"id":"test.native-resumes","kind":"quantity","unit":"points","default":0,"maximum":100}])");
		source["effects"].push_back(Json::parse(
		    R"({"id":"test.native.start","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.native-starts","amount":1}]})"));
		source["effects"].push_back(Json::parse(
		    R"({"id":"test.native.resume","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.native-resumes","amount":1}]})"));
		auto process =
		    Json::parse(R"({"id":"test.native.funded","kind":"process","native":"medical","shortage":"proportional",
		 "start_effect":"test.native.start","resume_effect":"test.native.resume",
		 "work_costs":[{"payer":"owner","store":"energy:clinical-cell","resource":"test.native-energy","per_work":2},
		 {"payer":"owner","property":"test.native-budget","per_work":2}]})");
		if (stop)
			process["shortage"] = "stop";
		source["effects"].push_back(process);
		auto &ship = source["entities"][0];
		auto clinical_cell = ship["resources"]["stores"][0];
		clinical_cell["id"] = "clinical-cell";
		clinical_cell["resource"] = "test.native-energy";
		clinical_cell["discharge_rate"] = 20;
		ship["resources"]["stores"].push_back(std::move(clinical_cell));
		ship["systems"]["medical"]["processes"] = {{"finite", {{"process", "test.native.funded"}}}};
		ship["systems"]["medical"]["properties"] = {
		    {"test.native-budget", 10000}, {"test.native-starts", 0}, {"test.native-resumes", 0}};
		ship["personnel"]["officers"][0]["compartment"] = "workshop";
		ship["personnel"]["officers"][0]["duty"] = "";
		ship["personnel"]["officers"][1]["condition"] = "healthy";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(world);
		require(world.submit(102, command(world, "order", "treat/workshop/nav")),
		        "Order finite clinical work using constrained electrical and quantity inputs");
		world.tick();
		if (stop)
		{
			require(officer(world, 2, 0)["treatment"] == 0 && finite_clinical_budget(world, 2, "medical") == 10000,
			        "Stop shortage performs no partial native treatment or quantity payment");
			ticks(world, 3);
			const auto saved = world.save();
			for (const auto &owner : saved.at("processes"))
				if (owner.at("entity") == 2 && owner.at("equipment") == "medical")
					for (const auto &[id, state] : owner.at("state").at("processes").items())
						if (state.at("parent_process") == "")
							require(!state.at("requested").get<bool>(),
							        "Stop shortage withdraws the native driver rather than automatically retrying");
		}
		else
		{
			require(officer(world, 2, 0)["treatment"] == 500 && finite_clinical_budget(world, 2, "medical") == 9999,
			        "Native remaining discharge limits proportional work and mixed quantity payment once");
			const auto saved = world.save();
			for (const auto &owner : saved.at("custom_properties"))
				if (owner.at("entity") == 2 && owner.at("equipment") == "medical")
					require(owner.at("values").at("test.native-starts") == 1 &&
					            owner.at("values").at("test.native-resumes") == 0,
					        "First native admission fires start once and does not fire resume");
			World resumed;
			resumed.restore(catalog, saved);
			ticks(world, 3);
			ticks(resumed, 3);
			require(world.save() == resumed.save(), "Mixed finite input credits continue identically across recovery");
			auto protected_source = source;
			protected_source["properties"].push_back(Json::parse(
			    R"({"id":"test.native-rate","kind":"attribute","unit":"permille","default":500,"maximum":1000})"));
			auto &protected_process = protected_source["effects"].back();
			protected_process["work_costs"][0]["per_work"] = 1;
			protected_process["rate"] = {{"fact", "owner.property"}, {"property", "test.native-rate"}};
			auto &protected_ship = protected_source["entities"][0];
			protected_ship["systems"]["medical"]["properties"]["test.native-rate"] = 500;
			protected_ship["resources"]["stores"][1]["stop_heat"] = 500;
			protected_ship["resources"]["stores"][1]["recover_heat"] = 100;
			const auto protected_catalog = load(protected_source);
			World protected_work;
			protected_work.load(protected_catalog, "test.map", {{"crew", "test.away"}});
			claim(protected_work);
			require(protected_work.submit(102, command(protected_work, "order", "treat/workshop/nav")),
			        "Order fractional prepaid electrical clinical work");
			protected_work.tick();
			require(officer(protected_work, 2, 0)["treatment"] == 500,
			        "Fractional clinical work purchases a bounded electrical credit");
			auto protected_saved = protected_work.save();
			auto &reserve = saved_body(protected_saved, 2)["resources"]["network"]["stores"][1];
			reserve["heat"] = 500;
			reserve["thermal_stop"] = true;
			protected_work.restore(protected_catalog, protected_saved);
			const auto protected_budget = finite_clinical_budget(protected_work, 2, "medical");
			protected_work.tick();
			require(
			    officer(protected_work, 2, 0)["treatment"] == 500 &&
			        finite_clinical_budget(protected_work, 2, "medical") == protected_budget,
			    "Prepaid electrical credits cannot bypass native thermal protection or charge another quantity input");
		}
	}
}

void finite_clinical_final_work()
{
	auto source = medical_fixture();
	source["properties"] = Json::parse(R"([
	 {"id":"test.native-budget","kind":"quantity","unit":"points","default":10000,"maximum":10000},
	 {"id":"test.native-rate","kind":"attribute","unit":"permille","default":2000,"maximum":10000}])");
	source["effects"].push_back(Json::parse(R"({"id":"test.native.fast","kind":"process","native":"medical",
	 "rate":{"fact":"owner.property","property":"test.native-rate"},
	 "work_costs":[{"payer":"owner","property":"test.native-budget","per_work":2}]})"));
	auto &ship = source["entities"][0];
	ship["systems"]["medical"]["processes"] = {{"finite", {{"process", "test.native.fast"}}}};
	ship["systems"]["medical"]["properties"] = {{"test.native-budget", 10000}, {"test.native-rate", 2000}};
	ship["personnel"]["officers"][0]["compartment"] = "workshop";
	ship["personnel"]["officers"][0]["duty"] = "";
	ship["personnel"]["officers"][1]["condition"] = "healthy";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	auto saved = world.save();
	saved_body(saved, 2)["personnel"]["officers"][0]["treatment"] = 7500;
	world.restore(catalog, saved);
	require(world.submit(102, command(world, "order", "treat/workshop/nav")),
	        "Finish a partially treated patient with a doubled process rate");
	world.tick();
	require(officer(world, 2, 0)["condition"] == "healthy" && finite_clinical_budget(world, 2, "medical") == 9999,
	        "Rate scaling charges only the final remaining clinical work");
	const auto completed = world.save();
	bool found = false;
	for (const auto &owner : completed.at("processes"))
		if (owner.at("entity") == 2 && owner.at("equipment") == "medical")
			for (const auto &[id, state] : owner.at("state").at("processes").items())
				if (state.at("parent_process") != "")
				{
					found = true;
					require(state.at("completed") == true && state.at("actual_work") == 500,
					        "A final accelerated clinical receipt reports actual remaining work without overshoot");
				}
	require(found, "A native finite clinical episode persists its completion receipt");
}

void finite_clinical_contributor_fraction()
{
	auto source = medical_fixture();
	source["properties"] = Json::array({{{"id", "test.clinical-work"},
	                                     {"kind", "quantity"},
	                                     {"unit", "work_milliunits"},
	                                     {"default", 0},
	                                     {"maximum", 100000}}});
	source["effects"].push_back(Json::parse(R"({"id":"test.finite.clinical-work","kind":"operations",
	 "operations":[{"kind":"property_add","target":"owner","property":"test.clinical-work","amount":0}]})"));
	for (const auto &[id, rate] : std::vector<std::pair<std::string, int>>{{"first", 1250}, {"replacement", 1000}})
		source["effects"].push_back({{"id", "test.finite." + id},
		                             {"kind", "process"},
		                             {"native", "medical"},
		                             {"rate", {{"value", rate}, {"unit", "permille"}}},
		                             {"work_effect", "test.finite.clinical-work"},
		                             {"work_values", {{"0", {{"amount", {{"fact", "event.work"}}}}}}}});
	auto &ship = source["entities"][0];
	ship["systems"]["medical"]["processes"] = {{"finite", {{"process", "test.finite.first"}}}};
	ship["systems"]["medical"]["properties"] = {{"test.clinical-work", 0}};
	ship["systems"]["medical-second"] = ship["systems"]["medical"];
	ship["systems"]["medical-second"]["processes"] = {{"finite", {{"process", "test.finite.replacement"}}}};
	ship["personnel"]["officers"][0]["compartment"] = "workshop";
	ship["personnel"]["officers"][0]["duty"] = "";
	ship["personnel"]["officers"][1]["condition"] = "healthy";
	ship["personnel"]["duties"].push_back({{"id", "medical-second-duty"},
	                                       {"label", "Replacement clinical team"},
	                                       {"team", "medical-second"},
	                                       {"department", "engineering"},
	                                       {"compartment", "workshop"}});
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] == "medical")
			controller["commands"][0]["consumers"].push_back("medical-second");
	ship["stations"][1]["consoles"][0]["feeds"].push_back("medical-second");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	const auto replacement_order = [&](const World &current, const std::string &action, const std::string &text)
	{
		auto order = command(current, action, text);
		order.system = "medical-second";
		const auto view = current.observe(102);
		for (const auto &team : view->gameplay.at("teams"))
			if (team.at("id") == "medical-second")
				order.value = team.at("revision");
		return order;
	};
	require(world.submit(102, replacement_order(world, "order", "survey/workshop")),
	        "Hold replacement team on its survey order");
	require(world.submit(102, command(world, "order", "treat/workshop/nav")), "Begin treatment with first contributor");
	world.tick();
	for (int i = 0; i < 12 && officer(world, 2, 0)["treatment"] != 7500; ++i)
		world.tick();
	require(officer(world, 2, 0)["treatment"] == 7500, "First contributor leaves a subunit of target-owned work");
	World resumed;
	resumed.restore(catalog, world.save());
	claim(resumed);
	for (auto *current : {&world, &resumed})
	{
		require(current->submit(102, command(*current, "order", "survey/workshop")),
		        "Hold first contributor on an explicit non-clinical order during replacement");
		require(current->submit(102, replacement_order(*current, "order", "treat/workshop/nav")),
		        "Assign replacement to the same native patient");
		current->tick();
	}
	require(officer(world, 2, 0)["condition"] == "healthy" && world.save() == resumed.save(),
	        "Replacement contributor commits the final 500 milliwork through exact continuation");
	std::int64_t emitted = -1;
	const auto completed = world.save();
	for (const auto &owner : completed.at("custom_properties"))
		if (owner.at("entity") == 2 && owner.at("equipment") == "medical-second")
			emitted = owner.at("values").at("test.clinical-work").get<std::int64_t>();
	bool replacement_receipt = false;
	for (const auto &owner : completed.at("processes"))
		if (owner.at("entity") == 2 && owner.at("equipment") == "medical-second")
			for (const auto &receipt : owner.at("state").at("work"))
				if (receipt.at("definition") == "test.finite.replacement" && receipt.at("work") == 500)
					replacement_receipt = true;
	require(replacement_receipt, "Replacement team owns the receipt for the patient's final 500 milliwork");
	if (emitted != 500)
		throw std::runtime_error("Native completion flushes the replacement contributor's fractional work into its "
		                         "authored work effect (emitted=" +
		                         std::to_string(emitted) + ")");
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "Native fractional completion does not replay after the target is healthy");
}

void finite_clinical_self_removal()
{
	for (const auto temporary : {false, true})
	{
		auto source = medical_fixture();
		source["properties"] = Json::parse(
		    R"([{"id":"test.native-budget","kind":"quantity","unit":"points","default":10000,"maximum":10000}])");
		source["effects"].push_back(Json::parse(R"({"id":"test.native.self","kind":"process","native":"medical",
		 "work_costs":[{"payer":"owner","property":"test.native-budget","per_work":1}],"work_effect":"test.native.self-remove"})"));
		source["effects"].push_back(Json::parse(
		    R"({"id":"test.native.self-remove","kind":"operations","operations":[{"kind":"remove","target":"owner","scope":"owner"}]})"));
		auto &ship = source["entities"][0];
		ship["systems"]["medical"]["processes"] = {
		    {"finite", {{"process", "test.native.self"}, {"requested", !temporary}}}};
		ship["systems"]["medical"]["properties"] = {{"test.native-budget", 10000}};
		ship["personnel"]["officers"][0]["compartment"] = "workshop";
		ship["personnel"]["officers"][0]["duty"] = "";
		ship["personnel"]["officers"][1]["condition"] = "healthy";
		if (temporary)
		{
			source["effects"].push_back(Json::parse(
			    R"({"id":"test.native.self-grant","kind":"process_grant","process":"test.native.self","duration_ticks":100})"));
			source["effects"].push_back(Json::parse(
			    R"({"id":"test.native.self-apply","kind":"operations","operations":[{"kind":"apply","target":"owner","equipment":"medical","effect":"test.native.self-grant","grant":"self"}]})"));
			source["effects"].push_back(Json::parse(
			    R"({"id":"test.native.self-rule","kind":"rule","trigger":"tick","phase":"periodic","period_ticks":1,
			 "when":{"op":"eq","args":[{"fact":"tick"},{"value":1,"unit":"ticks"}]},"effect":"test.native.self-apply"})"));
			ship["rules"].push_back({{"id", "self-grant"}, {"rule", "test.native.self-rule"}});
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(world);
		require(world.submit(102, command(world, "order", "treat/workshop/nav")),
		        "Order native work whose payload removes its process source");
		world.tick();
		for (int wait = 0; wait < 4 && officer(world, 2, 0)["treatment"] == 0; ++wait)
			world.tick();
		require(officer(world, 2, 0)["treatment"] == 1000 && finite_clinical_budget(world, 2, "medical") == 9999,
		        "Self-removing native work commits its physical outcome and payment once");
		const auto saved = world.save();
		std::size_t receipts = 0;
		for (const auto &owner : saved.at("processes"))
			if (owner.at("entity") == 2 && owner.at("equipment") == "medical")
			{
				for (const auto &receipt : owner.at("state").at("work"))
					if (receipt.at("definition") == "test.native.self")
					{
						++receipts;
						require(receipt.at("work") == 1000,
						        "A removed native process still publishes actual committed work");
					}
				for (const auto &[id, state] : owner.at("state").at("processes").items())
					if (state.at("parent_process") != "")
						require(state.at("admitted") == false && state.at("requested") == false &&
						            state.at("phase") == "off" && state.at("work_credits").empty() &&
						            state.at("idle_credits").empty(),
						        "Self-removal cannot be undone by post-payment credits or receipt publication");
			}
		require(receipts == 1, "Exactly one receipt survives native process self-removal");
		World resumed;
		resumed.restore(catalog, saved);
		ticks(world, 3);
		ticks(resumed, 3);
		require(world.save() == resumed.save() && officer(world, 2, 0)["treatment"] == 1000 &&
		            finite_clinical_budget(world, 2, "medical") == 9999,
		        "Stopped or removed native jobs restore without reviving the same demand or repeating work costs");
	}
}

void finite_clinical_driver_selection()
{
	auto source = medical_fixture();
	source["properties"] = Json::parse(R"([
	 {"id":"test.target-eligible","kind":"flag","default":false},
	 {"id":"test.driver-starts","kind":"quantity","unit":"count","default":0,"maximum":100},
	 {"id":"test.native-budget","kind":"quantity","unit":"points","default":20,"maximum":20}])");
	source["effects"].push_back(Json::parse(R"({"id":"test.selection.started","kind":"operations",
	 "operations":[{"kind":"property_add","target":"owner","property":"test.driver-starts","amount":1}]})"));
	source["effects"].push_back(Json::parse(R"({"id":"test.selection.enable-target","kind":"operations",
	 "operations":[{"kind":"property_set","target":"target","property":"test.target-eligible","value":true}]})"));
	for (const auto *id : {"first", "alternate"})
	{
		Json process{
		    {"id", std::string("test.selection.") + id},
		    {"kind", "process"},
		    {"native", "medical"},
		    {"entry_costs", Json::array({{{"payer", "owner"}, {"property", "test.native-budget"}, {"amount", 1}}})},
		    {"start_effect", "test.selection.started"}};
		if (std::string(id) == "first")
			process["requirements"] = {{"fact", "target.property"}, {"property", "test.target-eligible"}};
		else
			process["work_effect"] = "test.selection.enable-target";
		source["effects"].push_back(std::move(process));
	}
	auto &ship = source["entities"][0];
	ship["systems"]["medical"]["processes"] = {{"a-first", {{"process", "test.selection.first"}}},
	                                           {"z-alternate", {{"process", "test.selection.alternate"}}}};
	// The source is eligible but the actual patient initially is not. Reading
	// the unbound owner instead of the target would select the wrong driver.
	ship["systems"]["medical"]["properties"] = {
	    {"test.target-eligible", true}, {"test.driver-starts", 0}, {"test.native-budget", 20}};
	ship["systems"]["medical"]["treatment_ticks"] = 100;
	ship["personnel"]["officers"][0]["compartment"] = "workshop";
	ship["personnel"]["officers"][0]["duty"] = "";
	ship["personnel"]["officers"][0]["properties"] = {{"test.target-eligible", false}};
	ship["personnel"]["officers"][1]["condition"] = "healthy";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	require(world.submit(102, command(world, "order", "treat/workshop/nav")),
	        "Begin treatment with competing target-dependent process declarations");
	world.tick();
	const auto first = world.save();
	const auto values = [](const Json &saved, const std::string &officer_id = std::string{})
	{
		for (const auto &row : saved.at("custom_properties"))
			if (row.at("entity") == 2 && row.at("officer") == officer_id &&
			    row.at("equipment") == (officer_id.empty() ? "medical" : "") && row.at("compartment") == -1)
				return row.at("values");
		throw std::runtime_error("Missing driver-selection property owner");
	};
	const auto episode = [](const Json &saved)
	{
		for (const auto &owner : saved.at("processes"))
			if (owner.at("entity") == 2 && owner.at("equipment") == "medical")
				for (const auto &state : owner.at("state").at("processes"))
					if (state.at("parent_process") != "" && state.at("admitted") == true)
						return state;
		throw std::runtime_error("Missing admitted driver-selection episode");
	};
	const auto incumbent = episode(first);
	require(incumbent.at("definition") == "test.selection.alternate" && officer(world, 2, 0)["treatment"] == 1000 &&
	            values(first).at("test.driver-starts") == 1 && values(first).at("test.native-budget") == 19,
	        "An ineligible earlier declaration does not shadow an eligible alternate or charge entry costs");
	require(values(first, "nav").at("test.target-eligible") == true,
	        "The alternate's authored work effect makes the actual patient eligible for the earlier driver");
	World resumed;
	resumed.restore(catalog, first);
	ticks(world, 3);
	ticks(resumed, 3);
	const auto continued = world.save();
	const auto still_active = episode(continued);
	require(
	    still_active.at("definition") == "test.selection.alternate" &&
	        still_active.at("generation") == incumbent.at("generation") &&
	        still_active.at("run") == incumbent.at("run") && officer(world, 2, 0)["treatment"] == 4000 &&
	        values(continued).at("test.driver-starts") == 1 && values(continued).at("test.native-budget") == 19 &&
	        continued == resumed.save(),
	    "The admitted alternate retains its run when an earlier candidate becomes eligible, including across restore");
}

void finite_clinical_jobs()
{
	const auto run = [](const char *name, auto test)
	{
		try
		{
			test();
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error(std::string(name) + ": " + error.what());
		}
	};
	run("Native driver selection", finite_clinical_driver_selection);
	run("Native self-removal", finite_clinical_self_removal);
	run("Native contributor fraction", finite_clinical_contributor_fraction);
	run("Native structural commitments", finite_structural_commitments);
	run("Native electrical funding", finite_clinical_electrical_funding);
	run("Native final work", finite_clinical_final_work);
	const auto budget_definition =
	    Json::parse(R"({"id":"test.native-budget","kind":"quantity","unit":"points","default":10000,"maximum":10000})");
	{
		auto source = medical_fixture();
		source["properties"] = Json::array({budget_definition});
		source["effects"].push_back(Json::parse(R"({"id":"test.finite.recovery","kind":"process","native":"recovery",
		 "entry_costs":[{"payer":"owner","property":"test.native-budget","amount":3}],
		 "upkeep_costs":[{"payer":"owner","property":"test.native-budget","per_second":20}]})"));
		auto &ship = source["entities"][0];
		ship["properties"] = {{"test.native-budget", 10000}};
		ship["processes"]["finite-recovery"] = {{"process", "test.finite.recovery"}, {"native_id", "bay"}};
		ship["personnel"]["facilities"][0]["capacity"] = 2;
		ship["personnel"]["facilities"][0]["recovery_ticks"] = 30;
		for (auto &officer : ship["personnel"]["officers"])
		{
			officer["compartment"] = "workshop";
			officer["condition"] = "healthy";
			officer["duty"] = "";
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		auto saved = world.save();
		for (auto &patient : saved_body(saved, 2)["personnel"]["officers"])
		{
			patient["condition"] = "severe";
			patient["stabilised"] = true;
			patient["facility"] = "bay";
			patient["location"] = 1;
			patient["recovery"] = 0;
		}
		world.restore(catalog, saved);
		world.tick();
		require(officer(world, 2, 0)["recovery"] == 1000 && officer(world, 2, 1)["recovery"] == 1000,
		        "A finite facility retains simultaneous native patient work");
		const auto admitted_budget = finite_clinical_budget(world, 2);
		require(admitted_budget == 9992, "Two facility patients pay one entry and one upkeep unit each");
		world.tick();
		require(finite_clinical_budget(world, 2) == admitted_budget - 2,
		        "Alternating facility patients retain their own run without repeating entry fees");
		World resumed;
		resumed.restore(catalog, world.save());
		ticks(world, 3);
		ticks(resumed, 3);
		require(world.save() == resumed.save(), "Multiple patient process runs and fractional costs restore exactly");
		auto blocked = world.save();
		saved_body(blocked, 2)["combat"]["compartments"][1]["hp"] = 0;
		saved_body(blocked, 2)["combat"]["compartments"][1]["disabled"] = true;
		world.restore(catalog, blocked);
		const auto paused_budget = finite_clinical_budget(world, 2);
		const auto paused_progress = officer(world, 2, 0)["recovery"];
		ticks(world, 4);
		require(finite_clinical_budget(world, 2) == paused_budget &&
		            officer(world, 2, 0)["recovery"] == paused_progress,
		        "An unavailable facility pauses work and charges no idle job upkeep");
	}
	{
		auto source = medical_fixture();
		source["properties"] = Json::array({budget_definition});
		source["effects"].push_back(Json::parse(R"({"id":"test.finite.medical","kind":"process","native":"medical",
		 "upkeep_costs":[{"payer":"owner","property":"test.native-budget","per_second":20}],
		 "work_effect":"test.finite.rejected"})"));
		source["effects"].push_back(Json::parse(R"({"id":"test.finite.rejected","kind":"operations",
		 "operations":[{"kind":"property_subtract","target":"owner","property":"test.native-budget","amount":10001}]})"));
		auto &ship = source["entities"][0];
		ship["systems"]["medical"]["processes"] = {{"finite", {{"process", "test.finite.medical"}}}};
		ship["systems"]["medical"]["properties"] = {{"test.native-budget", 10000}};
		ship["personnel"]["officers"][0]["compartment"] = "workshop";
		ship["personnel"]["officers"][0]["duty"] = "";
		ship["personnel"]["officers"][1]["condition"] = "healthy";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(world);
		require(world.submit(102, command(world, "order", "treat/workshop/nav")),
		        "Order finite treatment with a rejecting work effect");
		ticks(world, 4);
		require(officer(world, 2, 0)["condition"] == "minor" && officer(world, 2, 0)["treatment"] == 0 &&
		            finite_clinical_budget(world, 2, "medical") == 10000,
		        "A rejecting work effect atomically rejects native clinical progress and all work/upkeep inputs");
		require(world.submit(102, command(world, "cancel", "")), "Cancel a blocked native job");
		ticks(world, 3);
		require(officer(world, 2, 0)["treatment"] == 0 && finite_clinical_budget(world, 2, "medical") == 10000,
		        "Cancelled native work neither progresses nor keeps charging upkeep");
	}
}
} // namespace
