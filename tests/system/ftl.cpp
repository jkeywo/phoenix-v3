#include "../support/assertions.hpp"
#include "../support/ftl_fixture.hpp"
#include "../support/away_fixture.hpp"
#include <iostream>
import phoenix.content.controllers;
import phoenix.simulation;
import phoenix.networking.projection;
using namespace phoenix;
using namespace phoenix::test;
#include "../support/machinery_baseline.hpp"
void ticks(World &w, int n)
{
	for (int i = 0; i < n; ++i)
		w.tick();
}
Observation navigator(const World &w, std::uint64_t claim = 101)
{
	const auto o = w.observe(claim);
	if (o->console == "navigator")
		return *o;
	for (const auto &c : o->additional_consoles)
		if (c.console == "navigator")
			return c;
	throw std::runtime_error("Navigator did not follow human");
}
Json battery(const World &w)
{
	return navigator(w).gameplay.at("ftl").at(0);
}
OperatingCommand command(const World &w, const std::string &action, std::int64_t value = 0)
{
	const auto o = navigator(w);
	OperatingCommand c{
	    o.own.id, action, value, action == "ftl.depart" ? "jump" : "", "jump", action == "ftl.depart" ? "" : "enabled"};
	c.console = "navigator";
	for (const auto &ctrl : o.gameplay.at("controllers"))
		if (ctrl.at("id") == "jump")
			c.generation = ctrl.at("generation");
	return c;
}
OperatingCommand isolate(const World &w, std::int64_t value)
{
	const auto o = w.observe(101);
	OperatingCommand c{o->own.id, "link_isolated", value, {}, "jump-feed", "isolated"};
	c.console = o->console;
	for (const auto &ctrl : o->gameplay.at("controllers"))
		if (ctrl.at("id") == "jump-feed")
			c.generation = ctrl.at("generation");
	return c;
}
OperatingCommand away_order(const World &w, std::string action, std::string text = {}, std::uint64_t claim = 101)
{
	const auto o = w.observe(claim);
	OperatingCommand c{o->own.id, "away." + action, o->gameplay.at("away_missions").at(0).at("revision"), "job"};
	c.console = o->console;
	c.controller = claim == 102 ? "team" : action == "transport" ? "transport" : "dispatch";
	c.text = text;
	for (const auto &ctrl : o->gameplay.at("controllers"))
		if (ctrl.at("id") == c.controller)
			c.generation = ctrl.at("generation");
	return c;
}
void displacement_departure()
{
	auto source = away_fixture();
	fit_ftl(source, "helm", "drive");
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto ship = *w.ship_for_slot("crew");
	require(w.claim(101, ship, "drive") && w.claim(102, ship, "observer"), "displacement crew claims");
	ticks(w, 4);
	require(w.submit(101, away_order(w, "open")), "open mission");
	w.tick();
	require(w.submit(102, away_order(w, "assign", "repair/team:team", 102)), "assign engineering team");
	w.tick();
	require(w.submit(101, away_order(w, "transport", "transport")), "assign displacement");
	require(w.submit(101, command(w, "system_enabled", 1)), "enable rescue FTL");
	ticks(w, 650);
	require(battery(w).at("ready"), "rescue FTL ready");
	const auto staged = w.save();
	auto disabled = staged;
	for (auto &body : disabled["bodies"])
		if (body.at("body").at("id") == ship)
		{
			body["combat"]["compartments"][0]["hp"] = 0;
			body["combat"]["compartments"][0]["disabled"] = true;
		}
	World damaged;
	damaged.restore(catalog, disabled);
	require(!damaged.submit(101, command(damaged, "ftl.depart")), "structural disablement prevents charged departure");
	require(w.submit(101, away_order(w, "send")), "send team");
	w.tick();
	require(battery(w).at("blocker") == "Displacement transfer active" && !w.submit(101, command(w, "ftl.depart")),
	        "active displacement blocks FTL");
	ticks(w, 5);
	require(!battery(w).at("left_behind").empty(), "deployed personnel warning is visible before departure");
	require(w.submit(101, command(w, "ftl.depart")), "deployed party does not prevent deliberate departure");
	w.tick();
	const auto report = w.observe(101)->gameplay.at("mission_report");
	require(!report.at("left_behind").empty(), "report records personnel left behind");
	require(!w.observe(101)->gameplay.at("combat").at("destroyed").get<bool>(),
	        "departure is not projected as destruction");
	ticks(w, 10);
	require(w.save().at("away_missions").at(0).at("phase") == "deployed", "departure never teleports party aboard");
	World restored;
	restored.restore(catalog, w.save());
	ticks(w, 3);
	ticks(restored, 3);
	require(restored.save() == w.save(), "deployed-party departure recovers exactly");
	// A qualified operator can be absent while a transfer is queued; it has not started.
	w.restore(catalog, staged);
	OperatingCommand relieve{ship, "personnel.relieve", 0, "nav"};
	relieve.generation = w.observe(101)->gameplay.at("personnel").at("revision");
	require(w.submit(101, relieve), "relieve displacement operator");
	w.tick();
	require(w.submit(101, away_order(w, "send")), "queue unavailable transfer");
	w.tick();
	require(w.submit(101, command(w, "ftl.depart")), "queued transfer permits departure");
	w.tick();
	require(w.save().at("away_missions").at(0).at("phase") == "open", "departure cancels queued transfer at origin");
}
void race_departure()
{
	auto source = ftl_fixture();
	auto &map = source["maps"][0];
	map["regions"] =
	    Json::array({{{"id", "finish"}, {"shape", "circle"}, {"x_mm", 100000}, {"y_mm", 0}, {"radius_mm", 1000}}});
	map["scenario"] = {{"tree", "test.race-a"},
	                   {"bindings", {{"actor", {{"entity", 1}}}}},
	                   {"situations", Json::array({{{"id", "other"},
	                                                {"tree", "test.race-b"},
	                                                {"bindings", {{"actor", {{"entity", 2}}}}},
	                                                {"start", true}}})}};
	for (int i = 1; i <= 2; ++i)
	{
		const std::string name = i == 1 ? "a" : "b";
		map["scenario"]["objectives"].push_back({{"id", name},
		                                         {"type", "reach_region"},
		                                         {"region", "finish"},
		                                         {"competition", "race"},
		                                         {"actor", {{"entity", i}}},
		                                         {"recipients", {{"ship", {{"entity", i}}}}},
		                                         {"title_key", "Reach finish"}});
		source["trees"].push_back(
		    {{"id", "test.race-" + name},
		     {"context", "scenario"},
		     {"initial", "run"},
		     {"states",
		      Json::array(
		          {{{"id", "run"},
		            {"entry", Json::array({{{"action", "add_objective"},
		                                    {"arguments",
		                                     Json::array({name, {{"observation", "self.entity"}}, "finish"})}}})}}})}});
	}
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map");
	require(w.claim(101, 1, "drive") && w.claim(102, 2, "drive"), "race crew claims");
	require(w.submit(101, command(w, "system_enabled", 1)), "race FTL on");
	ticks(w, 650);
	require(w.submit(101, command(w, "ftl.depart")), "race departure");
	w.tick();
	const auto report = w.observe(101)->gameplay.at("mission_report");
	require(report.at("objectives").size() == 1 && report.at("objectives").at(0).at("status") == "active",
	        "departure reports unfinished race without defeat");
	World restored;
	restored.restore(catalog, w.save());
	OperatingCommand speed{2, "speed_mm_s", 17000, {}, "propulsion", "speed"};
	const auto other = w.observe(102);
	for (const auto &ctrl : other->gameplay.at("controllers"))
		if (ctrl.at("id") == "propulsion")
			speed.generation = ctrl.at("generation");
	require(w.submit(102, speed), "remaining racer can still steer");
	require(restored.submit(102, speed), "restored remaining racer can still steer");
	ticks(w, 400);
	ticks(restored, 400);
	require(w.save() == restored.save(), "race departure continuation survives recovery");
	require(w.observe(101)->gameplay.at("mission_report") == report,
	        "later race resolution cannot replace departure report");
	bool won = false;
	const auto finished = w.save();
	for (const auto &o : finished.at("scenario").at("runtime").at("objectives"))
		won |= o.value("outcome", "") == "won" && o.value("completed_actor", std::uint64_t{0}) == 2;
	require(won, "remaining racer wins after physical arrival");
}
#include "../support/finite_craft_ftl_cases.hpp"
int main(int argc, char **argv)
{
	try
	{
		finite_ftl_cases();
		displacement_departure();
		race_departure();
		auto source = ftl_fixture();
		auto invalid = source;
		invalid["entities"][0]["resources"]["stores"][1]["initial"] = 1;
		rejects(
		    [&]
		    {
			    load(invalid);
		    },
		    "FTL cannot start precharged");
		invalid = source;
		invalid["entities"][0]["systems"]["port"]["power_store"] = "jump-cell";
		rejects(
		    [&]
		    {
			    load(invalid);
		    },
		    "FTL reserve cannot be shared by consumers");
		auto hot = source;
		hot["entities"][0]["resources"]["stores"][1]["stop_heat"] = 30;
		hot["entities"][0]["resources"]["stores"][1]["recover_heat"] = 10;
		const auto thermal_catalog = load(hot);
		World overheated;
		overheated.load(thermal_catalog, "test.map");
		require(overheated.claim(101, 1, "drive"), "thermal fixture claim");
		require(overheated.submit(101, command(overheated, "system_enabled", 1)), "thermal fixture enabled");
		ticks(overheated, 200);
		require(!battery(overheated).at("ready").get<bool>() && battery(overheated).at("power_draw") == 0 &&
		            !overheated.submit(101, command(overheated, "ftl.depart")),
		        "thermal stop blocks charging and departure");
		auto catalog = load(source);
		World w;
		w.load(catalog, "test.map");
		require(w.claim(101, 1, "drive") && w.claim(102, 2, "drive"), "two ships claimed");
		require(!w.can_claim(103, 1, "navigator"), "Navigator requires no lobby seat");
		require(battery(w).at("energy") == 0 && battery(w).at("enabled") == false, "FTL starts empty and off");
		require(!w.submit(101, command(w, "ftl.depart")), "uncharged click refused without queuing");
		require(w.submit(101, command(w, "system_enabled", 1)), "enable through owned Controller");
		ticks(w, 100);
		const auto partial = battery(w);
		require(partial.at("energy") > 0 && partial.at("energy") < partial.at("capacity") && partial.at("heat") > 0,
		        "charging pays power, stores energy and produces heat");
		World resumed;
		resumed.restore(catalog, w.save());
		ticks(w, 50);
		ticks(resumed, 50);
		require(w.save() == resumed.save(), "partial-charge continuation is exact");
		require(w.submit(101, isolate(w, 1)), "isolate upstream supply");
		ticks(w, 30);
		require(battery(w).at("energy") < partial.at("energy").get<std::int64_t>() + 250 &&
		            battery(w).at("power_draw") == 0,
		        "supply starvation prevents progress");
		require(w.submit(101, isolate(w, 0)), "restore upstream supply");
		for (int i = 0; i < 1000 && !battery(w).at("ready").get<bool>(); ++i)
			w.tick();
		require(battery(w).at("ready"), "fully powered reserve becomes ready");
		const auto heat = battery(w).at("heat").get<std::int64_t>();
		ticks(w, 100);
		require(battery(w).at("ready") && battery(w).at("heat").get<std::int64_t>() > heat &&
		            battery(w).at("power_draw") > 0,
		        "ready reserve continuously wastes real power as heat");
		require(w.observe(101)->gameplay.at("mission_report").is_null(), "early rejected click never fires later");
		auto stale = command(w, "ftl.depart");
		++stale.generation;
		require(!w.submit(101, stale) && !w.submit(102, command(w, "ftl.depart")),
		        "stale generations and foreign ship commands refused");
		require(w.submit(101, command(w, "ftl.depart")) && w.submit(101, command(w, "system_enabled", 0)),
		        "admit departure and simultaneous switch-off");
		w.tick();
		require(w.observe(101)->gameplay.at("mission_report").is_null(),
		        "authoritative tick rechecks changed readiness");
		const auto charged = battery(w).at("energy").get<std::int64_t>();
		ticks(w, 100);
		require(battery(w).at("energy").get<std::int64_t>() < charged && battery(w).at("power_draw") == 0,
		        "off reserve decays without reactor draw");
		const auto discharged = w.save();
		bool fractional = false;
		for (const auto &system : discharged.at("bodies").at(0).at("gameplay"))
			if (system.at("id") == "jump")
				fractional = system.at("ftl_discharge_remainder") != 0;
		require(fractional, "fixture exercises a real fractional discharge at the save boundary");
		resumed.restore(catalog, discharged);
		ticks(w, 7);
		ticks(resumed, 7);
		require(w.save() == resumed.save(), "off discharge remainder survives save");
		require(w.submit(101, command(w, "system_enabled", 1)), "re-enable");
		for (int i = 0; i < 1000 && !battery(w).at("ready").get<bool>(); ++i)
			w.tick();
		const auto departure = command(w, "ftl.depart");
		require(w.submit(101, departure) && w.submit(101, departure),
		        "duplicate inputs admitted before departure boundary");
		w.tick();
		const auto report = w.observe(101)->gameplay.at("mission_report");
		require(report.at("reason") == "ftl" && w.inspect().size() == 1 && !w.submit(101, departure),
		        "one departure retires ship and closes gameplay commands");
		require(!w.observe(101)->additional_consoles.empty() && navigator(w).gameplay.at("mission_report") == report,
		        "all crew consoles receive same report");
		const auto wire = network::observation_json(*w.observe(101));
		require(network::read_observation(wire, catalog).gameplay.at("mission_report") == report,
		        "report survives network projection");
		resumed.restore(catalog, w.save());
		ticks(w, 40);
		ticks(resumed, 40);
		require(w.save() == resumed.save() && w.observe(102)->gameplay.at("mission_report").is_null(),
		        "departure persists while other crew continues");
		auto old = w.save();
		for (auto &body : old["bodies"])
			body.erase("mission_report");
		World legacy;
		legacy.restore(catalog, old);
		require(legacy.inspect().size() == 2, "older saves without ending records remain active");
		auto malformed = w.save();
		malformed["bodies"][0]["mission_report"]["tick"] = -1;
		rejects(
		    [&]
		    {
			    resumed.restore(catalog, malformed);
		    },
		    "malformed departure refused transactionally");
		if (argc > 1 && std::string(argv[1]) == "--physical-trace")
			std::cout << machinery_physical_checkpoint(w.save()).dump() << "\n";
		else if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			auto continued = w.save();
			// Synthetic ZIP timestamps are not simulation continuation. Every restore
			// above still checks the real pack identity within its own run.
			continued.erase("packs");
			std::cout << sha256(json_bytes(continued)) << '\n';
		}
		else
			std::cout << "FTL charging, departure, crew report and continuation passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
