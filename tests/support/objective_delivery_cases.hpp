#pragma once
// Independent delivery objective: actual docking, Controller authority, finite cargo
// and frozen pending-work evidence all use ordinary production paths.
void objective_delivery_cases()
{
	auto source = docking_fixture();
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.delivery-submit","context":"controller","logistics_commands":true,
 "controller_action":{"label":"Request the disclosed delivery","inputs":[
 {"name":"source","type":"text"},{"name":"transfer","type":"text"},{"name":"revision","type":"integer"}]},
 "initial":"submit","states":[{"id":"submit","running":[
 {"attempt":[{"tasks":[{"action":"logistics.transfer","arguments":[{"observation":"input.source"},{"observation":"input.transfer"},{"observation":"input.revision"}]},
 {"action":"action.succeed","arguments":["delivery_requested"]}]},{"tasks":[{"action":"action.fail","arguments":["delivery_refused"]}]}]}]}]})json"));
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.delivery-policy","context":"controller","delivery_observations":true,"logistics_commands":true,
 "action_bindings":[{"id":"deliver","tree":"test.delivery-submit"}],
 "objective_bindings":[{"behavior":"deliver","candidate_source":"delivery","action":"deliver",
 "target":{"observation":"delivery_candidate.target"},
 "arguments":[{"observation":"delivery_candidate.source"},{"observation":"delivery_candidate.transfer"},{"observation":"delivery_candidate.revision"}]}],
 "initial":"watch","states":[{"id":"watch"}]})json"));
	source["trees"].push_back(Json::parse(R"json({"id":"test.delivery-other-work","context":"controller",
 "command_bindings":[{"channel":"other_work"}],"controller_action":{"label":"Other useful work"},
 "initial":"work","states":[{"id":"work","running":[{"action":"write.other_work","arguments":[1]},
 {"action":"action.succeed","arguments":["other_work_done"]}]}]})json"));
	auto &policy = *std::find_if(source["trees"].begin(), source["trees"].end(),
	                             [](const auto &tree)
	                             {
		                             return tree.at("id") == "test.delivery-policy";
	                             });
	policy["action_bindings"].push_back({{"id", "ready"}, {"tree", "test.delivery-other-work"}});
	policy["command_bindings"] = Json::array({{{"channel", "other_work"}}});
	policy["objective_bindings"].push_back({{"id", "ready"}, {"behavior", "ready"}, {"action", "ready"}});
	source["entities"][0]["standing_directives"] = Json::array(
	    {{{"id", "ready"}, {"behavior", "ready"}, {"tier", "directive_other"}, {"title_key", "Readiness"}}});
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
			controller["commands"].push_back({{"id", "other_work"},
			                                  {"action", "intent_integer"},
			                                  {"minimum", 0},
			                                  {"maximum", 1},
			                                  {"consumers", Json::array()}});
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
	{
		auto scoped = source;
		scoped["maps"][0]["scenario"]["objectives"][0]["system"] = "other-store";
		World restricted;
		restricted.load(load(scoped), "test.map", {{"crew", "test.away"}});
		ticks(restricted, 2);
		const auto snapshot = restricted.controller_decision_snapshot({2, "observer", "team"});
		require(std::any_of(snapshot.at("objective_candidates").begin(), snapshot.at("objective_candidates").end(),
		                    [](const auto &candidate)
		                    {
			                    return candidate.at("reason") == "no_scoped_delivery_instruction" &&
			                           !candidate.at("feasible").template get<bool>();
		                    }),
		        "A published store constraint cannot silently become a different delivery assignment");
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
	bool other_while_pending = false;
	for (int tick = 0; tick < 7; ++tick)
	{
		world.tick();
		const auto snapshot = world.controller_decision_snapshot({2, "observer", "team"});
		bool pending = false, other = false;
		for (const auto &candidate : snapshot.at("objective_candidates"))
		{
			pending |=
			    candidate.at("reason") == "delivery_already_in_progress" && !candidate.at("feasible").get<bool>();
			other |= candidate.at("binding") == "ready" && candidate.at("selected").get<bool>();
		}
		other_while_pending |= pending && other && snapshot.at("facts").at("action.id") == "ready" &&
		                       snapshot.at("facts").at("action.status") == "succeeded";
	}
	require(other_while_pending, "An accepted finite delivery leaves its Controller free for other useful work");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 120);
	ticks(resumed, 120);
	require(world.save() == resumed.save(), "Objective cargo and scoped pending-work evidence resume exactly");
	require(quantity(103) == 7, "The receiving store gains the actual requested cargo");
	require(world.save().at("logistics").at("requests").empty(),
	        "A persistent objective submits one request, without accumulating duplicate waiting deliveries");
}
