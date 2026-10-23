#pragma once

// Selection contracts cross authored content, World admission and continuation.
void stock_selection_cases()
{
	const std::string resource_a = "test." + std::string(123, 'a');
	auto source = combat_fixture();
	auto &ship = source["entities"][0];
	source["resources"] = Json::array();
	for (const auto &id : {resource_a, std::string("test.round-b")})
		source["resources"].push_back({{"id", id},
		                               {"label", id},
		                               {"tags", {"ammo", "cargo", "test-ammo"}},
		                               {"sizes", {{"ammo", 1}, {"cargo", 1}}},
		                               {"ammunition", {{"projectile", ship["systems"]["gun"]["projectile"]}}}});
	const Json stock = {{"capacity_tag", "ammo"},  {"capacity", 2},           {"filter", {{"tags", {"test-ammo"}}}},
	                    {"single_resource", true}, {"selection", resource_a}, {"initial", Json::object()}};
	ship["systems"]["mag"] = {{"kind", "magazine"}, {"compartment", "fore"}, {"stock", stock}};
	ship["systems"]["gun"]["load_ticks"] = 2;
	ship["systems"]["second"]["enabled"] = false;
	ship["logistics"] = {
	    {"controller", "team-a"},
	    {"units_per_second", 20},
	    {"concurrency", 1},
	    {"batch_size", 1},
	    {"stores",
	     Json::array(
	         {{{"id", "hold"}, {"compartment", "aft"}, {"controller", "team-a"}, {"main", true}, {"stock", stock}}})}};
	for (const bool recipe : {false, true})
	{
		auto invalid = source;
		const std::string overlong(129, 'x');
		if (recipe)
			invalid["recipes"] = Json::array({{{"id", overlong},
			                                   {"label", "Too long"},
			                                   {"inputs", Json::object()},
			                                   {"outputs", {{resource_a, 1}}},
			                                   {"work", 1}}});
		else
		{
			auto resource = invalid["resources"][0];
			resource["id"] = overlong;
			resource["label"] = "Too long";
			invalid["resources"].push_back(resource);
		}
		bool rejected = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		require(rejected,
		        "Loader rejects resource and recipe identities beyond the shared128-character authored limit");
	}
	const auto catalog = load(source);
	const auto setup = [&](World &world)
	{
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "Stock selection seats claimed");
	};
	const auto selection = [&](const World &world, bool magazine, const std::string &choice)
	{
		const auto who = magazine ? 101 : 102;
		const auto view = *world.observe(who);
		const auto &board = view.gameplay.at("logistics");
		const auto revision =
		    magazine ? view.gameplay.at("combat").at("magazines")[0].at("stock_revision") : board.at("revision");
		OperatingCommand command{1, magazine ? "magazine.select" : "store.select", revision.get<std::int64_t>(),
		                         magazine ? "mag" : "hold"};
		command.controller = magazine ? "mag" : "team-a";
		command.generation = controller(world, who, command.controller).at("generation");
		command.text = choice;
		return command;
	};
	World world;
	setup(world);
	const auto magazine = selection(world, true, "test.round-b");
	auto foreign = magazine;
	foreign.controller = "gun";
	foreign.generation = controller(world, 101, "gun").at("generation");
	require(!world.submit(101, foreign) && !world.submit(102, magazine),
	        "Resource selection rejects foreign Controller and Console");
	auto stale_generation = magazine;
	++stale_generation.generation;
	require(!world.submit(101, stale_generation), "Resource selection rejects stale Controller generations");
	const auto observation = *world.observe(101);
	require(eligible_console_command(observation, Json::object(), Json::object(), magazine).has_value(),
	        "Projected magazine choice resolves through console control boundary");
	require(!eligible_console_command(observation, Json::object(), Json::object(), foreign),
	        "Console resolver rejects a Controller that does not consume the magazine");
	require(world.submit(101, magazine), "Empty uncommitted magazine accepts explicit selection");
	world.tick();
	require(!world.submit(101, magazine), "Consumed stock revision cannot replay a selection");
	require(world.save().at("bodies")[0].at("stocks").at("system:mag").at("selection") == "test.round-b",
	        "Empty magazine retains explicit selection");
	const auto projected = network::read_observation(network::observation_json(*world.observe(102)), catalog);
	require(projected.gameplay.at("logistics").at("stores")[0].at("quantities").contains(resource_a) &&
	            projected.gameplay.at("logistics").at("stores")[0].at("selected_resource") == resource_a,
	        "Maximum-length authored resource IDs survive scoped inventory network projection");
	auto cargo_board = projected.gameplay.at("logistics");
	cargo_board["requests"] = Json::array({{{"id", 1},
	                                        {"source", 1},
	                                        {"destination", 2},
	                                        {"from", 0},
	                                        {"to", 0},
	                                        {"material", resource_a},
	                                        {"remaining", 1},
	                                        {"automatic", false},
	                                        {"cargo", 0}}});
	cargo_board["batches"] = Json::array({{{"id", 2},
	                                       {"source", 1},
	                                       {"destination", 2},
	                                       {"from", 0},
	                                       {"to", 0},
	                                       {"material", resource_a},
	                                       {"cargo", 0},
	                                       {"request", 1},
	                                       {"location", 1},
	                                       {"compartment", 0},
	                                       {"quantity", 1},
	                                       {"travel", 1},
	                                       {"crossing", 0},
	                                       {"edge_to", 1},
	                                       {"returning", false}}});
	cargo_board["cargo"] = Json::array({{{"id", 3},
	                                     {"carrier", 2},
	                                     {"target", 3},
	                                     {"quantity", 1},
	                                     {"report_tick", 0},
	                                     {"material", resource_a},
	                                     {"phase", "loading"}}});
	cargo_board["offers"] =
	    Json::array({{{"entity", 2}, {"store", "hold"}, {"material", resource_a}, {"connected", true}}});
	cargo_board["cargo_craft"] =
	    Json::array({{{"id", 2},
	                  {"authorised", true},
	                  {"available", true},
	                  {"controllable", true},
	                  {"stores", Json::array({{{"id", "hold"}, {"accepts", Json::array({resource_a})}}})}}});
	require(logistics_projection::board(cargo_board) == cargo_board,
	        "Maximum-length resource IDs remain intact in delivery, batch, cargo, offer and compatibility projections");
	const auto saved = world.save();
	World restored;
	restored.restore(catalog, saved);
	require(restored.save() == saved, "Selected empty magazine continues exactly across restore");
	const auto store = selection(world, false, "test.round-b");
	require(world.submit(102, store),
	        "General single-resource store supports explicit selection through its owning Controller");
	world.tick();
	require(!world.submit(102, store) &&
	            world.save().at("bodies")[0].at("stocks").at("logistics:hold").at("selection") == "test.round-b",
	        "General store selection persists and rejects stale board revisions");
	require(world.submit(102, selection(world, false, resource_a)), "Empty free store can deliberately switch back");
	world.tick();

	auto assisted_source = source;
	assisted_source["entities"][0]["control_layout"]["station_complexity"] = true;
	assisted_source["entities"][0]["control_layout"]["manual_workloads"] = {{"drive", {{"logistics", 3}}}};
	assisted_source["entities"][0]["control_layout"]["profiles"][0]["stars"] = 1;
	World assisted;
	assisted.load(load(assisted_source), "test.map");
	require(assisted.claim(101, 1, "drive") && assisted.claim(102, 1, "observer"), "Assisted stock seats claimed");
	const auto assisted_choice = selection(assisted, true, "test.round-b");
	require(!assisted.submit(101, assisted_choice) &&
	            !eligible_console_command(*assisted.observe(101), Json::object(), Json::object(), assisted_choice),
	        "Assisted logistics workload does not admit manual stock selection in World or console controls");

	auto loaded_source = source;
	loaded_source["entities"][0]["systems"]["mag"]["stock"]["initial"] = {{resource_a, 1}};
	World loaded;
	loaded.load(load(loaded_source), "test.map");
	require(loaded.claim(101, 1, "drive") && loaded.claim(102, 1, "observer"), "Loaded selection seats claimed");
	require(!loaded.submit(101, selection(loaded, true, "test.round-b")), "Nonempty stock prevents resource switch");
	loaded.tick();
	loaded.tick();
	const auto loaded_stock = loaded.save().at("bodies")[0].at("stocks").at("system:mag");
	require(loaded_stock.at("quantities").empty() && !loaded_stock.at("capacity_claims").empty(),
	        "Weapon owns return space after loading");
	require(!loaded.submit(101, selection(loaded, true, "test.round-b")),
	        "Loaded weapon return commitment prevents an empty magazine changing resource");
	OperatingCommand target{1, {}, 2, {}, "gun", "target", controller(loaded, 101, "gun").at("generation")};
	require(loaded.submit(101, target), "Loaded weapon receives target");
	loaded.tick();
	require(loaded.save().at("bodies")[0].at("stocks").at("system:mag").at("selection") == resource_a,
	        "Firing preserves empty selection until a deliberate new choice");
	require(loaded.submit(101, selection(loaded, true, "test.round-b")),
	        "Fired weapon releases return commitment for explicit selection");
	loaded.tick();

	auto processor_source = source;
	auto &processor_ship = processor_source["entities"][0];
	processor_ship["logistics"]["stores"][0]["stock"]["initial"] = {{resource_a, 2}};
	processor_source["recipes"] = Json::array({{{"id", "test.first"},
	                                            {"label", "First"},
	                                            {"inputs", {{resource_a, 1}}},
	                                            {"outputs", {{resource_a, 1}}},
	                                            {"work", 100}},
	                                           {{"id", "test.second"},
	                                            {"label", "Second"},
	                                            {"inputs", {{resource_a, 2}}},
	                                            {"outputs", {{resource_a, 1}}},
	                                            {"work", 100}}});
	processor_ship["processors"] = Json::array({{{"id", "fabricator"},
	                                             {"equipment", "mag"},
	                                             {"recipes", {"test.first", "test.second"}},
	                                             {"recipe", "test.first"},
	                                             {"mode", "batch"},
	                                             {"work_per_second", 20},
	                                             {"inputs", {{resource_a, "logistics:hold"}}},
	                                             {"outputs", {{resource_a, "system:mag"}}}}});
	const auto processor_catalog = load(processor_source);
	World processor;
	processor.load(processor_catalog, "test.map");
	require(processor.claim(101, 1, "drive") && processor.claim(102, 1, "observer"),
	        "Processor selection seats claimed");
	processor.tick();
	const auto before = processor.save();
	require(before.at("bodies")[0].at("stocks").at("logistics:hold").at("quantities").at(resource_a) == 1,
	        "Batch prepays exact ingredients");
	require(!processor.submit(101, selection(processor, true, "test.round-b")),
	        "Processor output commitment locks magazine selection");
	const auto row = processor.observe(101)->gameplay.at("processors")[0];
	OperatingCommand recipe{1, "processor.select", row.at("selection_revision"), "fabricator"};
	recipe.controller = "mag";
	recipe.generation = controller(processor, 101, "mag").at("generation");
	recipe.text = "test.second";
	require(eligible_console_command(*processor.observe(101), Json::object(), Json::object(), recipe).has_value(),
	        "Projected recipe selection resolves through console controls");
	require(!processor.submit(102, recipe) && processor.submit(101, recipe),
	        "Recipe selection uses equipment Controller ownership");
	processor.tick();
	const auto changed = processor.save();
	const auto &body = changed.at("bodies")[0];
	require(
	    body.at("stocks").at("logistics:hold").at("quantities").at(resource_a) == 1 &&
	        body.at("stocks").at("system:mag").at("capacity_claims").empty() &&
	        body.at("processors").at("fabricator").at("recipe") == "test.second" &&
	        body.at("processors").at("fabricator").at("progress") == 0,
	    "Changing recipe discards unfinished work, releases owned output claims and never refunds prepaid ingredients");
	require(!processor.submit(101, recipe), "Recipe selection rejects stale selection revisions");
	World processor_restored;
	processor_restored.restore(processor_catalog, changed);
	require(processor_restored.save() == changed, "Changed recipe and selection revision persist exactly");
}