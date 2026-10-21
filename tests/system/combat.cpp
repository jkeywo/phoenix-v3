#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = controller_fixture();
	auto ship = source["entities"][0];
	ship["id"] = "test.combat";
	ship.erase("motion_model");
	ship["tags"] = {"ship"};
	ship["render"]["radar_icon"] = "ship";
	ship["render"]["sprite"] = {
	    {"albedo", "test-core:assets/pixel.png"},
	    {"radar_icon", "ship"},
	    {"frames",
	     Json::array({{{"name", "single"},
	                   {"rect", {0, 0, 1, 1}},
	                   {"pivot", {0, 0}},
	                   {"azimuth", 0},
	                   {"depth_scale", 0},
	                   {"depth_offset", 0},
	                   {"attachments", {{"mount", {{"x", 0}, {"y", 0}, {"layer", 1}, {"depth_offset", 0}}}}}}})}};
	ship["render"]["images"] = Json::array();
	ship["render"]["mode"] = "camera";
	ship["collision"] = {{"shape", "box"}, {"mass_grams", 1000000}, {"half_width_mm", 10000}, {"half_length_mm", 5000}};
	const Json projectile = {{"visual", "test.visual"}, {"speed_mm_s", 100000}, {"lifetime_ticks", 50},
	                         {"penetration", 1000},     {"damage", 40},         {"damage_per_m", 2}};
	ship["systems"] = {
	    {"radar", {{"kind", "radar"}, {"range_mm", 200000}, {"shows", {"ship", "asteroid"}}, {"compartment", "fore"}}},
	    {"mount",
	     {{"kind", "turret"},
	      {"arc_mdeg", 270000},
	      {"traverse_mdeg_s", 90000},
	      {"marker", "mount"},
	      {"visual", "test.visual"},
	      {"compartment", "fore"}}},
	    {"mag",
	     {{"kind", "magazine"},
	      {"capacity", 3},
	      {"reload", {{"strategy", "infinite"}, {"ticks", 10}, {"amount", 2}}},
	      {"compartment", "fore"}}},
	    {"gun",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"second",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"team-a", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}},
	    {"team-b", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}}};
	ship["combat"] = {{"repair_console", "observer"},
	                  {"decks", Json::array({{{"id", "deck"},
	                                          {"weight", 1},
	                                          {"compartments", Json::array({{{"id", "fore"},
	                                                                         {"x_mm", 5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}},
	                                                                        {{"id", "aft"},
	                                                                         {"x_mm", -5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}}})}}})},
	                  {"connections", Json::array({{{"from", "fore"}, {"to", "aft"}, {"travel_ticks", 10}}})},
	                  {"destruction",
	                   {{"visual", "test.visual"},
	                    {"sound", "test-core:assets/pixel.png"},
	                    {"growth_ticks", 5},
	                    {"fade_ticks", 10},
	                    {"size_mm", 10000}}}};
	Json controllers = Json::array(), drive = Json::array(), support = Json::array();
	for (auto it = ship["systems"].begin(); it != ship["systems"].end(); ++it)
	{
		const auto kind = it.value().at("kind");
		const std::string console = kind == "repair_team" ? "observer" : "drive";
		Json commands =
		    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {it.key()}}}});
		if (kind == "weapon")
			commands.push_back({{"id", "target"}, {"action", "weapon_target"}, {"consumers", {it.key()}}});
		if (kind == "repair_team")
			commands.push_back({{"id", "assignment"}, {"action", "repair_compartment"}, {"consumers", {it.key()}}});
		controllers.push_back({{"id", it.key()}, {"console", console}, {"fallback", "hold"}, {"commands", commands}});
		(console == "drive" ? drive : support).push_back(it.key());
	}
	ship["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles",
	     Json::array(
	         {{{"id", "direct"}, {"consoles", {{"drive", {{"human", drive}}}, {"observer", {{"human", support}}}}}}})}};
	ship["stations"][0]["consoles"][0]["feeds"] = {"gun", "second", "mount", "mag", "radar"};
	ship["stations"][0]["consoles"][1]["feeds"] = {"team-a", "team-b", "radar"};
	Json visual = source["entities"][0];
	visual["id"] = "test.visual";
	visual.erase("control_layout");
	visual.erase("stations");
	visual["systems"] = Json::object();
	visual["collision"] = {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}};
	source["entities"] = Json::array({ship, visual});
	source["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 2}, {"definition", "test.combat"}, {"x_mm", 60000}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 3}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 60000}, {"heading_mdeg", 0}}});
	return source;
}
void claim(World &w)
{
	require(w.claim(101, 1, "drive") && w.claim(102, 1, "observer"), "claim combat seats");
}
OperatingCommand command(const World &w, std::uint64_t claimant, const std::string &controller,
                         const std::string &channel, std::int64_t value)
{
	const auto observation = w.observe(claimant);
	for (const auto &row : observation->gameplay.at("controllers"))
		if (row.at("id") == controller)
		{
			OperatingCommand c{1, {}, value, {}, controller, channel, row.at("generation")};
			return c;
		}
	throw std::runtime_error("missing Controller");
}
void submit(World &w, std::uint64_t claimant, const std::string &controller, const std::string &channel,
            std::int64_t value)
{
	require(w.submit(claimant, command(w, claimant, controller, channel, value)), "admitted combat command");
}
Json state(const World &w, int id = 1)
{
	const auto snapshot = w.save();
	for (const auto &body : snapshot.at("bodies"))
		if (body.at("body").at("id") == id)
			return body.at("combat");
	throw std::runtime_error("missing combat state " + std::to_string(id) + " " + w.save().at("bodies").dump());
}
std::int64_t hp(const World &w, int id = 2)
{
	std::int64_t total = 0;
	const auto s = state(w, id);
	for (const auto &c : s.at("compartments"))
		total += c.at("hp").get<std::int64_t>();
	return total;
}
void advance(World &w, int ticks)
{
	for (int n = 0; n < ticks; ++n)
		w.tick();
}
void add_fit(Json &source, const std::string &id, Json spec, std::vector<std::pair<std::string, std::string>> actions,
             const std::string &console = "drive")
{
	auto &ship = source["entities"][0];
	ship["systems"][id] = std::move(spec);
	Json commands =
	    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {id}}}});
	for (const auto &[channel, action] : actions)
		commands.push_back({{"id", channel}, {"action", action}, {"consumers", {id}}});
	ship["control_layout"]["controllers"].push_back(
	    {{"id", id}, {"console", console}, {"fallback", "hold"}, {"commands", commands}});
	ship["control_layout"]["profiles"][0]["consoles"][console]["human"].push_back(id);
	ship["stations"][0]["consoles"][console == "drive" ? 0 : 1]["feeds"].push_back(id);
}
Json powered_fixture()
{
	auto s = fixture();
	auto &ship = s["entities"][0];
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"capacity", 100000},
	                                              {"initial", 100000},
	                                              {"charge_rate", 100000},
	                                              {"discharge_rate", 100000}}})}};
	for (auto &c : ship["combat"]["decks"][0]["compartments"])
		c["hp"] = 10000;
	for (auto &c : ship["control_layout"]["controllers"])
		if (c["id"] == "team-a" || c["id"] == "team-b")
			c["commands"][0]["initial"] = 0;
	return s;
}
void add_beam(Json &s, int damage = 20, int bypass = 0)
{
	add_fit(s, "beam",
	        {{"kind", "weapon"},
	         {"family", "beam"},
	         {"compartment", "fore"},
	         {"range_mm", 200000},
	         {"damage_per_second", damage},
	         {"shield_bypass_permille", bypass},
	         {"penetration", 1000},
	         {"energy_per_second", 40},
	         {"power_store", "cell"}},
	        {{"target", "weapon_target"}});
}
Json missile_fixture(bool defence = false)
{
	auto s = powered_fixture();
	auto &ship = s["entities"][0];
	ship["systems"]["radar"]["shows"].push_back("missile");
	Json payload = {{"visual", "test.visual"},
	                {"speed_mm_s", 20000},
	                {"lifetime_ticks", 200},
	                {"penetration", 1000},
	                {"damage", 100},
	                {"damage_per_m", 5},
	                {"guidance", "homing"},
	                {"durability", 20},
	                {"radius_mm", 1000},
	                {"resistance_per_m", 1},
	                {"arc_height_mm", 20000},
	                {"seeker_range_mm", 200000},
	                {"blast_radius_mm", 12000},
	                {"trail", "smoke"},
	                {"destruction", {{"visual", "test.visual"}, {"chance_permille", 0}, {"affects_missiles", false}}}};
	add_fit(s, "missile-mag",
	        {{"kind", "magazine"},
	         {"compartment", "fore"},
	         {"capacity", 3},
	         {"reload", {{"strategy", "none"}}},
	         {"projectile", payload}},
	        {});
	add_fit(s, "launcher",
	        {{"kind", "weapon"},
	         {"compartment", "fore"},
	         {"magazines", {"missile-mag"}},
	         {"arc_mdeg", 360000},
	         {"fire_ticks", 1000},
	         {"range_mm", 200000}},
	        {{"target", "weapon_target"}});
	if (defence)
	{
		Json tracer = {{"visual", "test.visual"}, {"speed_mm_s", 1000000}, {"lifetime_ticks", 5},
		               {"penetration", 1000},     {"damage", 20},          {"damage_per_m", 2}};
		add_fit(s, "pdc-mag",
		        {{"kind", "magazine"},
		         {"compartment", "fore"},
		         {"capacity", 100},
		         {"reload", {{"strategy", "none"}}},
		         {"projectile", tracer}},
		        {});
		add_fit(s, "pdc",
		        {{"kind", "weapon"},
		         {"compartment", "fore"},
		         {"magazines", {"pdc-mag"}},
		         {"arc_mdeg", 360000},
		         {"fire_ticks", 1},
		         {"range_mm", 150000},
		         {"point_defence", true},
		         {"auto_energy_per_second", 200},
		         {"discrete_energy", 1},
		         {"power_store", "cell"}},
		        {{"target", "weapon_target"}, {"mode", "weapon_mode"}});
	}
	return s;
}
void ordnance_contracts()
{
	{
		auto s = missile_fixture();
		auto obstacle = s["entities"][1];
		obstacle["id"] = "test.cover";
		obstacle["collision"] = {{"shape", "circle"}, {"radius_mm", 3000}, {"mass_grams", 0}};
		obstacle["half_height_mm"] = 1000;
		s["entities"].push_back(obstacle);
		s["maps"][0]["entities"].push_back(
		    {{"id", 4}, {"definition", "test.cover"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		const auto c = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(c, map);
		claim(w);
		submit(w, 101, "launcher", "target", 2);
		advance(w, 30);
		require(w.save()["projectiles"].size() == 1 && w.save()["projectiles"][0]["altitude"].get<int>() > 1000,
		        "physical missile crosses low cover above horizon");
		World resumed;
		resumed.restore(c, w.save());
		advance(w, 40);
		advance(resumed, 40);
		require(w.save() == resumed.save(), "elevated seeker and detonation continuation");
		require(hp(w) < 20000 && w.save()["projectiles"].empty(),
		        "homing missile descends into physical target and applies area payload once");
	}
	{
		auto s = missile_fixture();
		auto &p = s["entities"][0]["systems"]["missile-mag"]["projectile"];
		p["guidance"] = "chaotic";
		p["chaos_mdeg"] = 40000;
		p["turn_mdeg_s"] = 360000;
		p["arc_height_mm"] = 1000;
		const auto c = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(c, map);
		claim(w);
		submit(w, 101, "launcher", "target", 2);
		advance(w, 10);
		const auto old = w.save()["projectiles"][0];
		require(old["position"]["y"].get<int>() != 0, "havok follows a chaotic path");
		World resumed;
		resumed.restore(c, w.save());
		submit(w, 101, "launcher", "target", 3);
		submit(resumed, 101, "launcher", "target", 3);
		advance(w, 10);
		advance(resumed, 10);
		require(w.save() == resumed.save() && w.save()["projectiles"][0]["target"] == 2,
		        "unguided launch payload and intent survive later assignment changes");
	}
	{
		auto s = missile_fixture(true);
		s["entities"][0]["systems"]["missile-mag"]["projectile"]["arc_height_mm"] = 10000;
		const auto c = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(c, map);
		claim(w);
		require(w.claim(201, 2, "drive"), "claim defender");
		auto mode = command(w, 201, "pdc", "mode", 1);
		mode.target = 2;
		require(w.submit(201, mode), "activate defender automatic interception");
		submit(w, 101, "launcher", "target", 2);
		advance(w, 10);
		World resumed;
		resumed.restore(c, w.save());
		advance(w, 30);
		advance(resumed, 30);
		require(w.save() == resumed.save(), "interception material progress survives continuation");
		require(state(w, 2)["systems"]["pdc"]["shots"].get<int>() > 1,
		        "high-penetration low-deposition defence needs repeated hits");
		require(w.save()["projectiles"].empty() && hp(w) == 20000,
		        "point defence physically intercepts elevated missiles before impact");
		World idle;
		idle.load(c, map);
		claim(idle);
		submit(idle, 101, "pdc", "mode", 1);
		advance(idle, 20);
		require(idle.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"] == 99800,
		        "Auto has a real standby draw without targets");
		submit(idle, 101, "pdc", "mode", 0);
		advance(idle, 20);
		require(idle.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"] == 99800,
		        "Off stops standby draw");
		submit(idle, 101, "pdc", "target", 2);
		idle.tick();
		require(state(idle)["systems"]["pdc"]["mode"] == 2, "ship assignment enters Manual");
		submit(idle, 101, "pdc", "target", 0);
		idle.tick();
		require(state(idle)["systems"]["pdc"]["mode"] == 1, "cancel returns Manual to Auto");
	}
}
void effects_contracts()
{
	{
		auto source = powered_fixture();
		add_beam(source, 20, 250);
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"capacity", 1000},
		         {"power_store", "cell"},
		         {"warmup_ticks", 1}},
		        {});
		const auto c = load(source);
		const std::string map = source["maps"][0]["id"];
		World w;
		w.load(c, map);
		claim(w);
		w.tick();
		submit(w, 101, "beam", "target", 2);
		advance(w, 10);
		World resumed;
		resumed.restore(c, w.save());
		advance(w, 10);
		advance(resumed, 10);
		require(hp(w) == 19995 && w.save() == resumed.save(),
		        "continuous shield bypass preserves fractional damage across ticks and resume");
	}
	{
		auto source = powered_fixture();
		source["entities"][0]["resources"]["stores"][0]["discharge_rate"] = 20;
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"capacity", 100},
		         {"initial", 20},
		         {"recharge_per_second", 80},
		         {"off_recharge_per_second", 0},
		         {"sustain_energy_per_second", 20},
		         {"warmup_ticks", 4},
		         {"power_store", "cell"}},
		        {});
		const auto c = load(source);
		World w;
		w.load(c, source["maps"][0]["id"]);
		claim(w);
		advance(w, 10);
		require(state(w)["systems"]["shield"]["warmup"] == 4 && state(w)["systems"]["shield"]["strength"] == 20,
		        "funded sustaining power retains coverage while recharge is unfunded");
	}
	for (const int chance : {0, 1000})
	{
		auto source = powered_fixture();
		add_beam(source, 20);
		auto bomb = source["entities"][1];
		bomb["id"] = "test.bomb";
		bomb["collision"] = {{"shape", "circle"}, {"radius_mm", 3000}, {"mass_grams", 0}};
		bomb["durability"] = 1;
		bomb["destruction"] = {{"sound", "test-core:assets/pixel.png"},
		                       {"chance_permille", chance},
		                       {"damage", 100},
		                       {"radius_mm", 100000}};
		source["entities"].push_back(bomb);
		source["maps"][0]["entities"].push_back(
		    {{"id", 4}, {"definition", "test.bomb"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		const auto c = load(source);
		World w;
		w.load(c, source["maps"][0]["id"]);
		claim(w);
		submit(w, 101, "beam", "target", 2);
		w.tick();
		require(chance ? hp(w) < 20000 && hp(w, 1) < 20000 : hp(w) == 20000 && hp(w, 1) == 20000,
		        "generic destruction odds and faction-independent blast collateral");
		const auto scene = w.scene();
		require(std::any_of(scene.begin(), scene.end(),
		                    [](const auto &v)
		                    {
			                    return v.definition.empty() && !v.sound.empty();
		                    }) == (chance != 0),
		        "audio-only destruction is projected without requiring a visual");
		submit(w, 101, "beam", "target", 0);
		w.tick();
		const auto health = hp(w);
		World resumed;
		resumed.restore(c, w.save());
		advance(w, 20);
		advance(resumed, 20);
		require(hp(w) == health && w.save() == resumed.save(),
		        "shared destruction applies once and survives restoration");
	}
	for (const int chance : {0, 1000})
	{
		auto source = missile_fixture(true);
		auto &ship = source["entities"][0];
		ship["systems"]["launcher"]["fire_ticks"] = 1;
		auto &payload = ship["systems"]["missile-mag"]["projectile"];
		payload["arc_height_mm"] = 0;
		payload["durability"] = 1;
		payload["shield_bypass_permille"] = 1000;
		payload["destruction"] = {{"visual", "test.visual"},
		                          {"chance_permille", chance},
		                          {"damage", 100},
		                          {"radius_mm", 100000},
		                          {"affects_missiles", false}};
		auto &pdc = ship["systems"]["pdc-mag"]["projectile"];
		pdc["penetration"] = 1;
		pdc["damage"] = 1;
		pdc["damage_per_m"] = 1000000;
		ship["systems"]["pdc"]["fire_ticks"] = 1000;
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"capacity", 1000},
		         {"power_store", "cell"},
		         {"warmup_ticks", 1}},
		        {});
		const auto c = load(source);
		World w;
		w.load(c, source["maps"][0]["id"]);
		claim(w);
		require(w.claim(201, 2, "drive"), "claim destruction test defender");
		submit(w, 101, "launcher", "target", 2);
		advance(w, 20);
		auto auto_mode = command(w, 201, "pdc", "mode", 1);
		auto_mode.target = 2;
		require(w.submit(201, auto_mode), "enable interception after missiles clear launch hull");
		w.tick();
		const auto snapshot = w.save();
		int missiles = 0;
		for (const auto &p : snapshot["projectiles"])
			if (p["definition"]["durability"].get<int>() > 0)
				++missiles;
		require(missiles == 2, "intercepted missile destruction excludes neighbouring missiles");
		require(hp(w) == 20000 && (chance ? state(w, 2)["systems"]["shield"]["strength"].get<int>() < 1000
		                                  : state(w, 2)["systems"]["shield"]["strength"].get<int>() == 1000),
		        "interception damage profile has no shield penetration and honours probability");
	}
}
void extended_contracts()
{
	{
		auto inline_fit = powered_fixture();
		add_fit(inline_fit, "shield",
		        {{"kind", "shield"}, {"compartment", "fore"}, {"capacity", 100}, {"power_store", "cell"}},
		        {{"discharge", "shield_discharge"}});
		auto reusable_fit = inline_fit;
		reusable_fit["equipment"] = Json::array();
		for (auto &spec : reusable_fit["entities"][0]["systems"].items())
		{
			const auto kind = spec.value().at("kind").get<std::string>();
			if (kind != "weapon" && kind != "turret" && kind != "magazine" && kind != "repair_team" && kind != "shield")
				continue;
			auto definition = spec.value();
			const auto id = "test.equipment." + spec.key();
			definition["id"] = id;
			reusable_fit["equipment"].push_back(definition);
			spec.value() = {{"template", id}};
		}
		const auto direct = load(inline_fit), reused = load(reusable_fit);
		World a, b;
		a.load(direct, inline_fit["maps"][0]["id"]);
		b.load(reused, inline_fit["maps"][0]["id"]);
		claim(a);
		claim(b);
		submit(a, 101, "gun", "target", 2);
		submit(b, 101, "gun", "target", 2);
		advance(a, 20);
		advance(b, 20);
		require(state(a) == state(b) && a.save()["projectiles"] == b.save()["projectiles"],
		        "combat equipment templates retain inline mechanism behavior through real composition");
	}

	{
		auto s = powered_fixture();
		auto &ship = s["entities"][0];
		auto payload = ship["systems"]["gun"]["projectile"];
		Json magazines = Json::array({"mag"});
		for (int n = 1; n <= 12; ++n)
		{
			const auto id = "ammo-" + std::to_string(n);
			magazines.push_back(id);
			payload["damage"] = 40 + n;
			add_fit(s, id,
			        {{"kind", "magazine"},
			         {"compartment", "fore"},
			         {"capacity", 3},
			         {"reload", {{"strategy", "none"}}},
			         {"projectile", payload}},
			        {});
		}
		ship["systems"]["gun"]["magazines"] = magazines;
		ship["systems"]["gun"]["load_ticks"] = 4;
		for (auto &c : ship["control_layout"]["controllers"])
			if (c["id"] == "gun")
				c["commands"].push_back({{"id", "magazine"}, {"action", "magazine_select"}, {"consumers", {"gun"}}});
		const auto catalog = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(catalog, map);
		claim(w);
		advance(w, 4);
		require(state(w)["systems"]["gun"]["loaded_magazine"] == 0, "weapon loads from selected magazine");
		submit(w, 101, "gun", "magazine", 12);
		w.tick();
		require(state(w)["systems"]["gun"]["unloading"] == 3 && state(w)["systems"]["mag"]["reserved_return"] == 1,
		        "selection begins timed unload without returning committed stock");
		World unloading;
		unloading.restore(catalog, w.save());
		submit(w, 101, "gun", "enabled", 0);
		submit(unloading, 101, "gun", "enabled", 0);
		advance(w, 5);
		advance(unloading, 5);
		require(state(w)["systems"]["gun"]["unloading"] == 3 && w.save() == unloading.save(),
		        "disabled unload pauses and resumes exactly");
		submit(w, 101, "gun", "enabled", 1);
		submit(w, 101, "gun", "magazine", 0);
		w.tick();
		require(state(w)["systems"]["gun"]["unloading"] == 2, "switching back cannot bypass committed unload");
		submit(w, 101, "gun", "magazine", 12);
		advance(w, 6);
		require(state(w)["systems"]["mag"]["ammo"] == 3 && state(w)["systems"]["ammo-12"]["ammo"] == 2,
		        "unload returns stock and loads arbitrary compatible type");
		require(state(w)["systems"]["gun"]["loaded_magazine"] == 12, "more than two magazine types supported");
		World resumed;
		resumed.restore(catalog, w.save());
		submit(w, 101, "gun", "target", 2);
		submit(resumed, 101, "gun", "target", 2);
		advance(w, 12);
		advance(resumed, 12);
		require(w.save() == resumed.save(), "loaded ammunition commitments survive continuation");
	}
	{
		auto s = powered_fixture();
		add_beam(s);
		const auto catalog = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(catalog, map);
		claim(w);
		submit(w, 101, "beam", "target", 2);
		advance(w, 20);
		require(hp(w) == 19980, "beam deposits constant authored damage per second");
		require(w.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"] == 99960,
		        "beam pays continuous energy");
		submit(w, 101, "beam", "enabled", 0);
		advance(w, 10);
		require(hp(w) == 19980, "disabled beam has no damage");
		auto blocker = s["entities"][1];
		blocker["id"] = "test.cover";
		blocker["collision"] = {{"shape", "circle"}, {"radius_mm", 5000}, {"mass_grams", 0}};
		s["entities"].push_back(blocker);
		s["maps"][0]["entities"].push_back(
		    {{"id", 4}, {"definition", "test.cover"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		const auto blocked = load(s);
		World b;
		b.load(blocked, map);
		claim(b);
		submit(b, 101, "beam", "target", 2);
		advance(b, 20);
		require(hp(b) == 20000, "solid cover stops beam before ship");
	}
	{
		auto s = powered_fixture();
		add_beam(s, 400, 250);
		for (int i = 0; i < 2; ++i)
			add_fit(s, "shield-" + std::to_string(i),
			        {{"kind", "shield"},
			         {"compartment", "fore"},
			         {"bearing_mdeg", 180000},
			         {"arc_mdeg", 90000},
			         {"layer", i},
			         {"capacity", i ? 1000 : 100},
			         {"recharge_per_second", 1},
			         {"off_recharge_per_second", 0},
			         {"warmup_ticks", 4},
			         {"sustain_energy_per_second", 20},
			         {"power_store", "cell"}},
			        {{"discharge", "shield_discharge"}});
		const auto catalog = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(catalog, map);
		claim(w);
		advance(w, 5);
		submit(w, 101, "beam", "target", 2);
		advance(w, 20);
		require(hp(w) == 19900, "shield bypass applies once across overlapping layers");
		require(state(w, 2)["systems"]["shield-0"]["strength"].get<int>() <= 1 &&
		            state(w, 2)["systems"]["shield-1"]["strength"].get<int>() < 1000,
		        "authored first layer depletes before second");
		World resumed;
		resumed.restore(catalog, w.save());
		advance(w, 10);
		advance(resumed, 10);
		require(w.save() == resumed.save(), "shield and beam continuation is exact");
	}
	{
		auto s = powered_fixture();
		add_fit(s, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"capacity", 100},
		         {"initial", 20},
		         {"recharge_per_second", 40},
		         {"off_recharge_per_second", 20},
		         {"warmup_ticks", 4},
		         {"sustain_energy_per_second", 20},
		         {"discharge_per_second", 20},
		         {"power_store", "cell"}},
		        {{"discharge", "shield_discharge"}});
		const auto catalog = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(catalog, map);
		claim(w);
		submit(w, 101, "shield", "enabled", 0);
		advance(w, 20);
		require(state(w)["systems"]["shield"]["strength"] == 40 && state(w)["systems"]["shield"]["warmup"] == 0,
		        "off shield retains strength and regenerates slowly using power");
		const auto before = w.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"].get<std::int64_t>() +
		                    state(w)["systems"]["shield"]["strength"].get<std::int64_t>();
		submit(w, 101, "shield", "discharge", 1);
		advance(w, 20);
		const auto after = w.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"].get<std::int64_t>() +
		                   state(w)["systems"]["shield"]["strength"].get<std::int64_t>();
		require(before == after, "redistribution conserves stored strength and real battery energy");
		submit(w, 101, "shield", "enabled", 1);
		w.tick();
		require(state(w)["systems"]["shield"]["warmup"] == 1, "raised shield ramps coverage");
	}
	{
		auto s = powered_fixture();
		add_beam(s, 20);
		auto &ship = s["entities"][0];
		ship["systems"]["radar"]["compartment"] = "aft";
		ship["systems"]["radar"]["faults"] = Json::array({{{"id", "receiver"},
		                                                   {"symptom", "Unstable reception"},
		                                                   {"diagnosis", "Receiver damaged"},
		                                                   {"chance_permille", 1000},
		                                                   {"damage_threshold", 1},
		                                                   {"effectiveness_permille", 500},
		                                                   {"repair_work", 100}}});
		const auto catalog = load(s);
		const std::string map = s["maps"][0]["id"];
		World w;
		w.load(catalog, map);
		claim(w);
		require(w.claim(201, 2, "drive") && w.claim(202, 2, "observer"), "claim damaged vessel seats");
		submit(w, 101, "beam", "target", 2);
		advance(w, 2);
		submit(w, 101, "beam", "target", 0);
		w.tick();
		const auto faults = w.observe(201)->gameplay["combat"]["faults"];
		require(!faults.empty() && faults[0]["diagnosis"] == "", "exact fault hidden until team inspection");
		auto enable = command(w, 202, "team-a", "enabled", 1);
		enable.target = 2;
		require(w.submit(202, enable), "enable inspecting team");
		auto repair = command(w, 202, "team-a", "assignment", 2);
		repair.target = 2;
		require(w.submit(202, repair), "assign inspecting team");
		advance(w, 12);
		require(w.observe(201)->gameplay["combat"]["faults"][0]["diagnosed"].get<bool>(),
		        "team arrival reveals diagnosis to owner");
	}
}

void review_contracts()
{
	{
		auto source = powered_fixture();
		source["entities"][0]["resources"]["stores"][0]["initial"] = 50000;
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"capacity", 100},
		         {"initial", 20},
		         {"energy_per_strength", 1},
		         {"discharge_per_second", 1},
		         {"discharge_efficiency_permille", 900},
		         {"power_store", "cell"}},
		        {{"discharge", "shield_discharge"}});
		const auto c = load(source);
		World w;
		w.load(c, source["maps"][0]["id"]);
		claim(w);
		submit(w, 101, "shield", "enabled", 0);
		submit(w, 101, "shield", "discharge", 1);
		advance(w, 53);
		World resumed;
		resumed.restore(c, w.save());
		advance(w, 147);
		advance(resumed, 147);
		require(w.save() == resumed.save(), "fractional conversion and rate budgets persist across continuation");
		require(state(w)["systems"]["shield"]["strength"] == 10 &&
		            w.save()["bodies"][0]["resources"]["network"]["stores"][0]["energy"] == 50009,
		        "low-rate inefficient discharge transfers exactly nine energy for ten strength");
	}
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		for (auto &c : ship["control_layout"]["controllers"])
		{
			if (c["id"] == "team-a")
				c["commands"].push_back({{"id", "priority"}, {"action", "repair_system"}, {"consumers", {"team-a"}}});
			if (c["id"] == "team-b")
				c["commands"][0]["initial"] = 0;
		}
		const auto c = load(source);
		World w;
		w.load(c, source["maps"][0]["id"]);
		claim(w);
		bool repair_choice = false;
		const auto support_view = w.observe(102);
		for (const auto &control : support_view->gameplay["controls"])
			if (control.value("combat_choice", std::string{}) == "repair" &&
			    control.value("value", std::int64_t{0}) == 1)
				repair_choice = true;
		require(repair_choice, "repair compartment choices are exposed through the Console action model");
		auto checkpoint = w.save();
		checkpoint["bodies"][0]["combat"]["compartments"][0]["hp"] = 0;
		checkpoint["bodies"][0]["combat"]["compartments"][0]["disabled"] = true;
		w.restore(c, checkpoint);
		submit(w, 102, "team-a", "priority", 1); // Sorted equipment roster: gun.
		advance(w, 30);
		require(state(w)["compartments"][0]["hp"].get<int>() >= 20 &&
		            !state(w)["compartments"][0]["disabled"].get<bool>(),
		        "specific system repair restores its disabling compartment even without a fault");
	}
	{
		auto source = controller_fixture();
		auto &ship = source["entities"][0];
		ship["motion_model"] = "vector";
		ship["control_layout"]["profiles"].erase(1);
		ship["combat"] = fixture()["entities"][0]["combat"];
		const Json fault = Json::array({{{"id", "output"},
		                                 {"symptom", "Reduced output"},
		                                 {"diagnosis", "Actuator damaged"},
		                                 {"effectiveness_permille", 500},
		                                 {"repair_work", 100}}});
		for (auto &spec : ship["systems"])
		{
			spec["compartment"] = "fore";
			spec["faults"] = fault;
		}
		add_fit(source, "steering",
		        {{"kind", "steering"}, {"turn_mdeg_s", 90000}, {"compartment", "fore"}, {"faults", fault}},
		        {{"heading", "heading_mdeg"}});
		source["entities"][0]["control_layout"]["controllers"].back()["commands"].erase(0);
		source["entities"].push_back(fixture()["entities"][1]);
		const auto c = load(source);
		World healthy;
		healthy.load(c, source["maps"][0]["id"]);
		claim(healthy);
		auto checkpoint = healthy.save();
		for (auto &states : checkpoint["bodies"][0]["combat"]["faults"])
			states[0]["active"] = true;
		World impaired;
		impaired.restore(c, checkpoint);
		submit(healthy, 101, "propulsion", "speed", 17000);
		submit(impaired, 101, "propulsion", "speed", 17000);
		advance(healthy, 10);
		advance(impaired, 10);
		const auto a = healthy.inspect().at(0), b = impaired.inspect().at(0);
		require(b.vx > 0 && b.vx < a.vx, "partial output faults reduce basic engine acceleration");
		submit(healthy, 101, "steering", "heading", 180000);
		submit(impaired, 101, "steering", "heading", 180000);
		advance(healthy, 10);
		advance(impaired, 10);
		require(impaired.inspect().at(0).heading != healthy.inspect().at(0).heading,
		        "partial output faults reduce basic steering traverse");
	}
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			source["entities"][1]["render"] = ship["render"];
			ship["systems"]["gun"]["muzzle_marker"] = "mount";
			ship["systems"]["second"].erase("turret");
			ship["systems"]["second"]["muzzle_marker"] = "mount";
			ship["systems"]["second"]["x_mm"] = 3000;
			const auto catalog = load(source);
			World w;
			w.load(catalog, "test.map");
			claim(w);
			submit(w, 101, "gun", "target", 2);
			w.tick();
			const auto scene = w.scene();
			const auto own = std::find_if(scene.begin(), scene.end(),
			                              [](const auto &b)
			                              {
				                              return b.id == 1;
			                              });
			require(own != scene.end() && own->muzzles.size() == 2,
			        "hull and turret muzzle bindings reach the scene through production content");
			require(std::any_of(scene.begin(), scene.end(),
			                    [](const auto &b)
			                    {
				                    return b.attack_source == 1 && b.attack_weapon == "gun" && b.muzzle_blend > 0;
			                    }),
			        "new projectiles retain their source weapon for sprite muzzle placement");
			World resumed;
			resumed.restore(catalog, w.save());
			require(resumed.save() == w.save(), "projectile muzzle identity survives save and restore");
			ship["systems"]["gun"]["muzzle_marker"] = "missing";
			rejects(
			    [&]
			    {
				    load(source);
			    },
			    "missing turret muzzle marker is rejected");
			ship["systems"]["gun"]["muzzle_marker"] = "mount";
			ship["systems"]["second"]["muzzle_marker"] = "missing";
			rejects(
			    [&]
			    {
				    load(source);
			    },
			    "missing hull muzzle marker is rejected");
		}

		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			Json duties = Json::array();
			for (const auto &c : ship["control_layout"]["controllers"])
			{
				Json duty{{"id", "controller-" + c.at("id").get<std::string>()},
				          {"label", "Watch"},
				          {"controller", c.at("id")},
				          {"console", c.at("console")},
				          {"department", "engineering"},
				          {"compartment", "fore"}};
				if (c.at("id") == "team-a")
					duty["effects"] = {{"team-a", {"repair"}}};
				duties.push_back(duty);
			}
			for (const auto *team : {"team-a", "team-b"})
				duties.push_back({{"id", std::string("leader-") + team},
				                  {"label", "Team leader"},
				                  {"team", team},
				                  {"department", "engineering"},
				                  {"compartment", "fore"},
				                  {"effects", {{team, {"repair"}}}}});
			ship["personnel"] = {
			    {"departments",
			     Json::array({{{"id", "engineering"}, {"label", "Engineering"}, {"station", "operations"}}})},
			    {"traits", Json::array({{{"id", "expert"}, {"label", "Expert"}, {"bonuses", {{"repair", 250}}}}})},
			    {"duties", duties},
			    {"officers", Json::array({{{"id", "controller"},
			                               {"name", "Controller officer"},
			                               {"department", "engineering"},
			                               {"compartment", "fore"},
			                               {"duty", "controller-team-a"},
			                               {"traits", {"expert"}}},
			                              {{"id", "leader"},
			                               {"name", "Team officer"},
			                               {"department", "engineering"},
			                               {"compartment", "fore"},
			                               {"duty", "leader-team-a"},
			                               {"traits", {"expert"}}}})}};
			ship["combat"]["decks"][0]["compartments"][1]["initial_hp"] = 0;
			const auto content = load(source);
			World w;
			w.load(content, "test.map");
			claim(w);
			const auto roster = w.observe(102)->gameplay.at("personnel");
			bool stacked = false;
			for (const auto &d : roster.at("duties"))
				if (d.at("id") == "leader-team-a")
					stacked = d.at("effects")[0].at("multiplier") == 1500;
			require(stacked, "Controller and team officer contributions sum once on physical team");
			require(w.submit(102, command(w, 102, "team-a", "assignment", 2)), "move team with its officer");
			for (int i = 0; i < 5; ++i)
				w.tick();
			require(w.observe(102)->gameplay.at("personnel").at("officers")[1].at("present").get<bool>(),
			        "assigned officer accompanies travelling team");
			World resumed;
			resumed.restore(content, w.save());
			for (int i = 0; i < 20; ++i)
			{
				w.tick();
				resumed.tick();
			}
			require(w.save() == resumed.save(), "moving team and attached officer survive restore");
			auto command_officer = OperatingCommand{1, "personnel.assign", 0, "controller"};
			command_officer.text = "leader-team-b";
			command_officer.generation = w.observe(102)->gameplay.at("personnel").at("revision");
			require(w.submit(102, command_officer), "assign officer to another physical team");
			w.tick();
			require(!w.observe(102)->gameplay.at("personnel").at("officers")[0].at("present").get<bool>(),
			        "officer must reach moving team's actual compartment");
		}

		auto source = fixture();
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			source = missile_fixture(true);
			add_beam(source, 400);
			source["entities"][0]["systems"]["launcher"]["load_ticks"] = 4;
			source["entities"][0]["combat"]["decks"][0]["compartments"][1]["initial_hp"] = 0;
			add_fit(source, "shield",
			        {{"kind", "shield"},
			         {"compartment", "fore"},
			         {"capacity", 1000},
			         {"power_store", "cell"},
			         {"warmup_ticks", 20}},
			        {{"discharge", "shield_discharge"}});
		}
		auto catalog = load(source);
		const auto map = source["maps"][0]["id"].get<std::string>();
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			World world;
			world.load(catalog, map);
			claim(world);
			submit(world, 101, "launcher", "target", 2);
			submit(world, 101, "beam", "target", 3);
			submit(world, 102, "team-a", "enabled", 1);
			submit(world, 102, "team-a", "assignment", 2);
			advance(world, 6);
			World resumed;
			resumed.restore(catalog, world.save());
			advance(resumed, 25);
			auto continued = resumed.save();
			// Synthetic ZIP creation timestamps are not simulation state. Restore
			// above still validates the actual pack identity within each run.
			continued.erase("packs");
			std::cout << sha256(json_bytes(continued)) << '\n';
			return 0;
		}

		{
			World w;
			w.load(catalog, map);
			claim(w);
			require(!w.submit(102, command(w, 101, "gun", "target", 2)), "Support cannot fire Tactical weapons");
			submit(w, 101, "gun", "target", 2);
			advance(w, 12);
			require(hp(w) < 200, "physical shot crosses compartments and damages target");
			const auto saved = w.save();
			World resumed;
			resumed.restore(catalog, saved);
			advance(w, 15);
			advance(resumed, 15);
			require(w.save() == resumed.save(),
			        "projectile, magazine and Controller continuation resumes deterministically");
			submit(w, 101, "second", "target", 3);
			w.tick();
			const auto s = state(w);
			require(s.at("systems").at("gun").at("target") == 3 && s.at("systems").at("second").at("target") == 3,
			        "shared turret retargets all active weapons");
			submit(w, 101, "gun", "target", 2);
			w.tick();
			require(state(w).at("systems").at("second").at("target") == 2,
			        "reasserting a weapon's original target retakes the shared turret after sibling retargeting");
			submit(w, 101, "gun", "target", 0);
			w.tick();
			require(state(w).at("systems").at("second").at("target") == 2,
			        "cancelling one weapon retains its neighbour");
			const auto observer = w.observe(102);
			require(observer->gameplay.at("combat").at("compartments").size() == 2,
			        "repair Console sees structural map");
			require(w.observe(101)->gameplay.at("combat").at("compartments").empty(),
			        "Tactical does not receive Repair map truth");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["combat"]["decks"][0]["compartments"][1]["initial_hp"] = 0;
			const auto c = load(setup);
			World one, two;
			one.load(c, map);
			two.load(c, map);
			claim(one);
			claim(two);
			submit(one, 102, "team-b", "enabled", 0);
			submit(one, 102, "team-a", "assignment", 2);
			submit(two, 102, "team-a", "assignment", 2);
			submit(two, 102, "team-b", "assignment", 2);
			advance(one, 30);
			advance(two, 30);
			const auto a = state(one).at("compartments")[1].at("hp").get<int>(),
			           b = state(two).at("compartments")[1].at("hp").get<int>();
			require(a > 0 && b > a && b < 2 * a, "two teams repair faster with diminishing returns after travel");
			require(!state(two).at("compartments")[1].at("disabled").get<bool>(),
			        "structural recovery clears disabled latch at threshold");
			const auto active_states = state(two).at("systems");
			require(!active_states.at("mag").contains("target") && !active_states.at("gun").contains("strength") &&
			            !active_states.at("team-a").contains("ammo"),
			        "continuation stores only the fitted mechanism state");
			auto foreign = two.save();
			foreign["bodies"][0]["combat"]["systems"]["mag"]["beam_tick"] = 0;
			bool foreign_refused = false;
			try
			{
				World candidate;
				candidate.restore(c, foreign);
			}
			catch (...)
			{
				foreign_refused = true;
			}
			require(foreign_refused, "continuation rejects fields from another mechanism kind");
			auto bad = two.save();
			bad["bodies"][0]["combat"]["systems"]["team-a"]["travel"] = -1;
			bool refused = false;
			try
			{
				two.restore(c, bad);
			}
			catch (...)
			{
				refused = true;
			}
			require(refused, "malformed team continuation rejected transactionally");
		}
		{
			auto setup = fixture();
			setup["entities"][0]["systems"]["gun"]["bearing_mdeg"] = 30000;
			setup["entities"][0]["systems"]["gun"]["y_mm"] = 5000;
			setup["entities"][0]["systems"]["gun"]["arc_mdeg"] = 2000;
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 30);
			require(state(w).at("systems").at("gun").at("shots").get<int>() >= 2,
			        "turret compensates for hosted gun bearing and sustains automatic fire");
		}
		{
			auto setup = fixture();
			setup["entities"][0]["systems"]["mag"]["reload"]["strategy"] = "none";
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 100);
			require(state(w).at("systems").at("mag").at("ammo") == 0 &&
			            state(w).at("systems").at("gun").at("shots") == 3,
			        "finite no-reload magazine exhausts exactly");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 1000},
			                                              {"initial", 1000},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["gun"]["power_store"] = "cell";
			ship["systems"]["gun"]["discrete_energy"] = 1;
			ship["systems"]["mag"]["power_store"] = "cell";
			ship["systems"]["mag"]["reload"] = {
			    {"strategy", "energy"}, {"ticks", 1000}, {"amount", 2}, {"energy_per_second", 20}};
			auto &rooms = setup["entities"][0]["combat"]["decks"][0]["compartments"];
			setup["maps"][0]["combat_duel"] = true;
			setup["maps"][0]["entities"].erase(2);
			rooms.erase(1);
			rooms[0]["x_mm"] = 0;
			rooms[0]["half_width_mm"] = 10000;
			setup["entities"][0]["combat"]["connections"] = Json::array();
			auto &p = setup["entities"][0]["systems"]["gun"]["projectile"];
			p["penetration"] = 100;
			p["damage"] = 1000;
			p["damage_per_m"] = 200;
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 12);
			require(w.save().at("bodies").at(1).at("destructible").at("destroyed_tick").get<std::int64_t>() >= 0,
			        "all compartments zero destroys ship");
			require(w.public_board(1).at("combat_result") == "Victory",
			        "duel outcome survives physical ship loss on the public Viewscreen board");
			const auto scene = w.scene();
			require(std::count_if(scene.begin(), scene.end(),
			                      [](const auto &b)
			                      {
				                      return !b.sound.empty();
			                      }) == 1,
			        "ship destruction emits a single shared billboard and sound event");
			const auto snapshot = w.save();
			World restored;
			restored.restore(c, snapshot);
			advance(w, 10);
			advance(restored, 10);
			require(w.save() == restored.save(), "destruction continuation survives restoration");
			require(w.save().at("bodies")[0].at("resources").at("network").at("stores")[0].at("energy") ==
			            snapshot.at("bodies")[0].at("resources").at("network").at("stores")[0].at("energy"),
			        "ended duel stops weapon and reload energy costs while session continues");
			require(!state(w).at("systems").at("gun").at("ready").get<bool>(),
			        "ended duel clears stale weapon readiness");
		}
		{
			auto long_source = fixture();
			long_source["entities"][0]["systems"]["gun"]["fire_ticks"] = 1000;
			for (auto &controller : long_source["entities"][0]["control_layout"]["controllers"])
				if (controller["id"] == "team-a" || controller["id"] == "team-b")
					controller["commands"][0]["initial"] = 0;
			auto side_source = long_source;
			side_source["maps"][0]["entities"][1]["heading_mdeg"] = 90000;
			auto long_content = load(long_source), side_content = load(side_source);
			World longitudinal, transverse;
			longitudinal.load(long_content, map);
			transverse.load(side_content, map);
			claim(longitudinal);
			claim(transverse);
			submit(longitudinal, 101, "gun", "target", 2);
			submit(transverse, 101, "gun", "target", 2);
			advance(longitudinal, 20);
			advance(transverse, 20);
			require(hp(longitudinal) == 160 && hp(transverse) == 180,
			        "long hull path deposits full payload; transverse path deposits half without double-counting "
			        "compartment boundary");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 100},
			                                              {"initial", 10},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["gun"]["power_store"] = "cell";
			ship["systems"]["gun"]["discrete_energy"] = 10;
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 40);
			require(state(w).at("systems").at("gun").at("shots") == 1,
			        "energy-funded weapon launches only its affordable shot");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 100},
			                                              {"initial", 5},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["mag"]["initial"] = 0;
			ship["systems"]["mag"]["power_store"] = "cell";
			ship["systems"]["mag"]["reload"] = {
			    {"strategy", "energy"}, {"ticks", 10}, {"amount", 2}, {"energy_per_second", 20}};
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			advance(w, 20);
			require(state(w).at("systems").at("mag").at("ammo") == 0, "unfunded reload does not invent ammunition");
			const auto snapshot = w.save();
			World resumed;
			resumed.restore(c, snapshot);
			advance(w, 20);
			advance(resumed, 20);
			require(w.save() == resumed.save(), "partly funded reload progress resumes without refunds");
		}
		{
			auto invalid = fixture();
			invalid["entities"][0]["combat"]["connections"].push_back(
			    {{"from", "aft"}, {"to", "fore"}, {"travel_ticks", 5}});
			rejects(
			    [&]
			    {
				    load(invalid);
			    },
			    "duplicate undirected repair edges are refused");
		}
		{
			auto invalid = fixture();
			invalid["entities"][0]["systems"]["gun"]["magazine"] = "mount";
			bool refused = false;
			try
			{
				load(invalid);
			}
			catch (...)
			{
				refused = true;
			}
			require(refused, "incompatible magazine binding rejected");
		}
		{
			auto setup = fixture();
			setup["entities"][1]["collision"] = {{"shape", "circle"}, {"radius_mm", 4000}, {"mass_grams", 0}};
			setup["maps"][0]["entities"].push_back(
			    {{"id", 4}, {"definition", "test.visual"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 30);
			require(hp(w) == 200, "solid cover intercepts physical projectile before the target");
		}
		{
			auto setup = fixture();
			setup["entities"][1]["collision"] = {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 0}};
			setup["entities"][1]["render"]["sprite"] = setup["entities"][0]["render"]["sprite"];
			setup["entities"][1]["render"]["images"] = Json::array();
			setup["maps"][0]["regions"] =
			    Json::array({{{"id", "remote-cover"},
			                  {"shape", "circle"},
			                  {"x_mm", 605000},
			                  {"y_mm", 5000},
			                  {"radius_mm", 2000},
			                  {"asteroids",
			                   {{"spacing_mm", 10000},
			                    {"jitter_mm", 0},
			                    {"density", .01},
			                    {"clustering_permille", 0},
			                    {"visual_distance_mm", 1000},
			                    {"decorative_fill_permille", 0},
			                    {"types", Json::array({{{"definition", "test.visual"}}})}}}}});
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			w.tick();
			auto saved = w.save();
			require(saved.value("asteroid_fields", Json::array()).empty() && saved.at("projectiles").size() == 1,
			        "remote physical cover starts outside all ship streaming windows");
			// Resume an in-flight shot near the remote field, independently of ships.
			saved["projectiles"][0]["position"] = {{"x", 600000}, {"y", 5000}};
			saved["projectiles"][0]["velocity"] = {{"x", 100000}, {"y", 0}};
			w.restore(c, saved);
			w.tick();
			require(w.save().at("projectiles").empty() && !w.save().at("asteroid_fields").empty(),
			        "projectile sweep loads and collides with distant solid asteroid cover");
		}
		{
			auto wide = fixture();
			wide["maps"][0]["entities"][2]["x_mm"] = -60000;
			auto narrow = wide;
			narrow["entities"][0]["systems"]["mount"]["arc_mdeg"] = 180000;
			auto a = load(wide), b = load(narrow);
			World top, bottom;
			top.load(a, map);
			bottom.load(b, map);
			claim(top);
			claim(bottom);
			submit(top, 101, "gun", "target", 3);
			submit(bottom, 101, "gun", "target", 3);
			advance(top, 65);
			advance(bottom, 65);
			require(state(top).at("systems").at("gun").at("shots").get<int>() > 0 &&
			            state(bottom).at("systems").at("gun").at("shots") == 0,
			        "270 degree turret reaches rear quarter while 180 degree turret cannot");
			require(state(bottom).at("systems").at("mount").at("heading").get<int>() <= 90000,
			        "traversal respects physical arc boundary");
			const auto projected = network::read_observation(network::observation_json(*bottom.observe(101)), b);
			require(projected.gameplay.at("combat").at("arcs").size() == 3,
			        "combat arc and readiness projection survives remote serialization");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["systems"]["drive"] = {{"kind", "engine"},
			                            {"max_speed_mm_s", 100000},
			                            {"acceleration_mm_s2", 1000000},
			                            {"compartment", "fore"}};
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "drive"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands",
			      Json::array(
			          {{{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"drive"}}},
			           {{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"drive"}}}})}});
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("drive");
			ship["stations"][0]["consoles"][0]["feeds"].push_back("drive");
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			advance(w, 5);
			require(hp(w, 1) == 200 && hp(w, 2) == 200, "stationary ships do not accumulate collision damage");
			submit(w, 101, "drive", "speed", 100000);
			advance(w, 30);
			require(hp(w, 1) < 200 && hp(w, 2) < 200,
			        "closing ship collision damages physical compartments on both ships");
		}
		{
			auto setup = fixture();
			for (auto &controller : setup["entities"][0]["control_layout"]["controllers"])
				if (controller.at("id") == "gun")
					controller["policy"] = "combat";

			setup["maps"][0]["factions"] =
			    Json::array({{{"id", "blue"}, {"label", "Blue"}}, {{"id", "red"}, {"label", "Red"}}});
			setup["maps"][0]["relationships"] = Json::array({{{"a", "blue"}, {"b", "red"}, {"relation", "hostile"}}});
			setup["maps"][0]["entities"][0]["faction"] = "blue";
			setup["maps"][0]["entities"][1]["faction"] = "red";
			setup["maps"][0]["entities"][2]["faction"] = "red";
			auto visible = load(setup);
			World ai;
			ai.load(visible, map);
			advance(ai, 25);
			require(state(ai).at("systems").at("gun").at("shots").get<int>() > 0,
			        "unoccupied Tactical uses ordinary admitted combat policy against visible ship contacts");

			require(state(ai).at("systems").at("gun").at("target") == 2,
			        "equidistant hostile candidates use stable identity");
			auto closer = ai.save();
			closer["bodies"][2]["body"]["y"] = 30000;
			ai.restore(visible, closer);
			ai.tick();
			require(state(ai).at("systems").at("gun").at("target") == 2,
			        "a still-valid target survives a nearer alternative");
			for (const auto *disposition : {"friendly", "neutral"})
			{
				auto peaceful = setup;
				peaceful["maps"][0]["relationships"][0]["relation"] = disposition;
				World passive;
				passive.load(load(peaceful), map);
				advance(passive, 25);
				require(state(passive).at("systems").at("gun").at("shots") == 0,
				        "autonomous combat excludes friendly and neutral contacts");
			}
			setup["entities"][0]["systems"]["radar"]["range_mm"] = 1000;
			auto hidden = load(setup);
			World blind;
			blind.load(hidden, map);
			advance(blind, 25);
			require(state(blind).at("systems").at("gun").at("shots") == 0,
			        "combat AI cannot target ships absent from its permitted sensor feed");
		}

		{
			auto source = fixture();
			for (auto &owner : source["entities"][0]["control_layout"]["controllers"])
				if (owner.at("id") == "team-b")
				{
					owner.erase("fallback");
					owner["tree"] = "test.request-repair";
					owner["commands"][0]["initial"] = 0;
				}
			source["trees"].push_back(Json::parse(
			    R"json({"id":"test.request-repair","context":"controller","initial":"request","states":[{"id":"request","entry":[{"action":"coordination.request","arguments":["repair","observer","team-a",1,1,1,300]}]}]})json"));
			const auto catalog = load(source);
			World world;
			world.load(catalog, map);
			auto damaged = world.save();
			damaged["bodies"][0]["combat"]["compartments"][0]["hp"] = 50;
			world.restore(catalog, damaged);
			advance(world, 12);
			const auto midway = world.save();
			for (const auto &row : midway.at("bodies")[0].at("console_controllers").at("controllers"))
				if (row.at("id") == "team-a")
					require(row.at("commands")[1].at("value") == 1,
					        "accepted repair request assigns ordinary repair-team channel");
			World resumed;
			resumed.restore(catalog, midway);
			advance(world, 100);
			advance(resumed, 100);
			require(world.save() == resumed.save(), "repair commitment resumes without repeating messages");
			const auto repaired = world.save();
			for (const auto &row : repaired.at("bodies")[0].at("console_controllers").at("controllers"))
				if (row.at("id") == "team-b")
					require(row.at("coordination")[1][0].at("status") == 3,
					        "repair completion follows observed restored compartment");
		}
		{
			auto source = fixture();
			add_fit(
			    source, "drive",
			    {{"kind", "engine"}, {"compartment", "aft"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 2000}},
			    {{"speed", "speed_mm_s"}});
			for (auto &owner : source["entities"][0]["control_layout"]["controllers"])
				if (owner.at("id") == "gun")
				{
					owner.erase("fallback");
					owner["tree"] = "test.request-range";
				}
			source["trees"].push_back(Json::parse(
			    R"json({"id":"test.request-range","context":"controller","initial":"request","states":[{"id":"request","entry":[{"action":"coordination.request","arguments":["manoeuvre","drive","drive",2,40000,1,300]}]}]})json"));
			source["maps"][0]["factions"] =
			    Json::array({{{"id", "blue"}, {"label", "Blue"}}, {{"id", "red"}, {"label", "Red"}}});
			source["maps"][0]["relationships"] = Json::array({{{"a", "blue"}, {"b", "red"}, {"relation", "hostile"}}});
			source["maps"][0]["entities"][0]["faction"] = "blue";
			source["maps"][0]["entities"][1]["faction"] = "red";
			const auto catalog = load(source);
			World world;
			world.load(catalog, map);
			advance(world, 20);
			const auto saved = world.save();
			for (const auto &row : saved.at("bodies")[0].at("console_controllers").at("controllers"))
				if (row.at("id") == "drive")
					require(row.at("commands")[1].at("value").get<int>() > 0,
					        "accepted manoeuvre request drives ordinary propulsion");
			World resumed;
			resumed.restore(catalog, saved);
			advance(world, 10);
			advance(resumed, 10);
			require(world.save() == resumed.save(), "manoeuvre commitment continuation is deterministic");
			require(world.claim(101, 1, "drive"), "operator takes over manoeuvre recipient");
			const auto handed = world.save();
			for (const auto &row : handed.at("bodies")[0].at("console_controllers").at("controllers"))
				if (row.at("id") == "drive")
					require(row.at("action").at("status") == "cancelled", "human handover cancels coordinated action");
		}
		extended_contracts();
		review_contracts();
		ordnance_contracts();
		effects_contracts();
		std::cout << "Combat system contracts passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
