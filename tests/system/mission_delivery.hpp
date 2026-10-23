// Synthetic delivery contracts. Inventory snapshots are boundary fixtures; the cargo
// integration case separately exercises loading, flight, docking and physical receipt.
void delivery_cases()
{
	auto source = fixture();
	auto &entity = source["entities"][0];
	entity["combat"] = Json::parse(
	    R"({"repair_console":"drive","decks":[{"id":"deck","weight":1,"compartments":[{"id":"hold","x_mm":0,"y_mm":0,"half_width_mm":1000,"half_length_mm":1000,"hp":100,"resistance_per_m":1,"recovery_hp":20}]}],"connections":[],"destruction":{}})");
	for (auto &[id, system] : entity["systems"].items())
		system["compartment"] = "hold";
	entity["logistics"] = Json::parse(
	    R"({"controller":"propulsion","materials":[{"id":"parcel"}],"stores":[{"id":"receiving","compartment":"hold","capacity":9,"accepts":["parcel"]}]})");
	auto &scenario = source["maps"][0]["scenario"];
	for (
	    auto f : Json::parse(
	        R"([{"id":"received","kind":"inventory","subject":{"entity":2},"field":"receiving","operation":"parcel"},{"id":"time","kind":"clock","field":"tick"},{"id":"delivery-state","kind":"objective","subject":{"entity":1},"operation":"delivery","field":"status"},{"id":"receipt-tick","kind":"objective","subject":{"entity":1},"operation":"delivery","field":"completed_tick"}])"))
		scenario["fact_bindings"].push_back(f);
	scenario["objectives"].push_back(Json::parse(
	    R"({"id":"delivery","type":"condition","title_key":"Receipt","actor":{"entity":1},"recipients":{"ship":{"entity":1}},"complete_when":{"all":[{"fact":"received","op":"ge","value":7},{"fact":"time","op":"le","value":27}]},"fail_when":{"fact":"time","op":"gt","value":27}})"));
	source["trees"].back()["states"][0]["entry"].push_back(
	    {{"action", "add_objective"}, {"arguments", {"delivery", {{"observation", "self.entity"}}, "delivery"}}});
	scenario["reports"][0]["assessments"] = Json::parse(
	    R"([{"id":"delivery","title_key":"Delivery","rules":[{"result":"prompt","title_key":"Prompt","explanation_key":"Full receipt by first boundary","when":{"all":[{"fact":"delivery-state","value":"completed"},{"fact":"receipt-tick","op":"le","value":13}]}},{"result":"late","title_key":"Late","explanation_key":"Full receipt by final boundary","when":{"fact":"delivery-state","value":"completed"}},{"result":"expired","title_key":"Expired","explanation_key":"Deadline passed","when":{"fact":"delivery-state","value":"failed"}}],"fallback":{"result":"unfinished","title_key":"Unfinished","explanation_key":"Receipt incomplete"}}])");
	const auto catalog = load(source);
	for (const auto receipt_tick : {13, 14, 27, 28})
	{
		World w;
		w.load(catalog, "test.map");
		require(w.claim(101, 1, "drive"), "Delivery boundary crew");
		ticks(w, 5);
		auto partial = w.save();
		for (auto &b : partial["bodies"])
			if (b["body"]["id"] == 2)
				b["inventory"]["stores"][0]["quantities"]["parcel"] = 3;
		w.restore(catalog, partial);
		ticks(w, receipt_tick - 6);
		require(status(w, "delivery") == 1, "Partial receipt leaves deadline active");
		auto full = w.save();
		for (auto &b : full["bodies"])
			if (b["body"]["id"] == 2)
				b["inventory"]["stores"][0]["quantities"]["parcel"] = 7;
		w.restore(catalog, full);
		World resumed;
		resumed.restore(catalog, full);
		ticks(w, 1);
		ticks(resumed, 1);
		require(w.save() == resumed.save(), "Receipt tick resumes exactly");
		require(status(w, "delivery") == (receipt_tick <= 27 ? 2 : 3),
		        "Inclusive receipt boundary, expiry cannot regain credit");
		require(w.submit(101, cmd(w, "system_enabled", 1)), "Prepare delivery report departure");
		ticks(w, 900);
		require(w.submit(101, cmd(w, "ftl.depart")), "Depart after delivery boundary");
		ticks(w, 1);
		auto report = w.observe(101)->gameplay.at("mission_report");
		const std::string expected = receipt_tick <= 13 ? "prompt" : receipt_tick <= 27 ? "late" : "expired";
		require(report.at("result") == "success" && report.at("assessments")[0].at("result") == expected,
		        "Delivery grading independent of rescue and latched at receipt");
		require(network::read_observation(network::observation_json(*w.observe(101)), catalog)
		                .gameplay.at("mission_report") == report,
		        "Independent assessments survive remote projection");
		resumed.restore(catalog, w.save());
		ticks(w, 3);
		ticks(resumed, 3);
		require(w.save() == resumed.save() && w.observe(101)->gameplay.at("mission_report") == report,
		        "Departure freezes independent results");
		checkpoint(w);
	}
}

void swept_approach_cases()
{
	auto source = ftl_fixture();
	source["entities"][0]["motion_model"] = "inertial";
	source["entities"][0]["collision"] = {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 1000}};
	source["maps"][0]["entities"][1]["y_mm"] = 100;
	source["maps"][0]["entities"][1]["x_mm"] = 0;
	source["maps"][0]["regions"] =
	    Json::parse(R"([{"id":"halfway","shape":"circle","x_mm":10000,"y_mm":0,"radius_mm":5000}])");
	source["maps"][0]["scenario"] = Json::parse(
	    R"({"tree":"test.approach","bindings":{"actor":{"entity":1},"objective":"approach"},"events":["distress"],"objectives":[{"id":"approach","type":"reach_region","region":"halfway","actor":{"entity":1},"recipients":{"ship":{"entity":1}},"title_key":"Approach"}]})");
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.approach","context":"scenario","initial":"approach","states":[{"id":"approach","entry":[{"action":"add_objective","arguments":["approach",{"observation":"self.entity"},"approach"]}],"transitions":[{"target":"distress","conditions":[{"left":{"observation":"objective.state"},"op":"eq","right":2}]}]},{"id":"distress","entry":[{"action":"emit_scenario_event","arguments":["distress",0,false,""]}]}]})"));
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map");
	ticks(w, 2);
	auto other = w.save();
	for (auto &b : other["bodies"])
		if (b["body"]["id"] == 2)
			b["body"]["velocity"]["x"] = 600000;
	w.restore(catalog, other);
	ticks(w, 1);
	require(status(w, "approach") == 1, "Other hulls crossing the radius cannot trigger the player approach");
	auto fast = w.save();
	for (auto &b : fast["bodies"])
		if (b["body"]["id"] == 1)
			b["body"]["velocity"]["x"] = 600000;
	w.restore(catalog, fast);
	World resumed;
	resumed.restore(catalog, fast);
	ticks(w, 10);
	ticks(resumed, 10);
	require(status(w, "approach") == 2 && w.save() == resumed.save(),
	        "Swept crossing triggers even when both endpoints are outside, including save/resume");
	const auto triggered = w.save();
	int distress = 0;
	for (const auto &event : triggered.at("scenario").at("runtime").at("events"))
		if (event.at("name") == "distress")
			++distress;
	require(distress == 1, "Distress trigger emits once");
	checkpoint(w);
}
