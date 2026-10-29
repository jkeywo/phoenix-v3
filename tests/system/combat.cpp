#include "../support/active_effects.hpp"
#include "../support/assertions.hpp"
#include "../support/stock_snapshot.hpp"
#include "../support/controller_fixture.hpp"
#include "../support/combat_fixture.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>
import phoenix.content.documents;
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.distribution;
import phoenix.content.resources;
import phoenix.content.scenarios;
import phoenix.graphics.audio_policy;
import phoenix.simulation;
import phoenix.networking.projection;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	return combat_fixture();
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
		{
			auto result = body.at("combat");
			for (auto &[system, state] : result["systems"].items())
				if (state.contains("stock"))
				{
					const auto &stock = saved_magazine_stock(body, system);
					state["ammo"] = saved_ammunition(body, system);
					std::int64_t reserved = 0;
					for (const auto &[key, claim] : stock.at("capacity_claims").items())
						if (key.starts_with("weapon:"))
							reserved += claim.at("quantity").get<std::int64_t>();
					state["reserved_return"] = reserved;
					state["refill"] = 0;
					if (body.contains("processors") && body.at("processors").contains(system))
						state["refill"] = body.at("processors").at(system).at("progress");
				}
			if (body.contains("shields"))
				for (const auto &c : body.at("shields").at("contributions"))
					if (!c.at("equipment").get<std::string>().empty())
						result["systems"][c.at("equipment").get<std::string>()] = c.at("state");
			return result;
		}
	throw std::runtime_error("missing combat state " + std::to_string(id) + " " + w.save().at("bodies").dump());
}
Json directed_intent(const World &world, const std::string &system, EntityId ship = 1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("directed"))
		if (row.at("ship") == ship && row.at("system") == system)
			return row;
	throw std::runtime_error("Missing directed intent");
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
void repair_workload_contract()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	auto &layout = ship["control_layout"];
	ship["systems"]["team-a"]["local_repair_priority"] = true;
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 1;
	for (auto &c : layout["controllers"])
	{
		if (c["id"] == "team-b")
			c["commands"][0]["initial"] = 0;
		if (c["id"] == "team-a")
			c["commands"].push_back({{"id", "priority"},
			                         {"action", "repair_system"},
			                         {"consumers", {"team-a"}},
			                         {"individual_stars", 2},
			                         {"hull_priority", true}});
	}
	auto full = layout["profiles"][0];
	full["id"] = "priorities";
	full["stars"] = 2;
	layout["profiles"].push_back(full);
	ship["systems"]["gun"]["faults"] = Json::array({{{"id", "jam"},
	                                                 {"symptom", "Jammed"},
	                                                 {"diagnosis", "Feed jam"},
	                                                 {"effect", "disabled"},
	                                                 {"repair_work", 100}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	auto saved = world.save();
	saved["bodies"][0]["combat"]["compartments"][0]["hp"] = 40;
	saved["bodies"][0]["combat"]["compartments"][1]["hp"] = 20;
	world.restore(catalog, saved);
	submit(world, 102, "team-a", "assignment", 2);
	advance(world, 30);
	require(state(world)["systems"]["team-a"]["destination"] == 1 && state(world)["compartments"][0]["hp"] == 40 &&
	            state(world)["compartments"][1]["hp"] > 20,
	        "One-star individual repair deployment persists while AI chooses local work");
	require(!world.submit(102, command(world, 102, "team-a", "priority", 1)),
	        "One star refuses detailed repair priority");
	require(world.submit(102, {1, "preset", 1, {}}), "Two-star repair priorities selected");
	world.tick();
	submit(world, 102, "team-a", "assignment", 1);
	world.tick();
	saved = world.save();
	saved["bodies"][0]["combat"]["faults"]["gun"][0]["active"] = true;
	world.restore(catalog, saved);
	submit(world, 102, "team-a", "priority", static_cast<std::int64_t>(ship["systems"].size()) + 1);
	advance(world, 20);
	require(state(world)["compartments"][0]["hp"] > 40 && state(world)["faults"]["gun"][0]["work"] == 0,
	        "Hull priority repairs the assigned compartment before equipment faults");
	saved = world.save();
	saved["bodies"][0]["combat"]["compartments"][0]["hp"] = 99;
	world.restore(catalog, saved);
	advance(world, 5);
	require(state(world)["compartments"][0]["hp"] == 100 && state(world)["faults"]["gun"][0]["work"] > 0,
	        "Completed hull priority falls back to useful compartment work");
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
void compartment_passthrough_contract()
{
	for (const auto initial : {0, 5, 10000})
	{
		auto source = powered_fixture();
		auto &projectile = source["entities"][0]["systems"]["gun"]["projectile"];
		projectile["damage"] = 25;
		projectile["damage_per_m"] = 10;
		projectile["penetration"] = 1000;
		auto target = source["entities"][0];
		target["id"] = "test.layered-target";
		target["combat"]["decks"][0]["compartments"][1]["initial_hp"] = initial;
		source["entities"].push_back(target);
		source["maps"][0]["entities"][1]["definition"] = "test.layered-target";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		submit(world, 101, "gun", "target", 2);
		world.tick();
		for (int tick = 0; tick < 40 && world.save().at("projectiles").empty(); ++tick)
			world.tick();
		require(!world.save().at("projectiles").empty(), "Launch one physical compartment traversal shot");
		submit(world, 101, "gun", "enabled", 0);
		world.tick();
		World resumed;
		resumed.restore(catalog, world.save());
		advance(world, 20);
		advance(resumed, 20);
		require(world.save() == resumed.save(), "Compartment traversal preserves in-flight save continuation");
		const auto compartments = state(world, 2).at("compartments");
		if (initial == 10000)
			require(compartments[0]["hp"] == 10000 && compartments[1]["hp"] == 9975,
			        "An intact compartment absorbs a shot whose damage it can withstand");
		else
			require(compartments[1]["hp"] == 0 && compartments[0]["hp"] == 10000 - (25 - initial),
			        "Destroyed and same-shot-destroyed compartments pass unused damage to the next compartment");
	}
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
	// An anonymous bearing can be selected, but cannot establish offensive authority.
	for (bool acquire : {false, true})
	{
		auto source = missile_fixture();
		auto &systems = source["entities"][0]["systems"];
		systems["radar"]["range_mm"] = 10000;
		systems["radar"]["long_range_mm"] = 200000;
		systems["missile-mag"]["projectile"]["seeker_range_mm"] = acquire ? 30000 : 1;
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		require(world.observe(101)->contacts.empty() && world.observe(101)->bearing_contacts.size() == 2,
		        "only anonymous bearings are permitted");
		submit(world, 101, "launcher", "target", 2);
		advance(world, 20);
		require(world.save().at("projectiles").empty(),
		        "homing launcher cannot use an unidentified bearing as fire authorisation");
	}

	{
		auto source = fixture();
		auto &systems = source["entities"][0]["systems"];
		systems["radar"]["range_mm"] = 10000;
		systems["radar"]["long_range_mm"] = 200000;
		systems["gun"]["range_mm"] = 40000;
		World unguided;
		unguided.load(load(source), "test.map");
		claim(unguided);
		submit(unguided, 101, "gun", "target", 2);
		advance(unguided, 1);
		const auto shots = unguided.save().at("projectiles");
		require(shots.empty(), "unguided gun preserves ammunition when selected bearing is unidentified");
	}
	{
		auto source = powered_fixture();
		add_beam(source);
		auto &systems = source["entities"][0]["systems"];
		systems["radar"]["range_mm"] = 10000;
		systems["radar"]["long_range_mm"] = 200000;
		World beam;
		beam.load(load(source), "test.map");
		claim(beam);
		submit(beam, 101, "beam", "target", 2);
		advance(beam, 10);
		require(hp(beam) == 20000, "beam cannot fire against an anonymous bearing");
	}

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
		require(idle.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]
		                   ["fixture.resource.energy"] == 99800,
		        "Auto has a real standby draw without targets");
		submit(idle, 101, "pdc", "mode", 0);
		advance(idle, 20);
		require(idle.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]
		                   ["fixture.resource.energy"] == 99800,
		        "Off stops standby draw");
		submit(idle, 101, "pdc", "target", 2);
		idle.tick();
		require(directed_intent(idle, "pdc").at("targeting_mode") == 0,
		        "target assignment preserves the independently selected Off policy");
		submit(idle, 101, "pdc", "mode", 2);
		idle.tick();
		require(directed_intent(idle, "pdc").at("targeting_mode") == 2,
		        "explicit designated policy activates the retained ship target");
		submit(idle, 101, "pdc", "target", 0);
		idle.tick();
		require(directed_intent(idle, "pdc").at("targeting_mode") == 2,
		        "target cancellation preserves the independently selected designated policy");
		submit(idle, 101, "pdc", "mode", 1);
		idle.tick();
		require(directed_intent(idle, "pdc").at("targeting_mode") == 1,
		        "automatic interception resumes only when selected");
	}
}
void shield_reaction_contracts(Json *trace = nullptr)
{
	{
		auto source = powered_fixture();
		add_beam(source, 20);
		source["effects"] = Json::parse(R"json([{"id":"test.mixed-reactions","kind":"shield","contributions":{"main":{
          "capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,
          "sustain_energy_per_second":0,"warmup_ticks":1,"post_damage":3,
          "reactions":[{"operations":[{"kind":"damage","target":"attacker","damage":4}]}]}}}])json");
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"shield_effect", "test.mixed-reactions"},
		         {"power_store", "cell"}},
		        {});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		world.tick();
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto saved = world.save();
		const auto &pending = saved.at("rules").at("pending");
		const auto bindings = compile_shield_effect(catalog, "test.mixed-reactions").contributions.at(0).shield.rules;
		require(pending.size() == 2 && bindings.size() == 2 && pending[0].at("rule") == bindings[0].rule &&
		            pending[1].at("rule") == bindings[1].rule && pending[0].at("id") != pending[1].at("id") &&
		            !pending[0].at("source_key").get<std::string>().empty(),
		        "authored shield rules retain distinct stable job and source identities across save");
		World resumed;
		resumed.restore(catalog, saved);
		for (auto *w : {&world, &resumed})
		{
			submit(*w, 101, "beam", "target", 0);
			w->tick();
		}
		require(world.save() == resumed.save() &&
		            world.save().at("bodies")[0].at("shields").at("contributions")[0].at("state").at("strength") == 93,
		        "authored shield rules both execute once after restoring their pending jobs");
	}
	{
		auto source = powered_fixture();
		add_beam(source, 200);
		source["effects"] = Json::parse(R"json([{"id":"test.negation","kind":"shield","contributions":{
          "outer":{"capacity":1,"initial":1,"recharge_per_second":0,"off_recharge_per_second":0,
            "sustain_energy_per_second":0,"warmup_ticks":1,"layer":0,"pre_damage_permille":0},
          "inner":{"capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,
            "sustain_energy_per_second":0,"warmup_ticks":1,"layer":1,"post_damage":5}}}])json");
		add_fit(
		    source, "shield",
		    {{"kind", "shield"}, {"compartment", "fore"}, {"shield_effect", "test.negation"}, {"power_store", "cell"}},
		    {});
		const auto catalog = load(source);
		World world;
		world.load(catalog, source["maps"][0]["id"]);
		claim(world);
		world.tick();
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto saved = world.save();
		const auto &contributions = saved.at("bodies")[1].at("shields").at("contributions");
		require(contributions[0].at("state").at("strength") == 100 && contributions[1].at("state").at("strength") == 1,
		        "pre-negation transforms presented damage before reserve limiting");
		require(saved.at("rules").at("pending").empty() && hp(world) == 20000,
		        "negated incoming damage neither leaks to hull nor invokes an unreached layer");
	}
	for (const auto cost : {7, 1000000000})
	{
		auto source = powered_fixture();
		add_beam(source, 20);
		source["effects"] = Json::parse(R"json([
          {"id":"test.reactive","kind":"shield","contributions":{"main":{
            "capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,
            "sustain_energy_per_second":0,"warmup_ticks":1,"reactions":[{
              "payer":"shield","energy":7,"operations":[
                {"kind":"apply","target":"protected","effect":"test.bubble","grant":"gift"},
                {"kind":"apply","target":"protected","equipment":"radar","effect":"test.reaction-modifier","grant":"sensor-gift"},
                {"kind":"damage","target":"attacker","damage":10}]}]}}},
          {"id":"test.bubble","kind":"shield","duration_ticks":20,"contributions":{"bubble":{
            "capacity":5,"initial":5,"recharge_per_second":0,"off_recharge_per_second":0,
            "sustain_energy_per_second":0,"warmup_ticks":1}}},
          {"id":"test.reaction-modifier","kind":"modifier","duration_ticks":20,
            "contributions":{"range":{"property":"sensors","delta_permille":500}}}])json");
		source["effects"][0]["contributions"]["main"]["reactions"][0]["energy"] = cost;
		add_fit(
		    source, "shield",
		    {{"kind", "shield"}, {"compartment", "fore"}, {"shield_effect", "test.reactive"}, {"power_store", "cell"}},
		    {});
		const auto catalog = load(source);
		World world;
		world.load(catalog, source["maps"][0]["id"]);
		claim(world);
		world.tick();
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto pending = world.save();
		require(pending.at("rules").at("pending").size() == 1, "reached shield produces one deferred reaction group");
		const auto &event = pending.at("rules").at("pending")[0];
		require(event.at("context").at("presented") == 1 && event.at("context").at("absorbed") == 1 &&
		            event.at("context").at("strength") == 99 && event.at("context").at("instigator").at("entity") == 1,
		        "queued reaction captures committed per-contribution result");
		World resumed;
		resumed.restore(catalog, pending);
		submit(world, 101, "beam", "target", 0);
		// Claims and pending operator commands are deliberately identical after resume.
		submit(resumed, 101, "beam", "target", 0);
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "pending typed reaction executes exactly once across continuation");
		const auto saved = world.save();
		const auto &target = saved.at("bodies")[1];
		const auto &attacker = saved.at("bodies")[0];
		if (trace)
		{
			auto before = pending;
			auto after = saved;
			// Pack ZIP timestamps vary between independently built synthetic fixtures.
			before.erase("packs");
			after.erase("packs");
			trace->push_back({{"pending", std::move(before)}, {"continued", std::move(after)}});
		}
		if (cost == 7)
		{
			require(phoenix::test::modifier_applications(saved, 2).size() == 1,
			        "paid deferred reaction installs one timed property application");
			World active;
			active.restore(catalog, saved);
			require(active.save() == saved, "active reaction modifier restores without duplication");
			require(target.at("shields").at("contributions").size() == 2,
			        ("paid reaction atomically applies reusable grant: " + saved.at("rules").dump() + " target " +
			         target.at("resources").dump() + " shields " + target.at("shields").dump())
			            .c_str());
			require(attacker.at("shields").at("contributions")[0].at("state").at("strength") == 90,
			        "paid reaction secondary damage resolves in next scheduled phase");
			require(target.at("resources")
			                .at("network")
			                .at("stores")[0]
			                .at("stock")
			                .at("quantities")
			                .at("fixture.resource.energy") == 99993,
			        "reaction pays its discrete cost exactly once");
		}
		else
		{
			require(phoenix::test::modifier_applications(saved, 2).empty(),
			        "unaffordable reaction installs no partial modifier");
			require(target.at("shields").at("contributions").size() == 1 &&
			            target.at("shields").at("contributions")[0].at("state").at("strength") == 99,
			        "failed reaction leaves original damage committed and applies no partial grant");
			require(attacker.at("shields").at("contributions")[0].at("state").at("strength") == 100 &&
			            target.at("resources")
			                    .at("network")
			                    .at("stores")[0]
			                    .at("stock")
			                    .at("quantities")
			                    .at("fixture.resource.energy") == 100000,
			        "unaffordable reaction neither spends nor damages");
		}
	}
}
void native_damage_rule_contracts()
{
	const auto marker_store = [](Json &source)
	{
		source["entities"][0]["resources"]["stores"].push_back(
		    {{"id", "marker"}, {"capacity", 1000}, {"initial", 0}, {"charge_rate", 1000}, {"discharge_rate", 1000}});
	};
	const auto quantity = [](const Json &saved, const char *store, int body_index = 1)
	{
		const auto index = std::string(store) == "cell" ? 0 : 1;
		return saved.at("bodies")
		    .at(body_index)
		    .at("resources")
		    .at("network")
		    .at("stores")
		    .at(index)
		    .at("stock")
		    .at("quantities")
		    .value("fixture.resource.energy", std::int64_t{0});
	};
	{
		auto source = powered_fixture();
		add_beam(source, 200);
		marker_store(source);
		source["effects"] = Json::parse(R"json([
          {"id":"test.native.gift","kind":"shield","contributions":{"new":{"capacity":500,"initial":500,
            "recharge_per_second":0,"off_recharge_per_second":0,"sustain_energy_per_second":0,"warmup_ticks":1}}},
          {"id":"test.native.paid-op","kind":"operations",
            "costs":[{"payer":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":7}],
            "operations":[{"kind":"apply","target":"protected","effect":"test.native.gift","grant":"gift"}]},
          {"id":"test.native.failed-op","kind":"operations",
            "costs":[{"payer":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":1000000000}],
            "operations":[{"kind":"add","target":"protected","store":"energy:marker","resource":"fixture.resource.energy","amount":100}]},
          {"id":"test.native.mark-op","kind":"operations","operations":[
            {"kind":"add","target":"protected","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
          {"id":"test.native.paid-pre","kind":"rule","trigger":"pre_shield_damage","effect":"test.native.paid-op","damage_permille":{"value":0,"unit":"permille"}},
          {"id":"test.native.failed-pre","kind":"rule","trigger":"pre_shield_damage","effect":"test.native.failed-op","damage_permille":{"value":0,"unit":"permille"}},
          {"id":"test.native.post-shield","kind":"rule","trigger":"post_shield_damage","effect":"test.native.mark-op"},
          {"id":"test.native.unreached","kind":"rule","trigger":"post_shield_damage","effect":"test.native.mark-op"},
          {"id":"test.native.post-entity","kind":"rule","trigger":"post_entity_damage","effect":"test.native.mark-op"},
          {"id":"test.native.layers","kind":"shield","contributions":{
            "a-weak":{"capacity":1,"initial":1,"recharge_per_second":0,"off_recharge_per_second":0,
              "sustain_energy_per_second":0,"warmup_ticks":1,"layer":0,
              "rules":[{"id":"pre","rule":"test.native.paid-pre"},{"id":"post","rule":"test.native.post-shield"}]},
            "b-peer":{"capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,
              "sustain_energy_per_second":0,"warmup_ticks":1,"layer":0,
              "rules":[{"id":"pre","rule":"test.native.failed-pre"},{"id":"post","rule":"test.native.post-shield"}]},
            "c-inner":{"capacity":100,"initial":100,"recharge_per_second":0,"off_recharge_per_second":0,
              "sustain_energy_per_second":0,"warmup_ticks":1,"layer":1,
              "rules":[{"id":"post","rule":"test.native.unreached"}]}}}
        ])json");
		source["entities"][0]["rules"] = Json::array({{{"id", "entity-post"}, {"rule", "test.native.post-entity"}}});
		add_fit(source, "shield",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"shield_effect", "test.native.layers"},
		         {"power_store", "cell"}},
		        {});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		world.tick();
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto saved = world.save();
		const auto &contributions = saved.at("bodies")[1].at("shields").at("contributions");
		std::map<std::string, std::int64_t> strengths;
		for (const auto &entry : contributions)
			strengths.emplace(entry.at("name").get<std::string>(), entry.at("state").at("strength"));
		require(strengths.at("a-weak") == 1 && strengths.at("b-peer") == 95 && strengths.at("c-inner") == 100 &&
		            strengths.at("new") == 500 && hp(world) == 20000,
		        "paid pre-cancellation and failed peer fallback recompute equal shares without using newly installed "
		        "reserves");
		require(quantity(saved, "cell") == 99993 && quantity(saved, "marker") == 0,
		        "requoting a failed peer pays the successful frozen pre-work exactly once and leaks no partial failure "
		        "operation");
		int shield_posts = 0, entity_posts = 0;
		for (const auto &job : saved.at("rules").at("pending"))
		{
			const auto rule = job.at("rule").get<std::string>();
			const auto &context = job.at("context");
			require(rule != "test.native.unreached", "unreached layers produce no damage hook");
			if (rule == "test.native.post-shield")
			{
				++shield_posts;
				require(context.at("presented") == 5 && context.at("absorbed") == context.at("modified"),
				        "post shield contexts expose their actual final assigned and absorbed shares");
			}
			if (rule == "test.native.post-entity")
			{
				++entity_posts;
				require(context.at("inflicted") == 5,
				        "pre topology additions do not hide actual entity damage in reserve balance differences");
			}
		}
		require(shield_posts == 2 && entity_posts == 1,
		        "each reached contribution and native entity emits one deferred post hook");
		World resumed;
		resumed.restore(catalog, saved);
		for (auto *current : {&world, &resumed})
		{
			submit(*current, 101, "beam", "target", 0);
			current->tick();
		}
		require(world.save() == resumed.save() && quantity(world.save(), "marker") == 3 &&
		            quantity(world.save(), "cell") == 99993,
		        "frozen pre topology and deferred post receipts continue exactly once across restore");
	}
	for (const int factor : {500, 0})
	{
		auto source = powered_fixture();
		add_beam(source, 200);
		marker_store(source);
		// This beam enters aft; put the fault-bearing mechanism in its actual
		// reached compartment rather than relying on untouched fore equipment.
		source["entities"][0]["systems"]["gun"]["compartment"] = "aft";
		source["entities"][0]["systems"]["gun"]["faults"] = Json::array({{{"id", "impact"},
		                                                                  {"symptom", "Impact"},
		                                                                  {"diagnosis", "Impact fault"},
		                                                                  {"chance_permille", 1000},
		                                                                  {"damage_threshold", 1}}});
		source["effects"] = Json::parse(R"json([
          {"id":"test.native.mark-op","kind":"operations","operations":[
            {"kind":"add","target":"protected","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
          {"id":"test.native.pre-compartment","kind":"rule","trigger":"pre_compartment_damage","damage_permille":{"value":500,"unit":"permille"}},
          {"id":"test.native.pre-system","kind":"rule","trigger":"pre_system_damage","damage_permille":{"value":0,"unit":"permille"}},
          {"id":"test.native.post-compartment","kind":"rule","trigger":"post_compartment_damage","effect":"test.native.mark-op"},
          {"id":"test.native.post-system","kind":"rule","trigger":"post_system_damage","effect":"test.native.mark-op"}
        ])json");
		source["effects"][1]["damage_permille"]["value"] = factor;
		source["entities"][0]["rules"] = Json::array();
		for (const auto *name : {"pre-compartment", "pre-system", "post-compartment", "post-system"})
			source["entities"][0]["rules"].push_back({{"id", name}, {"rule", "test.native." + std::string(name)}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		world.tick();
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto saved = world.save();
		require(hp(world) == 20000 - (factor ? 5 : 0) &&
		            !state(world, 2).at("faults").at("gun")[0].at("active").get<bool>(),
		        "compartment scaling/cancellation and system cancellation alter their own native damage stages");
		int compartments = 0, systems = 0;
		for (const auto &job : saved.at("rules").at("pending"))
		{
			if (job.at("rule") == "test.native.post-compartment")
			{
				++compartments;
				require(job.at("context").at("presented") == 10 &&
				            job.at("context").at("inflicted") == (factor ? 5 : 0),
				        "post compartment hooks capture actual committed damage even when cancelled");
			}
			if (job.at("rule") == "test.native.post-system")
			{
				++systems;
				require(job.at("context").at("presented") == 5 && job.at("context").at("modified") == 0 &&
				            job.at("context").at("inflicted") == 0,
				        "post system hooks capture a reached but cancelled fault hit");
			}
		}
		require(compartments == 1 && systems == (factor ? 1 : 0),
		        ("cancelled compartment damage does not reach native system faults: factor=" + std::to_string(factor) +
		         " compartments=" + std::to_string(compartments) + " systems=" + std::to_string(systems))
		            .c_str());
	}
	{
		auto source = powered_fixture();
		add_beam(source, 200);
		marker_store(source);
		source["effects"] = Json::parse(R"json([
          {"id":"test.native.destroy-op","kind":"operations","operations":[
            {"kind":"add","target":"source","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
          {"id":"test.native.destroy","kind":"rule","trigger":"destroy","effect":"test.native.destroy-op"}
        ])json");
		source["entities"][0]["rules"] = Json::array({{{"id", "body-death"}, {"rule", "test.native.destroy"}}});
		source["entities"][0]["systems"]["radar"]["rules"] =
		    Json::array({{{"id", "equipment-death"}, {"rule", "test.native.destroy"}}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		world.tick();
		auto weakened = world.save();
		// The incoming beam crosses aft before fore. Leave exactly one native
		// hull point there, with fore already disabled, to produce a lethal hit.
		weakened["bodies"][1]["combat"]["compartments"][0]["hp"] = 0;
		weakened["bodies"][1]["combat"]["compartments"][0]["disabled"] = true;
		weakened["bodies"][1]["combat"]["compartments"][1]["hp"] = 1;
		weakened["bodies"][1]["combat"]["compartments"][1]["disabled"] = false;
		world.restore(catalog, weakened);
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto dead = world.save();
		require(hp(world) == 0 && dead.at("bodies")[1].at("destructible").at("processed").get<bool>() &&
		            dead.at("rules").at("pending").size() == 2,
		        "native retirement emits body and equipment destruction bindings despite lethal source ineligibility");
		for (const auto &job : dead.at("rules").at("pending"))
			require(job.at("rule") == "test.native.destroy" && job.at("context").at("subject").at("entity") == 2 &&
			            job.at("context").at("instigator").at("entity") == 2,
			        "destruction receipts retain the native retired subject and explicit self instigator");
		World resumed;
		resumed.restore(catalog, dead);
		advance(world, 2);
		advance(resumed, 2);
		require(
		    world.save() == resumed.save() && world.save().at("rules").at("pending").empty() &&
		        world.save().at("rules").at("results").size() == 2,
		    "persisted native processed state prevents duplicate destruction delivery after checkpoint continuation");
	}
	{
		auto source = powered_fixture();
		marker_store(source);
		source["effects"] = Json::parse(R"json([
          {"id":"test.early.destroy-op","kind":"operations","operations":[
            {"kind":"add","target":"source","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
          {"id":"test.early.destroy","kind":"rule","trigger":"destroy","effect":"test.early.destroy-op"},
          {"id":"test.early.active","kind":"modifier","contributions":{"scan":{"property":"sensors","delta_permille":100}},
            "rules":[{"id":"destroy","rule":"test.early.destroy"}]},
          {"id":"test.early.apply","kind":"operations","operations":[
            {"kind":"apply","target":"subject","equipment":"radar","effect":"test.early.active","grant":"active"}]},
          {"id":"test.early.maintain","kind":"rule","trigger":"maintain","phase":"maintained",
            "when":{"fact":"source.eligible"},"effect":"test.early.apply"},
          {"id":"test.early.kill-op","kind":"operations","operations":[{"kind":"damage","target":"subject","damage":1000}]},
          {"id":"test.early.kill","kind":"rule","trigger":"tick","phase":"periodic","period_ticks":2,"effect":"test.early.kill-op",
            "when":{"op":"eq","args":[{"fact":"tick"},{"value":2,"unit":"ticks"}]}}
        ])json");
		source["entities"][0]["rules"] = Json::array(
		    {{{"id", "maintain"}, {"rule", "test.early.maintain"}}, {{"id", "kill"}, {"rule", "test.early.kill"}}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		world.tick();
		auto weakened = world.save();
		weakened["bodies"][1]["combat"]["compartments"][0]["hp"] = 1;
		weakened["bodies"][1]["combat"]["compartments"][0]["disabled"] = false;
		weakened["bodies"][1]["combat"]["compartments"][1]["hp"] = 0;
		weakened["bodies"][1]["combat"]["compartments"][1]["disabled"] = true;
		world.restore(catalog, weakened);
		advance(world, 2);
		const auto dead = world.save();
		int alive_owners = 0, destroy_receipts = 0;
		for (const auto &effect :
		     modifier_contributions(dead, dead.at("bodies")[1].at("body").at("id").get<std::uint64_t>()))
			if (effect.at("definition") == "test.early.active")
				++alive_owners;
		for (const auto &job : dead.at("rules").at("pending"))
			if (job.at("rule") == "test.early.destroy" && job.at("context").at("subject").at("entity") == 2)
				++destroy_receipts;
		require(hp(world) == 0 && alive_owners == 0 && destroy_receipts == 1 &&
		            dead.at("bodies")[1].at("destructible").at("rule_notified").get<bool>(),
		        "first native death freezes maintained active-owner destruction before its phase-boundary withdrawal");
		World resumed;
		resumed.restore(catalog, dead);
		advance(world, 2);
		advance(resumed, 2);
		require(world.save() == resumed.save() && world.save().at("rules").at("pending").empty(),
		        "retired active-owner destruction notification persists exactly once after the owner and granting rule "
		        "disappear");
	}
	{
		auto source = powered_fixture();
		add_beam(source, 200);
		marker_store(source);
		source["effects"] = Json::parse(R"json([
          {"id":"test.origin.remove-op","kind":"operations","operations":[{"kind":"remove","target":"owner","scope":"owner"}]},
          {"id":"test.origin.source-op","kind":"operations","operations":[
            {"kind":"add","target":"source","store":"energy:marker","resource":"fixture.resource.energy","amount":7}]},
          {"id":"test.origin.pre","kind":"rule","trigger":"pre_shield_damage","effect":"test.origin.remove-op",
            "when":{"op":"eq","args":[{"fact":"owner.property","property":"protection"},{"value":1000,"unit":"relative_permille"}]}},
          {"id":"test.origin.post","kind":"rule","trigger":"post_shield_damage","effect":"test.origin.source-op"},
          {"id":"test.origin.shield","kind":"shield","contributions":{"main":{"capacity":100,"initial":100,
            "recharge_per_second":0,"off_recharge_per_second":0,"sustain_energy_per_second":0,"warmup_ticks":1,
            "rules":[{"id":"pre","rule":"test.origin.pre"},{"id":"post","rule":"test.origin.post"}]}}},
          {"id":"test.origin.grant","kind":"operations","operations":[
            {"kind":"apply","target":"protected","effect":"test.origin.shield","grant":"shield"}]}
        ])json");
		source["trees"].push_back(
		    Json::parse(R"json({"id":"test.origin.grant-tree","context":"scenario","initial":"apply",
          "states":[{"id":"apply","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":2},{"type":"entity","id":1},"test.origin.grant"]}]}]})json"));
		source["maps"][0]["scenario"] = {{"tree", "test.origin.grant-tree"}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		advance(world, 3);
		require(world.save().at("bodies")[1].at("shields").at("contributions").size() == 1,
		        "caster grants an independently sourced shield to the protected victim");
		submit(world, 101, "beam", "target", 2);
		world.tick();
		const auto removed = world.save();
		require(removed.at("bodies")[1].at("shields").at("contributions").empty(),
		        "pre hook removes its own native shield and permits provenance pruning");
		int posts = 0;
		for (const auto &job : removed.at("rules").at("pending"))
			if (job.at("rule") == "test.origin.post")
			{
				++posts;
				require(job.at("context").at("source").at("entity") == 1 &&
				            job.at("context").at("owner").at("entity") == 2,
				        "post hook retains the original caster after pre removal prunes the native origin map");
			}
		require(posts == 1, "removed reached shield emits its one frozen post hook");
		World resumed;
		resumed.restore(catalog, removed);
		for (auto *current : {&world, &resumed})
		{
			submit(*current, 101, "beam", "target", 0);
			current->tick();
		}
		require(
		    world.save() == resumed.save() && quantity(world.save(), "marker", 0) == 7 &&
		        quantity(world.save(), "marker", 1) == 0,
		    "deferred post work changes the caster's store exactly once while the victim's store remains untouched");
	}
	{
		auto source = powered_fixture();
		source["effects"] = Json::parse(R"json([
          {"id":"test.unknown.secondary","kind":"operations","operations":[
            {"kind":"damage","target":"attacker","damage":1}]},
          {"id":"test.unknown.pre","kind":"rule","trigger":"pre_shield_damage","effect":"test.unknown.secondary"},
          {"id":"test.unknown.shield","kind":"shield","contributions":{"main":{"capacity":100,"initial":100,
            "recharge_per_second":0,"off_recharge_per_second":0,"sustain_energy_per_second":0,"warmup_ticks":1,
            "rules":[{"id":"pre","rule":"test.unknown.pre"}]}}},
          {"id":"test.unknown.apply","kind":"operations","operations":[
            {"kind":"apply","target":"subject","effect":"test.unknown.shield","grant":"shield"}]},
          {"id":"test.unknown.enter","kind":"rule","trigger":"enter","effect":"test.unknown.apply"},
          {"id":"test.unknown.incoming","kind":"operations","operations":[
            {"kind":"damage","target":"protected","damage":10}]}
        ])json");
		const auto &victim = source.at("maps")[0].at("entities")[1];
		source["maps"][0]["regions"] =
		    Json::array({{{"id", "unknown-origin"},
		                  {"shape", "circle"},
		                  {"x_mm", victim.at("x_mm")},
		                  {"y_mm", victim.at("y_mm")},
		                  {"radius_mm", 3000},
		                  {"rules", Json::array({{{"id", "enter"}, {"rule", "test.unknown.enter"}}})}}});
		source["trees"].push_back(
		    Json::parse(R"json({"id":"test.unknown.hit-tree","context":"scenario","initial":"wait",
          "states":[{"id":"wait","transitions":[{"target":"hit","conditions":[
            {"left":{"observation":"tick"},"op":"ge","right":2}]}]},
          {"id":"hit","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":2},{"type":"entity","id":1},"test.unknown.incoming"]}]}]})json"));
		source["maps"][0]["scenario"] = {{"tree", "test.unknown.hit-tree"}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		advance(world, 3);
		const auto pending = world.save();
		require(pending.at("bodies")[1].at("shields").at("contributions").size() == 1 &&
		            pending.at("bodies")[1].at("shields").at("contributions")[0].at("state").at("strength") == 90,
		        "native initial region entry grants only the victim a shield before the ordinary scenario hit");
		int secondary = 0;
		for (const auto &job : pending.at("rules").at("pending"))
			if (job.at("rule") == "" && job.at("damage") == 1)
			{
				++secondary;
				const auto &context = job.at("context");
				require(context.at("source").at("entity") == 0 && context.at("instigator").at("entity") == 1 &&
				            context.at("region") == "unknown-origin" && context.at("target").at("entity") == 1 &&
				            context.at("hit").at("target") == 1,
				        "queued pre-shield secondary damage preserves explicit unknown region source and actual "
				        "instigator");
			}
		require(secondary == 1 && hp(world, 1) == 20000,
		        "region-origin secondary damage remains deferred without inventing a caster or applying damage early");
		World resumed;
		resumed.restore(catalog, pending);
		advance(world, 2);
		advance(resumed, 2);
		require(world.save() == resumed.save() && hp(world, 1) == 19999 &&
		            world.save().at("rules").at("pending").empty(),
		        "unknown-source damage delivers once after restore through the native damage path without source "
		        "authentication");
	}
}
void stock_binding_contracts()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	auto projectile = ship["systems"]["gun"]["projectile"];
	source["resources"] = Json::array();
	for (const auto *id : {"test.round-a", "test.round-b"})
	{
		projectile["damage"] = std::string(id) == "test.round-a" ? 11 : 29;
		source["resources"].push_back({{"id", id},
		                               {"label", id},
		                               {"tags", {"ammo", "test-ammo"}},
		                               {"sizes", {{"ammo", 1}}},
		                               {"ammunition", {{"projectile", projectile}}}});
	}
	ship["systems"]["mag"] = {{"kind", "magazine"},
	                          {"compartment", "fore"},
	                          {"stock",
	                           {{"capacity_tag", "ammo"},
	                            {"capacity", 1},
	                            {"filter", {{"tags", {"test-ammo"}}}},
	                            {"single_resource", true},
	                            {"selection", "test.round-a"},
	                            {"initial", {{"test.round-a", 1}}}}}};
	ship["systems"]["gun"]["load_ticks"] = 2;
	ship["systems"]["second"]["enabled"] = false;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	advance(world, 2);
	const auto loaded = world.save();
	const auto &body = loaded.at("bodies")[0];
	const auto &stock = saved_magazine_stock(body, "mag");
	require(stock.at("quantities").empty() && stock.at("selection") == "test.round-a" &&
	            stock.at("capacity_claims").at("weapon:gun").at("quantity") == 1 &&
	            state(world)["systems"]["gun"]["loaded_resource"] == "test.round-a" &&
	            state(world)["systems"]["gun"]["loaded_quantity"] == 1,
	        "Loaded ammunition retains its resource identity and receiving space even when the magazine is empty");
	auto choose = [&](const std::string &resource)
	{
		const auto snapshot = world.save();
		auto c = command(world, 101, "mag", "", saved_magazine_stock(snapshot.at("bodies")[0], "mag").at("revision"));
		c.action = "magazine.select";
		c.system = "mag";
		c.text = resource;
		return c;
	};
	require(!world.submit(101, choose("test.round-b")),
	        "A loaded return commitment blocks magazine resource switching");
	World restored;
	restored.restore(catalog, loaded);
	for (int corruption = 0; corruption < 3; ++corruption)
	{
		auto invalid = loaded;
		if (corruption == 0)
			invalid["bodies"][0]["combat"]["systems"]["gun"]["loaded_resource"] = "test.round-b";
		if (corruption == 1)
			invalid["bodies"][0]["stocks"]["system:mag"]["capacity_claims"].erase("weapon:gun");
		if (corruption == 2)
		{
			auto &claims = invalid["bodies"][0]["stocks"]["system:mag"]["capacity_claims"];
			claims["unknown-owner"] = claims.at("weapon:gun");
			claims.erase("weapon:gun");
		}
		bool refused = false;
		try
		{
			restored.restore(catalog, invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && restored.save() == loaded, "Invalid loaded stock ownership is refused transactionally");
	}
	submit(world, 101, "gun", "target", 2);
	submit(restored, 101, "gun", "target", 2);
	world.tick();
	restored.tick();
	require(world.save() == restored.save() && state(world)["systems"]["gun"]["shots"] == 1,
	        "Loaded resource payload fires once across continuation");
	const auto fired = world.save();
	const auto &empty = saved_magazine_stock(fired.at("bodies")[0], "mag");
	require(empty.at("selection") == "test.round-a" && empty.at("quantities").empty() &&
	            empty.at("capacity_claims").empty(),
	        "Firing releases the return commitment while preserving empty magazine selection");
	const auto selection = choose("test.round-b");
	require(world.submit(101, selection),
	        "The owner can deliberately select a different resource once the magazine is empty and free");
	world.tick();
	const auto selected = world.save();
	require(saved_magazine_stock(selected.at("bodies")[0], "mag").at("selection") == "test.round-b" &&
	            !world.submit(101, selection),
	        "Resource selection persists and stale selection revisions are rejected");
}
void property_rate_contracts()
{
	auto source = powered_fixture();
	auto &magazine = source["entities"][0]["systems"]["mag"];
	magazine["initial"] = 0;
	magazine["power_store"] = "cell";
	magazine["reload"] = {{"strategy", "energy"}, {"ticks", 2}, {"amount", 2}, {"energy_per_second", 40}};
	source["effects"] = Json::parse(R"json([
      {"id":"test.reload-pause","kind":"modifier","duration_ticks":3,
       "contributions":{"pause":{"property":"reload","delta_permille":-1000}}},
      {"id":"test.cycle-pause","kind":"modifier","duration_ticks":3,
       "contributions":{"pause":{"property":"cycling","delta_permille":-1000}}}])json");
	source["maps"][0]["scenario"] = {{"tree", "test.pause-work"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	source["trees"].push_back(Json::parse(R"json({"id":"test.pause-work","context":"scenario","initial":"wait",
      "states":[{"id":"wait","transitions":[{"target":"pause","conditions":[{"left":{"observation":"tick"},"op":"ge","right":1}]}]},
      {"id":"pause","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"mag","test.reload-pause","pause"]}]}]})json"));
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	world.tick();
	require(state(world)["systems"]["mag"]["refill"] == 1000,
	        "replenishment retains fixed-point partial work before a modifier changes its rate");
	world.tick();
	require(state(world)["systems"]["mag"]["refill"] == 1000 && state(world)["systems"]["mag"]["ammo"] == 0,
	        "zero replenishment rate pauses existing work without manufacturing ammunition");
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 2; ++i)
	{
		world.tick();
		resumed.tick();
		require(state(world)["systems"]["mag"]["refill"] == 1000,
		        "zero replenishment rate preserves accumulated fractional work");
	}
	world.tick();
	resumed.tick();
	require(state(world)["systems"]["mag"]["ammo"] == 2 && state(world)["systems"]["mag"]["refill"] == 0,
	        "expiry resumes the existing replenishment batch at its original work threshold");
	require(world.save() == resumed.save(), "paused replenishment continuation differs after restore");

	source["entities"][0]["systems"]["mag"]["initial"] = 3;
	source["trees"].back() = Json::parse(R"json({"id":"test.pause-work","context":"scenario","initial":"pause",
      "states":[{"id":"pause","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"gun","test.cycle-pause","pause"]}]}]})json");
	World firing;
	firing.load(load(source), "test.map");
	claim(firing);
	submit(firing, 101, "gun", "target", 2);
	advance(firing, 3);
	require(state(firing)["systems"]["gun"]["shots"] == 0 && state(firing)["systems"]["mag"]["ammo"] == 3,
	        "zero weapon cycle rate blocks a new shot before ammunition is spent");
	firing.tick();
	require(state(firing)["systems"]["gun"]["shots"] == 1 && state(firing)["systems"]["mag"]["ammo"] == 2,
	        "positive cycle rate after expiry permits the next shot");
}
void effects_contracts()
{
	{
		auto source = powered_fixture();
		source["effects"] = Json::array({{{"id", "test.split-shield"},
		                                  {"kind", "shield"},
		                                  {"contributions",
		                                   {{"fore",
		                                     {{"capacity", 100},
		                                      {"initial", 0},
		                                      {"recharge_per_second", 20},
		                                      {"off_recharge_per_second", 0},
		                                      {"sustain_energy_per_second", 10},
		                                      {"warmup_ticks", 1}}},
		                                    {"aft",
		                                     {{"capacity", 100},
		                                      {"initial", 0},
		                                      {"recharge_per_second", 20},
		                                      {"off_recharge_per_second", 0},
		                                      {"sustain_energy_per_second", 10},
		                                      {"warmup_ticks", 1}}}}}}});
		add_fit(source, "split",
		        {{"kind", "shield"},
		         {"compartment", "fore"},
		         {"shield_effect", "test.split-shield"},
		         {"power_store", "cell"}},
		        {});
		const auto catalog = load(source);
		World world;
		world.load(catalog, source["maps"][0]["id"]);
		claim(world);
		advance(world, 20);
		const auto saved = world.save();
		const auto &contributions = saved.at("bodies")[0].at("shields").at("contributions");
		require(contributions.size() == 2 && contributions[0]["state"]["strength"] == 20 &&
		            contributions[1]["state"]["strength"] == 20,
		        "one equipment payer sustains and recharges both named contributions without spending its grant twice");
		require(contributions[0]["effect_id"] == contributions[1]["effect_id"] &&
		            contributions[0]["name"] != contributions[1]["name"],
		        "named shield contributions share effect identity while owning independent reserves");
		World resumed;
		resumed.restore(catalog, saved);
		advance(world, 3);
		advance(resumed, 3);
		require(world.save() == resumed.save(), "multi-contribution funding and fractions continue exactly");
		auto malformed = saved;
		malformed["bodies"][0]["shields"]["contributions"].erase(0);
		bool refused = false;
		const auto before = resumed.save();
		try
		{
			resumed.restore(catalog, malformed);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && resumed.save() == before, "incomplete named grant restore refuses transactionally");
	}

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
		for (const auto &body : scene)
			if (!body.sound.empty())
				require(std::count_if(body.audio.begin(), body.audio.end(),
				                      [&](const auto &cue)
				                      {
					                      return cue.kind == SceneAudioKind::Shot && cue.source == "explosion" &&
					                             cue.clip.asset == body.sound && cue.event == body.id &&
					                             cue.tick == body.effect_tick;
				                      }) == 1,
				        "World scene projection supplies exactly one ready-to-play cue per explosion");
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
		require(w.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]
		                ["fixture.resource.energy"] == 99960,
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
		const auto before =
		    w.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]["fixture.resource.energy"]
		        .get<std::int64_t>() +
		    state(w)["systems"]["shield"]["strength"].get<std::int64_t>();
		submit(w, 101, "shield", "discharge", 1);
		advance(w, 20);
		const auto after =
		    w.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]["fixture.resource.energy"]
		        .get<std::int64_t>() +
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
		require(faults.empty(), "individual system damage is absent before a team arrives");
		const auto damaged_sections = w.observe(202)->gameplay["combat"]["compartments"];
		require(std::any_of(damaged_sections.begin(), damaged_sections.end(),
		                    [](const Json &part)
		                    {
			                    return part.at("id") == "aft" && part.value("damaged", false) &&
			                           !part.value("inspected", false);
		                    }),
		        "section damage remains visible without revealing individual faults");
		auto enable = command(w, 202, "team-a", "enabled", 1);
		enable.target = 2;
		require(w.submit(202, enable), "enable inspecting team");
		auto repair = command(w, 202, "team-a", "assignment", 2);
		repair.target = 2;
		require(w.submit(202, repair), "assign inspecting team");
		advance(w, 12);
		require(w.observe(201)->gameplay["combat"]["faults"][0]["diagnosed"].get<bool>(),
		        "team arrival reveals diagnosis to owner");
		auto leave = command(w, 202, "team-a", "assignment", 1);
		leave.target = 2;
		require(w.submit(202, leave), "inspection team can leave the damaged section");
		advance(w, 2);
		require(w.observe(201)->gameplay["combat"]["faults"].empty(),
		        "previously diagnosed system damage disappears when the team leaves");
		const auto teams = w.observe(202)->gameplay.at("teams");
		require(std::any_of(teams.begin(), teams.end(),
		                    [](const Json &team)
		                    {
			                    return team.at("id") == "team-a" && team.value("work", "") == "Travelling";
		                    }),
		        "repair roster reports real team travel activity");
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
		            w.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"]
		                    ["fixture.resource.energy"] == 50009,
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
void resource_availability_contracts()
{
	// Four physical supply kinds share the same allocation seam. All quantities
	// belong to this synthetic experiment, independent of shipped equipment.
	for (const std::string disabled : {"reactor", "bank", "feed", "cooler"})
	{
		auto source = powered_fixture();
		auto &ship = source["entities"][0];
		ship["resources"] = Json::parse(R"json({
		 "stores":[
		  {"id":"source","capacity":10000,"initial":5000,"charge_rate":1000,"discharge_rate":1000},
		  {"id":"cell","capacity":10000,"initial":1000,"charge_rate":1000,"discharge_rate":1000,"initial_heat":200}],
		 "generators":[{"id":"reactor","store":"source","maximum_rate":40,"initial_output":40,"response_rate":40}],
		 "links":[{"id":"feed","from":"source","to":"cell","capacity_rate":40}],
		 "coolant_sources":[{"id":"coolant","initial":100000,"removal_rate":40}],
		 "cooling_links":[{"id":"cooler","source":"coolant","store":"cell","capacity_rate":40,"allocation_rate":40}]
		})json");
		add_fit(source, "drive",
		        {{"kind", "engine"},
		         {"compartment", "aft"},
		         {"max_speed_mm_s", 10000},
		         {"acceleration_mm_s2", 2000},
		         {"power_rate", 200},
		         {"power_store", "cell"}},
		        {{"speed", "speed_mm_s"}});
		add_fit(source, "reactor", {{"kind", "generator"}, {"generator", "reactor"}, {"compartment", "aft"}}, {});
		add_fit(source, "bank", {{"kind", "storage"}, {"power_store", "source"}, {"compartment", "aft"}}, {});
		add_fit(source, "feed", {{"kind", "distribution"}, {"conduit", "feed"}, {"compartment", "aft"}}, {});
		ship["control_layout"]["controllers"].back()["commands"] =
		    Json::array({{{"id", "isolate"}, {"action", "link_isolated"}, {"initial", 0}, {"consumers", {"feed"}}}});
		add_fit(source, "cooler", {{"kind", "coolant"}, {"cooling_link", "cooler"}, {"compartment", "aft"}}, {});
		ship["systems"][disabled]["compartment"] = "fore";
		const auto catalog = load(source);
		const std::string map = source["maps"][0]["id"];
		const auto fore = compile_combat(catalog, "test.combat")->compartment_ids.at("fore");
		const auto resources = *compile_ship_resources(catalog, "test.combat");
		const auto cell = resources.network.store_ids.at("cell");
		const auto supply = resources.network.store_ids.at("source");
		World world;
		world.load(catalog, map);
		claim(world);
		submit(world, 101, "drive", "speed", 10000);
		world.tick();
		auto checkpoint = world.save();
		checkpoint["bodies"][0]["combat"]["compartments"][fore]["hp"] = 0;
		checkpoint["bodies"][0]["combat"]["compartments"][fore]["disabled"] = true;
		world.restore(catalog, checkpoint);
		const auto before = checkpoint["bodies"][0]["resources"]["network"];
		world.tick();
		const auto after = world.save()["bodies"][0]["resources"]["network"];
		require(after["ticks"].get<std::uint64_t>() == before["ticks"].get<std::uint64_t>() + 1 &&
		            after["stores"][cell]["stock"]["quantities"]["fixture.resource.energy"] ==
		                before["stores"][cell]["stock"]["quantities"]["fixture.resource.energy"].get<int>() - 10 +
		                    after["telemetry"][cell]["accepted_incoming"].get<int>(),
		        "allocation preview commits one tick and debits operating work exactly once");
		require(after["generators"][0]["enabled"] == true && after["stores"][supply]["enabled"] == true &&
		            after["links"][0]["isolated"] == false && after["cooling_links"][0]["isolated"] == false,
		        "physical supply unavailability preserves accepted enabled and isolation settings");
		if (disabled == "reactor")
			require(after["telemetry"][supply]["generated"] == 0, "disabled generator produces no energy");
		if (disabled == "bank" || disabled == "feed")
			require(after["telemetry"][cell]["accepted_incoming"] == 0, "disabled supply path transfers no energy");
		if (disabled == "cooler")
			require(after["coolant_sources"][0]["last_heat_removed"] == 0, "disabled cooling link removes no heat");
		World resumed;
		resumed.restore(catalog, world.save());
		advance(world, 5);
		advance(resumed, 5);
		require(world.save() == resumed.save(), "unavailable supplies retain deterministic allocation continuation");
	}
}
#include "host_audio_contracts.hpp"
} // namespace
int main(int argc, char **argv)
{
	try
	{
		if (argc > 1 && std::string(argv[1]) == "--native-damage-rules")
		{
			native_damage_rule_contracts();
			std::cout << "Native damage rule contracts passed\n";
			return 0;
		}

		// Information/override selection is never an offensive-fire authorisation.
		for (const auto *relation : {"hostile", "friendly", "neutral", "unidentified"})
		{
			auto source = powered_fixture();
			add_beam(source);
			if (std::string(relation) == "unidentified")
				source["maps"][0]["entities"][1].erase("faction");
			else
				source["maps"][0]["relationships"][0]["relation"] = relation;
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map");
			claim(world);
			const auto initial = world.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"];
			submit(world, 101, "gun", "target", 2);
			submit(world, 101, "beam", "target", 2);
			advance(world, 20);
			const auto shots = state(world)["systems"]["gun"]["shots"].get<int>();
			if (std::string(relation) == "hostile")
			{
				require(shots > 0 && hp(world) < 20000, "confirmed hostile selection permits offensive execution");
				submit(world, 101, "radar", "enabled", 0);
				advance(world, 2);
				const auto stopped = state(world)["systems"]["gun"]["shots"];
				const auto energy =
				    world.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"];
				advance(world, 20);
				require(
				    state(world)["systems"]["gun"]["shots"] == stopped &&
				        world.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"] == energy,
				    ("loss of permitted identification stops further launcher and beam costs: shots " + stopped.dump() +
				     " -> " + state(world)["systems"]["gun"]["shots"].dump() + "; energy " + energy.dump() + " -> " +
				     world.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"].dump())
				        .c_str());
			}
			else
				require(shots == 0 && hp(world) == 20000 &&
				            world.save()["bodies"][0]["resources"]["network"]["stores"][0]["stock"]["quantities"] ==
				                initial,
				        "friendly, neutral and unidentified selections consume no offensive energy or ammunition");
		}
		compartment_passthrough_contract();
		repair_workload_contract();
		host_audio_contracts();
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
			{
				w.tick();
			}
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
			Json reactions = Json::array();
			shield_reaction_contracts(&reactions);
			std::cout << sha256(json_bytes(reactions)) << '\n';
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
			require(directed_intent(w, "gun").at("target") == 2 && directed_intent(w, "second").at("target") == 3,
			        "independently mounted directed capabilities retain separate targets");
			submit(w, 101, "gun", "target", 2);
			w.tick();
			require(directed_intent(w, "second").at("target") == 3,
			        "reasserting a target cannot retarget another directed capability");
			submit(w, 101, "gun", "target", 0);
			w.tick();
			require(directed_intent(w, "second").at("target") == 3, "cancelling one weapon retains its neighbour");
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
			setup["entities"][0]["systems"]["mount"]["bearing_mdeg"] = 180000;
			const auto c = load(setup);
			World fresh, resumed;
			fresh.load(c, map);
			const auto initial = state(fresh).at("systems").at("mount");
			require(initial.at("heading") == 180000 && initial.at("turn_remainder") == 0,
			        "non-forward turret starts at its authored bearing without fractional traverse");
			resumed.restore(c, fresh.save());
			require(resumed.save() == fresh.save(), "fresh angled turret survives the fleet setup save path");
			advance(fresh, 5);
			advance(resumed, 5);
			require(resumed.save() == fresh.save(), "angled turret continuation remains deterministic");
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
			require(w.save().at("bodies")[0]
			                .at("resources")
			                .at("network")
			                .at("stores")[0]
			                .at("stock")
			                .at("quantities")
			                .at("fixture.resource.energy") == snapshot.at("bodies")[0]
			                                                      .at("resources")
			                                                      .at("network")
			                                                      .at("stores")[0]
			                                                      .at("stock")
			                                                      .at("quantities")
			                                                      .at("fixture.resource.energy"),
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

			require(directed_intent(ai, "gun").at("target") == 2, "equidistant hostile candidates use stable identity");
			auto closer = ai.save();
			closer["bodies"][2]["body"]["y"] = 30000;
			ai.restore(visible, closer);
			ai.tick();
			require(directed_intent(ai, "gun").at("target") == 2, "a still-valid target survives a nearer alternative");
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
		resource_availability_contracts();
		extended_contracts();
		review_contracts();
		ordnance_contracts();
		stock_binding_contracts();
		property_rate_contracts();
		effects_contracts();
		shield_reaction_contracts();
		native_damage_rule_contracts();
		std::cout << "Combat system contracts passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
