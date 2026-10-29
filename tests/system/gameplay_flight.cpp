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

import phoenix.content.controllers;
import phoenix.content.distribution;
import phoenix.simulation;
import phoenix.session.local;
import phoenix.console;
import phoenix.console_controls;

#include "../support/machinery_baseline.hpp"
using namespace phoenix;
extern "C" void resource_allocation_lifecycle(const Catalog &);
extern "C" void named_resource_allocation_contracts(const Catalog &);
namespace
{
using phoenix::test::require;
Json fixture()
{
	auto source =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic paid flight");
	source["schema"] = 4;
	source["resources"] = Json::array({{{"id", "fixture.resource.energy"},
	                                    {"label", "Flight energy"},
	                                    {"tags", {"energy"}},
	                                    {"sizes", {{"energy", 1}}}}});
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
		{"id":"cell","resource":"fixture.resource.energy","capacity":100000,"initial":50000,"charge_rate":100000,
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
Json explicit_fixture(Json source)
{
	const auto compiled = load(source);
	for (auto &entity : source["entities"])
		entity = compiled.entities.at(entity.at("id").get<std::string>());

	// Keep this authoring builder editable before fixture_pack emits schema 2.
	// Binding comes from the production shared-widget compiler.
	for (auto &entity : source["entities"])
		if (entity.contains("control_layout"))
		{
			const auto id = entity.at("id").get<std::string>();
			const auto layout = compile_controllable_layout(compiled, id);
			auto &authored = entity["control_layout"];
			for (auto &instance : authored["controllers"])
			{
				auto row = bind_control_widget(compile_control_widget(compiled, instance.at("widget")), instance);
				for (const auto *key : {"widget", "equipment", "star_levels", "operations", "data_placement"})
					row.erase(key);
				instance = row;
			}
			for (std::size_t p = 0; p < layout.profiles.size(); ++p)
			{
				auto &profile = authored["profiles"][p];
				profile["consoles"] = Json::object();
				for (std::size_t c = 0; c < layout.controllers.size(); ++c)
				{
					const auto &controller = layout.controllers[c];
					profile["consoles"][controller.console][layout.profiles[p].human[c] ? "human" : "ai"].push_back(
					    controller.id);
				}
			}
			authored["schema"] = 1;
		}
	// The converted fittings reference the explicit catalogue produced by the
	// synthetic fixture materialiser; retain those definitions with the fittings.
	source["control_widgets"] = Json::array();
	for (const auto &[id, widget] : compiled.control_widgets)
		source["control_widgets"].push_back(widget);
	source["effects"] = Json::array();
	for (const auto &[id, effect] : compiled.effects)
		source["effects"].push_back(effect);
	return source;
}
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
	OperatingCommand request{actor, action, value, {}};
	const auto view = world.observe(console);
	bool found = false;
	if (view)
		for (const auto &owner : view->gameplay.at("controllers"))
			for (const auto &[channel, section] : owner.at("commands").items())
				if (section.at("action") == action &&
				    std::find(section.at("consumers").begin(), section.at("consumers").end(), id) !=
				        section.at("consumers").end())
				{
					require(!found, "Physical test command must have a unique Controller subscription");
					found = true;
					request.controller = owner.at("id");
					request.channel = channel;
					request.generation = owner.at("generation");
					if (section.at("semantics") == "request")
						request.request_id = section.at("request_id").get<std::uint64_t>() + 1;
				}
	if (!found || !world.submit(console, request))
		throw std::runtime_error("ordinary typed command refused: " + id + "." + action + "=" + std::to_string(value));
}
std::int64_t paid(const World &world)
{
	std::int64_t total = 0;
	std::set<std::string> counted;
	for (const auto console : {101u, 102u, 103u, 104u})
		if (const auto observed = world.observe(console))
			for (const auto &item : observed->systems)
				if (counted.insert(item.id).second && item.telemetry.contains("used_power"))
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
	require(crew_catalog.entities.at("test.ship").at("systems").at("drive").at("deceleration_mm_s2") ==
	            fixture()["entities"][0]["systems"]["drive"]["acceleration_mm_s2"],
	        "restricted crew assets must retain the resolved default passive damping rate");
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
	require(std::abs(coast.vx) < std::abs(moved.vx) && std::abs(coast.vy) < std::abs(moved.vy) &&
	            coast.heading == moved.heading,
	        "neutral manual controls must remove powered actuation and passively brake without turning");
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
Json targeted_fixture()
{
	auto source = fixture();
	auto &systems = source["entities"][0]["systems"];
	systems["boost"]["targets"] = Json::parse(
	    R"([{"system":"drive","property":"max_speed_mm_s","multiplier_permille":2000},{"system":"drive","property":"acceleration_mm_s2","multiplier_permille":2000},{"system":"turn","property":"turn_mdeg_s","multiplier_permille":2000}])");
	systems["impulse"]["targets"] = Json::parse(
	    R"([{"system":"drive","property":"max_speed_mm_s","multiplier_permille":5000},{"system":"drive","property":"acceleration_mm_s2","multiplier_permille":4000}])");
	systems["impulse"]["charge_energy"] = 20;
	systems["impulse"]["minimum_reserve"] = 100;
	systems["side"]["lateral_direction"] = "port";
	return source;
}
void zero_propulsion_modifier()
{
	auto source = targeted_fixture();
	source["effects"] = Json::parse(R"json([{"id":"test.engine-stop","kind":"modifier","duration_ticks":3,
      "contributions":{"stop":{"property":"propulsion","delta_permille":-1000}}}])json");
	source["maps"][0]["scenario"] = {{"tree", "test.engine-stop-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	source["trees"] =
	    Json::array({Json::parse(R"json({"id":"test.engine-stop-tree","context":"scenario","initial":"stop",
      "states":[{"id":"stop","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"drive","test.engine-stop","stop"]}]}]})json")});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "speed_mm_s", 10000, "drive");
	for (int i = 0; i < 3; ++i)
		world.tick();
	require(body(world).vx == 0, "zero propulsion pauses acceleration without a launch-curve minimum leak");
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	require(body(world).vx > 0 && world.save() == resumed.save(),
	        "expired propulsion modifier restores actual motion with deterministic continuation");
}
void bounded_propulsion_modifier()
{
	auto source = fixture();
	source["maps"][0]["entities"].erase(1);
	source["entities"][0]["systems"]["drive"]["max_speed_mm_s"] = 1000000000;
	source["entities"][0]["systems"]["drive"]["acceleration_mm_s2"] = 1000000000;
	const auto ordinary_catalog = load(source);
	source["effects"] = Json::parse(R"json([{"id":"test.engine-upper-bound","kind":"modifier",
      "contributions":{"acceleration":{"property":"propulsion","delta_permille":100000},
                       "speed":{"property":"speed","delta_permille":100000}}}])json");
	source["maps"][0]["scenario"] = {{"tree", "test.engine-bound-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	source["trees"] =
	    Json::array({Json::parse(R"json({"id":"test.engine-bound-tree","context":"scenario","initial":"apply",
      "states":[{"id":"apply","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"drive","test.engine-upper-bound","large"]}]}]})json")});
	World ordinary, modified;
	ordinary.load(ordinary_catalog, "test.map");
	modified.load(load(source), "test.map");
	for (auto *world : {&ordinary, &modified})
	{
		claim(*world);
		command(*world, "speed_mm_s", 1000000000, "drive");
		world->tick();
	}
	require(body(modified).vx > 0 && body(modified).vx == body(ordinary).vx && body(modified).x == body(ordinary).x,
	        "large property bonuses saturate at native physical rate bounds before flight arithmetic");
}
void speed_modifier_funding()
{
	for (const int reserve : {0, 1})
	{
		auto source = targeted_fixture();
		source["maps"][0]["entities"].erase(1);
		source["entities"][0]["resources"]["stores"][0]["initial"] = reserve;
		for (auto &spec : source["entities"][0]["systems"])
			if (spec.contains("power_store"))
				spec["powerless_start"] = true;
		source["effects"] = Json::parse(R"json([{"id":"test.cruise-speed","kind":"modifier","duration_ticks":5,
          "contributions":{"speed":{"property":"speed","delta_permille":1000}}}])json");
		source["maps"][0]["scenario"] = {{"tree", "test.cruise-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
		source["trees"] =
		    Json::array({Json::parse(R"json({"id":"test.cruise-tree","context":"scenario","initial":"apply",
          "states":[{"id":"apply","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"drive","test.cruise-speed","speed"]}]}]})json")});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		auto cruising = world.save();
		cruising["bodies"][0]["body"]["velocity"]["x"] = 10000;
		world.restore(catalog, cruising);
		claim(world);
		command(world, "speed_mm_s", 10000, "drive");
		world.tick();
		require(telemetry(world, "drive", "supply_permille") == (reserve ? 100 : 0) &&
		            telemetry(world, "drive", "stored_energy") == 0,
		        "speed modifier quotes its new cruise demand before allocating scarce energy");
		require(reserve ? body(world).vx > 10000 : body(world).vx == 10000,
		        "only funded movement accelerates beyond the old authored cruise cap");
		const auto exhausted_velocity = body(world).vx;
		world.tick();
		require(body(world).vx == exhausted_velocity,
		        "exhausted speed modifier cannot obtain free acceleration from an old zero-duty quote");
	}
}
void targeted_modifiers()
{
	const auto catalog = load(targeted_fixture());
	resource_allocation_lifecycle(catalog);
	auto named_source = targeted_fixture();
	auto &named_store = named_source["entities"][0]["resources"]["stores"][0];
	named_store["initial"] = 1;
	named_store["discharge_rate"] = 20;
	named_store["operation_heat_ppm"] = 1000000;
	named_resource_allocation_contracts(load(named_source));
	World boosted, ordinary;
	boosted.load(catalog, "test.map");
	ordinary.load(catalog, "test.map");
	claim(boosted);
	claim(ordinary);
	for (auto *world : {&boosted, &ordinary})
	{
		command(*world, "assisted", 0, "drive");
		command(*world, "assisted", 0, "turn");
		world->tick();
		command(*world, "longitudinal_thrust", 1000, "drive");
		command(*world, "manual_steering", 1000, "turn");
	}
	command(boosted, "boost", 1, "boost");
	const auto before = telemetry(boosted, "drive", "stored_energy");
	boosted.tick();
	ordinary.tick();
	require(body(boosted).vx > body(ordinary).vx && body(boosted).heading > body(ordinary).heading &&
	            telemetry(boosted, "boost", "actual_longitudinal") == 0 &&
	            telemetry(boosted, "boost", "effective_boost") == 1000,
	        "paid fitted modifiers change their targets rather than generating independent thrust");
	require(before - telemetry(boosted, "drive", "stored_energy") == paid(boosted),
	        "modifier previews cannot debit the resource network twice");
	command(boosted, "longitudinal_thrust", 0, "drive");
	command(boosted, "manual_steering", 0, "turn");
	command(boosted, "boost", 0, "boost");
	const auto coast = body(boosted);
	boosted.tick();
	require(body(boosted).vx < coast.vx && body(boosted).vx >= 0 && telemetry(boosted, "boost", "effective_boost") == 0,
	        "releasing a targeted boost removes modifiers while passive damping brakes a neutral velocity request");
	command(boosted, "lateral_thrust", 1000, "side", 102);
	boosted.tick();
	require(telemetry(boosted, "side", "actual_lateral", 102) == 0,
	        "port-only actuators cannot create starboard thrust");
	command(boosted, "lateral_thrust", -1000, "side", 102);
	boosted.tick();
	require(telemetry(boosted, "side", "actual_lateral", 102) < 0,
	        "directional actuators execute their supported direction");
	command(boosted, "impulse", 1, "impulse");
	command(boosted, "manual_steering", 1000, "turn");
	const auto heading = body(boosted).heading;
	boosted.tick();
	require(telemetry(boosted, "impulse", "impulse_charge_spent") == 10 && body(boosted).heading != heading,
	        "impulse pays charging slices and preserves normal steering while charging");
	const auto saved = boosted.save();
	World resumed;
	resumed.restore(catalog, saved);
	resumed.clear_claims();
	claim(resumed);
	command(resumed, "manual_steering", 1000, "turn");
	command(resumed, "lateral_thrust", -1000, "side", 102);
	boosted.tick();
	resumed.tick();
	require(telemetry(boosted, "impulse", "impulse_phase") == 2 &&
	            telemetry(resumed, "impulse", "impulse_charge_spent") == 20,
	        "exact paid charge expenditure survives recovery and completes once");
	const auto velocity = body(boosted).vx;
	boosted.tick();
	resumed.tick();
	require(body(boosted).vx > velocity && body(boosted).vx == body(resumed).vx,
	        "bound impulse transit starts on the following tick and survives recovery");
	for (const auto &bad : {std::string("missing"), std::string("boost")})
	{
		auto invalid = targeted_fixture();
		invalid["entities"][0]["systems"]["boost"]["targets"][0]["system"] = bad;
		bool refused = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "missing and recursive modifier targets must fail production loading");
	}
	auto invalid = targeted_fixture();
	invalid["entities"][0]["systems"]["impulse"]["minimum_reserve"] = 100001;
	bool refused = false;
	try
	{
		(void)load(invalid);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused, "minimum starting reserve must fit the connected local battery");
	// Both fittings spend their own energy and their paid percentage deltas add.
	auto stacked_source = targeted_fixture(), strongest_source = targeted_fixture();
	for (auto &binding : strongest_source["entities"][0]["systems"]["boost"]["targets"])
		binding["multiplier_permille"] = 3000;
	stacked_source["entities"][0]["systems"]["boost2"] = strongest_source["entities"][0]["systems"]["boost"];
	for (auto &binding : strongest_source["entities"][0]["systems"]["boost"]["targets"])
		binding["multiplier_permille"] = 4000;
	stacked_source["entities"][0]["stations"][0]["consoles"][0]["systems"].push_back("boost2");
	World stacked, strongest;
	stacked.load(load(stacked_source), "test.map");
	strongest.load(load(strongest_source), "test.map");
	claim(stacked);
	claim(strongest);
	for (auto *w : {&stacked, &strongest})
	{
		command(*w, "speed_mm_s", 10000, "drive");
		command(*w, "boost", 1, "boost");
	}
	command(stacked, "boost", 1, "boost2");
	stacked.tick();
	strongest.tick();
	require(body(stacked).vx == body(strongest).vx,
	        "multiple paid boosts add +100% and +200% to match one +300% modifier");
	auto limited_source = targeted_fixture();
	limited_source["entities"][0]["resources"]["stores"][0]["initial"] = 15;
	World limited;
	const auto limited_catalog = load(limited_source);
	limited.load(limited_catalog, "test.map");
	claim(limited);
	command(limited, "speed_mm_s", 10000, "drive");
	command(limited, "boost", 1, "boost");
	limited.tick();
	const auto fraction = telemetry(limited, "boost", "effective_boost");
	require(fraction > 0 && fraction < 1000, "a partially funded boost exposes only its paid bonus fraction");
	World limited_resumed;
	limited_resumed.restore(limited_catalog, limited.save());
	const auto limited_velocity = body(limited).vx;
	limited.tick();
	limited_resumed.tick();
	require(limited.save() == limited_resumed.save(),
	        "partial shared-reserve allocation preserves fractional requests and grants across recovery");
	require(telemetry(limited, "boost", "effective_boost") == 0 && body(limited).vx == limited_velocity,
	        "empty target and modifier batteries cannot produce additional acceleration");
	auto empty_source = targeted_fixture();
	empty_source["entities"][0]["systems"]["drive"]["power_store"] = "empty";
	empty_source["entities"][0]["systems"]["drive"]["powerless_start"] = true;
	empty_source["entities"][0]["resources"]["stores"].push_back(
	    {{"id", "empty"}, {"capacity", 1000}, {"initial", 0}, {"charge_rate", 0}, {"discharge_rate", 1000}});
	World empty;
	empty.load(load(empty_source), "test.map");
	claim(empty);
	command(empty, "speed_mm_s", 10000, "drive");
	command(empty, "boost", 1, "boost");
	empty.tick();
	require(body(empty).vx == 0 && telemetry(empty, "boost", "effective_boost") == 1000,
	        "a fully paid modifier cannot bypass its target engine's missing operating power");
	World charging;
	charging.load(catalog, "test.map");
	claim(charging);
	command(charging, "impulse", 1, "impulse");
	charging.tick();
	command(charging, "discharge_limit", 0, "storage", 104);
	charging.tick();
	require(telemetry(charging, "impulse", "impulse_progress") == 1000 &&
	            telemetry(charging, "impulse", "impulse_charge_spent") == 10,
	        "unpaid charging pauses without progressing or spending a second slice");
	command(charging, "impulse", 0, "impulse");
	charging.tick();
	require(telemetry(charging, "impulse", "impulse_progress") == 0 &&
	            telemetry(charging, "drive", "stored_energy") == 49990,
	        "charging cancellation discards progress without refunding spent energy");
	auto low_source = targeted_fixture();
	low_source["entities"][0]["resources"]["stores"][0]["initial"] = 99;
	World low;
	low.load(load(low_source), "test.map");
	claim(low);
	command(low, "impulse", 1, "impulse");
	low.tick();
	require(telemetry(low, "impulse", "impulse_phase") == 0 && telemetry(low, "impulse", "impulse_ready") == 0,
	        "charging admission checks the local battery threshold");
}
void impulse_interruptions()
{
	auto source = targeted_fixture();
	source["entities"].push_back({{"id", "test.obstacle"},
	                              {"render", source["entities"][0]["render"]},
	                              {"systems", Json::object()},
	                              {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 0}}}});
	source["maps"][0]["entities"][1]["definition"] = "test.obstacle";
	source["maps"][0]["entities"][1]["x_mm"] = 20000;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "impulse", 1, "impulse");
	world.tick();
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 2, "paid charge enters active transit");
	const auto active = world.save();
	for (int tick = 0; tick < 200 && telemetry(world, "impulse", "impulse_phase") != 0; ++tick)
		world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 0 && telemetry(world, "impulse", "impulse_reason") == 5,
	        "swept physical collision cancels impulse through the real collision path");
	command(world, "impulse", 1, "impulse");
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 1,
	        "a fresh impulse request can restart charging after collision without toggling a stale setting off first");
	world.restore(catalog, active);
	world.clear_claims();
	claim(world);
	command(world, "discharge_limit", 0, "storage", 104);
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 0 && telemetry(world, "impulse", "impulse_reason") == 3,
	        "insufficient ongoing operating power ends transit rather than granting free thrust");
	world.restore(catalog, active);
	world.clear_claims();
	claim(world);
	command(world, "engine_enabled", 0, "impulse");
	world.tick();
	require(telemetry(world, "impulse", "impulse_phase") == 0 && telemetry(world, "impulse", "impulse_reason") == 4,
	        "disabling the impulse fitting cancels physical transit");
	auto old = active;
	for (auto &system : old["bodies"][0]["gameplay"])
		if (system.contains("flight"))
			system["flight"]["schema"] = 4;
	bool refused = false;
	try
	{
		world.restore(catalog, old);
	}
	catch (const std::exception &error)
	{
		refused = std::string(error.what()).find("version") != std::string::npos;
	}
	require(refused, "incompatible flight saves explain their version requirement");
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
	require(body(world).vx > 0 && body(world).vx < sustained.vx &&
	            telemetry(world, "boost", "actual_longitudinal") == 0,
	        "releasing sustained boost removes extra thrust while neutral controls damp velocity gradually");
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
	require(body(world).vx > 0 && body(world).vx < velocity && telemetry(world, "impulse", "impulse_phase") == 0 &&
	            telemetry(world, "impulse", "impulse_progress") == 0,
	        "cancelling impulse clears charge and extra thrust while allowing passive damping");
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
	require(body(world).vx < first.vx && paid(world) == 0,
	        "releasing prepaid thrust must damp motion without consuming the remaining powered work credit");
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
	require(body(shared).vx * body(shared).vx + body(shared).vy * body(shared).vy <=
	                shared_exhausted.vx * shared_exhausted.vx + shared_exhausted.vy * shared_exhausted.vy &&
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
	require(body(world).vx * body(world).vx + body(world).vy * body(world).vy <=
	                coast.vx * coast.vx + coast.vy * coast.vy &&
	            telemetry(world, "drive", "actual_longitudinal") == 0 &&
	            telemetry(world, "side", "actual_lateral", 102) == 0,
	        "depleted continuous equipment must suppress powered output while permitting passive damping");
}

void follow_contact()
{
	for (int experiment = 0; experiment < 4; ++experiment)
	{
		const bool feed = experiment != 0;
		auto source = explicit_fixture(fixture());
		auto leader = source["entities"][0];
		leader["id"] = "test.leader";
		source["entities"].push_back(leader);
		source["maps"][0]["entities"][1]["definition"] = "test.leader";
		auto &ship = source["entities"][0];

		auto &layout = ship["control_layout"];
		for (auto &owner : layout["controllers"])
		{
			auto &commands = owner["commands"];
			commands.erase(std::remove_if(commands.begin(), commands.end(),
			                              [](const auto &c)
			                              {
				                              return c.at("action") == "follow_contact";
			                              }),
			               commands.end());
		}
		for (auto &owner : layout["controllers"])
			if (owner.at("id") == "drive")
			{
				owner["tree"] = "test.follow";
				owner.erase("fallback");
				owner["commands"].push_back({{"id", "follow"},
				                             {"action", "follow_contact"},
				                             {"initial", 0},
				                             {"arguments", {0, 15000, 8000}},
				                             {"consumers", {"drive", "turn"}}});
			}

		if (!feed)
			ship["stations"][0]["consoles"][0]["feeds"] = Json::array();
		if (experiment == 2)
		{
			ship["resources"]["stores"][0]["initial"] = 0;
			for (auto &system : ship["systems"])
				system["powerless_start"] = true;
		}
		if (experiment == 3)
			source["maps"][0]["entities"][1]["x_mm"] = 4000;
		source["trees"] = Json::parse(R"json([{"id":"test.follow","context":"controller","initial":"follow",
 "command_bindings":[{"channel":"follow","type":"boolean","action":"follow_contact"}],
 "states":[{"id":"follow","running":[{"action":"write.follow","arguments":[true,2,15000,8000]}]}]}])json");
		if (experiment == 1)
			for (int invalid = 0; invalid < 3; ++invalid)
			{
				auto bad = source;
				auto &arguments = bad["trees"][0]["states"][0]["running"][0]["arguments"];
				if (invalid < 2)
					arguments[1] = invalid == 0 ? 1 : 999;
				else
					arguments[2] = 0;
				World invalid_world;
				invalid_world.load(load(bad), "test.map");
				invalid_world.tick();
				require(body(invalid_world).vx == 0 && body(invalid_world).vy == 0,
				        "Self, missing or invalid separation follow target cannot create guidance");
			}

		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(103, 1, "sensors") && world.claim(201, 2, "helm"), "follow fixture claims");
		command(world, "speed_mm_s", experiment == 3 ? 0 : 3000, "drive", 201, 2);
		for (int i = 0; i < 40; ++i)
			world.tick();
		if (!feed)
		{
			require(body(world).vx == 0, "following must not use hidden contact position or velocity");
			continue;
		}
		if (experiment == 2 || experiment == 3)
		{
			require(body(world).vx == 0 && body(world).vy == 0,
			        "following must neither bypass unpaid propulsion nor drive into a nearby stopped leader");
			continue;
		}

		require(body(world).vx > 0, "following must move through ordinary powered propulsion");
		command(world, "heading_mdeg", 90000, "turn", 201, 2);
		for (int i = 0; i < 80; ++i)
			world.tick();
		require(body(world).heading != 0, "following must steer after observed leader turns");
		command(world, "speed_mm_s", 1000, "drive", 201, 2);
		for (int i = 0; i < 80; ++i)
			world.tick();
		require(body(world).vx != 0 || body(world).vy != 0, "following must continue after a leader speed change");
		command(world, "radar_enabled", 0, "radar", 103);
		world.tick();
		world.tick();
		const auto saved = world.save();
		World resumed;
		resumed.restore(catalog, saved);
		for (int i = 0; i < 20; ++i)
		{
			world.tick();
			resumed.tick();
		}
		require(world.save() == resumed.save(), "following with lost contact must continue identically after restore");
		command(world, "radar_enabled", 1, "radar", 103);
		world.tick();
		world.tick();
		const auto recovered = world.save();
		bool seen = false;
		for (const auto &b : recovered.at("bodies"))
			if (b.at("body").at("id") == 1)
				for (const auto &s : b.at("gameplay"))
					if (s.contains("flight") && s.at("flight").value("following", false))
					{
						seen = true;
						require(!s.at("flight").at("follow_stale").get<bool>(), "contact must be reacquired");
					}
		require(seen, "following continuation must be persisted");
	}
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
		const auto before_guidance = world.observe(101);
		if (feed)
			require(system(*before_guidance, "radar").maximum ==
			            source["entities"][0]["systems"]["radar"]["range_mm"].get<std::int64_t>(),
			        "Advanced radar projection preserves authored plotting range rather than generic system maximum");
		std::int64_t frozen_target_speed = 0;
		for (const auto &contact : before_guidance->contacts)
			if (contact.id == 2)
				frozen_target_speed = contact.vx;
		world.tick();
		const auto matched = telemetry(world, "drive", "matched_speed");
		require(matched == frozen_target_speed,
		        "guidance uses the Console's tick-start contact velocity, before target physics advances");
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
		require(telemetry(world, "drive", "match_stale") == 1,
		        "Sensor enable applies after tick-start evidence snapshot");
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
	require(telemetry(world, "b", "actual_longitudinal") == 0 && telemetry(world, "b", "used_power") == 0,
	        "passive correction must not quote or consume powered acceleration");
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
void nebula_sensor_memory_and_power()
{
	for (const int supply : {0, 500, 1000})
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["radar"]["energy_per_second"] = 40;
		ship["systems"]["radar"]["power_store"] = "sensor-cell";
		ship["systems"]["radar"]["powerless_start"] = true;
		ship["resources"]["stores"].push_back({{"id", "sensor-cell"},
		                                       {"capacity", 10000},
		                                       {"initial", supply ? 10000 : 0},
		                                       {"charge_rate", 40},
		                                       {"discharge_rate", supply == 500 ? 20 : 40}});
		source["maps"][0]["entities"][1]["x_mm"] = 45000;
		source["maps"][0]["regions"] = Json::array({{{"id", "cloud"},
		                                             {"shape", "circle"},
		                                             {"x_mm", 25000},
		                                             {"y_mm", 0},
		                                             {"radius_mm", 5000},
		                                             {"nebula", {{"density", .02}, {"sensor_factor", 1}}}}});
		for (const bool attenuation : {false, true})
		{
			source["maps"][0]["regions"][0]["nebula"]["sensor_factor"] = attenuation ? 1 : 0;
			World world;
			world.load(load(source), "test.map");
			claim(world);
			world.tick();
			const auto sensor = system(*world.observe(101), "radar");
			require(sensor.effective_range == 100000LL * supply / 1000,
			        "nebula sensor range bypassed paid power grant");
			const bool expected = supply == 1000 || (supply == 500 && !attenuation);
			require(!world.observe(101)->contacts.empty() == expected,
			        "nebula attenuation did not combine with partial sensor power");
		}
	}
	auto source = fixture();
	auto target = source["entities"][0];
	target["id"] = "test.target";
	target["systems"]["drive"]["acceleration_mm_s2"] = 200000;
	source["entities"][0]["systems"]["drive"]["acceleration_mm_s2"] = 1;
	source["entities"].push_back(target);
	auto &map = source["maps"][0];
	map["entities"][1]["definition"] = "test.target";
	map["entities"][1]["x_mm"] = 10000;
	map["regions"] = Json::array({{{"id", "cloud"},
	                               {"shape", "circle"},
	                               {"x_mm", 20000},
	                               {"y_mm", 0},
	                               {"radius_mm", 3000},
	                               {"nebula", {{"density", 100}, {"sensor_factor", 100}}}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	require(world.claim(201, 2, "helm"), "nebula target claim refused");
	command(world, "speed_mm_s", 10000, "drive", 201, 2);
	command(world, "selected_contact", 2, "drive");
	world.tick();
	command(world, "match_speed", 1, "drive");
	world.tick();
	require(telemetry(world, "drive", "match_stale") == 0, "visible target was not acquired before entering cloud");
	bool stale = false;
	for (int tick = 0; tick < 40; ++tick)
	{
		world.tick();
		if (telemetry(world, "drive", "match_stale"))
		{
			stale = true;
			break;
		}
	}
	require(stale && world.observe(101)->contacts.empty(), "cloud crossing failed to age lost contact evidence");
	const auto remembered = telemetry(world, "drive", "matched_speed");
	command(world, "speed_mm_s", -5000, "drive", 201, 2);
	world.tick();
	require(telemetry(world, "drive", "matched_speed") == remembered, "hidden target velocity leaked through nebula");
	World resumed;
	resumed.restore(catalog, world.save());
	resumed.clear_claims();
	claim(resumed);
	require(resumed.claim(201, 2, "helm"), "resumed nebula target claim refused");
	bool fresh = false;
	for (int tick = 0; tick < 70; ++tick)
	{
		world.tick();
		resumed.tick();
		require(telemetry(world, "drive", "matched_speed") == telemetry(resumed, "drive", "matched_speed") &&
		            telemetry(world, "drive", "match_stale") == telemetry(resumed, "drive", "match_stale"),
		        "nebula evidence continuation diverged");
		if (!telemetry(world, "drive", "match_stale"))
		{
			fresh = true;
			break;
		}
	}
	require(fresh && !world.observe(101)->contacts.empty() && telemetry(world, "drive", "matched_speed") != remembered,
	        "target returning from cloud failed reacquisition");
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
	for (int i = 0; i < 5; ++i)
	{
		auto source = fixture();
		if (i == 4)
		{
			auto &ship = source["entities"][0];
			ship["systems"]["generator"] = {{"kind", "generator"}, {"generator", "reactor"}, {"power_store", "cell"}};
			ship["resources"]["generators"] = Json::array({{{"id", "reactor"},
			                                                {"store", "cell"},
			                                                {"maximum_rate", 1000},
			                                                {"response_rate", 1000},
			                                                {"initial_output", 100}}});
			ship["stations"][0]["consoles"][3]["systems"].push_back("generator");
		}
		source = explicit_fixture(source);
		auto &ship = source["entities"][0];
		const std::string target = i == 4 ? "generator" : "drive";
		for (auto &owner : ship["control_layout"]["controllers"])
			if (owner.at("id") == target)
			{
				owner["tree"] = "test.invalid-action";
				owner.erase("fallback");
			}
		Json tree{{"id", "test.invalid-action"}, {"context", "controller"}, {"initial", "attempt"}};
		Json actions = Json::array();
		if (i == 0)
		{
			tree["command_bindings"] = Json::array({{{"channel", "assisted"}, {"type", "boolean"}}});
			actions.push_back({{"action", "write.assisted"}, {"arguments", {2}}});
		}
		if (i == 1 || i == 2)
		{
			tree["command_bindings"] =
			    Json::array({{{"channel", "destination"}, {"type", "boolean"}, {"action", "destination"}}});
			auto value = i == 1 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min();
			actions.push_back({{"action", "write.destination"}, {"arguments", {true, value, value}}});
		}
		if (i == 3)
		{
			tree["command_bindings"] = Json::array(
			    {{{"channel", "assisted"}, {"type", "boolean"}}, {{"channel", "match_speed"}, {"type", "boolean"}}});
			actions.push_back({{"action", "write.assisted"}, {"arguments", {false}}});
			actions.push_back({{"action", "write.match_speed"}, {"arguments", {true}}});
		}
		if (i == 4)
		{
			tree["command_bindings"] = Json::array({{{"channel", "generator_output"}}});
			actions.push_back(
			    {{"action", "write.generator_output"}, {"arguments", {std::numeric_limits<std::int64_t>::max()}}});
		}
		tree["states"] = Json::array({{{"id", "attempt"}, {"entry", actions}}});
		source["trees"] = Json::array({tree});
		if (i == 0)
		{
			phoenix::test::rejects(
			    [&]
			    {
				    (void)load(source);
			    },
			    "Invalid typed or bounded Controller writes must fail compilation");
			continue;
		}
		World world;
		world.load(load(source), "test.map");
		world.tick();
		claim(world);
		if (i == 4)
		{
			require(telemetry(world, "generator", "generator_output", 104) == 100,
			        "Out-of-range output cannot change physical generator state");
			continue;
		}
		require(telemetry(world, "drive", "assisted") == (i == 3 ? 0 : 1) &&
		            telemetry(world, "drive", "selected_contact") == 0 &&
		            telemetry(world, "drive", "match_speed") == 0 &&
		            system(*world.observe(101), "drive").requested == 0,
		        "Physically refused manual Match Speed must leave target and speed unchanged");
		auto unowned = source;
		unowned["trees"][0]["command_bindings"][0]["channel"] = "foreign";
		phoenix::test::rejects(
		    [&]
		    {
			    (void)load(unowned);
		    },
		    "Controller cannot acquire an undeclared command channel");
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
	require(saved.at("bodies").size() == 4 && saved.at("bodies")[2].at("systems").contains("engine") &&
	            saved.at("bodies")[2].contains("console_controllers") &&
	            saved.at("bodies")[3].at("systems").contains("starboard"),
	        "unified runtime equipment must save converted and explicit mechanisms through Controller continuation");
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "mixed equipment must retain complete tick-boundary state on restore");
	for (auto *world : {&ordered, &resumed})
	{
		require(world->release(303) && world->release(404), "mixed equipment claims must release independently");
		require(world->claim(303, 3) && world->claim(404, 4), "released mixed equipment must remain claimable");
		require(system(*world->observe(303), "engine").requested == 1500 &&
		            system(*world->observe(404), "port").requested == 3000,
		        "converted and explicit mechanisms retain accepted settings across the same Controller handover");
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
void independent_velocity_axes()
{
	auto source = fixture();
	source["maps"][0]["entities"].erase(1);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "speed_mm_s", 10000, "drive");
	command(world, "lateral_thrust", 1000, "side", 102);
	world.tick();
	require(body(world).vx == 200 && body(world).vy == 50,
	        "combined intent must preserve each engine's own acceleration without adding lateral main-engine thrust");
	command(world, "engine_enabled", 0, "drive");
	const auto previous = body(world);
	world.tick();
	require(body(world).vy > previous.vy && telemetry(world, "drive", "actual_longitudinal") == 0,
	        "a disabled main engine must not suppress a separately funded lateral engine");
}
void bounded_velocity_damping()
{
	for (const auto deceleration : {10000, 20, 1000000000})
	{
		const std::int64_t scale = deceleration == 1000000000 ? 100000000 : 1;
		auto source = fixture();
		source["maps"][0]["entities"].erase(1);
		auto &drive = source["entities"][0]["systems"]["drive"];
		drive["max_speed_mm_s"] = drive["reverse_speed_mm_s"] = drive["lateral_speed_mm_s"] = 10 * scale;
		drive["acceleration_mm_s2"] = drive["lateral_acceleration_mm_s2"] =
		    std::min<std::int64_t>(1000000000, 10000 * scale);
		drive["deceleration_mm_s2"] = deceleration;
		drive["power_rate"] = 20;
		source["entities"][0]["resources"]["stores"][0]["initial"] = 1;
		World world;
		world.load(load(source), "test.map");
		claim(world);
		command(world, "speed_mm_s", -10 * scale, "drive");
		command(world, "lateral_thrust", -800, "drive");
		world.tick();
		require(body(world).vx < 0 && body(world).vy < 0 && telemetry(world, "drive", "stored_energy") == 0,
		        "synthetic diagonal launch must spend its finite powered work before damping is checked");
		if (deceleration != 20)
		{
			const auto before = body(world);
			command(world, "speed_mm_s", before.vx, "drive");
			command(world, "lateral_thrust", -900, "drive");
			world.tick();
			const auto after = body(world);
			require(after.vx * after.vx + after.vy * after.vy <= before.vx * before.vx + before.vy * before.vy,
			        "clamped diagonal projection must not add kinetic speed when no powered work remains");
		}
		command(world, "speed_mm_s", 0, "drive");
		command(world, "lateral_thrust", 0, "drive");
		for (int i = 0; i < 40; ++i)
		{
			const auto before = body(world);
			world.tick();
			const auto after = body(world);
			const auto dx = after.vx - before.vx, dy = after.vy - before.vy;
			const std::int64_t limit = deceleration / World::ticks_per_second;
			require(dx * dx + dy * dy <= limit * limit,
			        "rounding must not exceed the authored per-tick damping budget");
		}
		require(body(world).vx == 0 && body(world).vy == 0,
		        "small diagonal drift must eventually stop rather than stall below one unit per axis");
	}
}
void passive_velocity_segments()
{
	auto source = fixture();
	source["maps"][0]["entities"].erase(1);
	source["entities"][0]["systems"]["drive"]["deceleration_mm_s2"] = 137;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "speed_mm_s", 6000, "drive");
	for (int i = 0; i < 30; ++i)
		world.tick();
	command(world, "speed_mm_s", 1000, "drive");
	command(world, "engine_enabled", 0, "drive");
	const auto overspeed = body(world);
	world.tick();
	require(body(world).vx < overspeed.vx && body(world).vx > 1000 && telemetry(world, "drive", "used_power") == 0,
	        "projection beyond the desired segment must damp excess speed with the engine disabled");
	const auto heat = telemetry(world, "drive", "heat");
	auto legacy = world.save();
	for (auto &entry : legacy["bodies"])
	{
		for (auto &operation : entry["systems"])
			operation.erase("damping_remainder");
		for (auto &equipment : entry["gameplay"])
			if (equipment.contains("flight"))
				equipment["flight"].erase("damping_remainder");
	}
	World legacy_resumed;
	legacy_resumed.restore(catalog, legacy);
	require(body(legacy_resumed).vx == body(world).vx,
	        "older saves without damping remainders must retain their velocity");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	require(resumed.save() == saved, "fractional passive damping must survive exact save recovery");
	resumed.clear_claims();
	claim(resumed);
	for (int i = 0; i < 25; ++i)
	{
		world.tick();
		resumed.tick();
		require(body(world).vx == body(resumed).vx, "fractional damping must continue identically after recovery");
	}
	command(world, "speed_mm_s", -1000, "drive");
	const auto opposite = body(world);
	world.tick();
	require(body(world).vx >= 0 && body(world).vx < opposite.vx && telemetry(world, "drive", "used_power") == 0,
	        "projection behind the desired segment must brake toward its zero endpoint without unpaid reverse "
	        "acceleration");
	command(world, "engine_enabled", 1, "drive");
	command(world, "speed_mm_s", 6000, "drive");
	command(world, "heading_mdeg", 90000, "turn");
	for (int i = 0; i < 25; ++i)
		world.tick();
	command(world, "engine_enabled", 0, "drive");
	const auto crossing = body(world);
	world.tick();
	require(crossing.vx > 0 && crossing.vy > 0 && crossing.vy < 6000 && body(world).vx < crossing.vx &&
	            body(world).vy == crossing.vy && telemetry(world, "drive", "used_power") == 0,
	        "interior projection must damp only unwanted transverse motion while retaining the requested segment");
	command(world, "speed_mm_s", 0, "drive");
	const auto energy = telemetry(world, "drive", "stored_energy");
	for (int i = 0; i < 1500; ++i)
		world.tick();
	require(body(world).vx == 0 && body(world).vy == 0 && telemetry(world, "drive", "stored_energy") == energy &&
	            telemetry(world, "drive", "heat") == heat,
	        "zero desired velocity must stop a disabled ship without energy use or overshoot");
	auto lateral_source = fixture();
	auto &lateral_ship = lateral_source["entities"][0];
	auto dry = lateral_ship["resources"]["stores"][0];
	dry["id"] = "lateral-cell";
	dry["initial"] = 0;
	lateral_ship["resources"]["stores"].push_back(dry);
	lateral_ship["systems"]["side"]["power_store"] = "lateral-cell";
	lateral_ship["systems"]["side"]["powerless_start"] = true;
	const auto lateral_catalog = load(lateral_source);
	World unfunded_lateral;
	unfunded_lateral.load(lateral_catalog, "test.map");
	claim(unfunded_lateral);
	command(unfunded_lateral, "assisted", 0, "drive");
	unfunded_lateral.tick();
	command(unfunded_lateral, "longitudinal_thrust", 1000, "drive");
	command(unfunded_lateral, "lateral_thrust", 1000, "side", 102);
	unfunded_lateral.tick();
	require(body(unfunded_lateral).vx > 0 && body(unfunded_lateral).vy == 0 &&
	            telemetry(unfunded_lateral, "side", "actual_lateral", 102) == 0,
	        "a combined velocity target must not obtain powered lateral movement from an unfunded lateral actuator");
	auto boosted_source = targeted_fixture();
	boosted_source["maps"][0]["entities"].erase(1);
	const auto boosted_catalog = load(boosted_source);
	World boosted;
	boosted.load(boosted_catalog, "test.map");
	claim(boosted);
	command(boosted, "assisted", 0, "drive");
	boosted.tick();
	command(boosted, "longitudinal_thrust", 1000, "drive");
	command(boosted, "boost", 1, "boost");
	for (int i = 0; i < 200; ++i)
		boosted.tick();
	const auto boosted_speed = body(boosted).vx;
	command(boosted, "boost", 0, "boost");
	boosted.tick();
	const auto nominal_speed = boosted_source["entities"][0]["systems"]["drive"]["max_speed_mm_s"].get<std::int64_t>();
	const auto damping = boosted_source["entities"][0]["systems"]["drive"]["acceleration_mm_s2"].get<std::int64_t>() /
	                     World::ticks_per_second;
	require(boosted_speed > nominal_speed && body(boosted).vx == boosted_speed - damping &&
	            body(boosted).vx > nominal_speed && telemetry(boosted, "drive", "actual_longitudinal") == 0,
	        "boost release must damp overspeed at the unmodified braking rate instead of clamping velocity or charging "
	        "for braking");
}
void steady_throttle_heading_changes()
{
	auto source = explicit_fixture(fixture());
	auto console = source["consoles"][0];
	console["id"] = "test.steady-helm";
	source["consoles"].push_back(console);
	source["entities"][0]["stations"][0]["consoles"][0]["definition"] = "test.steady-helm";
	// Both dials use independent Controller channels, as on an ordinary Helm.
	for (const auto &[kind, action, consumer, binding] :
	     std::vector<std::array<std::string, 4>>{{"throttle", "speed_mm_s", "drive", "engine.desired_speed"},
	                                             {"heading", "heading_mdeg", "turn", "steering.desired_heading"}})
		for (const auto &controller : source["entities"][0]["control_layout"]["controllers"])
			for (const auto &channel : controller["commands"])
				if (channel["action"] == action && channel["consumers"] == Json::array({consumer}))
					source["consoles"].back()["widgets"].push_back({{"id", kind},
					                                                {"kind", kind},
					                                                {"binding", binding},
					                                                {"label", kind},
					                                                {"x", 0},
					                                                {"y", 0},
					                                                {"width", 200},
					                                                {"height", 200},
					                                                {"controller", controller["id"]},
					                                                {"channel", channel["id"]}});
	// Keep the exercise clear of the other fixture actor.
	source["maps"][0]["entities"].erase(1);
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
	ConsoleModel model;
	model.refresh(session);
	require(model.set_speed_fraction(.6), "steady throttle request must be admitted");
	for (int i = 0; i < 100; ++i)
		session->tick();
	model.refresh(session);
	const auto speed = system(*model.observation(), "drive").telemetry.at("desired_speed");
	const auto speed_command = [&]() -> Json
	{
		for (const auto &controller : model.observation()->gameplay.at("controllers"))
			for (const auto &command : controller.at("commands"))
				if (command.at("action") == "speed_mm_s")
					return command;
		throw std::runtime_error("steady Helm is missing its speed command");
	};
	const auto original_speed_command = speed_command();
	for (const auto heading : {90., 180.})
	{
		const auto before = model.observation()->own;
		require(model.set_heading_degrees(heading), "heading dial request must be admitted independently");
		session->tick();
		model.refresh(session);
		const auto first = model.observation()->own;
		require(heading == 90. ? first.vx > 0 : first.vy > 0,
		        "a turn must retain some old-direction motion initially rather than snapping velocity to the heading");
		for (int i = 0; i < 30; ++i)
			session->tick();
		model.refresh(session);
		const auto after = model.observation()->own;
		require(after.heading == static_cast<std::int64_t>(heading * 1000) &&
		            system(*model.observation(), "drive").telemetry.at("desired_speed") == speed,
		        "a heading-only change must turn the hull while preserving the throttle setting");
		require(heading == 90. ? after.vy > before.vy : after.vx < before.vx,
		        "steady throttle must build velocity along the turned hull without another speed command");
		require(heading == 90. ? after.vx < before.vx && after.vx >= 0 : after.vy < before.vy && after.vy >= 0,
		        "turning must gradually damp transverse motion rather than rotating velocity with the hull");
		require(speed_command().at("revision") == original_speed_command.at("revision") &&
		            speed_command().at("value") == original_speed_command.at("value"),
		        "heading changes must not require or manufacture an additional throttle command");
	}
	auto stopped = make_local_session();
	stopped->add_pack(archive(source));
	stopped->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
	model.refresh(stopped);
	require(model.set_heading_degrees(90), "a stationary ship can turn with its initial zero throttle");
	for (int i = 0; i < 30; ++i)
		stopped->tick();
	model.refresh(stopped);
	require(model.observation()->own.heading == 90000 && model.observation()->own.vx == 0 &&
	            model.observation()->own.vy == 0 &&
	            system(*model.observation(), "drive").telemetry.at("actual_longitudinal") == 0 &&
	            system(*model.observation(), "drive").telemetry.at("desired_speed") == 0,
	        "a heading-only change at zero throttle must not create translational thrust or motion");
}
void manual_console_controls()
{
	auto source = explicit_fixture(fixture());
	auto console = source["consoles"][0];
	console["id"] = "test.manual-console";
	source["consoles"].push_back(console);
	source["entities"][0]["stations"][0]["consoles"][0]["definition"] = "test.manual-console";
	for (const auto &[kind, action, consumer, binding] :
	     std::vector<std::array<std::string, 4>>{{"throttle", "speed_mm_s", "drive", "engine.desired_speed"},
	                                             {"heading", "heading_mdeg", "turn", "steering.desired_heading"}})
		for (const auto &controller : source["entities"][0]["control_layout"]["controllers"])
			for (const auto &channel : controller["commands"])
				if (channel["action"] == action && channel["consumers"] == Json::array({consumer}))
					source["consoles"][1]["widgets"].push_back({{"id", kind},
					                                            {"kind", kind},
					                                            {"binding", binding},
					                                            {"label", kind},
					                                            {"x", 0},
					                                            {"y", 0},
					                                            {"width", 200},
					                                            {"height", 200},
					                                            {"controller", controller["id"]},
					                                            {"channel", channel["id"]}});
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
	ConsoleModel model;
	model.refresh(session);
	require(model.manual_available() && !model.manual_active(),
	        "bound Helm exposes combined manual mode with assistance as default");
	auto ambiguous_observation = *model.observation();
	const auto &safe_layout = session->catalog().consoles.at("test.manual-console");
	const auto manual = manual_console_binding(ambiguous_observation, safe_layout, "longitudinal_thrust", "speed_mm_s");
	require(!manual.empty(), "manual sibling resolves by physical consumers");
	const auto split = manual.find('/', 11);
	for (auto &owner : ambiguous_observation.gameplay["controllers"])
		if (owner.at("id") == manual.substr(11, split - 11))
			owner["commands"]["duplicate_thrust"] = owner["commands"].at(manual.substr(split + 1));
	require(manual_console_binding(ambiguous_observation, safe_layout, "longitudinal_thrust", "speed_mm_s").empty(),
	        "multiple sibling channels with the same physical consumers refuse manual binding");
	require(!model.set_manual_axes(500, 500), "axes are refused until both mode channels confirm manual operation");
	require(model.set_manual(true) && !model.manual_active(),
	        "requesting manual must not optimistically display confirmation");
	session->tick();
	model.refresh(session);
	require(model.manual_active() && model.set_manual_axes(375, 625),
	        "arbitrary proportional signed axes use owned Controller channels");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "drive").telemetry.at("longitudinal_thrust") == 625 &&
	            system(*model.observation(), "turn").telemetry.at("manual_steering") == 375,
	        "different engine and steering Controllers receive the same manual input sample");
	model.release_controls();
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "drive").telemetry.at("actual_longitudinal") == 0 &&
	            model.observation()->own.vx > 0,
	        "manual release removes powered actuation and starts passive braking");
	require(model.act(control_id(model, "destination", 1)), "authorised navigation can explicitly resume assistance");
	session->tick();
	model.refresh(session);
	require(!model.manual_active(),
	        "the mode indicator must follow physical assistance when another navigation command resumes it");
	require(model.set_manual(true), "manual can take over from active navigation");
	session->tick();
	model.refresh(session);
	require(model.manual_active() && system(*model.observation(), "drive").telemetry.at("navigating") == 0,
	        "manual takeover cancels existing navigation through the real physical path");
	require(model.set_manual_axes(-500, -700), "manual yaw and reverse thrust both accept negative effort");
	session->tick();
	model.refresh(session);
	require(model.set_manual(false), "returning to assistance uses normal admitted mode requests");
	session->tick();
	model.refresh(session);
	require(!model.manual_active() && system(*model.observation(), "drive").telemetry.at("longitudinal_thrust") == 0,
	        "assistance clears manual axes");
	require(model.set_manual(true), "manual can be selected again");
	session->tick();
	model.refresh(session);
	require(model.set_manual_axes(500, 500), "held axes can precede handover");
	require(model.act(control_id(model, "away", 1)), "handover uses the ordinary Console authority path");
	session->tick();
	model.refresh(session);
	require(!model.manual_available() && !model.set_manual_axes(500, 500),
	        "manual input cannot bypass delegated Controller ownership");
	model.release_controls();
}
void lateral_controls_and_delegation()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["stations"][0]["consoles"][0]["systems"].push_back("side");
	ship["stations"][0]["consoles"].erase(1);
	ship["systems"]["side"]["lateral_acceleration_mm_s2"] = 5000;
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
	ConsoleModel model;
	model.refresh(session);
	require(model.flight_button("lateral_thrust", -1000, true), "port button is a momentary admitted control");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "side").telemetry.at("actual_lateral") < 0,
	        "held port button fires lateral equipment");
	require(model.flight_button("lateral_thrust", 1000, true), "opposite button can be held concurrently");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "side").telemetry.at("actual_lateral") == 0,
	        "both lateral buttons mean neutral");
	require(model.flight_button("lateral_thrust", -1000, false), "one opposite button may be released independently");
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "side").telemetry.at("actual_lateral") > 0,
	        "remaining held button resumes its direction");
	model.release_controls();
	session->tick();
	model.refresh(session);
	require(system(*model.observation(), "side").telemetry.at("actual_lateral") == 0,
	        "input loss neutralises lateral controls");
	// Keep human forward/yaw commands while delegating only the lateral Controller.
	source = explicit_fixture(source);
	auto &layout = source["entities"][0]["control_layout"];
	std::string lateral_owner;
	for (auto &controller : layout["controllers"])
		for (const auto &channel : controller["commands"])
			if (channel["action"] == "lateral_thrust")
			{
				lateral_owner = controller.at("id").get<std::string>();
				controller["tree"] = "test.lateral-ai";
				controller.erase("fallback");
			}
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.lateral-ai","context":"controller","initial":"run","command_bindings":[{"channel":"lateral_thrust","type":"integer"}],"states":[{"id":"run","running":[{"action":"write.lateral_thrust","arguments":[{"observation":"navigation.lateral_thrust"}]}]}]})"));
	for (auto &profile : layout["profiles"])
	{
		auto &console = profile["consoles"]["helm"];
		if (console.contains("human"))
		{
			for (auto it = console["human"].begin(); it != console["human"].end();)
				if (*it == lateral_owner)
					it = console["human"].erase(it);
				else
					++it;
		}
		if (!console.contains("ai"))
			console["ai"] = Json::array();
		console["ai"].push_back(lateral_owner);
	}
	// A static, observable obstruction ahead leaves room for either side-step.
	auto obstacle = source["entities"][0];
	obstacle = {{"id", "test.obstacle"},
	            {"render", obstacle["render"]},
	            {"systems", Json::object()},
	            {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 0}}}};
	source["entities"].push_back(obstacle);
	source["maps"][0]["entities"][1]["definition"] = "test.obstacle";
	source["maps"][0]["entities"][1]["x_mm"] = 10000;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "helm"), "mixed-ownership Helm remains human claimable");
	world.tick();
	world.tick();
	bool used_lateral = telemetry(world, "side", "actual_lateral") != 0;
	for (int tick = 0; tick < 8; ++tick)
	{
		world.tick();
		used_lateral |= telemetry(world, "side", "actual_lateral") != 0;
	}
	require(used_lateral && telemetry(world, "drive", "longitudinal_thrust") == 0,
	        "AI-owned lateral Controller avoids observed obstructions on an otherwise human-owned Helm");
	auto blind_source = source;
	blind_source["entities"][0]["systems"]["radar"]["range_mm"] = 1;
	World blind;
	blind.load(load(blind_source), "test.map");
	require(blind.claim(101, 1, "helm"), "blind mixed-ownership Helm remains claimable");
	for (int tick = 0; tick < 4; ++tick)
		blind.tick();
	require(telemetry(blind, "side", "actual_lateral") != 0,
	        "ship lookout avoids close obstacles independently of radar visibility");
	for (auto &profile : source["entities"][0]["control_layout"]["profiles"])
	{
		auto &console = profile["consoles"]["helm"];
		for (auto it = console["ai"].begin(); it != console["ai"].end();)
			if (*it == lateral_owner)
				it = console["ai"].erase(it);
			else
				++it;
		if (!console.contains("human"))
			console["human"] = Json::array();
		console["human"].push_back(lateral_owner);
	}
	World human;
	human.load(load(source), "test.map");
	require(human.claim(101, 1, "helm"), "fully human-owned Helm remains claimable");
	for (int tick = 0; tick < 4; ++tick)
		human.tick();
	require(telemetry(human, "side", "actual_lateral") == 0,
	        "collision avoidance cannot override a human-owned lateral Controller");
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
	const auto binding = [&](const std::string &action)
	{
		std::string result;
		const auto observed = model.observation();
		for (const auto &owner : observed->gameplay.at("controllers"))
			for (const auto &[channel, field] : owner.at("commands").items())
				if (field.at("action") == action &&
				    std::find(field.at("consumers").begin(), field.at("consumers").end(), Json("drive")) !=
				        field.at("consumers").end())
				{
					require(result.empty(), "Synthetic semantic action has one widget channel");
					result = "controller/" + owner.at("id").get<std::string>() + "/" + channel;
				}
		require(!result.empty(), "Synthetic semantic widget channel exists");
		return result;
	};

	require(model.command("speed_mm_s", 1000, binding("speed_mm_s")) &&
	            model.command("heading_mdeg", 33000, binding("heading_mdeg")) &&
	            model.system_request(binding("speed_mm_s"), "speed_mm_s") == 1000 &&
	            model.system_request(binding("heading_mdeg"), "heading_mdeg") == 33000,
	        "composite flight System must retain separate pending feedback for speed and heading writes in one tick");
	session->tick();
	model.refresh(session);
	require(model.system_request(binding("speed_mm_s"), "speed_mm_s") == 1000 &&
	            model.system_request(binding("heading_mdeg"), "heading_mdeg") == 33000,
	        "committed composite-channel feedback must retain each setting in its original units");
	require(!control_id(model, "heading_mdeg", 50999).empty(),
	        "composite Engine heading increment must use desired heading rather than its independent speed request");
	require(model.system_minimum(binding("speed_mm_s")) == -5000 && model.set_speed_fraction(0, binding("speed_mm_s")),
	        "reverse throttle semantics must map their lower endpoint to the authored reverse bound");
	session->tick();
	model.refresh(session);
	require(model.system_request(binding("speed_mm_s")) == -5000,
	        "reverse slider command must commit through the live production session");
	require(model.command("assisted", 0, binding("assisted")),
	        "the semantic model must admit the assisted/manual control");
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
	require(model.act(control_id(model, "message.test.notice", 0)),
	        "explicit authored human report must use its current admitted descriptor");
	session->tick();
	model.refresh(session);
	require(model.announcement() == "Helm: Notice: Supply 7",
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
	require(model.act(control_id(model, "message.test.notice", 0)),
	        "a later explicit report remains independently sendable");
	session->tick();
	model.refresh(session);
	require(model.announcement() == "Helm: Notice: Supply 7",
	        "a newer delivery sequence must announce even when its translated text matches an older report");
	require(model.command("longitudinal_thrust", positive, binding("longitudinal_thrust")),
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
Json machinery_baseline()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["initial"] = 3000;
	ship["resources"]["stores"][0]["initial_heat"] = 80;
	ship["resources"]["generators"] = Json::array({{{"id", "reactor"},
	                                                {"store", "cell"},
	                                                {"maximum_rate", 200},
	                                                {"response_rate", 100},
	                                                {"initial_output", 100},
	                                                {"heat_ppm", 500000}}});
	ship["systems"]["generator"] = {{"kind", "generator"}, {"generator", "reactor"}, {"power_store", "cell"}};
	ship["resources"]["coolant_sources"] = Json::array({{{"id", "tank"},
	                                                     {"initial", 1000},
	                                                     {"capacity", 1000},
	                                                     {"removal_rate", 40},
	                                                     {"coolant_per_heat_ppm", 1000000}}});
	ship["resources"]["cooling_links"] = Json::array(
	    {{{"id", "feed"}, {"source", "tank"}, {"store", "cell"}, {"capacity_rate", 40}, {"allocation_rate", 40}}});
	ship["systems"]["cooler"] = {{"kind", "coolant"}, {"cooling_link", "feed"}};
	ship["stations"][0]["consoles"][3]["systems"].push_back("generator");
	ship["stations"][0]["consoles"][3]["systems"].push_back("cooler");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	Json trace = Json::array();
	const auto record = [&]
	{
		trace.push_back(phoenix::test::machinery_physical_checkpoint(world.save()));
	};
	record();
	command(world, "generator_output", 200, "generator", 104);
	command(world, "speed_mm_s", 10000, "drive");
	for (int tick = 0; tick < 8; ++tick)
	{
		world.tick();
		if (tick == 0 || tick == 2 || tick == 7)
			record();
	}
	command(world, "assisted", 0, "drive");
	command(world, "assisted", 0, "turn");
	command(world, "boost", 1, "boost");
	for (int tick = 0; tick < 4; ++tick)
	{
		world.tick();
		record();
	}
	command(world, "boost", 0, "boost");
	command(world, "impulse", 1, "impulse");
	world.tick();
	record();
	World resumed;
	resumed.restore(catalog, world.save());
	for (int tick = 0; tick < 4; ++tick)
	{
		world.tick();
		resumed.tick();
		record();
	}
	require(world.save() == resumed.save(), "Baseline impulse charge and native machinery continue exactly");
	command(world, "impulse", 0, "impulse");
	command(world, "system_enabled", 0, "cooler", 104);
	for (int tick = 0; tick < 3; ++tick)
	{
		world.tick();
		record();
	}
	return trace;
}

} // namespace

#include "../support/coordinated_impulse_cases.hpp"

int main(int argc, char **argv)
{
	try
	{
		if (argc > 1 && std::string(argv[1]) == "--baseline-json")
		{
			std::cout << machinery_baseline().dump() << '\n';
			return 0;
		}
		coordinated_impulse_cases();
		launch_curve_and_linear_braking();
		manual_and_conservation();
		boost_and_impulse();
		zero_propulsion_modifier();
		bounded_propulsion_modifier();
		speed_modifier_funding();
		targeted_modifiers();
		impulse_interruptions();
		paid_overdrive();
		prepaid_low_rate_actuation();
		unpaid_and_protected();
		follow_contact();
		match_speed();
		immutable_flight_demands();
		paid_sensor_startup();
		nebula_sensor_memory_and_power();
		disabled_cooling();
		invalid_controller_actions();
		equipment_ordering_and_recovery();
		independent_velocity_axes();
		bounded_velocity_damping();
		passive_velocity_segments();
		steady_throttle_heading_changes();
		manual_console_controls();
		semantic_controls();
		lateral_controls_and_delegation();
		std::cout << "Paid flight integration passed: control, resources, impulse, observed matching and semantics\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
