#pragma once
#include "fixtures.hpp"

namespace phoenix::test
{
// Independent synthetic mechanisms; no production hull or balance inheritance.
inline Json controller_fixture(bool split = false, std::int64_t initial = 0)
{
	auto source = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "controller fixture");
	source["schema"] = 2;
	source["entities"].erase(1);
	auto &entity = source["entities"][0];
	entity["id"] = "test.controllable";
	entity.erase("console");
	entity["systems"] = {{"port", {{"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 2000}}},
	                     {"starboard", {{"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 1000}}}};
	source["consoles"][0]["widgets"] = Json::array();
	entity["stations"] = Json::array(
	    {{{"id", "operations"},
	      {"consoles",
	       Json::array({{{"id", "drive"},
	                     {"definition", "test.console"},
	                     {"systems", Json::array()},
	                     {"feeds", {"port", "starboard"}}},
	                    {{"id", "observer"}, {"definition", "test.console"}, {"systems", Json::array()}}})}}});
	Json controllers = Json::array(), names = Json::array();
	auto add = [&](const char *id, Json consumers)
	{
		controllers.push_back(
		    {{"id", id},
		     {"console", "drive"},
		     {"fallback", "hold"},
		     {"commands",
		      Json::array(
		          {{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", initial}, {"consumers", consumers}}})}});
		names.push_back(id);
	};
	if (split)
	{
		add("port_drive", {"port"});
		add("starboard_drive", {"starboard"});
	}
	else
		add("propulsion", {"port", "starboard"});
	entity["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles", Json::array({{{"id", "direct"}, {"consoles", {{"drive", {{"human", names}}}}}},
	                              {{"id", "automatic"}, {"consoles", {{"drive", {{"ai", names}}}}}}})}};
	source["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.controllable"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	source["maps"][0]["ship_slots"] = Json::array();
	return source;
}
} // namespace phoenix::test
