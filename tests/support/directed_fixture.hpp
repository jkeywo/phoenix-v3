#pragma once
#include "combat_fixture.hpp"
namespace phoenix::test
{
inline Json &directed_effect(Json &source, const std::string &id)
{
	for (auto &effect : source["effects"])
		if (effect.at("id") == id)
			return effect;
	throw std::runtime_error("missing synthetic directed effect " + id);
}
inline Json directed_fixture()
{
	auto source = combat_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["mag"]["capacity"] = 100;
	ship["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	ship["systems"]["gun"]["fire_ticks"] = 20;
	ship["systems"]["gun"]["shows"] = {"ship", "asteroid"};
	ship["systems"]["gun"]["projectile"]["speed_mm_s"] = 1000000;
	ship["systems"]["gun"]["projectile"]["damage"] = 8;
	ship["systems"]["gun"]["projectile"]["damage_per_m"] = 1;
	for (auto &compartment : ship["combat"]["decks"][0]["compartments"])
		compartment["hp"] = 10000;
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller.at("id") == "team-a" || controller.at("id") == "team-b")
			controller["commands"][0]["initial"] = 0;
	materialise_fixture_resources(source);
	materialise_fixture_directed(source);
	// Make one real fitting switch between focused acquisition and launch.
	auto &gun = source["entities"][0]["systems"]["gun"];
	const auto fire = gun["directed"]["initial_mode"].get<std::string>();
	directed_effect(source, fire)["payloads"] = Json::array();
	const Json policies = Json::array({"fixture.targeting.designated", "fixture.targeting.off"});
	directed_effect(source, fire)["targeting"] = policies;
	source["effects"].push_back({{"id", "test.directed.scan"},
	                             {"kind", "directed_mode"},
	                             {"label", "Focused inspection"},
	                             {"operation", "scan"},
	                             {"range_mm", 200000},
	                             {"targeting", policies},
	                             {"capabilities", {{"compartment_condition", 12}, {"basic_identity", 0}}}});
	gun["directed"] = {{"modes", Json::array({"test.directed.scan", fire})},
	                   {"targeting", policies},
	                   {"initial_mode", "test.directed.scan"},
	                   {"initial_targeting", "fixture.targeting.designated"}};
	for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "gun")
		{
			controller["commands"][1]["action"] = "directed_target";
			controller["commands"].push_back(
			    {{"id", "operation"}, {"action", "directed_operating_mode"}, {"consumers", {"gun"}}});
			controller["commands"].push_back(
			    {{"id", "policy"}, {"action", "directed_targeting_mode"}, {"consumers", {"gun"}}});
		}
	return source;
}
inline void directed_burning(Json &source, std::int64_t period = 3, std::int64_t duration = 15)
{
	for (auto effect : Json::parse(R"json([
 {"id":"test.burn.tick","kind":"operations","operations":[{"kind":"damage","target":"protected","damage":2}]},
 {"id":"test.burn","kind":"periodic","duration_ticks":15,"period_ticks":3,"effect":"test.burn.tick"},
 {"id":"test.burn.apply","kind":"operations","operations":[{"kind":"apply","target":"protected","effect":"test.burn","grant":"burn"}]},
 {"id":"test.burn.payload","kind":"payload","bindings":[{"id":"ignite","outcome":"compartment","positive_damage":true,"effect":"test.burn.apply"}]}
 ])json"))
		source["effects"].push_back(std::move(effect));
	directed_effect(source, "test.burn")["period_ticks"] = period;
	directed_effect(source, "test.burn")["duration_ticks"] = duration;
	const auto &gun = source["entities"][0]["systems"]["gun"];
	const auto fire = gun["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["resource_payloads"] = true;
	const auto resource = source["entities"][0]["systems"]["mag"]["stock"]["selection"];
	for (auto &entry : source["resources"])
		if (entry.at("id") == resource)
			entry["effects"] = {"test.burn.payload"};
}
} // namespace phoenix::test
