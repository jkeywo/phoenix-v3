#include "../support/assertions.hpp"
#include "../support/directed_fixture.hpp"
#include "../support/stock_snapshot.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <type_traits>
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.sensing;
import phoenix.simulation;
import phoenix.networking.projection;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &world, int count)
{
	while (count-- > 0)
		world.tick();
}
Json body(const World &world, EntityId id = 1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("bodies"))
		if (row.at("body").at("id") == id)
			return row;
	throw std::runtime_error("Missing directed fixture body");
}
Json directed(const World &world, const std::string &system = "gun", EntityId ship = 1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("directed"))
		if (row.at("ship") == ship && row.at("system") == system)
			return row;
	throw std::runtime_error("Missing directed fixture capability");
}
Json weapon(const World &world, const std::string &system = "gun", EntityId ship = 1)
{
	return body(world, ship).at("combat").at("systems").at(system);
}
void command(World &world, const std::string &channel, std::int64_t value, const std::string &system = "gun",
             EntityId ship = 1, std::uint64_t claimant = 101)
{
	const auto observation = world.observe(claimant);
	for (const auto &controller : observation->gameplay.at("controllers"))
		if (controller.at("id") == system)
		{
			require(world.submit(claimant,
			                     OperatingCommand{ship, {}, value, {}, system, channel, controller.at("generation")}),
			        "Directed Controller command admitted");
			return;
		}
	throw std::runtime_error("Missing directed fixture Controller");
}
std::int64_t hp(const World &world, EntityId ship = 2)
{
	std::int64_t total = 0;
	const auto saved = body(world, ship);
	for (const auto &compartment : saved.at("combat").at("compartments"))
		total += compartment.at("hp").get<std::int64_t>();
	return total;
}
Json sensor(const World &world, const std::string &system = "gun")
{
	const auto saved = world.save();
	for (const auto &row : saved.at("sensors"))
		if (row.at("ship") == 1 && row.at("system") == system)
			return row;
	throw std::runtime_error("Missing multimode sensor acquisition state");
}
#include "../support/directed_blast_cases.hpp"
#include "../support/effect_transaction_cases.hpp"
void generated_directed_controls()
{
	auto source = directed_fixture();
	auto &ship = source["entities"][0];
	ship.erase("control_layout");
	auto &consoles = ship["stations"][0]["consoles"];
	consoles[0]["systems"] = Json::array();
	consoles[1]["systems"] = Json::array();
	for (const auto &[id, spec] : ship.at("systems").items())
		consoles[spec.at("kind") == "repair_team" ? 1 : 0]["systems"].push_back(id);
	const auto fire = ship["systems"]["gun"]["directed"]["modes"][1];
	ship["systems"]["gun"]["directed"]["initial_mode"] = fire;
	ship["systems"]["gun"]["directed"]["initial_targeting"] = "fixture.targeting.off";
	const auto converted = load(source);
	const auto layout = compile_controllable_layout(converted, "test.combat");
	const auto controller = std::find_if(layout.controllers.begin(), layout.controllers.end(),
	                                     [](const auto &row)
	                                     {
		                                     return row.id == "gun";
	                                     });
	require(controller != layout.controllers.end(), "Converted non-PDC weapon has a Controller");
	for (const auto &channel : controller->commands)
		require(channel.action != OperatingAction::WeaponMode && channel.action != OperatingAction::WeaponTarget,
		        "Generated directed controls prefer canonical actions over legacy aliases");
	for (const auto action : {OperatingAction::DirectedOperatingMode, OperatingAction::DirectedTargetingMode,
	                          OperatingAction::DirectedTarget})
		require(std::count_if(controller->commands.begin(), controller->commands.end(),
		                      [&](const auto &channel)
		                      {
			                      return channel.action == action;
		                      }) == 1,
		        "Each generated directed physical input has one canonical command owner");
	World world;
	world.load(converted, "test.map");
	require(world.claim(101, 1, "drive"), "Claim converted directed controls");
	ticks(world, 1);
	require(directed(world).at("operating_mode") == 1 && directed(world).at("targeting_mode") == 1,
	        "Generated controls preserve authored fire and off selections");
	command(world, "directed_target", 2);
	ticks(world, 2);
	require(directed(world).at("target") == 2 && directed(world).at("targeting_mode") == 1 &&
	            weapon(world).at("shots") == 0,
	        "Target selection does not implicitly enable a generated non-PDC capability");
	command(world, "directed_targeting_mode", 0);
	ticks(world, 30);
	require(directed(world).at("targeting_mode") == 0 && weapon(world).at("shots").get<std::int64_t>() > 0,
	        "Generated designated policy command enables non-PDC launch through ordinary admission");
	command(world, "directed_targeting_mode", 1);
	ticks(world, 1);
	const auto shots = weapon(world).at("shots");
	ticks(world, 30);
	require(directed(world).at("targeting_mode") == 1 && weapon(world).at("shots") == shots,
	        "Generated off policy command stops further non-PDC launches");
}
void validate_catalogue()
{
	auto source = directed_fixture();
	const auto catalog = load(source);
	const auto all = compile_directed(catalog, "test.combat");
	require(all.at("gun").modes.size() == 2 && all.at("gun").targeting.size() == 2,
	        "A single production-compiled capability has independent operation and targeting selections");
	auto edited = catalog;
	edited.effects.at("test.directed.scan")["range_mm"] = 123456;
	edited.effects.at("test.directed.scan")["capabilities"]["compartment_condition"] = 7;
	const auto compiled = compile_directed_catalog(edited);
	require(compiled.at("test.combat").at("gun").modes.front().range == 123456 &&
	            compile_sensors(edited, "test.combat").at("gun").capabilities.at("compartment_condition") == 7,
	        "A fresh catalogue and standalone native compiler reflect edited directed templates");
	edited.entities["test.unrelated-invalid"] = edited.entities.at("test.combat");
	edited.entities.at("test.unrelated-invalid")["systems"]["gun"]["directed"]["initial_mode"] = "missing.mode";
	require(compile_combat(edited, "test.combat").has_value(),
	        "Standalone native compilation validates only the requested entity");
	rejects(
	    [&]
	    {
		    compile_directed_catalog(edited);
	    },
	    "A whole-catalogue preparation still rejects an unrelated malformed entity");
	auto invalid = source;
	invalid["entities"][0]["systems"]["second"]["turret"] = "mount";
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Two independent capabilities cannot silently arbitrate a shared turret");
	invalid = source;
	directed_effect(invalid, "test.directed.scan")["targeting"] = {"missing.policy"};
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Unknown targeting policy rejected during catalogue compilation");
	invalid = source;
	invalid["entities"][0]["systems"]["gun"]["directed"]["initial_mode"] = "missing.mode";
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Initial operation must belong to the capability");
	directed_burning(invalid = source);
	directed_effect(invalid, "test.burn")["period_ticks"] = 0;
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Periodic reactions require a positive bounded cadence");
	invalid = source;
	directed_burning(invalid);
	directed_effect(invalid, "test.burn.payload")["bindings"][0]["outcome"] = "selected_contact";
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Payload cannot invent a selected-target outcome instead of an actual hit");
	invalid = source;
	invalid["effects"].push_back({{"id", "test.invalid.beam-intercept"},
	                              {"kind", "directed_mode"},
	                              {"operation", "beam"},
	                              {"energy_per_second", 20},
	                              {"targeting", {"fixture.targeting.intercept"}}});
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "Native automatic missile interception requires a launch operation");
}
Json switching_and_continuation()
{
	const auto content = load(directed_fixture());
	World world;
	try
	{
		world.load(content, "test.map");
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(std::string("world load: ") + error.what());
	}
	require(world.claim(101, 1, "drive"), "Claim multimode fitting");
	const auto mount_target = [](const World &value)
	{
		const auto observation = value.observe(101);
		for (const auto &system : observation->systems)
			if (system.id == "mount")
			{
				require(system.requested == system.telemetry.at("target"),
				        "Mount requested and target telemetry share the unique owner's intent");
				return system.requested;
			}
		throw std::runtime_error("Missing mount observation");
	};
	try
	{
		ticks(world, 1);
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(std::string("initial tick: ") + error.what());
	}
	try
	{
		command(world, "target", 2);
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(std::string("target command: ") + error.what());
	}
	try
	{
		ticks(world, 4);
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(std::string("scan ticks: ") + error.what());
	}
	require(weapon(world).at("shots") == 0 && directed(world).at("operating_mode") == 0,
	        "Scan selection cannot launch the weapon mechanism");
	const auto unfinished = sensor(world);
	command(world, "operation", 1);
	ticks(world, 1);
	require(weapon(world).at("shots").get<std::int64_t>() == 1,
	        "The same fitting launches through its selected fire operation");
	require(mount_target(world) == 2, "The same mount follows its owner when scan switches to launch");
	require(sensor(world).at("acquisitions") == unfinished.at("acquisitions"),
	        "Focused acquisition cannot advance while the fire operation owns this capability");
	const auto cooldown = weapon(world).at("cooldown").get<std::int64_t>();
	const auto ammunition =
	    saved_ammunition(body(world), "mag") + weapon(world).at("loaded_quantity").get<std::int64_t>();
	command(world, "operation", 0);
	ticks(world, 1);
	command(world, "operation", 1);
	ticks(world, 1);
	require(weapon(world).at("shots") == 1 && weapon(world).at("cooldown").get<std::int64_t>() > 0 && cooldown > 0,
	        "Immediate scan/fire switches cannot evade an existing firing cooldown");
	require(saved_ammunition(body(world), "mag") + weapon(world).at("loaded_quantity").get<std::int64_t>() ==
	            ammunition,
	        "Mode changes do not charge another round while still cooling down");
	command(world, "policy", 1);
	ticks(world, 1);
	require(directed(world).at("operating_mode") == 1 && directed(world).at("targeting_mode") == 1 &&
	            directed(world).at("target") == 2,
	        "Targeting policy changes independently of operating mode and retained target intent");
	require(mount_target(world) == 0, "Off preserves owner intent but leaves the physical mount inactive");
	const auto shots = weapon(world).at("shots");
	const auto paused_acquisition = sensor(world).at("acquisitions");
	ticks(world, 25);
	require(weapon(world).at("shots") == shots, "Off targeting policy suppresses launch without resetting its mode");
	require(sensor(world).at("acquisitions") == paused_acquisition,
	        "A retained scan target cannot keep accumulating exposure while another operation is selected");
	command(world, "policy", 0);
	command(world, "operation", 0);
	ticks(world, 1);
	const auto resumed_scan = sensor(world);
	require(!unfinished.at("tracks").empty() && !resumed_scan.at("tracks").empty(),
	        "Native equipment-local acquisition survives incompatible fire work");
	World resumed;
	resumed.restore(content, world.save());
	require(mount_target(world) == 2 && mount_target(resumed) == 2 &&
	            !body(world).at("combat").at("systems").at("mount").contains("target"),
	        "Restored mount derives its scanner target without a second persisted intent");
	for (int n = 0; n < 15; ++n)
	{
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "Multimode acquisition, readiness and intent continue exactly");
	}
	const auto good = world.save();
	require(!weapon(world).contains("target") && !weapon(world).contains("mode") && !sensor(world).contains("target"),
	        "Directed intent is persisted once, independently of native mechanism state");
	const auto observation = world.observe(101)->gameplay;
	for (const auto &scanner : observation.at("sensors").at("scanners"))
		if (scanner.at("id") == "gun")
			require(scanner.at("target") == directed(world).at("target"),
			        "Native scanner telemetry derives its selected target from the shared capability");
	auto wire = network::observation_json(*world.observe(101));
	auto &board = wire["gameplay"]["directed"];
	require(!board.empty(), "Directed capabilities have standard human and network projections");
	board[0]["hidden_faults"] = "PRIVATE";
	board[0]["operating_modes"][0]["hidden_target_range"] = 123456;
	const auto clean = network::read_observation(wire, content).gameplay.at("directed");
	require(!clean[0].contains("hidden_faults") && !clean[0]["operating_modes"][0].contains("hidden_target_range"),
	        "Directed projection allowlist strips hidden facts at both capability and choice boundaries");
	board[0]["operating_mode"] = 999;
	rejects(
	    [&]
	    {
		    network::read_observation(wire, content);
	    },
	    "Invalid directed projection selection rejected");
	for (int variant = 0; variant < 6; ++variant)
	{
		auto bad = good;
		if (variant == 0)
			bad["directed"][0]["operating_mode"] = 999;
		else if (variant == 1)
			bad["directed"].push_back(bad["directed"][0]);
		else if (variant == 2)
			bad["directed"].erase(bad["directed"].begin());
		else if (variant == 3)
			bad["directed"][0]["target"] = bad["directed"][0]["ship"];
		else if (variant == 4)
			bad["sensors"][0]["target"] = 2;
		else
			bad["bodies"][0]["combat"]["systems"]["gun"]["target"] = 2;
		rejects(
		    [&]
		    {
			    resumed.restore(content, bad);
		    },
		    "Malformed directed continuation rejected");
		require(resumed.save() == good, "Rejected directed restore leaves the running world unchanged");
	}
	return good;
}
Json burning_and_continuation()
{
	auto source = directed_fixture();
	directed_burning(source);
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim burning ammunition test gun");
	ticks(world, 1);
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	for (int i = 0; i < 12 && world.save().at("active_effects").at("conditions").empty(); ++i)
		world.tick();
	const auto applied = world.save().at("active_effects").at("conditions");
	require(!applied.empty(), "Actual penetrating hit applies a periodic condition to its struck compartment");
	for (const auto &effect : applied)
		require(effect.at("context").at("target") == 2 && effect.at("definition") == "test.burn",
		        "Burning uses actual impact context rather than the selected source or another contact");
	command(world, "enabled", 0);
	world.tick();
	const auto before = hp(world);
	World resumed;
	resumed.restore(content, world.save());
	const auto good = resumed.save();
	for (int variant = 0; variant < 4; ++variant)
	{
		auto bad = good;
		auto &active = bad["active_effects"]["conditions"];
		if (variant == 0)
			active.push_back(active[0]);
		else if (variant == 1)
			active[0]["clocks"].begin().value()["next"] = 9007199254740991LL;
		else if (variant == 2)
			active[0]["context"]["compartment"] = 999;
		else
			active[0]["definition"] = "unknown.periodic";
		rejects(
		    [&]
		    {
			    resumed.restore(content, bad);
		    },
		    "Invalid periodic identity, deadline or hit target rejected");
		require(resumed.save() == good, "Rejected periodic restore is atomic");
	}
	for (int i = 0; i < 20; ++i)
	{
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(),
		        "Periodic deadlines and payload receipts resume without replay or skipped damage");
	}
	require(hp(world) < before, "Inflicted periodic damage continues without operating or funding its gun");
	require(world.save().at("active_effects").at("conditions").empty(), "Expiry retires the periodic effect");
	const auto expired_hp = hp(world);
	ticks(world, 8);
	require(hp(world) == expired_hp, "An expired effect cannot perform another damage tick");
	return world.save();
}
Json distinct_scan_modes()
{
	auto source = directed_fixture();
	source["effects"].push_back({{"id", "test.directed.shield-scan"},
	                             {"kind", "directed_mode"},
	                             {"operation", "scan"},
	                             {"range_mm", 200000},
	                             {"targeting", {"fixture.targeting.designated", "fixture.targeting.off"}},
	                             {"capabilities", {{"shield_sectors", 0}, {"compartment_condition", 12}}}});
	source["entities"][0]["systems"]["gun"]["directed"]["modes"].push_back("test.directed.shield-scan");
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim multi-scan fitting");
	ticks(world, 1);
	command(world, "target", 2);
	ticks(world, 4);
	const auto first = sensor(world).at("acquisitions");
	const auto find = [](const Json &rows, const std::string &mode) -> Json
	{
		for (const auto &row : rows)
			if (row.at("mode") == mode && row.at("target") == 2)
				return row.at("samples");
		return Json::object();
	};
	require(find(first, "test.directed.shield-scan").empty(),
	        "An unselected scan mode cannot provide its instant capabilities or timed exposure");
	command(world, "operation", 2);
	ticks(world, 1);
	const auto second = sensor(world).at("acquisitions");
	const auto old_samples = find(first, "test.directed.scan");
	const auto new_samples = find(second, "test.directed.shield-scan");
	require(find(second, "test.directed.scan") == old_samples &&
	            new_samples.at("compartment_condition").at("exposure") == 1,
	        "Different scan definitions retain separate progress even for the same named capability");
	require(!new_samples.at("shield_sectors").at("data").is_null(),
	        "A zero-duration capability on a noninitial mode is acquired when that mode is selected");
	command(world, "policy", 1);
	ticks(world, 3);
	require(sensor(world).at("acquisitions") == second,
	        "Turning focused targeting Off stops both instant and timed scan work");
	require(!world.observe(101)->contacts.empty(),
	        "Independent broad radar detection remains available during focused Off");
	World resumed;
	resumed.restore(content, world.save());
	command(world, "policy", 0);
	command(resumed, "policy", 0);
	for (int n = 0; n < 4; ++n)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save(), "Distinct mode acquisition resumes exactly after restored Off state");
	return world.save();
}
Json funded_beam_and_loaded_round()
{
	auto source = directed_fixture();
	directed_burning(source, 3, 40);
	auto &ship = source["entities"][0];
	const auto fire = ship["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["load_ticks"] = 6;
	ship["systems"]["gun"]["power_store"] = "cell";
	source["resources"].push_back(
	    {{"id", "test.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"resource", "test.energy"},
	                                              {"capacity", 1000},
	                                              {"initial", 1000},
	                                              {"charge_rate", 20},
	                                              {"discharge_rate", 20}}})}};
	source["effects"].push_back({{"id", "test.directed.beam"},
	                             {"kind", "directed_mode"},
	                             {"operation", "beam"},
	                             {"range_mm", 200000},
	                             {"damage_per_second", 7},
	                             {"energy_per_second", 40},
	                             {"resource_payloads", true},
	                             {"targeting", {"fixture.targeting.designated", "fixture.targeting.off"}}});
	ship["systems"]["gun"]["directed"]["modes"].push_back("test.directed.beam");
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim funded beam and ammunition fitting");
	ticks(world, 1);
	command(world, "operation", 1);
	ticks(world, 6);
	require(weapon(world).at("shots") == 0 && weapon(world).at("loaded_quantity") == 1,
	        "Native loading commits a real round before launch readiness");
	const auto committed =
	    saved_ammunition(body(world), "mag") + weapon(world).at("loaded_quantity").get<std::int64_t>();
	command(world, "target", 2);
	command(world, "operation", 2);
	ticks(world, 7);
	require(world.save().at("active_effects").at("conditions").empty(),
	        "A beam does not inherit Burning from a retained loaded ammunition resource");
	World resumed;
	resumed.restore(content, world.save());
	for (int n = 0; n < 13; ++n)
	{
		world.tick();
		resumed.tick();
	}
	const auto damage = 20000 - hp(world);
	require(damage > 0 && damage < 7 && world.save() == resumed.save(),
	        "Partly funded beam scales its work and preserves fractional damage through restoration");
	require(saved_ammunition(body(world), "mag") + weapon(world).at("loaded_quantity").get<std::int64_t>() == committed,
	        "Sustained beam work preserves the preexisting loaded-round commitment without consuming ammunition");
	require(world.save().at("active_effects").at("conditions").empty(),
	        "Repeated beam contacts never invoke an unselected ammunition payload");
	const auto beam_steps = weapon(world).at("shots").get<std::int64_t>();
	command(world, "operation", 1);
	ticks(world, 12);
	require(weapon(world).at("shots") == beam_steps + 1 && !world.save().at("active_effects").at("conditions").empty(),
	        "Returning to launch completes normal loading and invokes the retained round's payload once");
	return world.save();
}
Json broad_radar_with_focused_off()
{
	auto source = directed_fixture();
	auto &ship = source["entities"][0];
	auto &radar = ship["systems"]["radar"];
	radar["power_store"] = "cell";
	radar["energy_per_second"] = 20;
	const auto scan = radar["directed"]["modes"][0].get<std::string>();
	directed_effect(source, scan)["targeting"].push_back("fixture.targeting.off");
	radar["directed"]["targeting"].push_back("fixture.targeting.off");
	source["resources"].push_back(
	    {{"id", "test.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"resource", "test.energy"},
	                                              {"capacity", 1000},
	                                              {"initial", 1000},
	                                              {"charge_rate", 100},
	                                              {"discharge_rate", 100}}})}};
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller.at("id") == "radar")
			controller["commands"].push_back(
			    {{"id", "policy"}, {"action", "directed_targeting_mode"}, {"consumers", {"radar"}}});
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim native broad radar with directed focus");
	ticks(world, 1);
	const auto energy = [&]
	{
		return body(world)
		    .at("resources")
		    .at("network")
		    .at("stores")[0]
		    .at("stock")
		    .at("quantities")
		    .at("test.energy")
		    .get<std::int64_t>();
	};
	const auto before = energy();
	command(world, "policy", 1, "radar");
	ticks(world, 20);
	require(directed(world, "radar").at("targeting_mode") == 1 && !world.observe(101)->contacts.empty(),
	        "Focused targeting Off leaves the same equipment's native broad detection available");
	require(energy() == before - 20,
	        "Broad radar continues paying its physical detection demand while focused operation is Off");
	return world.save();
}
Json indivisible_launch_funding()
{
	Json trace = Json::array();
	for (bool enough : {false, true})
	{
		auto source = directed_fixture();
		auto &ship = source["entities"][0];
		const auto fire = ship["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
		directed_effect(source, fire)["energy_per_second"] = 40;
		directed_effect(source, fire)["discrete_energy"] = 1;
		ship["systems"]["gun"]["power_store"] = "cell";
		source["resources"].push_back(
		    {{"id", "test.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
		ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
		                                              {"resource", "test.energy"},
		                                              {"capacity", 100},
		                                              {"initial", 100},
		                                              {"charge_rate", 100},
		                                              {"discharge_rate", enough ? 100 : 20}}})}};
		const auto content = load(source);
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive"), "Claim indivisible funded launch");
		ticks(world, 1);
		command(world, "target", 2);
		command(world, "operation", 1);
		ticks(world, 5);
		const auto energy = body(world)
		                        .at("resources")
		                        .at("network")
		                        .at("stores")[0]
		                        .at("stock")
		                        .at("quantities")
		                        .at("test.energy")
		                        .get<std::int64_t>();
		if (enough)
			require(weapon(world).at("shots") == 1 && energy < 100,
			        "An affordable launch commits its complete energy cost and one physical shot");
		else
			require(weapon(world).at("shots") == 0 && energy == 100 &&
			            saved_ammunition(body(world), "mag") +
			                    weapon(world).at("loaded_quantity").get<std::int64_t>() ==
			                100,
			        "An affordable discrete charge cannot be spent when the same launch lacks full operating funding");
		World resumed;
		resumed.restore(content, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(), "Prepared launch reservations and failed funding remain deterministic");
		trace.push_back(world.save());
	}
	return trace;
}
Json launch_processor_competition()
{
	Json trace = Json::array();
	for (bool funded : {false, true})
	{
		auto source = directed_fixture();
		auto &ship = source["entities"][0];
		const auto ammunition = ship["systems"]["mag"]["stock"]["selection"].get<std::string>();
		ship["systems"]["mag"]["stock"]["initial"] = {{ammunition, 1}};
		ship["systems"]["gun"]["power_store"] = "cell";
		const auto fire = ship["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
		directed_effect(source, fire)["energy_per_second"] = 40;
		directed_effect(source, fire)["discrete_energy"] = 1;
		source["resources"].push_back(
		    {{"id", "test.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
		ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
		                                              {"resource", "test.energy"},
		                                              {"capacity", 1000},
		                                              {"initial", 100},
		                                              {"charge_rate", 100},
		                                              {"discharge_rate", funded ? 100 : 20}}})}};
		source["recipes"] = Json::array({{{"id", "test.recycle-round"},
		                                  {"label", "Recycle round"},
		                                  {"work", 1},
		                                  {"inputs", {{ammunition, 1}}},
		                                  {"outputs", {{"test.energy", 5}}}}});
		ship["processors"] = Json::array({{{"id", "recycle"},
		                                   {"equipment", "second"},
		                                   {"recipe", "test.recycle-round"},
		                                   {"mode", "batch"},
		                                   {"work_per_second", 20},
		                                   {"inputs", {{ammunition, "system:mag"}}},
		                                   {"outputs", {{"test.energy", "energy:cell"}}}}});
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller.at("id") == "second")
				controller["commands"][0]["initial"] = 0;
		const auto content = load(source);
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive"), "Claim competing launch and processor");
		ticks(world, 1);
		command(world, "target", 2);
		command(world, "operation", 1);
		command(world, "enabled", 1, "second");
		world.tick();
		const auto energy = body(world)
		                        .at("resources")
		                        .at("network")
		                        .at("stores")[0]
		                        .at("stock")
		                        .at("quantities")
		                        .at("test.energy")
		                        .get<std::int64_t>();
		require(saved_ammunition(body(world), "mag") == 0,
		        "Only one consumer can take the last actual round from a shared magazine");
		if (funded)
			require(weapon(world).at("shots") == 1 && energy == 97,
			        "A prepared affordable launch protects its ammunition before a competing processor can consume it");
		else
			require(weapon(world).at("shots") == 0 && energy == 105,
			        "An unfunded launch releases its transient claim so the competing processor makes progress without "
			        "a lost charge");
		World resumed;
		resumed.restore(content, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(),
		        "Shared-stock arbitration leaves no transient launch claim in continuation");
		trace.push_back(world.save());
	}
	return trace;
}
Json changing_loaded_operation()
{
	auto source = directed_fixture();
	auto &gun = source["entities"][0]["systems"]["gun"];
	const auto fire = gun["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["load_ticks"] = 6;
	auto heavy = directed_effect(source, fire);
	heavy["id"] = "test.directed.heavy";
	heavy["ammo_use"] = 3;
	heavy["load_ticks"] = 4;
	heavy["fire_ticks"] = 1000;
	source["effects"].push_back(heavy);
	gun["directed"]["modes"].push_back("test.directed.heavy");
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim loading operation switch");
	ticks(world, 1);
	command(world, "operation", 1);
	ticks(world, 3);
	require(weapon(world).at("loading") == 3 && weapon(world).at("loaded_quantity") == 0,
	        "The original loading operation has meaningful unfinished work");
	command(world, "operation", 2);
	command(world, "target", 2);
	ticks(world, 1);
	require(weapon(world).at("loading") == 4 && weapon(world).at("loaded_quantity") == 0 &&
	            weapon(world).at("shots") == 0,
	        "Switching to a shorter load cannot reinterpret or instantly finish the original operation");
	ticks(world, 2);
	require(weapon(world).at("loaded_quantity") == 1 && weapon(world).at("unloading").get<std::int64_t>() > 0,
	        "The original quantity finishes and follows normal return commitments before incompatible ammunition use");
	World resumed;
	resumed.restore(content, world.save());
	for (int n = 0; n < 20; ++n)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save() && weapon(world).at("shots") == 1 &&
	            saved_ammunition(body(world), "mag") + weapon(world).at("loaded_quantity").get<std::int64_t>() == 97,
	        "Return, replacement loading and one three-round launch conserve stock through continuation");
	return world.save();
}
Json frozen_permitted_contact()
{
	auto source = directed_fixture();
	auto &ship = source["entities"][0];
	const auto fire = ship["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["discrete_energy"] = 2;
	ship["systems"]["gun"]["power_store"] = "cell";
	ship["systems"]["radar"]["power_store"] = "radar_cell";
	ship["systems"]["radar"]["energy_per_second"] = 20;
	source["resources"].push_back(
	    {{"id", "test.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"resource", "test.energy"},
	                                              {"capacity", 100},
	                                              {"initial", 100},
	                                              {"charge_rate", 40},
	                                              {"discharge_rate", 40}},
	                                             {{"id", "radar_cell"},
	                                              {"resource", "test.energy"},
	                                              {"capacity", 1},
	                                              {"initial", 1},
	                                              {"charge_rate", 20},
	                                              {"discharge_rate", 20}}})}};
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim funded contact-loss launch");
	ticks(world, 1);
	require(!world.observe(101)->contacts.empty(), "Prior funded broad detection supplies an admitted contact");
	const auto before =
	    body(world).at("resources").at("network").at("stores")[0].at("stock").at("quantities").at("test.energy");
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	const auto saved = body(world);
	bool radar_unfunded = false;
	for (const auto &grant : saved.at("resources").at("systems"))
		if (grant.at("id") == "radar")
			radar_unfunded = grant.at("supply_permille") == 0;
	require(radar_unfunded && weapon(world).at("shots") == 1 &&
	            saved.at("resources").at("network").at("stores")[0].at("stock").at("quantities").at("test.energy") ==
	                before.get<std::int64_t>() - 2,
	        "An authorized paid launch uses its prepared permitted aim even when the same allocation removes live "
	        "radar coverage");
	World resumed;
	resumed.restore(content, world.save());
	world.tick();
	resumed.tick();
	require(world.save() == resumed.save(), "Frozen aim commits exactly one shot and leaves no transient save state");
	return world.save();
}
Json refresh_and_distinct_sources()
{
	auto source = directed_fixture();
	directed_burning(source, 3, 20);
	const auto fire = source["entities"][0]["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
	directed_effect(source, fire)["fire_ticks"] = 2;
	source["maps"][0]["entities"][2]["x_mm"] = 30000;
	source["maps"][0]["entities"][2]["y_mm"] = 20000;
	source["maps"][0]["entities"][2]["heading_mdeg"] = 326310;
	source["maps"][0]["entities"][2]["faction"] = "blue";
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(303, 3, "drive"), "Claim independent burning sources");
	ticks(world, 1);
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	for (int n = 0; n < 12 && world.save().at("active_effects").at("conditions").empty(); ++n)
		world.tick();
	// Native effect creation precedes its first source-reconciliation phase.
	// Observe a real started clock before comparing refresh cadence.
	world.tick();
	const auto initial = world.save().at("active_effects").at("conditions");
	require(!initial.empty(), "Initial periodic instance created by a real hit");
	command(world, "target", 2, "gun", 3, 303);
	command(world, "operation", 1, "gun", 3, 303);
	ticks(world, 10);
	const auto active = world.save().at("active_effects").at("conditions");
	for (const auto &first : initial)
	{
		const auto same = std::find_if(active.begin(), active.end(),
		                               [&](const auto &row)
		                               {
			                               return row.at("id") == first.at("id");
		                               });
		require(same != active.end() && same->at("expiry") > first.at("expiry"),
		        "Repeated hits refresh the original instance without shortening its duration");
		const auto elapsed_periods = same->at("clocks").begin().value().at("next").template get<std::int64_t>() -
		                             first.at("clocks").begin().value().at("next").template get<std::int64_t>();
		require(elapsed_periods >= 6 && elapsed_periods % 3 == 0,
		        "Frequent refresh preserves cadence and cannot starve or manufacture periodic ticks");
	}
	bool first_source = false, third_source = false;
	for (const auto &row : active)
	{
		first_source |= row.at("origin").at("source").at("entity") == 1;
		third_source |= row.at("origin").at("source").at("entity") == 3;
	}
	require(first_source && third_source, "Different guns retain independently lasting conditions on the same victim");
	World resumed;
	resumed.restore(content, world.save());
	for (int n = 0; n < 8; ++n)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save(), "Refreshed conditions and two sources continue deterministically");
	return world.save();
}
Json source_loss_and_removal()
{
	auto source = directed_fixture();
	directed_burning(source, 3, 80);
	auto fragile = source["entities"][0];
	fragile["id"] = "test.fragile";
	for (auto &compartment : fragile["combat"]["decks"][0]["compartments"])
	{
		compartment["hp"] = 2;
		compartment["recovery_hp"] = 1;
	}
	source["entities"].push_back(fragile);
	source["maps"][0]["entities"][0]["definition"] = "test.fragile";
	source["maps"][0]["entities"][1]["heading_mdeg"] = 180000;
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(202, 2, "drive"), "Claim native source-loss combatants");
	ticks(world, 1);
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	for (int n = 0; n < 12 && world.save().at("active_effects").at("conditions").empty(); ++n)
		world.tick();
	require(!world.save().at("active_effects").at("conditions").empty(), "Burning exists before its gun is destroyed");
	command(world, "enabled", 0);
	command(world, "target", 1, "second", 2, 202);
	world.tick();
	for (int n = 0; n < 40 && hp(world, 1) > 0; ++n)
		world.tick();
	require(hp(world, 1) == 0, "Native return fire destroys the original firing ship");
	const auto before = hp(world);
	World resumed;
	resumed.restore(content, world.save());
	for (int n = 0; n < 6; ++n)
	{
		world.tick();
		resumed.tick();
	}
	require(hp(world) < before && world.save() == resumed.save(),
	        "Captured periodic work survives source destruction and restoration without reading a live gun");
	auto removal = directed_fixture();
	directed_burning(removal, 3, 80);
	auto &removal_operations = directed_effect(removal, "test.burn.tick")["operations"];
	removal_operations.insert(removal_operations.begin(),
	                          Json{{"kind", "remove"}, {"target", "protected"}, {"grant", "burn"}});
	World removed;
	removed.load(load(removal), "test.map");
	require(removed.claim(101, 1, "drive"), "Claim self-removing periodic proof");
	ticks(removed, 1);
	command(removed, "target", 2);
	command(removed, "operation", 1);
	removed.tick();
	for (int n = 0; n < 12 && removed.save().at("active_effects").at("conditions").empty(); ++n)
		removed.tick();
	require(!removed.save().at("active_effects").at("conditions").empty(), "Removal proof applies through impact");
	command(removed, "enabled", 0);
	ticks(removed, 5);
	require(removed.save().at("active_effects").at("conditions").empty(),
	        "An explicit effect operation removes the condition before its expiry");
	const auto removed_hp = hp(removed);
	ticks(removed, 10);
	require(hp(removed) == removed_hp, "Explicit removal prevents all remaining scheduled damage");
	return world.save();
}
Json scenario_removal()
{
	auto source = directed_fixture();
	directed_burning(source, 5, 80);
	source["effects"].push_back(
	    {{"id", "test.extinguish"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "remove"}, {"effect", "test.burn"}, {"target", "protected"}, {"all_sources", true}}})}});
	source["trees"].push_back(Json::parse(R"json({"id":"test.extinguish.tree","context":"scenario","initial":"wait",
 "states":[{"id":"wait","transitions":[{"target":"extinguish","conditions":[{"left":{"observation":"tick"},"op":"ge","right":10}]}]},
 {"id":"extinguish","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":2},{"type":"entity","id":1},"test.extinguish"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.extinguish.tree"}};
	const auto content = load(source);
	World world;
	world.load(content, "test.map");
	require(world.claim(101, 1, "drive"), "Claim scenario removal proof");
	ticks(world, 1);
	command(world, "target", 2);
	command(world, "operation", 1);
	world.tick();
	for (int n = 0; n < 7 && world.save().at("active_effects").at("conditions").empty(); ++n)
		world.tick();
	require(!world.save().at("active_effects").at("conditions").empty(),
	        "Impact creates condition before scheduled scenario removal");
	command(world, "enabled", 0);
	world.tick();
	World resumed;
	resumed.restore(content, world.save());
	for (int n = 0; n < 15; ++n)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save() && world.save().at("active_effects").at("conditions").empty(),
	        "Trusted scenario removal retires another source's periodic effects deterministically");
	const auto stopped = hp(world);
	ticks(world, 12);
	require(hp(world) == stopped, "Removed instances cannot resume from stale scheduled deadlines");
	return world.save();
}
Json native_hit_bindings()
{
	Json trace = Json::array();
	for (int variant = 0; variant < 3; ++variant)
	{
		auto source = directed_fixture();
		auto &ship = source["entities"][0];
		source["resources"].push_back(
		    {{"id", "test.hit-marker"}, {"label", "Hit receipt"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}});
		ship["resources"] = {{"stores", Json::array({{{"id", "marker"},
		                                              {"resource", "test.hit-marker"},
		                                              {"capacity", 10000},
		                                              {"initial", 0},
		                                              {"charge_rate", 10000},
		                                              {"discharge_rate", 10000}}})}};
		// A physical bystander intercepts a shot aimed at the farther contact.
		source["maps"][0]["entities"][2]["x_mm"] = 30000;
		source["maps"][0]["entities"][2]["y_mm"] = 0;
		Json bindings = Json::array();
		const auto outcomes =
		    variant == 2
		        ? std::vector<std::pair<std::string, int>>{{"entity", 1}}
		        : std::vector<std::pair<std::string, int>>{{"shield", 1}, {"compartment", 10}, {"system", 100}};
		for (const auto &[outcome, amount] : outcomes)
		{
			const auto id = "test.mark." + outcome;
			source["effects"].push_back({{"id", id},
			                             {"kind", "operations"},
			                             {"operations", Json::array({{{"kind", "add"},
			                                                          {"target", "protected"},
			                                                          {"store", "energy:marker"},
			                                                          {"resource", "test.hit-marker"},
			                                                          {"amount", amount}}})}});
			bindings.push_back(
			    {{"id", outcome}, {"outcome", outcome}, {"positive_damage", variant != 2}, {"effect", id}});
		}
		source["effects"].push_back({{"id", "test.hit.payload"}, {"kind", "payload"}, {"bindings", bindings}});
		const auto fire = ship["systems"]["gun"]["directed"]["modes"][1].get<std::string>();
		directed_effect(source, fire)["payloads"] = {"test.hit.payload"};
		if (variant == 0)
		{
			source["effects"].push_back(Json::parse(R"json({"id":"test.absorber","kind":"shield","contributions":{
 "outer":{"capacity":1000,"initial":1000,"layer":0,"warmup_ticks":1,"sustain_energy_per_second":0,"recharge_per_second":0,"off_recharge_per_second":0},
 "inner":{"capacity":1000,"initial":1000,"layer":1,"warmup_ticks":1,"sustain_energy_per_second":0,"recharge_per_second":0,"off_recharge_per_second":0}}})json"));
			source["trees"].push_back(
			    Json::parse(R"json({"id":"test.absorber.tree","context":"scenario","initial":"apply",
 "states":[{"id":"apply","entry":[{"action":"apply_shield_effect","arguments":[{"type":"entity","id":3},"test.absorber","cover"]}]}]})json"));
			source["maps"][0]["scenario"] = {{"tree", "test.absorber.tree"}};
		}
		else if (variant == 1)
		{
			ship["systems"]["radar"]["compartment"] = "aft";
			ship["systems"]["radar"]["faults"] = Json::array({{{"id", "hit"},
			                                                   {"symptom", "Hit"},
			                                                   {"diagnosis", "Impact"},
			                                                   {"effect", "disabled"},
			                                                   {"chance_permille", 1000},
			                                                   {"damage_threshold", 1},
			                                                   {"repair_work", 100}}});
		}
		else
			directed_effect(source, fire)["projectile"]["damage"] = 0;
		const auto content = load(source);
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive"), "Claim typed hit delivery source");
		ticks(world, 1);
		command(world, "target", 2);
		command(world, "operation", 1);
		ticks(world, 8);
		const auto marker = [&](EntityId id)
		{
			return body(world, id)
			    .at("resources")
			    .at("network")
			    .at("stores")[0]
			    .at("stock")
			    .at("quantities")
			    .value("test.hit-marker", std::int64_t{0});
		};
		require(marker(2) == 0 && marker(1) == 0 && marker(3) > 0,
		        "Payload uses the actual intervening object rather than the selected contact");
		if (variant == 0)
			require(marker(3) == 1 && hp(world, 3) == 20000,
			        "Only the reached outer shield contribution publishes a binding on full absorption");
		else if (variant == 1)
			require(marker(3) >= 110 && hp(world, 3) < 20000,
			        "Native compartment damage and affected equipment independently publish their typed bindings");
		else
			require(hp(world, 3) == 20000,
			        "An explicitly zero-damage contact still delivers a qualifying effect without inventing damage");
		World resumed;
		resumed.restore(content, world.save());
		world.tick();
		resumed.tick();
		require(world.save() == resumed.save(),
		        "Typed hit receipts survive continuation without duplicate application");
		trace.push_back(world.save());
	}
	return trace;
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const std::string selected = argc > 2 && std::string(argv[1]) == "--case" ? argv[2] : "";
		const std::vector<std::string> cases{
		    "catalogue",          "switching",        "scan-modes",        "funded-beam",
		    "broad-radar",        "launch-funding",   "launch-processor",  "loading-modes",
		    "frozen-contact",     "inflight-source",  "burning",           "refresh",
		    "source-loss",        "scenario-removal", "typed-hits",        "blasts",
		    "generated-controls", "periodic-order",   "periodic-rollback", "deferred-callback"};
		if (!selected.empty() && std::find(cases.begin(), cases.end(), selected) == cases.end())
			throw std::runtime_error("Unknown directed test case: " + selected);
		const auto run = [&](const char *name, auto action)
		{
			if (!selected.empty() && selected != name)
			{
				if constexpr (std::is_void_v<decltype(action())>)
					return;
				else
					return Json::array();
			}
			try
			{
				return action();
			}
			catch (const std::exception &error)
			{
				throw std::runtime_error(std::string(name) + ": " + error.what());
			}
		};
		run("catalogue",
		    []
		    {
			    validate_catalogue();
		    });
		run("generated-controls",
		    []
		    {
			    generated_directed_controls();
		    });
		Json trace = Json::array();
		trace.push_back(run("switching",
		                    []
		                    {
			                    return switching_and_continuation();
		                    }));
		trace.push_back(run("scan-modes",
		                    []
		                    {
			                    return distinct_scan_modes();
		                    }));
		trace.push_back(run("funded-beam",
		                    []
		                    {
			                    return funded_beam_and_loaded_round();
		                    }));
		trace.push_back(run("broad-radar",
		                    []
		                    {
			                    return broad_radar_with_focused_off();
		                    }));
		for (auto &state : run("launch-funding",
		                       []
		                       {
			                       return indivisible_launch_funding();
		                       }))
			trace.push_back(std::move(state));
		for (auto &state : run("launch-processor",
		                       []
		                       {
			                       return launch_processor_competition();
		                       }))
			trace.push_back(std::move(state));
		trace.push_back(run("loading-modes",
		                    []
		                    {
			                    return changing_loaded_operation();
		                    }));
		trace.push_back(run("frozen-contact",
		                    []
		                    {
			                    return frozen_permitted_contact();
		                    }));
		trace.push_back(run("inflight-source",
		                    []
		                    {
			                    return directed_inflight_source_loss();
		                    }));
		trace.push_back(run("burning",
		                    []
		                    {
			                    return burning_and_continuation();
		                    }));
		trace.push_back(run("refresh",
		                    []
		                    {
			                    return refresh_and_distinct_sources();
		                    }));
		trace.push_back(run("source-loss",
		                    []
		                    {
			                    return source_loss_and_removal();
		                    }));
		for (auto &state : run("periodic-rollback",
		                       []
		                       {
			                       return periodic_transaction_rollback();
		                       }))
			trace.push_back(std::move(state));
		trace.push_back(run("periodic-order",
		                    []
		                    {
			                    return periodic_ordered_removal();
		                    }));
		trace.push_back(run("deferred-callback",
		                    []
		                    {
			                    return deferred_callback_continuation();
		                    }));
		trace.push_back(run("scenario-removal",
		                    []
		                    {
			                    return scenario_removal();
		                    }));
		for (auto &state : run("typed-hits",
		                       []
		                       {
			                       return native_hit_bindings();
		                       }))
			trace.push_back(std::move(state));
		for (auto &state : run("blasts",
		                       []
		                       {
			                       return directed_blast_cases();
		                       }))
			trace.push_back(std::move(state));
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			for (auto &state : trace)
				state.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "Directed operations, impact conditions and continuation passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
