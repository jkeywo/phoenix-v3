#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import phoenix.simulation;
import phoenix.session.local;
import phoenix.console;

using namespace phoenix;
namespace
{
using phoenix::test::require;
Json fixture()
{
	auto source =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic paid flight");
	source["schema"] = 4;
	source["consoles"][0]["widgets"] = Json::array();
	auto &ship = source["entities"][0];
	ship.erase("console");
	ship["systems"] = Json::parse(R"json({
		"drive":{"kind":"engine","max_speed_mm_s":10000,"reverse_speed_mm_s":5000,
		         "acceleration_mm_s2":2000,"power_rate":200,"power_store":"cell","match_stale_ticks":2},
		"turn":{"kind":"steering","turn_mdeg_s":90000,"power_rate":100,"power_store":"cell"},
		"side":{"kind":"lateral","max_speed_mm_s":6000,"lateral_speed_mm_s":6000,
		        "lateral_acceleration_mm_s2":1000,"power_rate":100,"power_store":"cell"},
		"boost":{"kind":"boost","max_speed_mm_s":10000,"acceleration_mm_s2":2000,
		         "acceleration_multiplier_permille":2000,"speed_multiplier_permille":2000,
		         "power_rate":400,"power_store":"cell"},
		"impulse":{"kind":"impulse","max_speed_mm_s":10000,"acceleration_mm_s2":2000,
		           "charge_ticks":2,"acceleration_multiplier_permille":4000,"speed_multiplier_permille":5000,
		           "steering_permille":0,"charge_power_rate":200,"power_rate":600,"power_store":"cell"},
		"radar":{"kind":"radar","range_mm":100000},
		"storage":{"kind":"storage","power_store":"cell"}
	})json");
	ship["resources"] = Json::parse(R"json({"stores":[
		{"id":"cell","capacity":100000,"initial":50000,"charge_rate":100000,
		 "discharge_rate":100000,"stop_heat":100000,"recover_heat":50000}
	]})json");
	ship["stations"] = Json::parse(R"json([
		{"id":"bridge","consoles":[
			{"id":"helm","definition":"test.console","systems":["drive","turn","boost","impulse"],"feeds":["radar"]},
			{"id":"lateral","definition":"test.console","systems":["side"]},
			{"id":"sensors","definition":"test.console","systems":["radar"]},
			{"id":"engineering","definition":"test.console","systems":["storage"]}
		]}
	])json");
	source["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.ship"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 2}, {"definition", "test.ship"}, {"x_mm", 50000}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	return source;
}
using phoenix::test::archive;
using phoenix::test::load;
BodyView body(const World &world, EntityId id = 1)
{
	for (const auto &item : world.inspect())
		if (item.id == id)
			return item;
	throw std::runtime_error("synthetic flight actor missing");
}
SystemView system(const Observation &observation, const std::string &id)
{
	for (const auto &item : observation.systems)
		if (item.id == id)
			return item;
	throw std::runtime_error("required permitted fitted System missing");
}
std::int64_t telemetry(const World &world, const std::string &id, const std::string &field, std::uint64_t console = 101)
{
	const auto observed = world.observe(console);
	require(observed.has_value(), "claimed Console must retain its public observation");
	return system(*observed, id).telemetry.at(field);
}
void claim(World &world)
{
	require(world.claim(101, 1, "helm") && world.claim(102, 1, "lateral") && world.claim(103, 1, "sensors") &&
	            world.claim(104, 1, "engineering"),
	        "independent fitted flight and Engineering Consoles must be claimable");
}
void command(World &world, const std::string &action, std::int64_t value, const std::string &id,
             std::uint64_t console = 101, EntityId actor = 1)
{
	if (!world.submit(console, {actor, action, value, id}))
		throw std::runtime_error("ordinary typed command refused: " + id + "." + action + "=" + std::to_string(value));
}
std::int64_t paid(const World &world)
{
	std::int64_t total = 0;
	std::set<std::string> counted;
	for (const auto console : {101u, 102u, 103u, 104u})
		if (const auto observed = world.observe(console))
			for (const auto &item : observed->systems)
				if (item.controlled && counted.insert(item.id).second && item.telemetry.contains("used_power"))
					total += item.telemetry.at("used_power") / World::ticks_per_second;
	return total;
}
void launch_curve_and_linear_braking()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "speed_mm_s", 10000, "drive");
	world.tick();
	const auto launch = body(world).vx;
	for (int i = 0; i < 30; ++i)
		world.tick();
	const auto before = body(world).vx;
	world.tick();
	require(launch > body(world).vx - before && body(world).vx > before,
	        "forward acceleration must taper as speed rises while continuing toward the target");
	command(world, "speed_mm_s", 0, "drive");
	const auto braking_start = body(world).vx;
	world.tick();
	const auto first = braking_start - body(world).vx;
	const auto middle = body(world).vx;
	world.tick();
	require(first > 0 && std::abs(first - (middle - body(world).vx)) <= 1 && launch > first,
	        "launch must be stronger than nominal linear braking, with equal braking decrements");
	World resumed;
	resumed.load(catalog, "test.map");
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 10; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(body(world).vx == body(resumed).vx, "curved flight retains deterministic save continuation");
}
void manual_and_conservation()
{
	const auto catalog = load(fixture());
	const auto crew_catalog = resolve_packs({console_assets(catalog, "test.ship")});
	require(!resolve_systems(crew_catalog, "test.ship").empty(),
	        "advanced flight bounds survive restricted crew asset verification");
	World world;
	world.load(catalog, "test.map");
	claim(world);
	require(!world.submit(101, {1, "lateral_thrust", 1000, "side"}) &&
	            !world.submit(102, {1, "manual_steering", 1000, "turn"}),
	        "separate flight actuator ownership must prevent cross-Console control");
	command(world, "assisted", 0, "drive");
	command(world, "assisted", 0, "turn");
	world.tick();
	command(world, "longitudinal_thrust", -1000, "drive");
	command(world, "lateral_thrust", 1000, "side", 102);
	command(world, "manual_steering", 1000, "turn");
	require(world.debug().at("pending_outputs").size() == 3,
	        "disposable inspection must expose admitted outputs before their tick executes");
	const auto before = telemetry(world, "drive", "stored_energy");
	world.tick();
	const auto moved = body(world);
	const auto debug = world.debug();
	require(debug.at("pending_outputs").empty() && !debug.at("resource_grants").empty() &&
	            debug.at("resource_grants")[0].contains("telemetry"),
	        "disposable inspection must consume executed outputs and expose actual grants and resource telemetry");
	require(moved.vx < 0 && moved.vy > 0 && moved.heading > 0 && telemetry(world, "drive", "actual_longitudinal") < 0 &&
	            telemetry(world, "side", "actual_lateral", 102) > 0,
	        "reverse longitudinal, lateral and independent steering must execute paid fixed-point motion");
	require(before - telemetry(world, "drive", "stored_energy") == paid(world),
	        "multiple fitted consumers sharing one reserve must debit each paid grant exactly once");
	command(world, "longitudinal_thrust", 0, "drive");
	command(world, "lateral_thrust", 0, "side", 102);
	command(world, "manual_steering", 0, "turn");
	world.tick();
	const auto coast = body(world);
	require(coast.vx == moved.vx && coast.vy == moved.vy && coast.heading == moved.heading,
	        "neutral manual controls must remove actuation while preserving momentum and heading");
	command(world, "assisted", 1, "drive");
	world.tick();
	require(telemetry(world, "drive", "longitudinal_thrust") == 0,
	        "switching to assistance must clear held manual thrust rather than retain a stale axis");
	command(world, "speed_mm_s", -5000, "drive");
	world.tick();
	require(system(*world.observe(101), "drive").requested == -5000 &&
	            !world.can_submit(101, {1, "speed_mm_s", -5001, "drive"}),
	        "assisted reverse commands obey the fitted authored reverse-speed bound");
}
void boost_and_impulse()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "assisted", 0, "drive");
	command(world, "assisted", 0, "turn");
	world.tick();
	command(world, "boost", 1, "boost");
	world.tick();
	const auto first = body(world);
	world.tick();
	const auto sustained = body(world);
	require(first.vx > 0 && sustained.vx > first.vx, "boost must remain active across ticks while paid and enabled");
	command(world, "boost", 0, "boost");
	world.tick();
	require(body(world).vx == sustained.vx && telemetry(world, "boost", "actual_longitudinal") == 0,
	        "releasing sustained boost removes extra thrust without deleting velocity");
	command(world, "impulse", 1, "impulse");
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 1 &&
	            telemetry(world, "impulse", "impulse_progress") == 1000,
	        "first paid impulse tick must advance charging rather than instantly entering transit");
	const auto charge = world.save();
	World resumed;
	resumed.restore(catalog, charge);
	resumed.clear_claims();
	claim(resumed);
	world.tick();
	resumed.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 2 &&
	            telemetry(resumed, "impulse", "impulse_progress") == telemetry(world, "impulse", "impulse_progress"),
	        "impulse charging must continue through save and recovery without free progress or reset");
	command(world, "manual_steering", 1000, "turn");
	command(resumed, "manual_steering", 1000, "turn");
	const auto heading = body(world).heading;
	world.tick();
	resumed.tick();
	require(body(world).vx > sustained.vx && body(world).heading == heading &&
	            telemetry(world, "turn", "actual_steering") == 0,
	        "active impulse must accelerate through paid transit and restrict independent steering");
	require(body(world).vx == body(resumed).vx && body(world).x == body(resumed).x &&
	            telemetry(world, "drive", "stored_energy") == telemetry(resumed, "drive", "stored_energy"),
	        "restored impulse must match uninterrupted physical execution and shared energy debit");
	world.submit(101, {1, "boost", 1, "boost"});
	world.tick();
	require(telemetry(world, "boost", "actual_longitudinal") == 0,
	        "separately fitted boost cannot stack physical output on active impulse");
	command(world, "boost", 0, "boost");
	command(world, "manual_steering", 0, "turn");
	command(world, "impulse", 0, "impulse");
	const auto velocity = body(world).vx;
	world.tick();
	require(body(world).vx == velocity && telemetry(world, "impulse", "impulse_phase") == 0 &&
	            telemetry(world, "impulse", "impulse_progress") == 0,
	        "cancelling impulse clears charge and extra thrust while preserving momentum");
}
void paid_overdrive()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["overdrive_permille"] = 2000;
	ship["resources"]["stores"][0]["overdrive_heat_permille"] = 1500;
	ship["resources"]["stores"][0]["stress_rate"] = 20;
	ship["resources"]["stores"][0]["stress_limit"] = 10000;
	ship["systems"]["drive"]["heat_rate"] = 20;
	ship["systems"]["side"]["heat_rate"] = 10;
	ship["systems"]["turn"]["heat_rate"] = 10;
	const auto catalog = load(source);
	World ordinary, overdriven;
	ordinary.load(catalog, "test.map");
	overdriven.load(catalog, "test.map");
	for (auto *world : {&ordinary, &overdriven})
	{
		claim(*world);
		command(*world, "assisted", 0, "drive");
		command(*world, "assisted", 0, "turn");
		world->tick();
		command(*world, "longitudinal_thrust", 1000, "drive");
		command(*world, "lateral_thrust", 1000, "side", 102);
		command(*world, "manual_steering", 1000, "turn");
	}
	command(overdriven, "overdrive", 1, "storage", 104);
	const auto before = telemetry(overdriven, "drive", "stored_energy");
	ordinary.tick();
	overdriven.tick();
	require(body(overdriven).vx > body(ordinary).vx && body(overdriven).vy > body(ordinary).vy &&
	            body(overdriven).heading > body(ordinary).heading && paid(overdriven) > paid(ordinary),
	        "paid overdrive must increase real longitudinal, lateral and turn output together with actual energy cost");
	require(before - telemetry(overdriven, "drive", "stored_energy") == paid(overdriven) &&
	            telemetry(overdriven, "drive", "heat") > telemetry(ordinary, "drive", "heat") &&
	            telemetry(overdriven, "drive", "stress") > 0,
	        "overdrive must conserve its enlarged debit and apply authored external heat and finite stress");
	const auto saved = overdriven.save();
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "overdrive output/heat remainders must survive raw save and restore");
	resumed.clear_claims();
	claim(resumed);
	for (auto *world : {&ordinary, &overdriven, &resumed})
	{
		command(*world, "longitudinal_thrust", 0, "drive");
		command(*world, "lateral_thrust", 0, "side", 102);
		command(*world, "manual_steering", 0, "turn");
		command(*world, "impulse", 1, "impulse");
		world->tick();
	}
	require(telemetry(overdriven, "impulse", "impulse_phase") == 2 &&
	            telemetry(ordinary, "impulse", "impulse_phase") == 1 &&
	            telemetry(resumed, "impulse", "impulse_progress") ==
	                telemetry(overdriven, "impulse", "impulse_progress"),
	        "extra paid overdrive may accelerate impulse preparation and must resume without losing or duplicating "
	        "progress");
	// Doubling requested output on a discharge-constrained reserve must not grant
	// doubled thrust when only the ordinary amount of energy was paid.
	ship["resources"]["stores"][0]["discharge_rate"] = 200;
	const auto limited_catalog = load(source);
	World limited, control;
	limited.load(limited_catalog, "test.map");
	control.load(limited_catalog, "test.map");
	for (auto *world : {&limited, &control})
	{
		claim(*world);
		command(*world, "assisted", 0, "drive");
		world->tick();
		command(*world, "longitudinal_thrust", 1000, "drive");
	}
	command(limited, "overdrive", 1, "storage", 104);
	limited.tick();
	control.tick();
	require(telemetry(limited, "drive", "supply_permille") == 500 && body(limited).vx == body(control).vx &&
	            paid(limited) == paid(control),
	        "overdrive output must be scaled by the fraction of its enlarged energy request actually paid");
}
void prepaid_low_rate_actuation()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["drive"]["power_rate"] = 1;
	ship["resources"]["stores"][0]["initial"] = 1;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "assisted", 0, "drive");
	world.tick();
	command(world, "longitudinal_thrust", 1000, "drive");
	world.tick();
	require(telemetry(world, "drive", "stored_energy") == 0 && paid(world) == 1 && body(world).vx > 0,
	        "a one-unit-per-second flight actuator must debit its first unit before producing its first thrust");
	const auto first = body(world);
	command(world, "longitudinal_thrust", 0, "drive");
	for (int tick = 0; tick < 7; ++tick)
		world.tick();
	require(body(world).vx == first.vx && paid(world) == 0,
	        "pausing prepaid thrust must preserve momentum without consuming the remaining work credit");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "prepaid low-rate work must survive raw World continuation");
	resumed.clear_claims();
	claim(resumed);
	command(world, "longitudinal_thrust", 1000, "drive");
	command(resumed, "longitudinal_thrust", 1000, "drive");
	for (int tick = 0; tick < 19; ++tick)
	{
		const auto prior = body(world).vx;
		world.tick();
		resumed.tick();
		require(body(world).vx > prior && body(world).vx == body(resumed).vx && paid(world) == 0,
		        "resumed thrust must consume only the remainder of its previously paid finite work");
	}
	const auto exhausted = body(world).vx;
	world.tick();
	require(body(world).vx == exhausted && telemetry(world, "drive", "supply_permille") == 0,
	        "after twenty low-rate thrust ticks the spent unit cannot fund a twenty-first effect");
	ship["systems"]["side"]["power_rate"] = 1;
	const auto shared_catalog = load(source);
	World shared;
	shared.load(shared_catalog, "test.map");
	claim(shared);
	command(shared, "assisted", 0, "drive");
	shared.tick();
	command(shared, "longitudinal_thrust", 1000, "drive");
	command(shared, "lateral_thrust", 1000, "side", 102);
	for (int tick = 0; tick < 10; ++tick)
	{
		const auto previous = body(shared);
		shared.tick();
		require(body(shared).vx > previous.vx && body(shared).vy > previous.vy,
		        "two low-rate consumers may share only the one unit of work actually prepaid on their common reserve");
	}
	const auto shared_exhausted = body(shared);
	shared.tick();
	require(body(shared).vx == shared_exhausted.vx && body(shared).vy == shared_exhausted.vy &&
	            telemetry(shared, "drive", "supply_permille") == 0 &&
	            telemetry(shared, "side", "supply_permille", 102) == 0,
	        "shared prepaid credit must exhaust once rather than be duplicated independently for each consumer");
	ship["resources"]["stores"][0]["initial"] = 0;
	for (auto &spec : ship["systems"])
		if (spec.contains("power_store"))
			spec["powerless_start"] = true;
	const auto empty_catalog = load(source);
	World empty;
	empty.load(empty_catalog, "test.map");
	claim(empty);
	command(empty, "assisted", 0, "drive");
	empty.tick();
	command(empty, "longitudinal_thrust", 1000, "drive");
	for (int tick = 0; tick < 25; ++tick)
		empty.tick();
	require(body(empty).vx == 0 && telemetry(empty, "drive", "supply_permille") == 0,
	        "empty one-unit-per-second equipment cannot perform borrowed work before its first debit");
}
void unpaid_and_protected()
{
	for (const bool overheated : {false, true})
	{
		auto source = fixture();
		auto &store = source["entities"][0]["resources"]["stores"][0];
		if (overheated)
			store["initial_heat"] = 100000;
		else
			store["initial"] = 0;
		for (auto &spec : source["entities"][0]["systems"])
			if (spec.contains("power_store"))
				spec["powerless_start"] = true;
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		command(world, "assisted", 0, "drive");
		world.tick();
		command(world, "longitudinal_thrust", 1000, "drive");
		command(world, "impulse", 1, "impulse");
		world.tick();
		world.tick();
		require(body(world).vx == 0 && telemetry(world, "drive", "actual_longitudinal") == 0 &&
		            telemetry(world, "impulse", "impulse_progress") == 0,
		        "empty or thermally stopped reserves cannot produce thrust or advance impulse charge");
	}
	auto source = fixture();
	source["entities"][0]["resources"]["stores"][0]["initial"] = 15;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "assisted", 0, "drive");
	world.tick();
	command(world, "longitudinal_thrust", 1000, "drive");
	command(world, "lateral_thrust", 1000, "side", 102);
	world.tick();
	require(telemetry(world, "drive", "stored_energy") == 0 && paid(world) == 15 && body(world).vx > 0 &&
	            body(world).vy > 0,
	        "simultaneous consumers must share and exhaust a finite reserve without duplicating energy");
	const auto coast = body(world);
	world.tick();
	require(body(world).vx == coast.vx && body(world).vy == coast.vy &&
	            telemetry(world, "drive", "actual_longitudinal") == 0 &&
	            telemetry(world, "side", "actual_lateral", 102) == 0,
	        "depleted continuous equipment must suppress further output while retaining existing velocity");
}
void match_speed()
{
	for (const bool feed : {false, true})
	{
		auto source = fixture();
		if (!feed)
			source["entities"][0]["stations"][0]["consoles"][0]["feeds"] = Json::array();
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		require(world.claim(201, 2, "helm"), "observed actor must retain its own independent operating authority");
		command(world, "speed_mm_s", 3000, "drive", 201, 2);
		command(world, "selected_contact", 2, "drive");
		world.tick();
		command(world, "match_speed", 1, "drive");
		world.tick();
		world.tick();
		const auto matched = telemetry(world, "drive", "matched_speed");
		if (!feed)
		{
			require(matched == 0 && telemetry(world, "drive", "match_stale") != 0 && body(world, 2).vx > 0 &&
			            system(*world.observe(101), "drive").requested == 0,
			        "Match Speed without a permitted sensor feed must not discover hidden target velocity");
			continue;
		}
		require(matched > 0 && telemetry(world, "drive", "match_stale") == 0,
		        "Match Speed must follow the selected contact's permitted observed speed");
		command(world, "heading_mdeg", 90000, "turn");
		world.tick();
		require(telemetry(world, "drive", "match_speed") == 1, "steering alone must not cancel persistent Match Speed");
		command(world, "radar_enabled", 0, "radar", 103);
		world.tick();
		const auto disabled_save = world.save();
		World disabled_resumed;
		disabled_resumed.restore(catalog, disabled_save);
		auto future = disabled_save;
		for (auto &body : future["bodies"])
			for (auto &state : body["gameplay"])
				if (state.contains("flight"))
					state["flight"]["observed_tick"] = future.at("tick").get<std::uint64_t>() + 1;
		bool rejected_future = false;
		try
		{
			disabled_resumed.restore(catalog, future);
		}
		catch (const std::exception &)
		{
			rejected_future = true;
		}
		require(rejected_future && disabled_resumed.save() == disabled_save,
		        "future-dated saved observations must be rejected without replacing the current World");
		require(disabled_resumed.save() == disabled_save,
		        "advanced radar enabled state must round-trip before any authority or tick changes");
		disabled_resumed.clear_claims();
		claim(disabled_resumed);
		require(disabled_resumed.observe(101)->contacts.empty(),
		        "restoring a disabled advanced radar cannot reveal contacts before reenabling");
		const auto last_known = telemetry(world, "drive", "matched_speed");
		for (int tick = 0; tick < 4; ++tick)
		{
			world.tick();
			disabled_resumed.tick();
		}
		require(disabled_resumed.observe(101)->contacts.empty() &&
		            telemetry(disabled_resumed, "drive", "matched_speed") == last_known,
		        "disabled restored sensing must retain old evidence while withholding live target updates");
		require(telemetry(world, "drive", "match_stale") == 1 &&
		            telemetry(world, "drive", "matched_speed") == last_known &&
		            system(*world.observe(101), "drive").requested == last_known,
		        "stale Match Speed must retain last observed speed while unseen target motion continues");
		command(world, "radar_enabled", 1, "radar", 103);
		world.tick();
		require(telemetry(world, "drive", "match_stale") == 0 &&
		            telemetry(world, "drive", "matched_speed") > last_known,
		        "fresh observations of the same contact must resume Match Speed updates");
		command(world, "speed_mm_s", 1000, "drive");
		world.tick();
		require(telemetry(world, "drive", "match_speed") == 0 && system(*world.observe(101), "drive").requested == 1000,
		        "explicit throttle input must cancel matching and retain its new speed request");
	}
}
void immutable_flight_demands()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["a"] = ship["systems"]["drive"];
	ship["systems"]["b"] = ship["systems"]["drive"];
	ship["systems"].erase("drive");
	ship["stations"][0]["consoles"][0]["systems"] = {"a", "b", "turn", "boost", "impulse"};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "speed_mm_s", 1000, "a");
	world.tick();
	require(body(world).vx > 0 && telemetry(world, "b", "actual_longitudinal") == 0 &&
	            telemetry(world, "b", "used_power") == 0,
	        "an idle actuator cannot re-plan free braking after another fitted actuator accelerates");
	world.tick();
	require(telemetry(world, "b", "actual_longitudinal") < 0 && telemetry(world, "b", "used_power") > 0,
	        "opposing correction must be quoted and paid on the next common motion snapshot");
	World paired;
	paired.load(catalog, "test.map");
	claim(paired);
	command(paired, "speed_mm_s", 150, "a");
	command(paired, "speed_mm_s", 150, "b");
	for (int tick = 0; tick < 4; ++tick)
	{
		paired.tick();
		require(body(paired).vx == 150,
		        "same-target fitted actuators must clip against aggregate motion without overshoot");
	}
	ship["systems"]["a"] = ship["systems"]["turn"];
	ship["systems"].erase("turn");
	ship["stations"][0]["consoles"][0]["systems"] = {"a", "b", "boost", "impulse"};
	const auto steering_catalog = load(source);
	World steering;
	steering.load(steering_catalog, "test.map");
	claim(steering);
	command(steering, "heading_mdeg", 90000, "a");
	command(steering, "speed_mm_s", 1000, "b");
	steering.tick();
	require(body(steering).heading > 0 && body(steering).vx > 0 && body(steering).vy == 0,
	        "funded thrust must retain its prepared heading basis after an earlier steering actuator turns");
}
void paid_sensor_startup()
{
	for (const bool funded : {false, true})
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["radar"]["energy_per_second"] = 1;
		ship["systems"]["radar"]["power_store"] = "sensor-cell";
		ship["systems"]["radar"]["powerless_start"] = true;
		ship["resources"]["stores"].push_back({{"id", "sensor-cell"},
		                                       {"capacity", 1},
		                                       {"initial", funded ? 1 : 0},
		                                       {"charge_rate", 20},
		                                       {"discharge_rate", 20}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		require(world.observe(101)->contacts.empty(), "paid radar cannot reveal contacts before its first funded tick");
		command(world, "selected_contact", 2, "drive");
		world.tick();
		require(world.observe(101)->contacts.empty() != funded,
		        "only funded radar may reveal contacts, including work prepaid with the last energy unit");
		command(world, "match_speed", 1, "drive");
		world.tick();
		require((telemetry(world, "drive", "match_stale") != 0) == !funded,
		        "the first Match Speed observation must come from an actually funded sensor tick");
		const auto saved = world.save();
		World resumed;
		resumed.restore(catalog, saved);
		resumed.clear_claims();
		claim(resumed);
		for (int tick = 0; tick < 18; ++tick)
		{
			world.tick();
			resumed.tick();
			require(world.observe(101)->contacts.empty() != funded &&
			            resumed.observe(101)->contacts.empty() == world.observe(101)->contacts.empty(),
			        "paid sensor work credit must survive continuation without an energy-balance shortcut");
		}
		world.tick();
		require(world.observe(101)->contacts.empty(), "exhausted sensor credit cannot reveal fresh contacts");
	}
}
void disabled_cooling()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["initial_heat"] = 10;
	ship["resources"]["coolant_sources"] =
	    Json::parse(R"([{"id":"tank","initial":100,"removal_rate":40,"coolant_per_heat_ppm":1000000}])");
	ship["resources"]["cooling_links"] =
	    Json::parse(R"([{"id":"feed","source":"tank","store":"cell","capacity_rate":40,"allocation_rate":40}])");
	ship["systems"]["cooler"] = {{"kind", "coolant"}, {"cooling_link", "feed"}, {"enabled", false}};
	ship["stations"][0]["consoles"][3]["systems"].push_back("cooler");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	world.tick();
	require(telemetry(world, "storage", "heat", 104) == 10 && telemetry(world, "cooler", "coolant_stock", 104) == 100,
	        "authored disabled coolant equipment must neither remove heat nor spend finite coolant");
	command(world, "system_enabled", 1, "cooler", 104);
	world.tick();
	require(telemetry(world, "storage", "heat", 104) == 8 && telemetry(world, "cooler", "coolant_stock", 104) == 98,
	        "enabling authored disabled cooling must reconnect its real resource link");
	command(world, "system_enabled", 0, "cooler", 104);
	world.tick();
	require(telemetry(world, "storage", "heat", 104) == 8 && telemetry(world, "cooler", "coolant_stock", 104) == 98,
	        "runtime disable must use the same isolation semantics as authored initial disable");
}
void invalid_controller_actions()
{
	const Json self{{"observation", "self.system"}};
	const auto action = [&](const char *name, Json arguments)
	{
		arguments.insert(arguments.begin(), self);
		return Json{{"action", name}, {"arguments", std::move(arguments)}};
	};
	const auto maximum = std::numeric_limits<std::int64_t>::max();
	const auto minimum = std::numeric_limits<std::int64_t>::min();
	const std::vector<Json> cases{
	    Json::array({action("set_flight_mode", Json::array({2}))}),
	    Json::array({action("set_destination", Json::array({{{"type", "region"}, {"id", 1}}, maximum, maximum}))}),
	    Json::array({action("set_destination", Json::array({{{"type", "region"}, {"id", 1}}, minimum, minimum}))}),
	    Json::array({action("set_flight_mode", Json::array({1})),
	                 action("set_match_speed", Json::array({{{"type", "entity"}, {"id", 2}}, true}))}),
	    Json::array({action("set_generator_output", Json::array({maximum}))})};
	for (std::size_t i = 0; i < cases.size(); ++i)
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		const std::string target = i == 4 ? "generator" : "drive";
		if (i == 4)
		{
			ship["systems"]["generator"] = {{"kind", "generator"}, {"generator", "reactor"}, {"power_store", "cell"}};
			ship["resources"]["generators"] = Json::array({{{"id", "reactor"},
			                                                {"store", "cell"},
			                                                {"maximum_rate", 1000},
			                                                {"response_rate", 1000},
			                                                {"initial_output", 100}}});
			ship["stations"][0]["consoles"][3]["systems"].push_back("generator");
		}
		ship["controllers"] = {{target, "test.invalid-action"}};
		source["trees"].push_back({{"id", "test.invalid-action"},
		                           {"context", "system"},
		                           {"initial", "attempt"},
		                           {"states", Json::array({{{"id", "attempt"}, {"entry", cases[i]}}})}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		world.tick();
		claim(world);
		if (i == 4)
			require(
			    telemetry(world, "generator", "generator_output", 104) == 100,
			    "out-of-range AI generator permille must refuse before overflowing multiplication or changing output");
		else
			require(telemetry(world, "drive", "assisted") == (i == 3 ? 0 : 1) &&
			            telemetry(world, "drive", "selected_contact") == 0 &&
			            telemetry(world, "drive", "match_speed") == 0 &&
			            system(*world.observe(101), "drive").requested == 0,
			        "invalid AI mode/destination and refused manual Match Speed must preserve all previous flight "
			        "settings atomically");
	}
}
std::string control_id(const ConsoleModel &model, const std::string &action, std::int64_t value)
{
	static constexpr char hex[] = "0123456789abcdef";
	for (const auto &descriptor : model.observation()->gameplay.at("controls"))
		if (descriptor.at("action") == action && descriptor.at("value") == value)
		{
			std::string encoded;
			for (const unsigned char byte : descriptor.at("id").get<std::string>())
			{
				encoded += hex[byte >> 4];
				encoded += hex[byte & 15];
			}
			return "game.control/" + encoded;
		}
	throw std::runtime_error("required authored semantic button missing");
}
void equipment_ordering_and_recovery()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["aux-drive"] = ship["systems"]["drive"];
	ship["systems"]["aux-drive"]["acceleration_mm_s2"] = 700;
	ship["stations"][0]["consoles"][0]["systems"].push_back("aux-drive");
	auto legacy = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "mixed equipment")
	                  .at("entities")[0];
	legacy["id"] = "test.legacy";
	source["entities"].push_back(legacy);
	auto basic = legacy;
	basic["id"] = "test.basic";
	basic["systems"]["port"] = basic["systems"]["engine"];
	basic["systems"]["port"]["kind"] = "engine";
	basic["systems"]["starboard"] = basic["systems"]["port"];
	basic["systems"].erase("engine");
	source["entities"].push_back(basic);
	for (const auto &[id, definition] : std::vector<std::pair<int, std::string>>{{3, "test.legacy"}, {4, "test.basic"}})
		source["maps"][0]["entities"].push_back(
		    {{"id", id}, {"definition", definition}, {"x_mm", id * 200000}, {"y_mm", 0}, {"heading_mdeg", 0}});
	const auto catalog = load(source);
	World ordered, reversed;
	ordered.load(catalog, "test.map");
	std::reverse(source["maps"][0]["entities"].begin(), source["maps"][0]["entities"].end());
	reversed.load(load(source), "test.map");
	for (auto *world : {&ordered, &reversed})
	{
		claim(*world);
		require(world->claim(201, 2, "helm") && world->claim(303, 3) && world->claim(404, 4),
		        "legacy, named basic and paid equipment must coexist with independent ownership");
		command(*world, "speed_mm_s", 9000, "drive");
		command(*world, "speed_mm_s", 2000, "aux-drive");
		command(*world, "heading_mdeg", 45000, "turn");
		command(*world, "speed_mm_s", 4000, "drive", 201, 2);
		command(*world, "speed_mm_s", 1500, "engine", 303, 3);
		command(*world, "speed_mm_s", 3000, "port", 404, 4);
		command(*world, "speed_mm_s", 1000, "starboard", 404, 4);
	}
	for (int i = 0; i < 8; ++i)
	{
		ordered.tick();
		reversed.tick();
		require(ordered.save().at("bodies") == reversed.save().at("bodies"),
		        "mixed capability tables and creation order must not reorder shared-body actuation or resource state");
	}
	require(ordered.inspect().size() == 4 && ordered.scene().size() == 4 && ordered.observe(101)->contacts.size() == 1,
	        "internal equipment identities must not appear as bodies, scene objects or radar contacts");
	const auto saved = ordered.save();
	require(saved.at("bodies").size() == 4 && !saved.at("bodies")[2].contains("systems") &&
	            saved.at("bodies")[3].at("systems").contains("starboard"),
	        "unified runtime equipment must retain each legacy or named save representation");
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "mixed equipment must retain complete tick-boundary state on restore");
	for (auto *world : {&ordered, &resumed})
	{
		require(world->release(303) && world->release(404), "mixed equipment claims must release independently");
		require(world->claim(303, 3) && world->claim(404, 4), "released mixed equipment must remain claimable");
		require(world->observe(303)->requested_speed == 0 && system(*world->observe(404), "port").requested == 3000,
		        "legacy release resets and named equipment's retained settings must survive normalization");
	}
	for (int i = 0; i < 5; ++i)
	{
		ordered.tick();
		resumed.tick();
		require(ordered.save() == resumed.save(), "mixed equipment continuation diverged after handover");
	}
	auto malformed = saved;
	malformed["bodies"][0]["gameplay"][0]["id"] = "unknown-fitting";
	const auto live = resumed.save();
	bool refused = false;
	try
	{
		resumed.restore(catalog, malformed);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused && resumed.save() == live && resumed.inspect().size() == 4,
	        "failed equipment restore must leave the live parent, children and continuation unchanged");
	malformed = saved;
	malformed["delegated"] = std::map<EntityId, std::set<std::string>>{{3, {"engine"}}};
	refused = false;
	try
	{
		resumed.restore(catalog, malformed);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused && resumed.save() == live,
	        "runtime normalization must not admit formerly unsupported legacy delegation state on restore");
}
void semantic_controls()
{
	auto source = fixture();
	source["entities"][0]["systems"]["drive"]["turn_mdeg_s"] = 90000;
	source["consoles"][0]["knowledge_roles"] = {"test.operator"};
	source["knowledge"] = Json::parse(R"json({
		"roles":["test.operator"],"schemas":[
			{"id":"test.report","producers":["test.operator"],"readers":["test.operator"],
			 "fields":[{"id":"amount","type":"integer"}]}
		]
	})json");
	source["messages"] = Json::parse(R"json([
		{"id":"test.notice","schema":"test.report","destination":{"kind":"console","id":"helm"},
		 "title_key":"Notice","body_key":"Supply {amount}","automatic":false,"delay_ticks":1,
		 "fields":{"amount":7},"parameters":{"amount":"7"}}
	])json");
	source["entities"][0]["message_templates"] = {"test.notice"};
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
	ConsoleModel model;
	model.refresh(session);
	require(model.command("speed_mm_s", 1000, "drive") && model.command("heading_mdeg", 33000, "drive") &&
	            model.system_request("drive", "speed_mm_s") == 1000 &&
	            model.system_request("drive", "heading_mdeg") == 33000,
	        "composite flight System must retain separate pending feedback for speed and heading writes in one tick");
	session->tick();
	model.refresh(session);
	require(model.system_request("drive", "speed_mm_s") == 1000 &&
	            model.system_request("drive", "heading_mdeg") == 33000,
	        "committed composite-channel feedback must retain each setting in its original units");
	require(!control_id(model, "heading_mdeg", 50999).empty(),
	        "composite Engine heading increment must use desired heading rather than its independent speed request");
	require(model.system_minimum("drive") == -5000 && model.set_speed_fraction(0, "drive"),
	        "reverse throttle semantics must map their lower endpoint to the authored reverse bound");
	session->tick();
	model.refresh(session);
	require(model.system_request("drive") == -5000,
	        "reverse slider command must commit through the live production session");
	require(model.command("assisted", 0, "drive"), "the semantic model must admit the assisted/manual control");
	session->tick();
	model.refresh(session);
	std::int64_t positive = 0;
	for (const auto &descriptor : model.observation()->gameplay.at("controls"))
		if (descriptor.at("action") == "longitudinal_thrust" && descriptor.at("value").get<std::int64_t>() > 0)
			positive = descriptor.at("value");
	require(positive > 0 && !model.gameplay_controls().empty() && !model.gameplay_readouts().empty(),
	        "permitted operating buttons and telemetry must have a shared headless semantic projection");
	const auto button = control_id(model, "longitudinal_thrust", positive);
	require(model.hold_control(button), "signed manual button press must use ordinary admitted commands");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "drive").telemetry.at("actual_longitudinal") > 0,
	        "held semantic manual input must produce only paid actuation");
	require(model.release_control(button),
	        "manual button release must explicitly submit neutral to its original System");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "drive").telemetry.at("actual_longitudinal") == 0 &&
	            !model.act("game.control/xyz") && !model.command("arbitrary-private-action", 1, "drive"),
	        "neutral release must stop actuation and malformed/unprojected semantic actions must be refused");
	require(model.command("message.test.notice", 0, "drive"),
	        "explicit authored human report must use its current admitted descriptor");
	session->tick();
	model.refresh(session);
	require(model.announcement() == "Notice: Supply 7",
	        "fresh eligible notification must translate its template and substitute parameters once");
	ConsoleModel reopened;
	reopened.refresh(session);
	require(reopened.announcement().empty(),
	        "opening a Console must baseline existing notification sequences without replaying history");
	require(model.act("game.contacts"), "permitted contact readout supplies independent user feedback");
	const auto contact_notice = model.announcement();
	model.refresh(session);
	require(model.announcement() == contact_notice,
	        "repeated notification snapshots must not replace new feedback with an old alert");
	require(model.command("message.test.notice", 0, "drive"), "a later explicit report remains independently sendable");
	session->tick();
	model.refresh(session);
	require(model.announcement() == "Notice: Supply 7",
	        "a newer delivery sequence must announce even when its translated text matches an older report");
	require(model.command("longitudinal_thrust", positive, "drive"),
	        "a manual axis can remain active before an explicit authority handover");
	session->tick();
	model.refresh(session);
	const auto away = control_id(model, "away", 1);
	require(model.act(away), "explicit Away must be available through the projected safe button identity");
	session->tick();
	model.refresh(session);
	require(!system(*model.observation(), "drive").controlled &&
	            system(*model.observation(), "drive").telemetry.at("longitudinal_thrust") == 0,
	        "Away must transfer effective control and clear stale human axes through the real authority path");
}
} // namespace

int main()
{
	try
	{
		launch_curve_and_linear_braking();
		manual_and_conservation();
		boost_and_impulse();
		paid_overdrive();
		prepaid_low_rate_actuation();
		unpaid_and_protected();
		match_speed();
		immutable_flight_demands();
		paid_sensor_startup();
		disabled_cooling();
		invalid_controller_actions();
		equipment_ordering_and_recovery();
		semantic_controls();
		std::cout << "Paid flight integration passed: control, resources, impulse, observed matching and semantics\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
