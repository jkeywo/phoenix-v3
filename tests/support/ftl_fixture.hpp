#pragma once
#include "controller_fixture.hpp"
namespace phoenix::test
{
inline void fit_ftl(Json &source, const std::string &station, const std::string &engineer)
{
	auto &e = source["entities"][0];
	e["systems"]["jump"] = {{"kind", "ftl"},
	                        {"enabled", false},
	                        {"power_store", "jump-cell"},
	                        {"charge_per_second", 100},
	                        {"charging_loss_per_second", 20},
	                        {"ready_loss_per_second", 50},
	                        {"off_discharge_per_second", 83}};
	e["systems"]["jump-feed"] = {{"kind", "distribution"}, {"conduit", "jump-feed"}};
	if (e.contains("combat"))
		for (auto key : {"jump", "jump-feed"})
			e["systems"][key]["compartment"] = "bridge";
	auto &resources = e["resources"];
	resources["stores"].push_back({{"id", "jump-cell"},
	                               {"capacity", 2400},
	                               {"initial", 0},
	                               {"charge_rate", 100},
	                               {"discharge_rate", 100},
	                               {"operation_heat_ppm", 1000000}});
	if (!resources.contains("links"))
		resources["links"] = Json::array();
	resources["links"].push_back({{"id", "jump-feed"}, {"from", "cell"}, {"to", "jump-cell"}, {"capacity_rate", 100}});
	source["consoles"].push_back(
	    {{"id", "test.navigator"}, {"name", "Navigator"}, {"human_seeking", true}, {"widgets", Json::array()}});
	for (auto &s : e["stations"])
		if (s.at("id") == station)
			s["consoles"].push_back({{"id", "navigator"},
			                         {"definition", "test.navigator"},
			                         {"systems", Json::array()},
			                         {"feeds", {"jump"}},
			                         {"human_preferences", {station}}});
	for (auto &s : e["stations"])
		for (auto &c : s["consoles"])
			if (c.at("id") == engineer)
				c["feeds"].push_back("jump-feed");
	e["control_layout"]["controllers"].push_back(
	    {{"id", "jump"},
	     {"console", "navigator"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 0}, {"consumers", {"jump"}}}})}});
	e["control_layout"]["controllers"].push_back(
	    {{"id", "jump-feed"},
	     {"console", engineer},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "isolated"}, {"action", "link_isolated"}, {"initial", 0}, {"consumers", {"jump-feed"}}}})}});
	for (auto &p : e["control_layout"]["profiles"])
	{
		p["consoles"]["navigator"] = {{"human", {"jump"}}};
		p["consoles"][engineer]["human"].push_back("jump-feed");
	}
	if (e.contains("personnel"))
	{
		const auto department = e["personnel"]["departments"][0]["id"];
		for (const auto *id : {"jump", "jump-feed"})
			e["personnel"]["duties"].push_back({{"id", id},
			                                    {"label", id},
			                                    {"department", department},
			                                    {"controller", id},
			                                    {"compartment", "bridge"},
			                                    {"effects", Json::object()}});
	}
}
inline Json ftl_fixture()
{
	auto source = controller_fixture();
	auto &e = source["entities"][0];
	e["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                           {"capacity", 100000},
	                                           {"initial", 100000},
	                                           {"charge_rate", 1000},
	                                           {"discharge_rate", 1000}}})}};
	fit_ftl(source, "operations", "drive");
	for (auto &s : source["entities"][0]["stations"])
		for (auto &c : s["consoles"])
			if (c.at("id") == "observer")
				c["feeds"] = Json::array();
	source["maps"][0]["entities"].push_back(
	    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 10000}, {"y_mm", 0}, {"heading_mdeg", 0}});
	return source;
}
} // namespace phoenix::test
