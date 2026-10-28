#pragma once
namespace phoenix::test
{
inline void power_panel_cases()
{
	auto source = controller_session_fixture();
	auto &ship = source["entities"][0];
	ship["resources"] = Json::parse(R"json({
 "stores":[{"id":"main","capacity":1000,"initial":600,"charge_rate":700,"discharge_rate":700},
           {"id":"load","capacity":400,"initial":200,"charge_rate":300,"discharge_rate":300,"group":"loads"}],
 "groups":[{"id":"loads","budget_rate":300}],
 "generators":[{"id":"plant","store":"main","maximum_rate":700,"response_rate":700,"initial_output":0}],
 "links":[{"id":"feed","from":"main","to":"load","capacity_rate":300}],
 "coolant_sources":[{"id":"tank","initial":500,"capacity":500,"removal_rate":40}],
 "cooling_links":[{"id":"cooling","source":"tank","store":"load","capacity_rate":40,"allocation_rate":0}]
 })json");
	ship["systems"]["port"]["power_store"] = "load";
	ship["systems"]["starboard"]["power_store"] = "load";
	ship["systems"]["reactor"] = {{"kind", "generator"}, {"generator", "plant"}};
	ship["systems"]["distribution"] = {{"kind", "distribution"}, {"group", "loads"}};
	ship["systems"]["cooler"] = {{"kind", "coolant"}, {"cooling_link", "cooling"}};
	ship["systems"]["cell"] = {{"kind", "storage"}, {"power_store", "load"}};
	for (const auto *id : {"reactor", "distribution", "cooler", "cell"})
		ship["stations"][0]["consoles"][0]["feeds"].push_back(id);
	auto &layout = ship["control_layout"];
	layout["station_complexity"] = true;
	layout["controllers"].push_back(Json::parse(R"json({"id":"power","console":"drive","fallback":"hold","commands":[
 {"id":"output","action":"generator_output","consumers":["reactor"]},
 {"id":"budget","action":"group_allocation","initial":300,"consumers":["distribution"]},
 {"id":"cooling","action":"cooling_allocation","consumers":["cooler"],"individual_stars":3},
 {"id":"charge","action":"charge_limit","consumers":["cell"]},
 {"id":"intent","action":"intent_integer","minimum":0,"maximum":300,"initial":50,"consumers":[],"ui_system":"distribution"}
 ]})json"));
	for (auto &profile : layout["profiles"])
		profile["consoles"]["drive"] = {{"human", {"propulsion", "power"}}};
	layout["profiles"][0]["stars"] = 3;
	layout["profiles"][1]["stars"] = 2;
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
	ConsoleModel model;
	model.refresh(session);
	const auto board = model.observation()->gameplay.at("power");
	require(board.at("reserves").size() == 2 && board.at("coolant").size() == 1 && board.at("groups").size() == 1,
	        "shared power stores and coolant sources are projected once, with their real group");
	require(board.at("groups")[0].at("energy") == 200 && board.at("groups")[0].at("capacity") == 400,
	        "group battery reading aggregates real member reserves without counting fittings twice");
	require(session->claim_console(2, {model.observation()->own.id, "observer"}),
	        "claim a Console without power feeds");
	const auto unprivileged = session->console_snapshot(2);
	require(unprivileged && unprivileged->gameplay.at("power").at("reserves").empty() &&
	            unprivileged->gameplay.at("power").at("groups").empty(),
	        "power board does not expose resource reserves outside the Console's permitted feeds");
	const auto detailed = power_settings(*model.observation());
	require(std::any_of(detailed.begin(), detailed.end(),
	                    [](const auto &s)
	                    {
		                    return s.action == "group_allocation" && s.maximum == 300;
	                    }),
	        "slider travel uses the fitted allocation rating rather than the command protocol's broad numeric limit");
	require(std::any_of(detailed.begin(), detailed.end(),
	                    [](const auto &s)
	                    {
		                    return s.coolant;
	                    }),
	        "detailed engineering exposes the permitted coolant slider");
	require(std::any_of(detailed.begin(), detailed.end(),
	                    [](const auto &s)
	                    {
		                    return s.action == "intent_integer" && s.subject == "distribution";
	                    }),
	        "abstract allocation controls use an authored physical presentation subject");
	require(model.command("generator_output", 137, "controller/power/output") &&
	            model.command("intent_integer", 123, "controller/power/intent"),
	        "power sliders admit intermediate values through the real channel ranges");
	session->tick();
	model.refresh(session);
	bool applied = false;
	for (const auto &c : model.observation()->gameplay.at("controllers"))
		if (c.at("id") == "power")
			applied =
			    c.at("commands").at("output").at("value") == 137 && c.at("commands").at("intent").at("value") == 123;
	require(applied, "slider requests reach authoritative Controller settings");
	require(!model.command("generator_output", 701, "controller/power/output"),
	        "slider cannot exceed the fitted range");
	require(model.command("preset", 1), "select lower-complexity engineering");
	session->tick();
	model.refresh(session);
	const auto reduced = power_settings(*model.observation());
	require(std::none_of(reduced.begin(), reduced.end(),
	                     [](const auto &s)
	                     {
		                     return s.coolant;
	                     }) &&
	            !model.command("cooling_allocation", 17, "controller/power/cooling"),
	        "complexity downgrade removes the coolant control and refuses stale slider input");
}
} // namespace phoenix::test
