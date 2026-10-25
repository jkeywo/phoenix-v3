#pragma once
#include "away_fixture.hpp"
namespace phoenix::test
{
inline Json away_assignment_fixture()
{
	auto source = away_fixture();
	auto &layout = source["entities"][0]["control_layout"];
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 3;
	auto assisted = layout["profiles"][0];
	assisted["id"] = "assisted";
	assisted["stars"] = 1;
	assisted["consoles"]["observer"] = {{"ai", {"team"}}};
	layout["profiles"].push_back(assisted);
	source["maps"][0]["away_missions"][0]["slots"].push_back(
	    {{"id", "specialist"}, {"kind", "officer"}, {"trait", "repair"}, {"required", false}});

	const auto page = [](const std::string &component, const std::string &label)
	{
		return Json{{"id", component}, {"kind", "component"}, {"component", component}, {"label", label}};
	};
	auto &primary = source["consoles"][0];
	primary["name"] = "Dispatch";
	primary["layout"] = {{"schema", 1},
	                     {"root",
	                      {{"id", "station"},
	                       {"kind", "tabs"},
	                       {"children", Json::array({page("gameplay", "Helm"), page("personnel", "Personnel"),
	                                                 page("away", "Away missions")})}}}};
	auto contributor = primary;
	contributor["id"] = "test.contributor";
	contributor["name"] = "Support";
	contributor["layout"]["root"]["children"] =
	    Json::array({page("repair", "Repair"), page("personnel", "Personnel"), page("away", "Away missions")});
	source["consoles"].push_back(contributor);
	source["entities"][0]["stations"][1]["consoles"][0]["definition"] = "test.contributor";
	return source;
}
} // namespace phoenix::test
