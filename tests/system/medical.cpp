#include "../support/assertions.hpp"
#include "../support/away_fixture.hpp"
#include "../support/combat_fixture.hpp"
#include <algorithm>
#include <iostream>
import phoenix.simulation;
import phoenix.console_controls;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
#include "../support/machinery_baseline.hpp"
namespace
{
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
Json order_tree(const std::string &id, const std::string &action)
{
	return {{"id", id},
	        {"context", "team"},
	        {"initial", "work"},
	        {"states",
	         Json::array(
	             {{{"id", "work"},
	               {"running", Json::array({{{"action", action}}})},
	               {"transitions", Json::array({{{"target", "done"},
	                                             {"conditions", Json::array({{{"left", {{"observation", "work_done"}}},
	                                                                          {"op", "eq"},
	                                                                          {"right", true}}})}}})}},
	              {{"id", "done"}, {"entry", Json::array({{{"action", "complete"}}})}}})}};
}
Json medical_fixture()
{
	auto s = away_fixture();
	auto target = s["entities"][0];
	target["id"] = "test.patient";
	target["personnel"]["officers"][0]["condition"] = "minor";
	auto &ship = s["entities"][0];
	s["trees"].push_back(order_tree("test.treat", "treat"));
	s["trees"].push_back(order_tree("test.recover", "recover"));
	s["trees"].push_back(order_tree("test.custom", "move"));
	ship["systems"]["medical"] = {{"kind", "team"},
	                              {"type", "clinical"},
	                              {"capabilities", {"medical"}},
	                              {"compartment", "workshop"},
	                              {"treatment_ticks", 8},
	                              {"stabilise_ticks", 4},
	                              {"orders", Json::array({{{"id", "treat"},
	                                                       {"label", "Treat patients"},
	                                                       {"target", "patients"},
	                                                       {"capability", "medical"},
	                                                       {"tree", "test.treat"}},
	                                                      {{"id", "recover"},
	                                                       {"label", "Recover patients"},
	                                                       {"target", "patients"},
	                                                       {"capability", "medical"},
	                                                       {"tree", "test.recover"}},
	                                                      {{"id", "survey"},
	                                                       {"label", "Survey compartment"},
	                                                       {"target", "compartments"},
	                                                       {"tree", "test.custom"}}})}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "medical"},
	     {"console", "observer"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"medical"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back("medical");
	ship["stations"][1]["consoles"][0]["feeds"].push_back("medical");
	auto &p = ship["personnel"];
	p["duties"].push_back({{"id", "medical-controller"},
	                       {"label", "Medical coordination"},
	                       {"controller", "medical"},
	                       {"console", "observer"},
	                       {"department", "engineering"},
	                       {"compartment", "workshop"},
	                       {"effects", Json::object()}});
	p["duties"].push_back({{"id", "medical-team"},
	                       {"label", "Clinical team"},
	                       {"team", "medical"},
	                       {"department", "engineering"},
	                       {"compartment", "workshop"},
	                       {"effects", {{"medical", {"medical"}}}}});
	p["traits"].push_back({{"id", "medical"}, {"label", "Medical specialist"}, {"bonuses", {{"medical", 1000}}}});
	p["facilities"] =
	    Json::array({{{"id", "bay"}, {"compartment", "workshop"}, {"capacity", 1}, {"recovery_ticks", 6}}});
	p["officers"][0]["condition"] = "minor";
	p["officers"][1]["condition"] = "severe";
	s["entities"].push_back(target);
	s["maps"][0]["entities"][0]["definition"] = "test.patient";
	s["maps"][0]["away_missions"][0]["slots"][0]["capability"] = "repair";
	s["maps"][0]["away_missions"][0]["slots"].push_back(
	    {{"id", "medical"}, {"kind", "team"}, {"capability", "medical"}, {"required", false}});
	return s;
}
OperatingCommand away_order(const World &w, const std::string &action, const std::string &text = {},
                            std::uint64_t claim = 101, const std::string &controller = "dispatch")
{
	auto o = w.observe(claim);
	const auto &m = o->gameplay.at("away_missions")[0];
	OperatingCommand c{o->own.id, "away." + action, m.at("revision"), "job"};
	c.console = o->console;
	c.controller = controller;
	c.text = text;
	for (const auto &ctrl : o->gameplay.at("controllers"))
		if (ctrl["id"] == controller)
			c.generation = ctrl.at("generation");
	return c;
}
Json &saved_body(Json &save, EntityId id)
{
	for (auto &b : save["bodies"])
		if (b["body"]["id"] == id)
			return b;
	throw std::runtime_error("missing body");
}
Json officer(const World &w, EntityId id, int index)
{
	auto s = w.save();
	return saved_body(s, id)["personnel"]["officers"][index];
}
OperatingCommand command(const World &w, const std::string &action, const std::string &text)
{
	auto view = w.observe(102);
	for (const auto &t : view->gameplay.at("teams"))
		if (t["id"] == "medical")
			for (const auto &c : view->gameplay.at("controllers"))
				if (c["id"] == "medical")
				{
					OperatingCommand o{view->own.id, "team." + action, t.at("revision"), "medical"};
					o.text = text;
					o.controller = "medical";
					o.generation = c.at("generation");
					return o;
				}
	throw std::runtime_error("missing medical team");
}
void claim(World &w)
{
	require((w.observe(101).has_value() || w.claim(101, 2, "drive")) &&
	            (w.observe(102).has_value() || w.claim(102, 2, "observer")),
	        "claim clinical fixture");
}
Json casualty_fixture(const std::string &condition, bool lethal = false)
{
	auto s = combat_fixture();
	auto &ship = s["entities"][0];
	ship["personnel"] = {{"departments", Json::array({{{"id", "crew"}, {"label", "Crew"}, {"station", "operations"}}})},
	                     {"traits", Json::array()},
	                     {"duties", Json::array()},
	                     {"officers", Json::array()}};
	auto &p = ship["personnel"];
	for (const auto &c : ship["control_layout"]["controllers"])
		p["duties"].push_back({{"id", c["id"]},
		                       {"label", c["id"]},
		                       {"controller", c["id"]},
		                       {"console", c["console"]},
		                       {"department", "crew"},
		                       {"compartment", "fore"},
		                       {"effects", Json::object()}});
	for (const auto *team : {"team-a", "team-b"})
		p["duties"].push_back({{"id", std::string("duty-") + team},
		                       {"label", team},
		                       {"team", team},
		                       {"department", "crew"},
		                       {"compartment", "fore"},
		                       {"effects", Json::object()}});
	p["officers"].push_back({{"id", "watch"},
	                         {"name", "Watch officer"},
	                         {"department", "crew"},
	                         {"compartment", "fore"},
	                         {"duty", "radar"},
	                         {"traits", Json::array()}});
	p["casualties"] = {{"significant_damage", 1},
	                   {"lethal_damage", 1},
	                   {"lethal", lethal},
	                   {"heavy_below_permille", 900},
	                   {"stun_permille", condition == "stunned" ? 1000 : 0},
	                   {"minor_permille", condition == "minor" ? 1000 : 0},
	                   {"severe_permille", condition == "severe" ? 1000 : 0},
	                   {"killed_permille", condition == "dead" ? 1000 : 0}};
	return s;
}
void shoot(World &w)
{
	auto view = w.observe(201);
	for (const auto &c : view->gameplay.at("controllers"))
		if (c["id"] == "gun")
		{
			OperatingCommand o{1, "", 2, "", "gun", "target", c.at("generation")};
			require(w.submit(201, o), "fire at casualty fixture");
			return;
		}
	throw std::runtime_error("missing gun");
}
void deploy_clinical_party(World &w)
{
	ticks(w, 4);
	require(w.submit(101, away_order(w, "open")), "open clinical regression mission");
	ticks(w, 1);
	require(w.submit(102, away_order(w, "assign", "repair/team:team", 102, "team")), "stage repair team");
	ticks(w, 1);
	require(w.submit(102, away_order(w, "assign", "medical/team:medical", 102, "medical")), "stage clinical team");
	ticks(w, 1);
	require(w.submit(101, away_order(w, "transport", "transport", 101, "transport")), "select clinical transport");
	ticks(w, 7);
	require(w.submit(101, away_order(w, "send")), "deploy clinical regression party");
	for (int i = 0; i < 20 && w.observe(101)->gameplay.at("away_missions")[0]["phase"] != "deployed"; ++i)
		w.tick();
	require(w.observe(101)->gameplay.at("away_missions")[0]["phase"] == "deployed",
	        "clinical party physically arrived");
}
void assisted_team_destinations()
{
	auto source = medical_fixture();
	auto &ship = source["entities"][0];
	ship["personnel"]["officers"][1]["condition"] = "healthy";
	ship["combat"]["decks"][0]["compartments"][0]["initial_hp"] = 30;
	ship["combat"]["decks"][0]["compartments"][1]["initial_hp"] = 20;
	auto &layout = ship["control_layout"];
	layout["station_complexity"] = true;
	layout["manual_workloads"] = {{"observer", {{"team_assignment", 1}, {"medical", 3}}}};
	layout["profiles"][0]["stars"] = 1;
	layout["profiles"][0]["consoles"]["observer"] = {{"human", Json::array()}, {"ai", {"team", "medical"}}};
	for (auto &controller : layout["controllers"])
		if (controller["id"] == "team")
		{
			controller.erase("fallback");
			controller["tree"] = "test.local-repair";
			controller["commands"].push_back(
			    {{"id", "assignment"}, {"action", "repair_compartment"}, {"initial", 0}, {"consumers", {"team"}}});
		}
	source["trees"].push_back(Json::parse(R"json({"id":"test.local-repair","context":"controller",
 "command_bindings":[{"channel":"assignment","action":"repair_compartment"}],"initial":"work",
 "states":[{"id":"work","running":[{"action":"write.assignment","arguments":[0]}]}]})json"));
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	ticks(world, 10);
	const auto waiting = world.save();
	auto waiting_copy = waiting;
	require(saved_body(waiting_copy, 2)["combat"]["compartments"][0]["hp"] == 30 &&
	            officer(world, 2, 0)["treatment"] == 0,
	        "Occupied one-star station does not deploy repair or Medical to another room automatically");
	auto send = [&](const std::string &team, const std::string &text)
	{
		const auto view = world.observe(102);
		OperatingCommand c{2, "team.order", 0, team};
		c.console = "observer";
		c.controller = team;
		c.text = text;
		for (const auto &row : view->gameplay.at("teams"))
			if (row.at("id") == team)
				c.value = row.at("revision");
		for (const auto &controller : view->gameplay.at("controllers"))
			if (controller.at("id") == team)
				c.generation = controller.at("generation");
		return c;
	};
	const auto medical = send("medical", "auto/bridge"), repair = send("team", "auto/bridge");
	const auto projected = network::read_observation(network::observation_json(*world.observe(102)), catalog);
	require(
	    projected.gameplay.at("teams") == world.observe(102)->gameplay.at("teams") &&
	        eligible_console_command(projected, Json{{"widgets", Json::array()}}, Json::object(), medical).has_value(),
	    "Remote one-star projection admits department destination authority on an AI team Controller");
	require(!world.submit(102, send("medical", "treat/bridge")),
	        "Destination authority does not grant detailed clinical orders");
	auto foreign = repair;
	foreign.controller = "transport";
	require(!world.submit(102, foreign), "Another Controller cannot command the repair team");
	require(world.submit(102, medical) && world.submit(102, repair), "Station chooses both team destinations manually");
	world.tick();
	require(officer(world, 2, 0)["treatment"] == 0, "Medical work waits for physical compartment arrival");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 20);
	ticks(resumed, 20);
	require(world.save() == resumed.save(),
	        "Manual destinations and automatic local work retain in-flight continuation");
	auto completed = world.save();
	require(saved_body(completed, 2)["combat"]["compartments"][0]["hp"] > 30 &&
	            officer(world, 2, 0)["condition"] == "healthy",
	        "Repair and Medical choose useful work automatically at the assigned compartment");
	ticks(world, 100);
	auto finished = world.save();
	const auto &repair_state = saved_body(finished, 2)["combat"]["systems"]["team"];
	require(repair_state["location"] == 0 && repair_state["destination"] == 0 &&
	            saved_body(finished, 2)["combat"]["compartments"][1]["hp"] < 100,
	        "A finished team stays at its manual destination instead of chasing another damaged room");
	require(world.submit(102, send("medical", "auto/workshop")), "Reposition Medical to an empty compartment");
	ticks(world, 20);
	const auto repositioned = world.observe(102);
	for (const auto &team : repositioned->gameplay.at("teams"))
		if (team.at("id") == "medical")
			require(team.at("location") == 1 && team.at("assigned_compartment") == 1,
			        "Medical physically follows its destination even without a patient there");
	require(world.submit(101, away_order(world, "open")), "Activate away work with manual onboard destinations");
	ticks(world, 6);
	const auto manifest = world.save().at("away_missions")[0].at("assignments");
	require(manifest.at("repair") == "team:team" && manifest.at("medical") == "team:medical",
	        "Manual onboard destinations preserve automatic department-owned away staffing");
}
void team_effect_actions()
{
	auto source = medical_fixture();
	source["properties"] = Json::parse(R"json([
 {"id":"test.team.events","kind":"quantity","unit":"events","default":0,"maximum":1000},
 {"id":"test.team.fuel","kind":"quantity","unit":"fuel","default":10,"maximum":1000}
 ])json");
	auto &ship = source["entities"][0];
	ship["properties"] = {{"test.team.events", Json::object()}};
	ship["systems"]["medical"]["properties"] = {{"test.team.fuel", Json::object()}};
	ship["combat"]["decks"][0]["compartments"][0]["properties"] = {{"test.team.events", Json::object()}};
	source["effects"] = Json::parse(R"json([
 {"id":"test.team.success","kind":"operations","costs":[{"payer":"source","property":"test.team.fuel","amount":2}],
  "operations":[{"kind":"property_add","target":"target","property":"test.team.events","amount":1}]},
 {"id":"test.team.failure","kind":"operations","costs":[{"payer":"source","property":"test.team.fuel","amount":100}],
  "operations":[{"kind":"property_add","target":"target","property":"test.team.events","amount":100}]},
 {"id":"test.team.target","kind":"operations","costs":[{"payer":"source","property":"test.team.fuel","amount":2}],
  "operations":[{"kind":"property_add","target":"target","property":"test.team.events","amount":3}]}
 ])json");
	for (auto &tree : source["trees"])
		if (tree["id"] == "test.custom")
		{
			tree["states"][0].erase("transitions");
			tree["states"][0]["running"].push_back({{"call", "test.team.effects"}});
		}
	source["trees"].push_back(Json::parse(R"json({"id":"test.team.effects","context":"team","initial":"apply",
 "states":[{"id":"apply","running":[
  {"action":"apply_effect_group","arguments":["test.team.success","self"]},
  {"action":"apply_effect_group","arguments":["test.team.failure","self"]},
  {"action":"apply_effect_group","arguments":["test.team.target","target"]}
 ]}]})json"));
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	require(world.submit(102, command(world, "order", "survey/bridge")), "start team effect order");
	world.tick();
	const auto value = [](const World &w, const std::string &property, const std::string &equipment = {},
	                      std::int64_t compartment = -1)
	{
		const auto state = w.save();
		for (const auto &row : state.at("custom_properties"))
			if (row.at("entity") == 2 && row.at("equipment") == equipment && row.at("compartment") == compartment)
				return row.at("values").at(property).get<std::int64_t>();
		throw std::runtime_error("missing team effect property");
	};
	require(value(world, "test.team.events") == 1 && value(world, "test.team.fuel", "medical") == 6 &&
	            value(world, "test.team.events", {}, 0) == 3,
	        "referenced team effects bind source equipment and selected compartment, with atomic funding");
	auto saved = world.save();
	require(saved_body(saved, 2)["combat"]["systems"]["medical"]["activity"] == "move",
	        "shared effect action preserves the native team activity");
	require(saved.at("rules").at("results").size() == 1, "failed team effect produces one bounded failure receipt");
	World restored;
	restored.restore(catalog, saved);
	ticks(world, 5);
	ticks(restored, 5);
	require(world.save() == restored.save() && value(world, "test.team.events") == 1 &&
	            value(world, "test.team.fuel", "medical") == 6 && value(world, "test.team.events", {}, 0) == 3 &&
	            world.save().at("rules").at("results").size() == 1,
	        "successful and failed team effects do not replay during running or restoration");
	for (const auto *invalid : {"group", "role"})
	{
		auto bad = source;
		for (auto &tree : bad["trees"])
			if (tree["id"] == "test.team.effects")
				tree["states"][0]["running"][0]["arguments"][std::string(invalid) == "group" ? 0 : 1] = "missing";
		bool rejected = false;
		try
		{
			(void)load(bad);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		require(rejected, "production loading rejects invalid team effect reference or target role");
	}
}

void finish_clinical_order(World &w)
{
	for (int i = 0; i < 60; ++i)
	{
		w.tick();
		auto save = w.save();
		if (saved_body(save, 2)["combat"]["systems"]["medical"]["order"] == "")
			return;
	}
	throw std::runtime_error("Clinical regression order did not complete");
}
void clinical_execution_regressions(Json &trace)
{
	for (int emission : {0, 1, 2})
	{
		auto source = medical_fixture();
		for (auto &tree : source["trees"])
			if (tree["id"] == "test.treat" && emission > 0)
			{
				tree["states"][0]["entry"] = tree["states"][0]["running"];
				if (emission == 1)
					tree["states"][0].erase("running");
			}
		source["entities"][0]["systems"]["medical"]["treatment_ticks"] = 12;
		auto alternate = source["entities"][0]["systems"]["medical"]["orders"][0];
		alternate["id"] = "severe-only";
		alternate["patient_states"] = {"severe"};
		source["entities"][0]["systems"]["medical"]["orders"].push_back(alternate);
		// Remote patients are reserves so they remain at their physical clinical location.
		source["entities"][1]["personnel"]["officers"][0]["duty"] = "";
		source["entities"][1]["personnel"]["officers"][0]["compartment"] = "workshop";
		const auto catalog = load(source);
		World onboard;
		onboard.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(onboard);
		require(onboard.submit(102, command(onboard, "order", "treat/bridge")), "order retained onboard treatment");
		onboard.tick();
		require(officer(onboard, 2, 0)["treatment"] == 0, "onboard treatment waits for physical arrival");
		for (int i = 0; i < 10 && officer(onboard, 2, 0)["treatment"] == 0; ++i)
			onboard.tick();
		require(officer(onboard, 2, 0)["treatment"] == 1000, "onboard action performs one work increment per tick");
		World onboard_resume;
		onboard_resume.restore(catalog, onboard.save());
		ticks(onboard, 2);
		ticks(onboard_resume, 2);
		require(officer(onboard, 2, 0)["treatment"] == 3000 && onboard.save() == onboard_resume.save(),
		        "entry/running onboard action retains intent across ticks and restore");
		finish_clinical_order(onboard);
		require(officer(onboard, 2, 0)["condition"] == "healthy", "retained onboard treatment completes");

		World away;
		away.load(catalog, "test.map", {{"crew", "test.away"}});
		auto ready = away.save();
		for (auto &o : saved_body(ready, 2)["personnel"]["officers"])
			o["condition"] = "healthy";
		away.restore(catalog, ready);
		claim(away);
		deploy_clinical_party(away);
		auto travelling = away.save();
		const auto &travel_care = travelling["away_missions"][0]["care"]["medical"];
		require(travel_care["travel"] > 0 && travel_care["activity"] == "treat" &&
		            officer(away, 1, 0)["treatment"] == 0,
		        "away tree starts during travel without treating remotely");
		World travel_resume;
		travel_resume.restore(catalog, travelling);
		for (int i = 0; i < 10 && officer(away, 1, 0)["treatment"] == 0; ++i)
		{
			away.tick();
			travel_resume.tick();
		}
		require(officer(away, 1, 0)["treatment"] == 1000 && away.save() == travel_resume.save(),
		        "away entry/running action survives travel and increments once on arrival");
		auto partial = away.save();
		World resumed;
		resumed.restore(catalog, partial);
		ticks(away, 2);
		ticks(resumed, 2);
		require(officer(away, 1, 0)["treatment"] == 3000 && away.save() == resumed.save(),
		        "away retained action and patient progress resume identically");
		ticks(away, 20);
		require(officer(away, 1, 0)["condition"] == "healthy" &&
		            away.save()["away_missions"][0]["care"]["medical"]["done"] == true,
		        "away entry/running treatment reaches authored completion");
		trace.push_back(away.save());
		for (auto old : {travelling, partial})
		{
			old["away_missions"][0]["schema"] = 4;
			bool rejected = false;
			try
			{
				World retired;
				retired.restore(catalog, old);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "retired away clinical schema rejected without restarting patient work");
		}
		auto invalid = partial;
		invalid["away_missions"][0]["care"]["medical"]["activity"] = "unknown";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, invalid);
		    },
		    "unknown retained away activity rejected");
		// A second order shares the same program, so its continuation remains valid.
		auto selected = partial;
		selected["away_missions"][0]["care"]["medical"]["order"] = "severe-only";
		World filtered;
		filtered.restore(catalog, selected);
		ticks(filtered, 20);
		require(officer(filtered, 1, 0)["condition"] == "minor" && officer(filtered, 1, 0)["treatment"] == 1000,
		        "away care resolves the saved order and honours its patient-state filter");
	}
}
void patient_selection_regressions()
{
	auto source = medical_fixture();
	auto &orders = source["entities"][0]["systems"]["medical"]["orders"];
	orders[0]["patient_states"] = {"minor", "severe"};
	orders[1]["patient_states"] = {"stabilised"};
	source["trees"].push_back(order_tree("test.stabilise", "stabilise"));
	orders.push_back({{"id", "stabilise"},
	                  {"label", "Stabilise"},
	                  {"target", "patients"},
	                  {"patient_states", {"minor", "severe", "stabilised"}},
	                  {"tree", "test.stabilise"}});
	auto minor_order = orders[0];
	minor_order["id"] = "minor";
	minor_order["patient_states"] = {"minor"};
	orders.push_back(minor_order);
	for (const auto *id : {"minor", "priority"})
		source["entities"][0]["personnel"]["officers"].push_back({{"id", id},
		                                                          {"name", id},
		                                                          {"department", "engineering"},
		                                                          {"compartment", "bridge"},
		                                                          {"condition", "minor"},
		                                                          {"traits", Json::array()}});
	const auto catalog = load(source);
	World seed;
	seed.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(seed);
	auto initial = seed.save();
	auto &patients = saved_body(initial, 2)["personnel"]["officers"];
	for (auto &p : patients)
	{
		p["duty"] = "";
		p["present"] = false;
		p["location"] = 0;
	}
	patients[0]["condition"] = "severe";
	patients[0]["stabilised"] = true;
	World recovery;
	recovery.restore(catalog, initial);
	require(!recovery.submit(102, command(recovery, "order", "recover/bridge/chief")),
	        "individual priority must match recovery order filter");
	require(recovery.submit(102, command(recovery, "order", "recover/bridge/nav")),
	        "select stabilised recovery patient");
	finish_clinical_order(recovery);
	require(officer(recovery, 2, 0)["facility"] == "bay" && officer(recovery, 2, 1)["stabilised"] == false,
	        "recovery escorts stabilised patient despite higher-severity unstabilised neighbour");
	World stabilise;
	stabilise.restore(catalog, initial);
	require(stabilise.submit(102, command(stabilise, "order", "stabilise/bridge/minor")),
	        "a broad order filter cannot override stabilisation action restrictions");
	finish_clinical_order(stabilise);
	require(officer(stabilise, 2, 1)["stabilised"] == true && officer(stabilise, 2, 2)["condition"] == "minor" &&
	            officer(stabilise, 2, 2)["treatment"] == 0,
	        "stabilisation completes without treating or waiting for minor/excluded patients");
	World minor;
	minor.restore(catalog, initial);
	require(!minor.submit(102, command(minor, "order", "minor/bridge/chief")), "minor order rejects severe priority");
	require(minor.submit(102, command(minor, "order", "minor/bridge/minor")), "prioritise one eligible minor patient");
	minor.tick();
	for (int i = 0; i < 10 && officer(minor, 2, 2)["treatment"] == 0; ++i)
		minor.tick();
	require(officer(minor, 2, 2)["treatment"] == 1000 && officer(minor, 2, 3)["treatment"] == 0 &&
	            officer(minor, 2, 1)["treatment"] == 0,
	        "individual priority breaks severity ties only among order-eligible patients");
	finish_clinical_order(minor);
	require(officer(minor, 2, 2)["condition"] == "healthy" && officer(minor, 2, 3)["condition"] == "healthy" &&
	            officer(minor, 2, 1)["stabilised"] == false,
	        "filtered treatment completes while excluded severe casualty remains");
	World triage;
	triage.restore(catalog, initial);
	require(!triage.submit(102, command(triage, "order", "treat/bridge/nav")),
	        "treatment excludes stabilised priority");
	require(triage.submit(102, command(triage, "order", "treat/bridge/minor")), "order mixed-severity triage");
	triage.tick();
	for (int i = 0; i < 15 && officer(triage, 2, 1)["stabilised"] == false; ++i)
		triage.tick();
	require(officer(triage, 2, 1)["stabilised"] == true && officer(triage, 2, 2)["treatment"] == 0,
	        "severe triage precedes individual minor priority");
}
void property_work_regressions()
{
	for (const auto delta : {1000, -250})
	{
		auto source = medical_fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["medical"]["injured_effectiveness_permille"] = 250;
		ship["systems"]["medical"]["treatment_ticks"] = 30;
		auto &people = ship["personnel"];
		people["officers"][0]["duty"] = "";
		people["officers"][0]["compartment"] = "workshop";
		people["officers"][1]["condition"] = "healthy";
		people["traits"][2]["bonuses"]["medical"] = delta;
		people["officers"].push_back({{"id", "medic"},
		                              {"name", "Medic"},
		                              {"department", "engineering"},
		                              {"duty", "medical-team"},
		                              {"compartment", "workshop"},
		                              {"traits", {"medical"}}});
		ship["logistics"] = Json::parse(R"({"controller":"medical","materials":[{"id":"medical"}],
          "stores":[{"id":"kit","system":"medical","compartment":"workshop","capacity":2,"accepts":["medical"],"initial":{"medical":1}}],
          "consumers":[{"system":"medical","store":"kit","costs":{"treat":{"medical":1}}}]})");
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(world);
		auto saved = world.save();
		saved_body(saved, 2)["combat"]["systems"]["medical"]["condition"] = "injured";
		world.restore(catalog, saved);
		require(world.submit(102, command(world, "order", "treat/workshop/nav")), "order additive clinical work");
		world.tick();
		if (delta > 0)
		{
			for (int i = 0; i < 10 && officer(world, 2, 0)["treatment"] == 0; ++i)
				world.tick();
			require(officer(world, 2, 0)["treatment"] == 1250,
			        "officer effect and team injury combine additively at physical treatment");
			World resumed;
			resumed.restore(catalog, world.save());
			ticks(world, 4);
			ticks(resumed, 4);
			require(world.save() == resumed.save(),
			        "paid additive treatment continues with derived officer effects once");
		}
		else
		{
			ticks(world, 8);
			require(officer(world, 2, 0)["treatment"] == 0 &&
			            world.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("medical") ==
			                1,
			        "zero clinical work neither advances nor pays a new indivisible treatment");
			saved = world.save();
			saved_body(saved, 2)["combat"]["systems"]["medical"]["condition"] = "healthy";
			world.restore(catalog, saved);
			world.tick();
			require(officer(world, 2, 0)["treatment"] == 750 &&
			            world.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("medical") ==
			                0,
			        "recovery resumes the existing task and pays its first work once");
		}
	}
}
void property_continuation_trace(Json &trace)
{
	auto source = medical_fixture();
	auto &ship = source["entities"][0];
	auto &people = ship["personnel"];
	people["officers"][0]["duty"] = "";
	people["officers"][0]["compartment"] = "workshop";
	people["officers"][1]["condition"] = "healthy";
	people["officers"].push_back({{"id", "medic"},
	                              {"name", "Medic"},
	                              {"department", "engineering"},
	                              {"duty", "medical-team"},
	                              {"compartment", "workshop"},
	                              {"traits", {"medical"}}});
	ship["systems"]["medical"]["treatment_ticks"] = 200;
	source["effects"] = Json::parse(R"json([{"id":"test.clinical-aid","kind":"modifier","duration_ticks":40,
      "contributions":{"clinical":{"property":"medical","delta_permille":250}}}])json");
	source["trees"].push_back(Json::parse(R"json({"id":"test.clinical-aid-tree","context":"scenario","initial":"apply",
      "states":[{"id":"apply","entry":[{"action":"apply_modifier_effect",
        "arguments":[{"observation":"self.entity"},"medical","test.clinical-aid","aid"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.clinical-aid-tree"}, {"bindings", {{"actor", {{"slot", "crew"}}}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	claim(world);
	require(world.submit(102, command(world, "order", "treat/workshop/nav")), "order timed-effect clinical work");
	world.tick();
	for (int i = 0; i < 10 && officer(world, 2, 0)["treatment"] == 0; ++i)
		world.tick();
	const auto before = officer(world, 2, 0)["treatment"].get<std::int64_t>();
	world.tick();
	require(officer(world, 2, 0)["treatment"] == before + 2250,
	        "scenario and maintained officer effects combine on actual clinical work");
	trace.push_back(world.save());
	World resumed;
	resumed.restore(catalog, world.save());
	for (auto *current : {&world, &resumed})
	{
		OperatingCommand relieve{2, "personnel.relieve", 0, "medic"};
		relieve.generation = current->observe(102)->gameplay.at("personnel").at("revision");
		require(current->submit(102, relieve), "relieve clinical maintained source during timed effect");
		current->tick();
	}
	const auto relieved = officer(world, 2, 0)["treatment"].get<std::int64_t>();
	world.tick();
	resumed.tick();
	require(officer(world, 2, 0)["treatment"] == relieved + 1250 && world.save() == resumed.save(),
	        "relief removes only maintained work contribution across exact continuation");
	trace.push_back(world.save());
	ticks(world, 45);
	ticks(resumed, 45);
	const auto expired = officer(world, 2, 0)["treatment"].get<std::int64_t>();
	world.tick();
	resumed.tick();
	require(officer(world, 2, 0)["treatment"] == expired + 1000 && world.save() == resumed.save(),
	        "timed clinical effect expires while preserving completed work and continuation");
	trace.push_back(world.save());
}
void system_casualty_regressions()
{
	for (const auto *system : {"radar", "team-a"})
		for (int scenario = 0; scenario < 3; ++scenario)
		{
			const bool heavy = scenario != 1, lethal = scenario != 2;
			auto source = casualty_fixture("dead", lethal);
			auto &ship = source["entities"][0];
			ship["systems"][system]["faults"] = Json::array({{{"id", "failure"},
			                                                  {"symptom", "Failure"},
			                                                  {"diagnosis", "Failed component"},
			                                                  {"chance_permille", 0},
			                                                  {"damage_threshold", 1},
			                                                  {"effectiveness_permille", 500},
			                                                  {"repair_work", 100000}}});
			ship["personnel"]["officers"][0]["duty"] = std::string(system) == "radar" ? "radar" : "duty-team-a";
			const auto catalog = load(source);
			World battle;
			battle.load(catalog, "test.map");
			auto saved = battle.save();
			saved_body(saved, 2)["combat"]["faults"][system][0]["active"] = heavy;
			battle.restore(catalog, saved);
			require(battle.claim(201, 1, "drive"), "claim System-bound casualty fixture");
			shoot(battle);
			bool hit = false;
			for (int i = 0; i < 30 && !hit; ++i)
			{
				battle.tick();
				auto state = battle.save();
				hit = saved_body(state, 2)["combat"]["compartments"][0]["hp"] < 100;
			}
			require(
			    hit && (officer(battle, 2, 0)["condition"] == "dead") == (heavy && lethal),
			    "Controller/team duty uses bound System damage, still requiring prior heavy damage and lethal event");
		}
}
} // namespace

#include "../support/finite_team_cases.hpp"

int main(int argc, char **argv)
{
	try
	{
		assisted_team_destinations();
		team_effect_actions();
		finite_clinical_jobs();
		property_work_regressions();
		{
			auto source = medical_fixture();
			auto &ship = source["entities"][0];
			ship["systems"]["medical"]["orders"][0]["patient_states"] = {"minor", "severe"};
			ship["systems"]["medical"]["orders"][1]["patient_states"] = {"stabilised"};
			ship["control_layout"]["station_complexity"] = true;
			ship["control_layout"]["manual_workloads"] = {{"observer", {{"medical", 2}}}};
			ship["personnel"]["officers"][1]["compartment"] = "bridge";
			ship["personnel"]["officers"][1]["duty"] = "";
			ship["control_layout"]["profiles"][0]["stars"] = 1;
			auto detailed = ship["control_layout"]["profiles"][0];
			detailed["id"] = "clinical";
			detailed["stars"] = 2;
			ship["control_layout"]["profiles"].push_back(detailed);
			const auto clinical = ship["control_layout"]["profiles"].size() - 1;
			const auto catalog = load(source);
			World assigned;
			assigned.load(catalog, "test.map", {{"crew", "test.away"}});
			claim(assigned);
			require(assigned.submit(102, command(assigned, "order", "auto/workshop")),
			        "One-star compartment assignment admitted");
			ticks(assigned, 30);
			require(officer(assigned, 2, 1)["condition"] == "severe" &&
			            !officer(assigned, 2, 1)["stabilised"].get<bool>(),
			        "Assigned Medical does not leave its compartment for another casualty");
			require(!assigned.submit(102, command(assigned, "order", "treat/bridge")),
			        "One star rejects direct clinical orders despite team authority");
			require(assigned.submit(102, command(assigned, "order", "auto/bridge")),
			        "Reassign Medical to the casualty compartment");
			ticks(assigned, 20);
			World resumed;
			resumed.restore(catalog, assigned.save());
			ticks(assigned, 180);
			ticks(resumed, 180);
			require(assigned.save() == resumed.save(), "Assigned clinical care survives continuation");
			require(officer(assigned, 2, 1)["condition"] == "healthy",
			        "Assigned Medical chooses treatment then recovery");
			require(assigned.observe(102)->gameplay.at("teams")[0].at("assigned_compartment") == 0,
			        "Clinical assignment survives the recovery trip");
			require(assigned.submit(102, {2, "preset", static_cast<std::int64_t>(clinical)}),
			        "Select two-star clinical workload");
			ticks(assigned, 1);
			require(assigned.observe(102)->gameplay.at("teams")[0].at("patient_control") == true,
			        "Two stars exposes clinical orders and patient priorities");
		}
		{
			auto source = medical_fixture();
			source["entities"][0]["systems"]["medical"]["orders"][0]["patient_states"] = {"minor", "severe"};
			source["entities"][0]["systems"]["medical"]["orders"][1]["patient_states"] = {"stabilised"};
			auto &layout = source["entities"][0]["control_layout"];
			layout["station_complexity"] = true;
			layout["profiles"][0]["stars"] = 1;
			auto &support = layout["profiles"][0]["consoles"]["observer"];
			auto &human = support["human"];
			human.erase(std::find(human.begin(), human.end(), Json("medical")));
			support["ai"] = {"medical"};
			World assisted;
			assisted.load(load(source), "test.map", {{"crew", "test.away"}});
			claim(assisted);
			ticks(assisted, 5);
			require(!assisted.submit(102, command(assisted, "order", "treat/bridge")),
			        "One-star Medical refuses manual clinical orders");
			ticks(assisted, 200);
			require(officer(assisted, 2, 1)["condition"] == "healthy",
			        "Assisted Medical treats and recovers a severe casualty automatically");
			require(assisted.submit(101, away_order(assisted, "open")), "Open assisted away mission");
			ticks(assisted, 1);
			require(assisted.save().at("away_missions")[0].at("assignments").at("medical") == "team:medical",
			        "AI-operated Medical supplies its department's away contribution automatically");
		}
		{
			auto source = medical_fixture();
			auto &ship = source["entities"][0];
			for (auto &officer : ship["personnel"]["officers"])
			{
				officer["duty"] = "";
				officer["compartment"] = "workshop";
				officer["condition"] = "minor";
			}
			ship["logistics"] = Json::parse(R"({"controller":"medical","materials":[{"id":"medical"}],
              "stores":[{"id":"kit","system":"medical","compartment":"workshop","capacity":2,"accepts":["medical"],"initial":{"medical":1}}],
              "consumers":[{"system":"medical","store":"kit","costs":{"treat":{"medical":1},"stabilise":{"medical":1}}}]})");
			const auto catalog = load(source);
			World supplied;
			supplied.load(catalog, "test.map", {{"crew", "test.away"}});
			claim(supplied);
			ticks(supplied, 3);
			World resumed;
			resumed.restore(catalog, supplied.save());
			ticks(supplied, 20);
			ticks(resumed, 20);
			require(supplied.save() == resumed.save(), "Paid medical work resumes without charging twice");
			const auto a = officer(supplied, 2, 0), b = officer(supplied, 2, 1);
			require((a["condition"] == "healthy") != (b["condition"] == "healthy"),
			        "One medical supply heals one patient and depletion blocks the next");
		}
		{
			auto source = medical_fixture();
			source["entities"][0]["logistics"] = Json::parse(
			    R"({"controller":"medical","materials":[{"id":"medical"}],"stores":[{"id":"kit","system":"medical","compartment":"workshop","capacity":10,"accepts":["medical"]}]})");
			const auto catalog = load(source);
			World closed;
			closed.load(catalog, "test.map", {{"crew", "test.away"}});
			claim(closed);
			require(closed.submit(102, command(closed, "order", "treat/bridge")), "Clinical order begins real travel");
			ticks(closed, 1);
			auto route = [&](const std::string &mode)
			{
				const auto o = *closed.observe(102);
				OperatingCommand c{2, "logistics.route", o.gameplay.at("logistics").at("revision"), "bridge"};
				c.console = "observer";
				c.controller = "medical";
				c.text = "workshop/" + mode;
				for (const auto &ctrl : o.gameplay.at("controllers"))
					if (ctrl.at("id") == "medical")
						c.generation = ctrl.at("generation");
				return c;
			};
			require(closed.submit(102, route("close")), "Close a route during clinical transit");
			ticks(closed, 12);
			require(officer(closed, 2, 0)["treatment"] == 0 && officer(closed, 2, 0)["condition"] == "minor",
			        "Clinical teams cannot cross a deliberate delivery-route seal");
			require(closed.submit(102, route("open")), "Authorise the shared route reopening");
			ticks(closed, 20);
			require(officer(closed, 2, 0)["condition"] == "healthy",
			        "Clinical work resumes after authorised reopening");
		}
		Json trace = Json::array();
		property_continuation_trace(trace);
		clinical_execution_regressions(trace);
		patient_selection_regressions();
		system_casualty_regressions();
		auto source = medical_fixture();
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(w);
		const auto projected = w.observe(102)->gameplay.at("teams");
		require(team_projection::board(projected) == projected,
		        "bounded team projection preserves orders and patients");
		require(w.observe(101)->gameplay.at("teams").empty(), "foreign Controller receives no clinical team authority");
		auto survey = command(w, "order", "survey/bridge");
		require(eligible_console_command(*w.observe(102), Json::object(), Json::object(), survey).has_value(),
		        "remote client admits a permitted generic team order");
		require(!eligible_console_command(*w.observe(101), Json::object(), Json::object(), survey),
		        "remote client refuses foreign team authority");
		require(w.submit(102, survey), "custom authored movement order admitted");
		require(!w.submit(101, survey), "foreign Station cannot order medical team");
		ticks(w, 2);
		require(!w.submit(102, survey), "stale order revision refused");
		auto snapshot = w.save();
		World resumed;
		resumed.restore(catalog, snapshot);
		claim(resumed);
		ticks(w, 4);
		ticks(resumed, 4);
		require(w.save() == resumed.save(), "custom order traversal and tree resume identically");
		require(w.submit(102, command(w, "order", "treat/bridge")), "compartment triage order admitted");
		ticks(w, 4);
		auto mid = w.save();
		World midway;
		midway.restore(catalog, mid);
		claim(midway);
		ticks(w, 10);
		ticks(midway, 10);
		require(w.save() == midway.save(), "partial treatment continuation matches");
		require(officer(w, 2, 0)["condition"] == "healthy", "minor injury heals with a leaderless medical team");
		require(w.submit(102, command(w, "order", "treat/workshop")), "severe casualty triage admitted");
		ticks(w, 10);
		require(officer(w, 2, 1)["condition"] == "severe" && officer(w, 2, 1)["stabilised"] == true,
		        "field care stabilises severe injury without silently healing");
		auto moved = w.save();
		saved_body(moved, 2)["personnel"]["officers"][1]["location"] = 0;
		w.restore(catalog, moved);
		claim(w);
		require(w.submit(102, command(w, "order", "recover/bridge")), "facility recovery admitted");
		ticks(w, 7);
		snapshot = w.save();
		World facility;
		facility.restore(catalog, snapshot);
		claim(facility);
		ticks(w, 12);
		ticks(facility, 12);
		require(w.save() == facility.save(), "facility occupancy and recovery resume identically");
		require(officer(w, 2, 1)["condition"] == "healthy", "facility restores severe casualty");
		auto legacy = w.save();
		for (auto &body : legacy["bodies"])
		{
			for (auto &o : body["personnel"]["officers"])
				for (const auto *key : {"condition", "facility", "escort", "stabilised", "stun_until", "treatment",
				                        "recovery", "casualty_tick"})
					o.erase(key);
			for (const auto *key : {"revision", "order", "activity", "patient", "work_done"})
				body["combat"]["systems"]["team"].erase(key);
		}
		World legacy_restore;
		legacy_restore.restore(catalog, legacy);
		require(officer(legacy_restore, 2, 0)["condition"] == "healthy", "legacy officer state defaults to healthy");
		auto normalised = legacy_restore.save();
		require(saved_body(normalised, 2)["combat"]["systems"]["team"]["location"] ==
		            saved_body(legacy, 2)["combat"]["systems"]["team"]["location"],
		        "legacy repair traversal migrates without resetting physical location");
		auto malformed = w.save();
		saved_body(malformed, 2)["personnel"]["officers"][0]["condition"] = "unknown";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "unknown saved injury rejected");
		malformed = w.save();
		saved_body(malformed, 2)["combat"]["systems"]["medical"]["order"] = "absent";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "unknown saved order rejected");
		malformed = w.save();
		for (auto &o : saved_body(malformed, 2)["personnel"]["officers"])
		{
			o["condition"] = "severe";
			o["stabilised"] = true;
			o["present"] = false;
			o["location"] = 1;
			o["edge_to"] = -1;
			o["remaining"] = 0;
			o["facility"] = "bay";
			o["recovery"] = 0;
		}
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "over-capacity facility state rejected");
		auto invalid = source;
		invalid["entities"][0]["systems"]["medical"]["capabilities"] = {"security"};
		rejects(
		    [&]
		    {
			    load(invalid);
		    },
		    "medical order cannot grant unsupported capability");
		// Qualified clinical expertise speeds real treatment while leaderless teams remain useful.
		auto expert_source = source;
		expert_source["entities"][0]["personnel"]["officers"].push_back({{"id", "medic"},
		                                                                 {"name", "Medic"},
		                                                                 {"department", "engineering"},
		                                                                 {"duty", "medical-team"},
		                                                                 {"compartment", "workshop"},
		                                                                 {"traits", {"medical"}}});
		const auto expert_catalog = load(expert_source);
		World expert;
		expert.load(expert_catalog, "test.map", {{"crew", "test.away"}});
		claim(expert);
		ticks(expert, 12);
		require(officer(expert, 2, 0)["condition"] == "healthy", "attached medical officer improves treatment rate");
		// Optional clinical deployment cannot substitute for the required repair capability.
		auto away_source = source;
		for (auto &o : away_source["entities"][0]["personnel"]["officers"])
			o.erase("condition");
		away_source["entities"][1]["personnel"]["officers"][0]["condition"] = "minor";
		away_source["trees"].push_back(order_tree("test.unused", "move"));
		const auto away_catalog = load(away_source);
		World away;
		away.load(away_catalog, "test.map", {{"crew", "test.away"}});
		claim(away);
		ticks(away, 4);
		require(eligible_console_command(*away.observe(101), Json::object(), Json::object(), away_order(away, "open"))
		            .has_value(),
		        "remote client admits Dispatch mission control");
		require(away.submit(101, away_order(away, "open")), "open medical mission");
		ticks(away, 1);
		require(!away.submit(102, away_order(away, "assign", "repair/team:medical", 102, "medical")),
		        "medical team cannot fill required repair slot");
		require(away.submit(102, away_order(away, "assign", "repair/team:team", 102, "team")),
		        "assign required engineering team");
		ticks(away, 1);
		require(away.submit(102, away_order(away, "assign", "medical/team:medical", 102, "medical")),
		        "assign optional clinical team");
		ticks(away, 1);
		require(away.submit(101, away_order(away, "transport", "transport", 101, "transport")), "bind transport");
		ticks(away, 7);
		require(!away.submit(102, command(away, "order", "treat/bridge")),
		        "away reservation refuses onboard clinical order");
		require(away.submit(101, away_order(away, "send")), "send real medical party");
		ticks(away, 5);
		auto away_saved = away.save();
		World away_resume;
		away_resume.restore(away_catalog, away_saved);
		claim(away_resume);
		ticks(away, 12);
		ticks(away_resume, 12);
		require(away.save() == away_resume.save(), "away clinical traversal and work resume identically");
		require(officer(away, 1, 0)["condition"] == "healthy", "deployed medical team heals target ship officer");
		require(away.submit(101, away_order(away, "recall")), "recall clinical party");
		ticks(away, 5);
		require(away.observe(101)->gameplay.at("away_missions")[0]["phase"] == "recovered",
		        "medical party needs and completes physical recall");
		trace.push_back(away.save());
		// Actual projectile damage exercises casualty hooks, rather than calling an injury helper.
		for (const auto *condition : {"stunned", "minor", "severe", "dead"})
		{
			auto combat_source = casualty_fixture(condition, true);
			const auto combat_catalog = load(combat_source);
			World battle;
			battle.load(combat_catalog, "test.map");
			require(battle.claim(201, 1, "drive"), "claim casualty gun");
			if (std::string(condition) == "dead")
			{
				auto damaged = battle.save();
				saved_body(damaged, 2)["combat"]["compartments"][0]["hp"] = 80;
				battle.restore(combat_catalog, damaged);
				require(battle.observe(201).has_value() || battle.claim(201, 1, "drive"),
				        "reclaim prepared lethal fixture");
			}
			shoot(battle);
			ticks(battle, 1);
			for (int i = 0; i < 70 && officer(battle, 2, 0)["condition"] == "healthy"; ++i)
				battle.tick();
			require(officer(battle, 2, 0)["condition"] == condition,
			        "significant projectile event applies authored casualty outcome");
			if (std::string(condition) == "stunned")
			{
				const auto stopped_view = battle.observe(201);
				for (const auto &c : stopped_view->gameplay.at("controllers"))
					if (c["id"] == "gun")
					{
						OperatingCommand stop{1, "", 0, "", "gun", "enabled", c.at("generation")};
						require(battle.submit(201, stop), "stop gun after casualty");
					}
				ticks(battle, 10);
				const auto end = officer(battle, 2, 0)["stun_until"].get<std::int64_t>();
				auto saved = battle.save();
				World restart;
				restart.restore(combat_catalog, saved);
				while (officer(battle, 2, 0)["condition"] == "stunned" &&
				       static_cast<std::int64_t>(battle.ticks()) <= end + 1000)
				{
					battle.tick();
					restart.tick();
				}
				require(officer(battle, 2, 0)["condition"] == "healthy" && battle.save() == restart.save(),
				        "20-second stun clears on deterministic resumed simulation time");
				require(static_cast<std::int64_t>(battle.ticks()) ==
				            officer(battle, 2, 0)["casualty_tick"].get<std::int64_t>() + 20 * World::ticks_per_second +
				                1,
				        "stun lasts 20 seconds from the final significant injury event");
			}
			if (std::string(condition) == "dead")
				require(officer(battle, 2, 0)["duty"] == "", "death permanently removes duty assignment");
		}
		for (bool lethal : {false, true})
		{
			auto protected_source = casualty_fixture("dead", lethal);
			const auto protected_catalog = load(protected_source);
			World protected_world;
			protected_world.load(protected_catalog, "test.map");
			require(protected_world.claim(201, 1, "drive"), "claim protected casualty fixture");
			shoot(protected_world);
			// Inspect the first damaging event: a healthy compartment cannot immediately kill its officer.
			for (int n = 0; n < 30; ++n)
			{
				protected_world.tick();
				auto save = protected_world.save();
				if (saved_body(save, 2)["combat"]["compartments"][0]["hp"] < 100)
				{
					require(officer(protected_world, 2, 0)["condition"] != "dead",
					        "death requires damage that existed before the event");
					break;
				}
			}
			if (!lethal)
			{
				ticks(protected_world, 60);
				require(officer(protected_world, 2, 0)["condition"] != "dead",
				        "nonlethal events never kill despite authored death probability");
			}
		}
		trace.push_back(w.save());
		// Synthetic ZIP timestamps are not simulation state. Restores above still
		// validate the actual content identity within each run.
		for (auto &state : trace)
			state.erase("packs");
		if (argc > 1 && std::string(argv[1]) == "--physical-trace")
			std::cout << machinery_physical_trace(trace).dump() << "\n";
		else if (argc > 1 && std::string(argv[1]) == "--trace-state")
			std::cout << trace.dump() << "\n";
		else if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << "\n";
		else
			std::cout
			    << "PASS generic orders, clinical triage, facility recovery, projectile casualties and continuation\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
