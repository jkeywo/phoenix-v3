#pragma once
// Independent delivery objective: actual docking, Controller authority, finite cargo
// and a persisted submission latch all use ordinary production paths.
void objective_delivery_cases()
{
	auto source = docking_fixture();
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.delivery-policy","context":"controller","logistics_commands":true,"workflow_observations":true,
 "memory":[{"name":"submitted","type":"integer","initial":0}],"initial":"watch",
 "states":[{"id":"watch","transitions":[{"target":"submit","conditions":[
 {"left":{"observation":"workflow.delivery"},"op":"eq","right":true},
 {"left":{"observation":"workflow.delivery_id"},"op":"ne","right":{"memory":"submitted"}}]}]},
 {"id":"submit","entry":[{"attempt":[{"tasks":[
 {"action":"logistics.transfer","arguments":[{"observation":"workflow.delivery_source"},{"observation":"workflow.delivery_transfer"},{"observation":"logistics.revision"}]},
 {"memory":"submitted","value":{"observation":"workflow.delivery_id"}}]},{"tasks":[]}]}],
 "transitions":[{"target":"watch","conditions":[{"left":{"elapsed":true},"op":"ge","right":5}]}]}]})json"));
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.delivery-scenario","context":"scenario","initial":"offer","states":[{"id":"offer","entry":[
 {"action":"add_objective","arguments":["deliver",{"observation":"self.entity"},"deliver"]}]}]})json"));
	source["maps"][0]["regions"] = Json::parse(R"json([
 {"id":"drop","shape":"circle","x_mm":10000,"y_mm":0,"radius_mm":100}])json");
	source["maps"][0]["scenario"] = Json::parse(R"json({
 "tree":"test.delivery-scenario","bindings":{"actor":{"slot":"crew"}},
 "fact_bindings":[{"id":"received","kind":"inventory","subject":{"entity":1},"field":"hold","operation":"parts"}],
 "objectives":[{"id":"deliver","type":"condition","region":"drop","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},
 "title_key":"Deliver components","complete_when":{"fact":"received","op":"ge","value":7},
 "delivery":{"target":{"entity":1},"port":"aft","source":"hold","store":"hold","material":"parts","quantity":3}}]})json");
	for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
		if (controller["id"] == "team")
		{
			controller.erase("fallback");
			controller["tree"] = "test.delivery-policy";
		}
	for (const bool missing_quantity : {true, false})
	{
		auto invalid = source;
		auto &delivery = invalid["maps"][0]["scenario"]["objectives"][0]["delivery"];
		if (missing_quantity)
			delivery.erase("quantity");
		else
			delivery["material"] = "test.unknown-cargo";
		bool refused = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, missing_quantity ? "Delivery instructions require an explicit quantity"
		                                  : "Delivery instructions require an existing material");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	require(world.claim(101, 2, "drive") && world.claim(102, 2, "observer") && world.claim(103, 1, "observer"),
	        "Delivery operators claim their own operations");
	const auto quantity = [&](std::uint64_t claim)
	{
		return world.observe(claim)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts").get<int>();
	};
	ticks(world, 12);
	require(quantity(102) == 4 && quantity(103) == 4, "An order cannot transfer cargo through proximity alone");
	require(world.submit(101, command(world, 101, "dock", "connect", "fore", "1/aft")),
	        "Delivery uses an ordinary compatible physical dock");
	ticks(world, 30);
	require(quantity(102) == 4 && quantity(103) == 4, "AI delivery cannot override a human logistics operation");
	world.release(102);
	ticks(world, 7);
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 120);
	ticks(resumed, 120);
	require(world.save() == resumed.save(), "Objective cargo and its submission latch resume exactly");
	require(quantity(103) == 7, "The receiving store gains the actual requested cargo");
	require(world.save().at("logistics").at("requests").empty(),
	        "A persistent objective submits one request, without accumulating duplicate waiting deliveries");
}
