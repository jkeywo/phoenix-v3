// Included in the process integration fixture's private namespace.
void add_fit(Json &source, const std::string &id, Json spec, Json commands = Json::array())
{
	auto &ship = source["entities"][0];
	spec["compartment"] = "fore";
	spec["properties"] = ship["properties"];
	ship["systems"][id] = spec;
	if (commands.empty())
		commands.push_back({{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {id}}});
	ship["control_layout"]["controllers"].push_back(
	    {{"id", id}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["drive"]["human"].push_back(id);
	ship["stations"][0]["consoles"][0]["feeds"].push_back(id);
	ship["personnel"]["duties"].push_back({{"id", id},
	                                       {"label", id},
	                                       {"department", "watch"},
	                                       {"compartment", "fore"},
	                                       {"console", "drive"},
	                                       {"controller", id}});
}
Json generation_fixture()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["initial"] = 0;
	ship["resources"]["generators"] = Json::array(
	    {{{"id", "supply"}, {"store", "cell"}, {"maximum_rate", 40}, {"response_rate", 40}, {"initial_output", 40}}});
	add_fit(source, "generator", {{"kind", "generator"}, {"generator", "supply"}, {"power_store", "cell"}},
	        Json::array(
	            {{{"id", "output"}, {"action", "generator_output"}, {"initial", 40}, {"consumers", {"generator"}}}}));
	source["effects"].push_back(
	    {{"id", "test.generation"}, {"kind", "process"}, {"label", "Generation"}, {"native", "generation"}});
	ship["systems"]["generator"]["processes"] = {{"main", {{"process", "test.generation"}}}};
	return source;
}
Json generation_boundaries()
{
	auto source = generation_fixture();
	auto &ship = source["entities"][0];
	effect(source, "test.generation")["upkeep_costs"] =
	    Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_second", 20}}});
	ship["systems"]["generator"]["properties"]["test.fuel"] = 1;
	ship["systems"]["radar"]["power_store"] = "cell";
	ship["systems"]["radar"]["energy_per_second"] = 20;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world);
	require(energy(world) == 1 && property(world, "test.fuel", "generator") == 0 &&
	            state(world, "main", "generator").at("actual_work") == 2,
	        "Pre-existing fuel starts generation and accepted current output supplies a downstream consumer that tick");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 2);
	ticks(resumed, 2);
	require(energy(world) == 0 && state(world, "main", "generator").at("actual_work") == 0 &&
	            world.save() == resumed.save(),
	        "Exhausted startup input blocks further generation while already committed power remains usable once");
	Json blocked = Json::array();
	for (bool circular : {false, true})
	{
		auto rejected = generation_fixture();
		auto &owner = rejected["entities"][0];
		effect(rejected, "test.generation")["upkeep_costs"] =
		    Json::array({{{"payer", "owner"},
		                  {"store", circular ? "energy:other" : "energy:cell"},
		                  {"resource", "fixture.resource.energy"},
		                  {"per_second", 20}}});
		if (circular)
		{
			owner["resources"]["stores"].push_back({{"id", "other"},
			                                        {"resource", "fixture.resource.energy"},
			                                        {"capacity", 1000},
			                                        {"initial", 0},
			                                        {"charge_rate", 1000},
			                                        {"discharge_rate", 1000}});
			owner["resources"]["generators"].push_back({{"id", "second"},
			                                            {"store", "other"},
			                                            {"maximum_rate", 40},
			                                            {"response_rate", 40},
			                                            {"initial_output", 40}});
			add_fit(rejected, "second-generator",
			        {{"kind", "generator"}, {"generator", "second"}, {"power_store", "other"}},
			        Json::array({{{"id", "output"},
			                      {"action", "generator_output"},
			                      {"initial", 40},
			                      {"consumers", {"second-generator"}}}}));
			auto second = effect(rejected, "test.generation");
			second["id"] = "test.second-generation";
			second["upkeep_costs"][0]["store"] = "energy:cell";
			rejected["effects"].push_back(second);
			owner["systems"]["second-generator"]["processes"] = {{"main", {{"process", "test.second-generation"}}}};
		}
		const auto rejected_catalog = load(rejected);
		World empty;
		empty.load(rejected_catalog, "test.map");
		ticks(empty, 4);
		require(energy(empty) == 0 && state(empty, "main", "generator").at("actual_work") == 0 &&
		            (!circular ||
		             (energy(empty, 1) == 0 && state(empty, "main", "second-generator").at("actual_work") == 0)),
		        "A producer cannot buy its input from its own or a circular producer's promised current output");
		blocked.push_back(portable(empty.save()));
	}
	auto full = generation_fixture();
	auto &reserve = full["entities"][0]["resources"]["stores"][0];
	reserve["capacity"] = 3;
	reserve["initial"] = 3;
	full["entities"][0]["resources"]["generators"][0]["initial_output"] = 0;
	full["entities"][0]["resources"]["generators"][0]["response_rate"] = 20;
	effect(full, "test.generation")["work_costs"] =
	    Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
	full["effects"].push_back({{"id", "test.reserve.consume"},
	                           {"kind", "operations"},
	                           {"operations", Json::array({{{"kind", "consume"},
	                                                        {"target", "protected"},
	                                                        {"store", "energy:cell"},
	                                                        {"resource", "fixture.resource.energy"},
	                                                        {"amount", 2}}})}});
	scenario(full, {{6, "test.reserve.consume"}});
	const auto full_catalog = load(full);
	World saturated;
	saturated.load(full_catalog, "test.map");
	ticks(saturated, 5);
	require(energy(saturated) == 3 && property(saturated, "test.fuel", "generator") == 100 &&
	            state(saturated, "main", "generator").at("actual_work") == 0,
	        "Full reserves accept no generation work and incur no work-proportional charge");
	const auto ramp =
	    body(saturated).at("resources").at("network").at("generators")[0].at("actual_rate").get<std::int64_t>();
	require(ramp > 0 && ramp < 40, "Native generator response still ramps while its reserve is full");
	World saturated_resumed;
	saturated_resumed.restore(full_catalog, saturated.save());
	ticks(saturated, 22);
	ticks(saturated_resumed, 22);
	require(energy(saturated) == 3 && property(saturated, "test.fuel", "generator") == 98 &&
	            saturated.save() == saturated_resumed.save(),
	        "Accepted generation refills exact freed capacity, charges accepted energy only, and preserves native ramp "
	        "continuation");
	return Json::array({portable(world.save()), blocked, portable(saturated.save())});
}
Json thermal_and_cooling()
{
	Json trace = Json::array();
	for (bool coolant : {false, true})
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		ship["resources"]["stores"][0]["initial_heat"] = coolant ? 0 : 10;
		ship["resources"]["coolant_sources"] = Json::array({{{"id", "tank"},
		                                                     {"initial", coolant ? 20 : 0},
		                                                     {"capacity", 20},
		                                                     {"removal_rate", 40},
		                                                     {"coolant_per_heat_ppm", 1000000}}});
		ship["resources"]["cooling_links"] = Json::array(
		    {{{"id", "feed"}, {"source", "tank"}, {"store", "cell"}, {"capacity_rate", 40}, {"allocation_rate", 40}}});
		add_fit(source, "cooler", {{"kind", "coolant"}, {"cooling_link", "feed"}},
		        Json::array({{{"id", "allocation"},
		                      {"action", "cooling_allocation"},
		                      {"initial", 40},
		                      {"consumers", {"cooler"}}}}));
		source["effects"].push_back(
		    {{"id", "test.cooling"}, {"kind", "process"}, {"native", "cooling"}, {"label", "Cooling"}});
		ship["systems"]["cooler"]["processes"] = {{"main", {{"process", "test.cooling"}}}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		ticks(world, 4);
		const auto network = body(world).at("resources").at("network");
		require(network.at("stores")[0].at("heat") == (coolant ? 0 : 10) &&
		            state(world, "main", "cooler").at("actual_work") == 0,
		        "Native cooling performs no completed work with no heat or no coolant");
		require(network.at("coolant_sources")[0]
		                .at("stock")
		                .at("quantities")
		                .value("fixture.resource.coolant", std::int64_t{0}) == (coolant ? 20 : 0),
		        "Idle or starved cooling cannot consume fictional coolant");
		trace.push_back(portable(world.save()));
	}
	auto source = fixture();
	source["entities"][0]["resources"]["coolant_sources"] =
	    Json::array({{{"id", "tank"}, {"initial", 20}, {"capacity", 20}, {"removal_rate", 40}}});
	source["effects"].push_back(
	    {{"id", "test.thermal.add-remove"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array({{{"kind", "heat_add"}, {"target", "protected"}, {"store", "energy:cell"}, {"amount", 10}},
	                   {{"kind", "heat_remove"}, {"target", "protected"}, {"store", "energy:cell"}, {"amount", 4}}})}});
	source["effects"].push_back(
	    {{"id", "test.thermal.rollback"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "heat_add"}, {"target", "protected"}, {"store", "energy:cell"}, {"amount", 50}},
	           {{"kind", "heat_remove"}, {"target", "protected"}, {"store", "energy:missing"}, {"amount", 1}}})}});
	source["effects"].push_back(
	    {{"id", "test.thermal.zero"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "heat_remove"}, {"target", "protected"}, {"store", "energy:cell"}, {"amount", 1000}}})}});
	scenario(source, {{1, "test.thermal.add-remove"}, {5, "test.thermal.rollback"}, {9, "test.thermal.zero"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 3);
	require(body(world).at("resources").at("network").at("stores")[0].at("heat") == 6 && energy(world) == 1000,
	        "Shared heat operations mutate the existing thermal owner without inventing energy payment");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 5);
	ticks(resumed, 5);
	require(body(world).at("resources").at("network").at("stores")[0].at("heat") == 6,
	        "An invalid later thermal target rolls back earlier staged heating");
	ticks(world, 5);
	ticks(resumed, 5);
	require(body(world).at("resources").at("network").at("stores")[0].at("heat") == 0 && world.save() == resumed.save(),
	        "Heat removal clamps at zero and its consequences continue exactly");
	require(body(world)
	                .at("resources")
	                .at("network")
	                .at("coolant_sources")[0]
	                .at("stock")
	                .at("quantities")
	                .at("fixture.resource.coolant") == 20,
	        "Direct thermal effects do not silently spend coolant");
	trace.push_back(portable(world.save()));
	return trace;
}

Json processor_commitments()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	for (const auto *id : {"test.ore", "test.product"})
		source["resources"].push_back({{"id", id}, {"label", id}, {"tags", {"cargo"}}, {"sizes", {{"cargo", 1}}}});
	source["recipes"] = Json::array({{{"id", "test.batch"},
	                                  {"label", "Batch"},
	                                  {"inputs", {{"test.ore", 2}}},
	                                  {"outputs", {{"test.product", 2}}},
	                                  {"work", 4}}});
	const auto store = [](const std::string &resource, int initial)
	{
		return Json{{"capacity_tag", "cargo"},
		            {"capacity", 20},
		            {"filter", {{"resources", {resource}}}},
		            {"initial", initial ? Json{{resource, initial}} : Json::object()}};
	};
	ship["logistics"] = {{"controller", "team-a"},
	                     {"units_per_second", 20},
	                     {"concurrency", 1},
	                     {"batch_size", 1},
	                     {"stores", Json::array({{{"id", "input"},
	                                              {"compartment", "aft"},
	                                              {"controller", "team-a"},
	                                              {"main", true},
	                                              {"stock", store("test.ore", 10)}},
	                                             {{"id", "output"},
	                                              {"compartment", "aft"},
	                                              {"controller", "team-a"},
	                                              {"stock", store("test.product", 0)}}})}};
	ship["processors"] = Json::array({{{"id", "factory"},
	                                   {"equipment", "radar"},
	                                   {"recipe", "test.batch"},
	                                   {"mode", "batch"},
	                                   {"work_per_second", 10},
	                                   {"energy_per_second", 0},
	                                   {"inputs", {{"test.ore", "logistics:input"}}},
	                                   {"outputs", {{"test.product", "logistics:output"}}}}});
	source["effects"].push_back(
	    {{"id", "test.factory"},
	     {"kind", "process"},
	     {"label", "Factory"},
	     {"native", "processor"},
	     {"requirements", {{"fact", "owner.property"}, {"property", "test.allowed"}}},
	     {"entry_costs", Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}})},
	     {"start_effect", "test.process.start"}});
	ship["systems"]["radar"]["processes"] = {{"factory", {{"process", "test.factory"}}}};
	source["effects"].push_back({{"id", "test.factory.block"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "property_set"},
	                                                          {"target", "protected"},
	                                                          {"equipment", "radar"},
	                                                          {"property", "test.allowed"},
	                                                          {"value", false}}})}});
	source["effects"].push_back({{"id", "test.factory.resume"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "property_set"},
	                                                          {"target", "protected"},
	                                                          {"equipment", "radar"},
	                                                          {"property", "test.allowed"},
	                                                          {"value", true}}})}});
	scenario(source, {{2, "test.factory.block"}, {8, "test.factory.resume"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 5);
	const auto paused = body(world);
	const auto progress = paused.at("processors").at("factory").at("progress");
	require(state(world, "factory", "radar").at("phase") == "paused" && progress.get<std::int64_t>() > 0 &&
	            progress.get<std::int64_t>() < 4000 &&
	            paused.at("stocks").at("logistics:input").at("quantities").at("test.ore") == 8 &&
	            !paused.at("stocks").at("logistics:output").at("capacity_claims").empty(),
	        "Interrupted process retains native batch ingredients, partial work and receiving commitments");
	const auto run = state(world, "factory", "radar").at("run");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 2);
	ticks(resumed, 2);
	require(body(world).at("processors").at("factory").at("progress") == progress,
	        "Blocked processor advances no native batch work");
	bool completed = false;
	for (int i = 0; i < 20; ++i)
	{
		ticks(world);
		ticks(resumed);
		const auto quantities = body(world).at("stocks").at("logistics:output").at("quantities");
		if (quantities.value("test.product", 0) == 2)
		{
			completed = true;
			break;
		}
	}
	require(completed && body(world).at("stocks").at("logistics:input").at("quantities").at("test.ore") == 8 &&
	            property(world, "test.fuel", "radar") == 97 && state(world, "factory", "radar").at("run") == run,
	        "Resuming completes the committed batch without paying its ingredients or process entry again");
	require(world.save() == resumed.save(), "Native batch commitments and process pause state resume exactly");
	return portable(world.save());
}

Json native_work_payments_and_modifiers()
{
	Json refused = Json::array();
	for (int mode = 0; mode < 3; ++mode)
	{
		auto source = generation_fixture();
		Json group{
		    {"id", "test.generation.consequence"},
		    {"kind", "operations"},
		    {"operations",
		     Json::array(
		         {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 1}}})}};
		if (mode == 0)
			group["costs"] = Json::array({{{"payer", "owner"},
			                               {"store", "energy:cell"},
			                               {"resource", "fixture.resource.energy"},
			                               {"amount", 1}}});
		else if (mode == 1)
		{
			group["payer"] = "generator";
			group["energy"] = 1;
		}
		else
			group["operations"].push_back({{"kind", "consume"},
			                               {"target", "owner"},
			                               {"store", "energy:cell"},
			                               {"resource", "fixture.resource.energy"},
			                               {"amount", 2000}});
		source["effects"].push_back(group);
		effect(source, "test.generation")["work_effect"] = "test.generation.consequence";
		if (mode == 2)
			effect(source, "test.generation")["work_costs"] =
			    Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 1}}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		ticks(world, 3);
		require(energy(world) == 0 && property(world, "test.events", "generator") == 0 &&
		            property(world, "test.fuel", "generator") == 100 &&
		            state(world, "main", "generator").at("actual_work") == 0,
		        "Generator inline work payments cannot self-fund and failed work consequences roll back native output "
		        "and inputs");
		refused.push_back(portable(world.save()));
	}
	auto source = generation_fixture();
	source["effects"].push_back(
	    {{"id", "test.generation.modifier"},
	     {"kind", "modifier"},
	     {"contributions", {{"output", {{"property", "generation"}, {"delta_permille", 500}}}}}});
	source["effects"].push_back({{"id", "test.generation.modify"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"equipment", "generator"},
	                                                          {"effect", "test.generation.modifier"},
	                                                          {"grant", "output"}}})}});
	scenario(source, {{1, "test.generation.modify"}});
	const auto catalog = load(source);
	World generated;
	generated.load(catalog, "test.map");
	ticks(generated, 4);
	require(state(generated, "main", "generator").at("actual_work") == 3,
	        "Native generation throughput uses the shared additive property modifier");
	auto cooled = fixture();
	auto &ship = cooled["entities"][0];
	ship["resources"]["stores"][0]["initial_heat"] = 100;
	ship["resources"]["coolant_sources"] = Json::array({{{"id", "tank"},
	                                                     {"initial", 100},
	                                                     {"capacity", 100},
	                                                     {"removal_rate", 100},
	                                                     {"coolant_per_heat_ppm", 1000000}}});
	ship["resources"]["cooling_links"] = Json::array(
	    {{{"id", "feed"}, {"source", "tank"}, {"store", "cell"}, {"capacity_rate", 100}, {"allocation_rate", 40}}});
	add_fit(
	    cooled, "cooler", {{"kind", "coolant"}, {"cooling_link", "feed"}},
	    Json::array(
	        {{{"id", "allocation"}, {"action", "cooling_allocation"}, {"initial", 40}, {"consumers", {"cooler"}}}}));
	cooled["effects"].push_back(
	    {{"id", "test.cooling"}, {"kind", "process"}, {"native", "cooling"}, {"label", "Cooling"}});
	ship["systems"]["cooler"]["processes"] = {{"main", {{"process", "test.cooling"}}}};
	cooled["effects"].push_back({{"id", "test.cooling.modifier"},
	                             {"kind", "modifier"},
	                             {"contributions", {{"output", {{"property", "cooling"}, {"delta_permille", 1000}}}}}});
	cooled["effects"].push_back({{"id", "test.cooling.modify"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"equipment", "cooler"},
	                                                          {"effect", "test.cooling.modifier"},
	                                                          {"grant", "output"}}})}});
	scenario(cooled, {{1, "test.cooling.modify"}});
	const auto cooling_catalog = load(cooled);
	World cooling;
	cooling.load(cooling_catalog, "test.map");
	ticks(cooling, 4);
	require(state(cooling, "main", "cooler").at("actual_work") == 4,
	        "Native cooling throughput uses shared modifiers while retaining physical link and coolant limits");
	World resumed;
	resumed.restore(cooling_catalog, cooling.save());
	ticks(cooling, 3);
	ticks(resumed, 3);
	require(cooling.save() == resumed.save(),
	        "Modified cooling work and fractional native accounting continue exactly");
	return Json::array({refused, portable(generated.save()), portable(cooling.save())});
}

Json producer_electrical_inputs()
{
	Json trace = Json::array();
	for (int mode = 0; mode < 4; ++mode)
	{
		auto source = generation_fixture();
		auto &ship = source["entities"][0];
		ship["resources"]["stores"].push_back({{"id", "starter"},
		                                       {"resource", "fixture.resource.energy"},
		                                       {"capacity", 100},
		                                       {"initial", 10},
		                                       {"charge_rate", 100},
		                                       {"discharge_rate", 20},
		                                       {"operation_heat_ppm", 1000000},
		                                       {"stop_heat", 100},
		                                       {"recover_heat", 50}});
		effect(source, "test.generation")["work_costs"] = Json::array({{{"payer", "owner"},
		                                                                {"store", "energy:starter"},
		                                                                {"resource", "fixture.resource.energy"},
		                                                                {"per_work", 1}}});
		if (mode > 0)
		{
			effect(source, "test.generation").erase("work_costs");
			effect(source, "test.generation")["upkeep_costs"] = Json::array({{{"payer", "owner"},
			                                                                  {"store", "energy:starter"},
			                                                                  {"resource", "fixture.resource.energy"},
			                                                                  {"per_second", 20}}});
			if (mode == 3)
			{
				ship["systems"]["radar"]["power_store"] = "starter";
				ship["systems"]["radar"]["energy_per_second"] = 20;
			}
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		if (mode == 1 || mode == 2)
		{
			auto checkpoint = world.save();
			auto &starter = checkpoint["bodies"][0]["resources"]["network"]["stores"][1];
			if (mode == 1)
				starter["enabled"] = false;
			else
			{
				starter["heat"] = 100;
				starter["thermal_stop"] = true;
			}
			world.restore(catalog, checkpoint);
		}
		ticks(world);
		const auto starter = body(world).at("resources").at("network").at("stores")[1];
		if (mode < 3)
			require(
			    energy(world) == 0 && energy(world, 1) == 10 && starter.at("heat") == (mode == 2 ? 100 : 0) &&
			        state(world, "main", "generator").at("actual_work") == 0,
			    "Producer payment respects native discharge, isolation and thermal protection without debit or heat");
		else
			require(energy(world) == 2 && energy(world, 1) == 9 && starter.at("heat") == 1,
			        "Affordable producer input heats once and consumes the shared native discharge budget before "
			        "downstream demand");
		World resumed;
		resumed.restore(catalog, world.save());
		ticks(world);
		ticks(resumed);
		require(world.save() == resumed.save(),
		        "Native producer input protection and thermal accounting continue exactly");
		trace.push_back(portable(world.save()));
	}
	for (int mode = 0; mode < 3; ++mode)
	{
		const bool funded = mode != 0;
		auto source = generation_fixture();
		source["entities"][0]["resources"]["stores"][0]["initial"] = mode == 2 ? 4 : funded ? 1 : 0;
		if (mode == 2)
		{
			effect(source, "test.generation")["upkeep_costs"] = Json::array({{{"payer", "owner"},
			                                                                  {"store", "energy:cell"},
			                                                                  {"resource", "fixture.resource.energy"},
			                                                                  {"per_second", 20}}});
			effect(source, "test.generation")["work_costs"] = Json::array({{{"payer", "owner"},
			                                                                {"store", "energy:cell"},
			                                                                {"resource", "fixture.resource.energy"},
			                                                                {"per_work", 1}}});
		}
		source["effects"].push_back(
		    {{"id", "test.generation.self-stop"},
		     {"kind", "operations"},
		     {"operations", Json::array({{{"kind", "remove"}, {"target", "owner"}, {"scope", "owner"}}})}});
		source["effects"].push_back(
		    {{"id", "test.generation.stop-fee"},
		     {"kind", "operations"},
		     {"costs", Json::array({{{"payer", "owner"},
		                             {"store", "energy:cell"},
		                             {"resource", "fixture.resource.energy"},
		                             {"amount", 1}}})},
		     {"operations",
		      Json::array(
		          {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.events"}, {"amount", 1}}})}});
		effect(source, "test.generation")["work_effect"] = "test.generation.self-stop";
		effect(source, "test.generation")["stop_effect"] = "test.generation.stop-fee";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		ticks(world);
		require(energy(world) == (funded ? 2 : 0) && property(world, "test.events", "generator") == (funded ? 1 : 0),
		        "Expanded stop-handler fee uses pre-generation input and never the producer's newly promised output");
		World resumed;
		resumed.restore(catalog, world.save());
		require(world.save() == resumed.save(), "Paid static self-stop leaves no stale execution credits on restore");
		ticks(world, 2);
		ticks(resumed, 2);
		require(world.save() == resumed.save(),
		        "Stopped static run cannot reacquire paid work credits during continuation");
		trace.push_back(portable(world.save()));
	}
	return trace;
}

Json competing_thermal_work()
{
	auto source = fixture();
	source["effects"].push_back(
	    {{"id", "test.thermal.failed-work"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array({{{"kind", "heat_add"}, {"target", "owner"}, {"store", "energy:missing"}, {"amount", 1}}})}});
	auto failed = effect(source, "test.process.tick");
	failed["id"] = "test.thermal.failed";
	failed["work_effect"] = "test.thermal.failed-work";
	failed.erase("work_values");
	source["effects"].push_back(failed);
	source["entities"][0]["processes"] = {{"a-failed", {{"process", "test.thermal.failed"}}},
	                                      {"b-good", {{"process", "test.process.tick"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world);
	require(property(world, "test.work") == 1000 && state(world, "a-failed").at("actual_work") == 0 &&
	            state(world, "b-good").at("actual_work") == 1000 &&
	            body(world).at("resources").at("network").at("stores")[0].at("heat") == 0,
	        "Thermal preflight failure is attributed to its process while a valid peer commits exactly once");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 3);
	ticks(resumed, 3);
	require(property(world, "test.work") == 4000 && world.save() == resumed.save(),
	        "Repeated invalid thermal work cannot discard its valid peer or break continuation");
	return portable(world.save());
}

Json basic_propulsion_work()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["motion_model"] = "vector";
	for (const auto *field : {"resources", "processors", "combat", "personnel", "logistics"})
		ship.erase(field);
	ship["systems"] = {{"engine",
	                    {{"kind", "engine"},
	                     {"max_speed_mm_s", 10000},
	                     {"acceleration_mm_s2", 2000},
	                     {"properties", ship["properties"]}}}};
	ship["stations"] = Json::array({{{"id", "operations"},
	                                 {"consoles", Json::array({{{"id", "drive"},
	                                                            {"definition", "test.console"},
	                                                            {"systems", Json::array()},
	                                                            {"feeds", {"engine"}}}})}}});
	ship["control_layout"] = {
	    {"schema", 1},
	    {"default_profile", "direct"},
	    {"controllers",
	     Json::array(
	         {{{"id", "engine"},
	           {"console", "drive"},
	           {"fallback", "hold"},
	           {"commands",
	            Json::array(
	                {{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 1}, {"consumers", {"engine"}}}})}}})},
	    {"profiles", Json::array({{{"id", "direct"}, {"consoles", {{"drive", {{"human", {"engine"}}}}}}}})}};
	source["effects"].push_back(
	    {{"id", "test.basic.propulsion"},
	     {"kind", "process"},
	     {"native", "propulsion"},
	     {"label", "Basic propulsion"},
	     {"work_costs", Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"per_work", 100}}})}});
	source["entities"][0]["systems"]["engine"]["processes"] = {{"main", {{"process", "test.basic.propulsion"}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world);
	if (!(state(world, "main", "engine").at("actual_work") == 10 && property(world, "test.fuel", "engine") == 99))
		throw std::runtime_error("Basic engine near its requested speed funds its actual small thrust rather than a "
		                         "full tick of work: process=" +
		                         state(world, "main", "engine").dump() + " fuel=" +
		                         property(world, "test.fuel", "engine").dump() + " body=" + body(world).dump());
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world);
	ticks(resumed);
	require(state(world, "main", "engine").at("actual_work") == 0 && property(world, "test.fuel", "engine") == 99 &&
	            world.save() == resumed.save(),
	        "Basic engine at requested speed incurs no work charge and its clipped actuation continues exactly");
	return portable(world.save());
}

Json destroyed_process_owner()
{
	auto source = generation_fixture();
	add_fit(source, "engine", {{"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 2000}});
	add_fit(source, "steering", {{"kind", "steering"}, {"turn_mdeg_s", 90000}});
	auto &map = source["maps"][0];
	map["entities"] = Json::array();
	map["ship_slots"] = Json::array(
	    {{{"id", "crew"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}, {"allowed_ships", {"test.combat"}}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.combat"}});
	ticks(world, 2);
	require(state(world, "main", "generator").at("admitted") == true,
	        "Player ship machinery is admitted before physical destruction");
	auto damaged = world.save();
	damaged["bodies"][0]["destructible"]["durability"] = 0;
	damaged["bodies"][0]["destructible"]["destroyed_tick"] = world.ticks();
	for (auto &compartment : damaged["bodies"][0]["combat"]["compartments"])
	{
		compartment["hp"] = 0;
		compartment["disabled"] = true;
	}
	world.restore(catalog, damaged);
	ticks(world);
	const auto lost = world.save();
	require(lost.at("bodies")[0].at("mission_report").at("reason") == "destroyed",
	        "Physical ship loss finalises its authoritative mission departure");
	for (const auto &owner : lost.at("processes"))
		if (owner.at("entity") == 1)
			for (const auto &process : owner.at("state").at("processes"))
				require(process.at("admitted") == false && process.at("requested") == false,
				        "Destroyed player ship retires all machinery intent and admitted runs");
	World resumed;
	resumed.restore(catalog, lost);
	require(resumed.save() == lost, "Destroyed machinery owner checkpoint restores exactly");
	ticks(world, 3);
	ticks(resumed, 3);
	require(world.save() == resumed.save(), "Destroyed machinery remains retired through continuation");
	return portable(world.save());
}
