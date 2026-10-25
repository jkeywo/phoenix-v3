#pragma once
#include "away_fixture.hpp"
namespace phoenix::test
{
inline Json docking_fixture()
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
} // namespace phoenix::test
