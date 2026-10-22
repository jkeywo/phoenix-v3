#include "../support/assertions.hpp"
#include "../support/away_fixture.hpp"
#include <iostream>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
Json fixture()
{
	auto s = away_fixture();
	auto &e = s["entities"][0];
	e["faction"] = "crew";
	s["maps"][0]["factions"] = Json::array({{{"id", "crew"}, {"label", "Crew"}}});
	for (auto &c : e["control_layout"]["controllers"])
		if (c["id"] == "engine")
		{
			c["commands"].push_back({{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"engine"}}});
			c["commands"].push_back({{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"steering"}}});
		}
	e["docking"] = Json::parse(R"({"ports":[
 {"id":"fore","controller":"engine","compartment":"bridge","compatibility":"standard","x_mm":5000,"heading_mdeg":0,"mating_ticks":3,"separation_ticks":3,"auto_accept_allies":true,"supply":true,"emergency_release":true},
 {"id":"aft","controller":"engine","compartment":"bridge","compatibility":"standard","x_mm":-5000,"heading_mdeg":180000,"mating_ticks":3,"separation_ticks":3,"auto_accept_allies":true,"supply":true}]})");
	e["logistics"] = Json::parse(R"({"controller":"team","units_per_second":100,"materials":[{"id":"parts"}],
 "stores":[{"id":"hold","compartment":"workshop","capacity":20,"main":true,"accepts":["parts"],"initial":{"parts":4}}]})");
	auto &m = s["maps"][0]["away_missions"][0];
	m["allow_ship_transport"] = true;
	m["docking_ports"] = {"aft"};
	return s;
}
OperatingCommand command(const World &w, std::uint64_t claim, const std::string &kind, const std::string &verb,
                         const std::string &system, const std::string &text = {})
{
	const auto o = *w.observe(claim);
	OperatingCommand c{o.own.id, kind + "." + verb, 0, system};
	c.console = o.console;
	c.text = text;
	c.controller = kind == "dock" ? "engine" : kind == "logistics" ? "team" : verb == "assign" ? "team" : "dispatch";
	for (const auto &ctrl : o.gameplay.at("controllers"))
		if (ctrl.at("id") == c.controller)
			c.generation = ctrl.at("generation");
	if (kind == "dock")
		c.value = o.gameplay.at("docking").at("revision");
	else if (kind == "logistics")
		c.value = o.gameplay.at("logistics").at("revision");
	else
		c.value = o.gameplay.at("away_missions")[0].at("revision");
	return c;
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		{
			auto approach = fixture();
			approach["maps"][0]["entities"][0]["x_mm"] = 30000;
			const auto content = load(approach);
			World flight;
			flight.load(content, "test.map", {{"crew", "test.away"}});
			require(flight.claim(101, 2, "drive"), "Claim assisted docking Helm");
			ticks(flight, 5);
			require(flight.submit(101, command(flight, 101, "dock", "assist", "fore", "1/aft")),
			        "Request assisted docking approach");
			ticks(flight, 20);
			require(flight.observe(101)->own.x > 0, "Docking assistance uses real propulsion");
			OperatingCommand manual{2, "speed_mm_s", 0};
			manual.console = "drive";
			manual.controller = "engine";
			manual.channel = "speed";
			const auto takeover = *flight.observe(101);
			for (const auto &ctrl : takeover.gameplay.at("controllers"))
				if (ctrl.at("id") == "engine")
					manual.generation = ctrl.at("generation");
			require(flight.submit(101, manual), "Admit manual takeover during approach");
			ticks(flight, 1);
			require(flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") == "free",
			        "Manual flight cancels docking assistance");
			require(flight.submit(101, command(flight, 101, "dock", "assist", "fore", "1/aft")),
			        "Resume an assisted approach explicitly");
			for (int i = 0;
			     i < 1200 && flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") != "docked"; ++i)
				flight.tick();
			require(flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") == "docked",
			        "Assisted approach reaches a real mating envelope");
		}
		for (const bool powered : {false, true})
		{
			auto source = fixture();
			source["maps"][0]["entities"][0]["x_mm"] = 30000;
			if (powered)
			{
				source["entities"][0]["motion_model"] = "inertial";
				for (const auto *system : {"engine", "steering"})
				{
					source["entities"][0]["systems"][system]["power_store"] = "cell";
					source["entities"][0]["systems"][system]["power_rate"] = 20;
				}
			}
			const auto catalog = load(source);
			World flight;
			flight.load(catalog, "test.map", {{"crew", "test.away"}});
			require(flight.claim(101, 2, "drive") && flight.claim(103, 1, "drive"), "Claim moving docking ships");
			ticks(flight, 5);
			const auto observed = *flight.observe(103);
			OperatingCommand move{1, "speed_mm_s", 100};
			move.console = "drive";
			move.controller = "engine";
			move.channel = "speed";
			for (const auto &ctrl : observed.gameplay.at("controllers"))
				if (ctrl.at("id") == "engine")
					move.generation = ctrl.at("generation");
			require(flight.submit(103, move), "Receiver moves through ordinary Helm commands");
			ticks(flight, 1);
			auto drifting = flight.save();
			for (auto &body : drifting["bodies"])
				if (body.at("body").at("id") == 2)
					body["body"]["velocity"] = {{"x", 0}, {"y", 500}};
			flight.restore(catalog, drifting);
			const std::string remote = powered ? "fore" : "aft";
			require(flight.submit(101, command(flight, 101, "dock", "assist", "fore", "1/" + remote)),
			        "Approach moving receiver with initial lateral drift");
			ticks(flight, 20);
			World resumed;
			resumed.restore(catalog, flight.save());
			for (int i = 0;
			     i < 2400 && flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") != "docked"; ++i)
			{
				flight.tick();
				resumed.tick();
			}
			require(flight.save() == resumed.save(), "Moving docking guidance resumes deterministically");
			if (flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") != "docked")
			{
				const auto failed = flight.save();
				for (const auto &body : failed.at("bodies"))
					std::cerr << body.at("body").dump() << " dock " << body.value("dock_connection", Json()).dump()
					          << '\n';
			}
			require(flight.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") == "docked",
			        powered ? "Powered inertial docking navigates around a moving hull"
			                : "Basic docking tracks a moving receiver");
		}

		{
			auto source = fixture();
			source["entities"][0]["logistics"]["batch_size"] = 1;
			for (auto &port : source["entities"][0]["docking"]["ports"])
				port["units_per_second"] = 1;
			const auto catalog = load(source);
			World deliveries;
			deliveries.load(catalog, "test.map", {{"crew", "test.away"}});
			require(deliveries.claim(101, 2, "drive") && deliveries.claim(102, 2, "observer") &&
			            deliveries.claim(103, 1, "observer"),
			        "Claim both physical delivery endpoints");
			ticks(deliveries, 5);
			require(deliveries.submit(101, command(deliveries, 101, "dock", "connect", "fore", "1/aft")),
			        "Dock cancellation fixture");
			ticks(deliveries, 5);
			require(deliveries.submit(102, command(deliveries, 102, "logistics", "transfer", "hold", "1/hold/parts/1")),
			        "Dispatch returnable supply");
			ticks(deliveries, 1);
			auto batch = deliveries.save().at("logistics").at("batches").at(0);
			require(deliveries.submit(
			            102, command(deliveries, 102, "logistics", "cancel", "batch:" + batch.at("id").dump())),
			        "Cancel before crossing");
			ticks(deliveries, 1);
			World resumed;
			resumed.restore(catalog, deliveries.save());
			ticks(deliveries, 30);
			ticks(resumed, 30);
			require(deliveries.save() == resumed.save(), "Cancelled batch resumes without losing transit progress");
			require(deliveries.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 4,
			        "Early cancellation returns actual supplies");
			require(deliveries.submit(102, command(deliveries, 102, "logistics", "transfer", "hold", "1/hold/parts/1")),
			        "Dispatch crossing supply");
			bool crossing = false, crossed = false;
			for (int i = 0; i < 150 && !crossed; ++i)
			{
				deliveries.tick();
				const auto batches = deliveries.save().at("logistics").at("batches");
				if (batches.empty())
					continue;
				batch = batches.at(0);
				if (batch.at("crossing").get<std::int64_t>() > 0 && !crossing)
				{
					crossing = true;
					require(!deliveries.submit(
					            102, command(deliveries, 102, "logistics", "cancel", "batch:" + batch.at("id").dump())),
					        "Cannot redirect active crossing");
					require(deliveries.submit(
					            103, command(deliveries, 103, "logistics", "route", "bridge", "workshop/close")),
					        "Close receiving route after physical dispatch");
				}
				if (batch.at("location") == 1)
				{
					crossed = true;
					require(!deliveries.submit(
					            102, command(deliveries, 102, "logistics", "cancel", "batch:" + batch.at("id").dump())),
					        "Cargo already across stays at receiver");
				}
			}
			require(crossing && crossed, "Delivery physically crosses gateway");
			resumed.restore(catalog, deliveries.save());
			require(deliveries.submit(103, command(deliveries, 103, "logistics", "route", "bridge", "workshop/open")),
			        "Reopen receiving route");
			require(resumed.submit(103, command(resumed, 103, "logistics", "route", "bridge", "workshop/open")),
			        "Reopen resumed receiving route");
			ticks(deliveries, 30);
			ticks(resumed, 30);
			require(deliveries.save() == resumed.save(), "Refused cancellation preserves valid continuation");
			require(
			    deliveries.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 3 &&
			        deliveries.observe(103)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 5,
			    "Crossed delivery conserves supply at receiving endpoint");
		}

		const auto source = fixture();
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "Docking crew claims");
		ticks(w, 4);
		require(w.submit(101, command(w, 101, "away", "open", "job")), "Open dock-supported mission");
		ticks(w, 1);
		require(w.submit(102, command(w, 102, "away", "assign", "job", "repair/team:team")),
		        "Assign physical repair party");
		ticks(w, 1);
		require(w.submit(101, command(w, 101, "away", "transport", "job", "ship")), "Select docking ship transport");
		ticks(w, 12);
		require(!w.submit(101, command(w, 101, "away", "send", "job")), "Proximity alone cannot deploy a ship party");
		const auto connect = command(w, 101, "dock", "connect", "fore", "1/aft");
		auto wrong = connect;
		wrong.generation++;
		require(!w.submit(101, wrong), "Obsolete docking authority refused");
		require(w.submit(101, connect), "Admit compatible docking request");
		ticks(w, 2);
		World resumed;
		resumed.restore(catalog, w.save());
		ticks(w, 4);
		ticks(resumed, 4);
		require(w.save() == resumed.save(), "Mating resumes exactly");
		require(w.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") == "docked",
		        "Finite mating completes");
		const auto projected = w.observe(101)->gameplay.at("docking");
		require(docking_projection::board(projected) == projected, "Docking projection retains only permitted fields");
		require(w.submit(102, command(w, 102, "logistics", "transfer", "hold", "1/hold/parts/3")),
		        "Request physical docked stock transfer");
		ticks(w, 7);
		World supply_resume;
		supply_resume.restore(catalog, w.save());
		ticks(w, 30);
		ticks(supply_resume, 30);
		require(w.save() == supply_resume.save(), "Docked supply resumes without duplication");
		require(w.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 1,
		        "Transfer consumes finite source stock");
		require(w.submit(101, command(w, 101, "away", "send", "job")), "Mated mission port permits deployment");
		ticks(w, 25);
		require(w.observe(101)->gameplay.at("away_missions")[0].at("phase") == "deployed",
		        "Ship party crosses actual dock");
		require(w.submit(101, command(w, 101, "dock", "undock", "fore")), "Request ordinary separation");
		ticks(w, 5);
		require(w.observe(101)->gameplay.at("docking").at("ports")[0].at("phase") == "free",
		        "Finite separation completes");
		require(w.observe(101)->gameplay.at("away_missions")[0].at("phase") == "deployed",
		        "Undocking leaves delivered party in place");
		auto bad = w.save();
		bad["bodies"][1]["dock_connection"] = {{"parent", 2}};
		bool refused = false;
		try
		{
			resumed.restore(catalog, bad);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Malformed docking continuation refused");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			auto trace = w.save();
			trace.erase("packs");
			std::cout << trace.dump() << '\n';
		}
		else
			std::cout << "Docking, supply and dock-supported away integration passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
