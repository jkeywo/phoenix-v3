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

import phoenix.content;

using namespace phoenix;
namespace st = phoenix::state_tree;
namespace
{
using phoenix::test::archive;
using phoenix::test::load;
using phoenix::test::rejects;
using phoenix::test::require;
Json fixture()
{
	auto result =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic gameplay core");
	result["schema"] = 4;
	// Empty presentation avoids binding a legacy throttle ambiguously when a
	// composition places multiple independent engine instances.
	result["consoles"][0]["widgets"] = Json::array();
	result["knowledge"] = Json::parse(R"json({
		"roles":["operator","restricted"],
		"schemas":[
			{"id":"report","producers":["operator"],"readers":["operator"],"ship_publishable":true,
			 "stale_after_ticks":3,"fields":[
				{"id":"amount","type":"integer","publishable":true},
				{"id":"private-note","type":"text","required":false,"publishable":false}]},
			{"id":"private","producers":["restricted"],"readers":["restricted"],
			 "fields":[{"id":"secret","type":"integer"}]}
		]
	})json");
	return result;
}
Json network_fixture()
{
	return Json::parse(R"json({
		"stores":[
			{"id":"bus","capacity":500,"initial":200,"charge_rate":1000,"discharge_rate":1000},
			{"id":"junction","capacity":100,"charge_rate":1000,"discharge_rate":1000},
			{"id":"a","capacity":20,"charge_rate":1000,"discharge_rate":1000},
			{"id":"b","capacity":20,"charge_rate":1000,"discharge_rate":1000},
			{"id":"hot","capacity":20,"initial":10,"charge_rate":100,"discharge_rate":100,
			 "initial_heat":10,"stop_heat":20,"recover_heat":5}
		],
		"links":[
			{"id":"shared","from":"bus","to":"junction","capacity_rate":100},
			{"id":"a-feed","from":"junction","to":"a","capacity_rate":1000},
			{"id":"b-feed","from":"junction","to":"b","capacity_rate":1000},
			{"id":"alternate","from":"bus","to":"a","capacity_rate":40,"isolated":true}
		],
		"coolant_sources":[{"id":"tank","initial":100,"removal_rate":40,"coolant_per_heat_ppm":1000000}],
		"cooling_links":[{"id":"hot-feed","source":"tank","store":"hot","capacity_rate":40,"allocation_rate":40}]
	})json");
}
Json equipment_fixture()
{
	auto result = fixture();
	result["equipment"] = Json::parse(R"json([
		{"id":"test.drive-template","kind":"engine","max_speed_mm_s":1000,"acceleration_mm_s2":200,
		 "power_store":"cell"},
		{"id":"test.drive-variant","extends":"test.drive-template","acceleration_mm_s2":300}
	])json");
	result["assemblies"] = Json::parse(R"json([
		{"id":"test.drive-unit",
		 "systems":{"drive":{"template":"test.drive-variant"}},
		 "resources":{
			"stores":[{"id":"cell","capacity":40,"initial":20,"charge_rate":40,"discharge_rate":40}],
			"links":[{"id":"feed","from":"$supply","to":"cell","capacity_rate":40}]},
		 "services":{"supply":{"kind":"power","direction":"input"},
		             "reserve":{"kind":"power","direction":"output","target":"cell"}}}
	])json");
	auto &ship = result["entities"][0];
	ship["resources"] = network_fixture();
	ship["services"] = {{"main", {{"kind", "power"}, {"direction", "output"}, {"target", "bus"}}}};
	ship["assemblies"] = Json::parse(R"json([
		{"id":"left","assembly":"test.drive-unit","bindings":{"supply":"main"}},
		{"id":"right","assembly":"test.drive-unit","bindings":{"supply":"main"},
		 "overrides":{"systems":{"drive":{"acceleration_mm_s2":400}}}}
	])json");
	return result;
}
Json tree_fixture()
{
	auto result = fixture();
	result["trees"] = Json::parse(R"json([
		{"id":"test.child","context":"scenario","initial":"run",
		 "parameters":{"amount":{"type":"integer","default":3}},
		 "memory":[{"name":"remembered","type":"integer","initial":{"parameter":"amount"}}],
		 "states":[{"id":"run","running":[
			{"action":"emit_scenario_event","arguments":["child",{"memory":"remembered"},false,""]}
		 ]}]},
		{"id":"test.parent","context":"scenario","initial":"root",
		 "states":[
			{"id":"root","initial":"work","entry":[
				{"action":"emit_scenario_event","arguments":["root",0,false,""]}],
			 "transitions":[{"target":"root","reason":"reactivate-ancestor","conditions":[
				{"left":{"elapsed":true},"op":"ge","right":2}]}]},
			{"id":"work","parent":"root","running":[
				{"call":"test.child","parameters":{"amount":3}},
				{"call":"test.child","parameters":{"amount":8}}]}
		 ]},
		{"id":"test.derived","extends":"test.parent","overrides":[
			{"state":"root","transitions":[{"target":"root","reason":"derived-reactivation","conditions":[
				{"left":{"elapsed":true},"op":"ge","right":3}]}]}]},
		{"id":"test.ordered","context":"system","initial":"run",
		 "memory":[{"name":"copied","type":"integer","initial":0}],
		 "states":[{"id":"run","running":[
			{"action":"set_speed","arguments":[{"observation":"self.system"},10]},
			{"memory":"copied","value":{"intent":"speed_mm_s"}},
			{"action":"set_speed","arguments":[{"observation":"self.system"},20]},
			{"action":"set_group_allocation","arguments":[{"observation":"self.system"},"a",10]},
			{"action":"set_group_allocation","arguments":[{"observation":"self.system"},"b",20]},
			{"action":"set_group_allocation","arguments":[{"observation":"self.system"},"a",30]}
		 ]}]},
		{"id":"test.bounded","context":"scenario","initial":"run","budget":2,
		 "memory":[{"name":"changed","type":"integer","initial":0}],
		 "states":[{"id":"run","entry":[
			{"action":"emit_scenario_event","arguments":["pending-effect",0,false,""]}],
		 "running":[{"memory":"changed","value":1}]}]}
	])json");
	result["trees"].push_back(Json::parse(R"json(
		{"id":"test.grandchild","extends":"test.derived","overrides":[
			{"state":"work","running":[{"call":"test.child","parameters":{"amount":11}}]}]}
	)json"));
	return result;
}
ResourceAmount stored(const ResourceNetwork &network)
{
	ResourceAmount total = 0;
	for (const auto &store : network.stores())
		total += store.energy;
	return total;
}
void equipment_and_resources()
{
	auto source = equipment_fixture();
	auto catalog = load(source);
	const auto &ship = catalog.entities.at("test.ship");
	require(ship.at("systems").at("left.drive").at("power_store") == "left.cell" &&
	            ship.at("systems").at("right.drive").at("power_store") == "right.cell",
	        "assembly placements must prefix independent stores and fitted bindings");
	require(ship.at("systems").at("left.drive").at("acceleration_mm_s2") == 300 &&
	            ship.at("systems").at("right.drive").at("acceleration_mm_s2") == 400,
	        "equipment inheritance and local assembly overrides must resolve before production compilation");
	const auto original = catalog.entities;
	resolve_equipment(catalog);
	require(catalog.entities == original, "composition resolution must not duplicate providers on a second pass");
	const auto &definition = catalog.resource_definitions.at("test.ship");
	require(definition.store_ids.contains("left.cell") && definition.store_ids.contains("right.cell") &&
	            definition.links.at(definition.link_ids.at("left.feed")).from == definition.store_ids.at("bus") &&
	            definition.links.at(definition.link_ids.at("right.feed")).from == definition.store_ids.at("bus"),
	        "repeated placements must bind the same real provider rather than duplicate its capacity");
	ResourceNetwork network(definition, 17);
	const auto a = definition.store_ids.at("a"), b = definition.store_ids.at("b"), hot = definition.store_ids.at("hot");
	network.request_operation(a, 100);
	network.request_operation(b, 100);
	const auto before = stored(network);
	network.tick();
	ResourceAmount consumed = 0, lost = 0, generated = 0;
	for (const auto &report : network.telemetry())
	{
		consumed += report.consumed;
		lost += report.lost;
		generated += report.generated;
	}
	require(stored(network) == before + generated - consumed - lost,
	        "resource transfers and simultaneous operation must conserve stored energy");
	require(network.telemetry()[a].accepted_incoming + network.telemetry()[b].accepted_incoming <= 5,
	        "downstream consumers must share the finite upstream conduit capacity");
	require(network.telemetry()[hot].heat_removed == 2 && network.coolant_sources()[0].remaining == 98,
	        "finite coolant is consumed for actual heat removed, not its reservation alone");
	for (int tick = 0; tick < 7; ++tick)
		network.tick();
	require(network.stores()[hot].heat == 0 && network.coolant_sources()[0].remaining == 90,
	        "idle cooling reservations must stop consuming coolant when equipment has no heat");
	network.set_link_isolated(definition.link_ids.at("shared"), true);
	network.set_link_isolated(definition.link_ids.at("alternate"), false);
	network.tick();
	require(network.telemetry()[a].accepted_incoming <= 2 && network.telemetry()[b].accepted_incoming == 0,
	        "isolation must remove the shared route while a real alternate route retains its finite capacity");
	const auto saved = network.save();
	ResourceNetwork resumed(definition, 999);
	resumed.restore(saved);
	for (int tick = 0; tick < 8; ++tick)
	{
		network.tick();
		resumed.tick();
	}
	require(network.save() == resumed.save(),
	        "resource continuation must preserve quantities, controls and division remainders");
	auto invalid = source;
	invalid["entities"][0]["assemblies"][0]["bindings"]["supply"] = "missing-provider";
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "unresolved assembly services must fail the production loader");
	invalid = source;
	invalid["entities"][0]["assemblies"].push_back(invalid["entities"][0]["assemblies"][0]);
	rejects(
	    [&]
	    {
		    load(invalid);
	    },
	    "duplicate placement identities must fail instead of sharing runtime state");
	WorkshopDocument document;
	document.load_pack(archive(source));
	require(document.definitions()["entities"][0].contains("assemblies") &&
	            !document.definitions()["entities"][0]["systems"].contains("left.drive"),
	        "Workshop opening must restore composition sources rather than freeze expanded fitted Systems");
	document.definitions()["equipment"][0]["max_speed_mm_s"] = 1700;
	document.definitions()["assemblies"][0]["systems"]["drive"]["acceleration_mm_s2"] = 500;
	const auto edited = document.verify();
	require(edited.entities.at("test.ship").at("systems").at("left.drive").at("max_speed_mm_s") == 1700 &&
	            edited.entities.at("test.ship").at("systems").at("left.drive").at("acceleration_mm_s2") == 500 &&
	            edited.entities.at("test.ship").at("systems").at("right.drive").at("acceleration_mm_s2") == 400,
	        "Workshop template/assembly edits must propagate while preserving explicit placement overrides");
	WorkshopDocument reopened;
	reopened.load_pack(document.export_pack());
	require(reopened.verify().entities == edited.entities,
	        "Workshop verification, export and reopen must preserve editable composition and resolved behavior");
}

void overload_and_recovery()
{
	auto source = fixture();
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[
			{"id":"bus","capacity":1000,"initial":500,"charge_rate":100,"discharge_rate":100},
			{"id":"a","capacity":50,"initial":10,"charge_rate":100,"discharge_rate":100,"group":"a"},
			{"id":"b","capacity":50,"initial":10,"charge_rate":100,"discharge_rate":100,"group":"b"}],
		"groups":[{"id":"a","budget_rate":100,"restore_ticks":2},{"id":"b","budget_rate":100,"restore_ticks":2}],
		"links":[{"id":"a","from":"bus","to":"a","capacity_rate":100},
		         {"id":"b","from":"bus","to":"b","capacity_rate":100}]
	})json");
	const auto catalog = load(source);
	const auto &definition = catalog.resource_definitions.at("test.ship");
	ResourceNetwork network(definition, 71);
	const auto a = definition.store_ids.at("a"), b = definition.store_ids.at("b");
	network.request_operation(a, 20);
	network.request_operation(b, 20);
	network.tick();
	const auto cut_count = static_cast<int>(network.groups()[0].cut) + static_cast<int>(network.groups()[1].cut);
	require(cut_count == 1, "oversubscribed incoming groups must cut one eligible flow to fit real supply");
	require(network.groups()[0].budget_rate == 100 && network.groups()[1].budget_rate == 100 &&
	            network.telemetry()[a].consumed == 1 && network.telemetry()[b].consumed == 1,
	        "a flow cut preserves settings and local battery operation");
	const auto cut = network.groups()[0].cut ? ResourceIndex{0} : ResourceIndex{1};
	const auto supplied = cut == 0 ? ResourceIndex{1} : ResourceIndex{0};
	network.set_group_budget(cut, 0);
	network.tick();
	require(network.groups()[cut].cut && !network.groups()[supplied].cut,
	        "restoration must wait for its sustainable interval without evicting a healthy group");
	ResourceNetwork resumed(definition, 1);
	resumed.restore(network.save());
	network.tick();
	resumed.tick();
	require(!network.groups()[cut].cut && !network.groups()[supplied].cut && network.save() == resumed.save(),
	        "sustainable restoration and random state must continue identically after save recovery");
	// A cycle cannot manufacture incoming supply from the receiver's own reserve.
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[
			{"id":"a","capacity":100,"initial":50,"charge_rate":100,"discharge_rate":100,"group":"incoming"},
			{"id":"b","capacity":100,"charge_rate":100,"discharge_rate":100}],
		"groups":[{"id":"incoming","budget_rate":20,"restore_ticks":1}],
		"links":[{"id":"a-b","from":"a","to":"b","capacity_rate":100},
		         {"id":"b-a","from":"b","to":"a","capacity_rate":100}]
	})json");
	const auto circular_catalog = load(source);
	const auto &circular_definition = circular_catalog.resource_definitions.at("test.ship");
	ResourceNetwork circular(circular_definition, 17);
	const auto receiver = circular_definition.store_ids.at("a");
	circular.request_operation(receiver, 20);
	circular.tick();
	require(
	    circular.groups()[0].cut && circular.telemetry()[receiver].accepted_incoming == 0 &&
	        circular.telemetry()[receiver].consumed == 1,
	    "directed conduit cycles cannot restore incoming allocation by recirculating the receiver's own local reserve");
	// Per-second reservations must not oscillate with sub-tick integer transfer credit.
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[
			{"id":"bus","capacity":100,"initial":100,"charge_rate":1,"discharge_rate":1},
			{"id":"receiver","capacity":100,"charge_rate":1,"discharge_rate":1,"group":"steady"}],
		"groups":[{"id":"steady","budget_rate":1,"restore_ticks":1}],
		"links":[{"id":"feed","from":"bus","to":"receiver","capacity_rate":1}]
	})json");
	const auto fractional_catalog = load(source);
	const auto &fractional_definition = fractional_catalog.resource_definitions.at("test.ship");
	ResourceNetwork fractional(fractional_definition, 19);
	const auto fractional_receiver = fractional_definition.store_ids.at("receiver");
	fractional.request_operation(fractional_receiver, 1);
	ResourceAmount paid = 0;
	for (int tick = 0; tick < 40; ++tick)
	{
		fractional.tick();
		paid += fractional.telemetry()[fractional_receiver].consumed;
		require(!fractional.groups()[0].cut,
		        "a sustainable one-unit-per-second reservation must remain supplied between fractional transfer ticks");
	}
	require(paid == 2,
	        "fractional generation/transfer/operation credit must conserve exactly two units over two seconds");
}

void generation_and_thermal_recovery()
{
	auto source = fixture();
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[
			{"id":"capacitor","capacity":100,"initial":100,"charge_rate":100,"discharge_rate":100},
			{"id":"protected","capacity":100,"initial":50,"charge_rate":100,"discharge_rate":100,
			 "initial_heat":25,"stop_heat":20,"recover_heat":5}],
		"generators":[{"id":"generator","store":"capacitor","maximum_rate":100,
		               "response_rate":2000,"initial_output":100}],
		"coolant_sources":[{"id":"tank","initial":100,"removal_rate":100}],
		"cooling_links":[{"id":"protected","source":"tank","store":"protected",
		                 "capacity_rate":100,"allocation_rate":100}]
	})json");
	const auto catalog = load(source);
	const auto &definition = catalog.resource_definitions.at("test.ship");
	ResourceNetwork network(definition);
	const auto capacitor = definition.store_ids.at("capacitor"), protected_store = definition.store_ids.at("protected");
	network.tick();
	require(network.telemetry()[capacitor].generated == 0 && network.stores()[capacitor].energy == 100,
	        "a full capacitor must reject generation rather than drawing energy that no consumer accepts");
	network.request_operation(capacitor, 100);
	network.tick();
	require(network.telemetry()[capacitor].consumed == 5 && network.telemetry()[capacitor].generated == 5 &&
	            network.stores()[capacitor].energy == 100,
	        "paid operation must open real capacitor headroom that generation can replenish in the same tick");
	network.request_operation(protected_store, 100);
	network.set_store_enabled(protected_store, false);
	for (int tick = 0; tick < 5; ++tick)
		network.tick();
	require(network.stores()[protected_store].heat == 0 && !network.stores()[protected_store].thermal_stop &&
	            !network.stores()[protected_store].enabled && network.telemetry()[protected_store].consumed == 0,
	        "cooling and thermal recovery must respect a deliberate shutdown rather than restart the equipment");
}
void resource_forecasts_and_fractional_overdrive()
{
	{
		auto input = fixture();
		input["entities"][0]["resources"] = Json::parse(R"json({"stores":[{
		 "id":"cell","capacity":100,"initial":100,"charge_rate":100,"discharge_rate":100,
		 "operation_heat_ppm":333333,"depletion_heat_ppm":250000,"overdrive_heat_permille":1500}]})json");
		const auto content = load(input);
		const auto &spec = content.resource_definitions.at("test.ship");
		ResourceNetwork ordinary(spec), late(spec);
		ordinary.set_overdrive(0, true);
		late.set_overdrive(0, true);
		for (int tick = 0; tick < 4; ++tick)
		{
			ordinary.request_operation(0, 0, 3);
			ordinary.tick();
			late.tick();
			require(late.spend_external(0, 3), "late exact payment uses the current discharge allowance");
			const auto before = late.save();
			require(!late.spend_external(0, 3) && late.save() == before,
			        "unaffordable late payment cannot partially debit or change heat fractions");
			require(late.save().at("stores") == ordinary.save().at("stores"),
			        "ordinary and late debits retain identical energy and fractional heat accounting");
			ResourceNetwork resumed(spec);
			resumed.restore(late.save());
			require(resumed.save() == late.save(), "late payment fractions and telemetry survive recovery");
		}
		require(late.stores()[0].energy == 88 && late.stores()[0].heat == 7,
		        "twelve paid units produce three operating heat, four after overdrive, plus three depletion heat");
	}
	auto source = fixture();
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[{"id":"cell","capacity":100,"initial":50,"charge_rate":100,"discharge_rate":100,
		           "initial_heat":10,"stop_heat":100,"recover_heat":5,"operation_heat_ppm":1000000}],
		"coolant_sources":[{"id":"tank","initial":10,"removal_rate":20,"coolant_per_heat_ppm":100000}],
		"cooling_links":[{"id":"feed","source":"tank","store":"cell","capacity_rate":20,"allocation_rate":20}]
	})json");
	const auto catalog = load(source);
	const auto &definition = catalog.resource_definitions.at("test.ship");
	ResourceNetwork network(definition);
	const auto cell = definition.store_ids.at("cell"), tank = definition.coolant_source_ids.at("tank");
	network.request_operation(cell, 100);
	network.tick();
	const auto power = network.forecast(cell);
	const auto coolant = network.coolant_forecast(tank);
	require(power.depletion_ticks == 9 && power.thermal_stop_ticks == 22 && coolant.depletion_ticks == 99 &&
	            coolant.observed_coolant_used == 0 && coolant.observed_heat_removed == 1,
	        "forecasts must use actual net energy/heat and fractional finite coolant use rather than rounded stock "
	        "debit alone");
	ResourceNetwork resumed(definition);
	resumed.restore(network.save());
	require(resumed.coolant_forecast(tank).depletion_ticks == coolant.depletion_ticks &&
	            resumed.save() == network.save(),
	        "current forecast observations and fractional coolant liability must survive recovery");
	network.apply_operating_heat(cell, 100);
	network.set_store_enabled(cell, false);
	network.tick();
	require(network.forecast(cell).thermal_stop_ticks == 0 && network.forecast(cell).thermal_recovery_ticks > 0 &&
	            !network.stores()[cell].enabled,
	        "cooldown forecasts must describe real heat recovery without undoing deliberate equipment disable");
	network.set_cooling(0, 20, true);
	network.tick();
	require(network.coolant_forecast(tank).depletion_ticks == -1,
	        "constant-last-tick forecasts cannot invent future coolant consumption after isolation");
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[{"id":"cell","capacity":100,"initial":100,"charge_rate":100,"discharge_rate":100,
		           "overdrive_permille":1500}]
	})json");
	const auto fractional_catalog = load(source);
	const auto &fractional_definition = fractional_catalog.resource_definitions.at("test.ship");
	ResourceNetwork fractional(fractional_definition);
	fractional.set_overdrive(0, true);
	fractional.request_operation(0, 1);
	for (int tick = 0; tick < 20; ++tick)
		fractional.tick();
	ResourceNetwork fractional_resumed(fractional_definition);
	fractional_resumed.restore(fractional.save());
	for (int tick = 0; tick < 20; ++tick)
	{
		fractional.tick();
		fractional_resumed.tick();
	}
	require(fractional.stores()[0].energy == 97 && fractional.save() == fractional_resumed.save(),
	        "one-unit-per-second work at 150 percent overdrive must pay exactly three units over two seconds across "
	        "recovery");
}
void prepaid_fractional_work()
{
	auto source = fixture();
	source["entities"][0]["resources"] = Json::parse(R"json({
		"stores":[{"id":"cell","capacity":1,"initial":1,"charge_rate":20,"discharge_rate":20}],
		"coolant_sources":[{"id":"tank","initial":10,"removal_rate":20,"power_store":"cell"}],
		"cooling_links":[{"id":"feed","source":"tank","store":"cell","capacity_rate":20,"allocation_rate":20}]
	})json");
	// The production fitted verifier requires a dedicated, positive-rate pump.
	source["entities"][0]["systems"]["pump"] = {
	    {"kind", "coolant"}, {"power_store", "cell"}, {"energy_per_second", 1}, {"cooling_link", "feed"}};
	const auto catalog = load(source);
	const auto &definition = catalog.resource_definitions.at("test.ship");
	ResourceNetwork network(definition);
	network.request_operation(0, 1);
	network.apply_operating_heat(0, 10);
	network.tick();
	require(
	    network.stores()[0].energy == 0 && network.telemetry()[0].consumed == 1 &&
	        network.telemetry()[0].work_funded == 1000 && network.stores()[0].work_credit == 19000 &&
	        network.coolant_sources()[0].last_heat_removed == 1,
	    "sub-unit work must reserve its first real energy unit before operation or powered cooling produces output");
	network.request_operation(0, 0);
	for (int tick = 0; tick < 37; ++tick)
		network.tick();
	require(network.stores()[0].work_credit == 19000 && network.telemetry()[0].work_funded == 0 &&
	            network.coolant_sources()[0].last_heat_removed == 0,
	        "pausing a low-rate consumer must retain prepaid credit without spending it or running a disabled pump");
	ResourceNetwork resumed(definition);
	resumed.restore(network.save());
	network.request_operation(0, 1);
	resumed.request_operation(0, 1);
	for (int tick = 0; tick < 19; ++tick)
	{
		network.tick();
		resumed.tick();
		require(network.telemetry()[0].work_funded == 1000 && network.telemetry()[0].consumed == 0,
		        "previously paid fractional credit must support precisely its remaining low-rate work");
	}
	require(network.stores()[0].work_credit == 0 && network.save() == resumed.save(),
	        "prepaid credit, cooling and paused continuation must agree with uninterrupted execution");
	const auto before_rejected_restore = network.save();
	auto invalid_credit = before_rejected_restore;
	invalid_credit["stores"][0]["work_credit"] = resource_work_unit;
	rejects(
	    [&]
	    {
		    network.restore(invalid_credit);
	    },
	    "continuation cannot mint a whole free unit of prepaid work");
	require(network.save() == before_rejected_restore,
	        "a rejected prepaid-credit restore must leave live state unchanged");
	network.tick();
	require(network.telemetry()[0].work_funded == 0 && network.stores()[0].work_credit == 0,
	        "a depleted store with no prepaid credit must refuse work and cannot mint credit on repeated denial");
	ResourceNetwork empty(definition);
	auto saved = empty.save();
	saved["stores"][0]["energy"] = 0;
	empty.restore(saved);
	empty.request_operation(0, 1);
	for (int tick = 0; tick < 25; ++tick)
	{
		empty.tick();
		require(empty.telemetry()[0].work_funded == 0 && empty.stores()[0].work_credit == 0,
		        "an initially empty low-rate consumer cannot borrow output ahead of its first payment");
	}
}

void trees()
{
	const auto source = tree_fixture();
	const auto catalog = load(source);
	{
		auto edited = catalog;
		edited.trees.at("test.parent")["initial"] = "missing";
		rejects(
		    [&]
		    {
			    verify_catalog(edited);
		    },
		    "Standalone verification must reject edited trees even when a previous compiled program exists");
		require(edited.tree_programs.at("test.parent").signature == catalog.tree_programs.at("test.parent").signature,
		        "Standalone verification must not mutate the caller's compiled programs on failure");
		edited = catalog;
		edited.tree_programs.clear();
		verify_catalog(edited);
		require(edited.tree_programs.empty(),
		        "Standalone verification rebuilds programs privately without requiring a cache");
	}
	const auto &program = catalog.tree_programs.at("test.parent");
	std::vector<st::Value> observations(program.registry.observations.size());
	st::Instance instance;
	const auto first = st::step(program, instance, observations);
	require(first.outputs.size() == 3 && first.outputs[1].arguments[1].number == 3 &&
	            first.outputs[2].arguments[1].number == 8,
	        "separate reusable reference placements must keep independent typed parameters and memory");
	const auto saved = st::save(program, instance);
	auto resumed = st::load(program, saved);
	const auto second = st::step(program, instance, observations);
	const auto second_resumed = st::step(program, resumed, observations);
	require(second.outputs.size() == 1 && second_resumed.outputs.size() == 1 &&
	            second.debug.transition == "reactivate-ancestor" &&
	            st::save(program, instance) == st::save(program, resumed),
	        "ancestor reentry emits its entry effect once and resumes without repeating child effects");
	const auto third = st::step(program, instance, observations);
	require(
	    third.outputs.size() == 2 && third.outputs[0].call_path == first.outputs[1].call_path &&
	        third.outputs[0].call_activations != first.outputs[1].call_activations,
	    "later subtree activations need distinct complete effect keys despite identical placement and local task IDs");
	st::interrupt(program, instance, observations);
	require(st::step(program, instance, observations).outputs.empty(),
	        "interrupted decisions must stay stopped until explicit reset");
	const auto stopped = st::load(program, st::save(program, instance));
	require(stopped.interrupted && stopped.path.empty(), "interrupted lifecycle must survive save and restore");
	auto handover = stopped;
	st::resume(handover);
	handover = st::load(program, st::save(program, handover));
	st::interrupt(program, handover, observations);
	handover = st::load(program, st::save(program, handover));
	require(handover.interrupted && !handover.resume_pending,
	        "a handover controller can be stopped and serialized before its next evaluation");
	st::Instance virgin;
	st::interrupt(program, virgin, observations);
	st::resume(virgin);
	virgin = st::load(program, st::save(program, virgin));
	require(!st::step(program, virgin, observations).outputs.empty(),
	        "a never-started controller can interrupt, resume, serialize and initialise normally");
	const auto allocator = instance.next_activation;
	st::reset(instance);
	const auto restarted = st::step(program, instance, observations);
	require(!restarted.outputs.empty() && restarted.outputs[0].activation >= allocator,
	        "explicit restart must not reuse a prior root effect activation identity");
	const auto &derived = catalog.tree_programs.at("test.derived");
	st::Instance variant;
	st::step(derived, variant, observations);
	require(st::step(derived, variant, observations).outputs.empty(),
	        "named structural override must replace the inherited transition timing");
	require(st::step(derived, variant, observations).debug.transition == "derived-reactivation",
	        "resolved inherited transition conditions must govern actual evaluation");
	const auto &grandchild = catalog.tree_programs.at("test.grandchild");
	st::Instance descendant;
	const auto descendant_first = st::step(grandchild, descendant, observations);
	require(descendant_first.outputs.size() == 2 && descendant_first.outputs[1].arguments[1].number == 11,
	        "descendant structural overrides must apply to their own named state");
	require(st::step(grandchild, descendant, observations).outputs.empty() &&
	            st::step(grandchild, descendant, observations).debug.transition == "derived-reactivation",
	        "multi-generation structural inheritance must retain ancestor patches on distinct states");
	const auto &ordered = catalog.tree_programs.at("test.ordered");
	std::vector<st::Value> system_observations(ordered.registry.observations.size());
	system_observations[static_cast<st::Index>(GameplayTreeObservation::SelfSystem)] =
	    st::Value::reference(st::Type::System, 5);
	st::Instance writer;
	const auto ordered_result = st::step(ordered, writer, system_observations);
	require(writer.memory[0].number == 10 && ordered_result.outputs.size() == 3,
	        "later tasks must read earlier intent and distinct distributor groups must retain separate outputs");
	bool speed = false, group_a = false, group_b = false;
	for (const auto &output : ordered_result.outputs)
	{
		if (output.action == static_cast<st::Index>(GameplayTreeAction::SetSpeed))
			speed = output.arguments[1].number == 20;
		if (output.action == static_cast<st::Index>(GameplayTreeAction::SetGroupAllocation))
		{
			if (output.arguments[1].text == "a")
				group_a = output.arguments[2].number == 30;
			if (output.arguments[1].text == "b")
				group_b = output.arguments[2].number == 20;
		}
	}
	require(speed && group_a && group_b,
	        "final typed outputs must apply last-write precedence only within their full conflict keys");
	const auto &bounded = catalog.tree_programs.at("test.bounded");
	st::Instance limited;
	const auto before = st::save(bounded, limited);
	rejects(
	    [&]
	    {
		    st::step(bounded, limited, observations);
	    },
	    "bounded evaluation must refuse excessive work");
	require(st::save(bounded, limited) == before,
	        "budget failure must roll back memory and one-shot markers before any effects escape");
	auto malformed = saved;
	malformed["instance"]["path"][0]["activation"] = -1;
	rejects(
	    [&]
	    {
		    st::load(program, malformed);
	    },
	    "negative saved identities must not wrap to unsigned activation IDs");
	malformed = saved;
	malformed["instance"]["calls"][0]["task"] = 999999;
	rejects(
	    [&]
	    {
		    st::load(program, malformed);
	    },
	    "saved reference continuations must resolve to active compiled call sites");
	auto forbidden = source;
	forbidden["trees"][3]["states"][0]["running"][0] = {{"action", "spawn_group"}, {"arguments", {"group", "handle"}}};
	rejects(
	    [&]
	    {
		    load(forbidden);
	    },
	    "System trees cannot acquire scenario spawning authority through authored actions");
	auto recursive = source;
	recursive["trees"][0]["states"][0]["running"].push_back({{"call", "test.child"}});
	rejects(
	    [&]
	    {
		    load(recursive);
	    },
	    "reference cycles must be refused at the production content boundary");
}

void knowledge()
{
	const auto catalog = load(fixture());
	const auto &definition = catalog.knowledge_definition;
	const auto role = definition.role("operator"), restricted = definition.role("restricted");
	const auto report = definition.schema("report"), amount = definition.field(report, "amount"),
	           note = definition.field(report, "private-note");
	KnowledgeRuntime runtime(definition);
	runtime.register_console({11, 1, 10, {role}, false});
	runtime.register_console({12, 1, 20, {role}, false});
	runtime.register_console({13, 1, 30, {restricted}, false});
	runtime.register_console({21, 2, 40, {role}, true});
	runtime.register_system(101, 11);
	runtime.register_controller(1001, 12);
	runtime.register_controller(1002, 12);
	runtime.register_controller(1003, 11);
	runtime.register_ai(1001, 12);
	runtime.register_ai(1002, 12);
	KnowledgeRecordInput input;
	input.schema = report;
	input.producer = role;
	input.subject = 1;
	input.provenance = {101, 0, 1, 4, EvidenceKind::Fact};
	input.fields = {{amount, std::int64_t{7}}, {note, std::string{"private machinery detail"}}};
	require(runtime.write_console(11, input, 0), "permitted machinery producer must write its Console evidence");
	require(runtime.read_ship(12).empty(), "Console observations must not become automatically shipwide");
	rejects(
	    [&]
	    {
		    runtime.publish(11, report, 1, {note}, false, 0);
	    },
	    "private fields must not be published even within the same ship");
	const auto publication = runtime.publish(11, report, 1, {amount}, true, 0);
	const auto published = runtime.read_ship(12);
	require(published.size() == 1 && published[0].value.fields.size() == 1 &&
	            published[0].value.provenance.observed_tick == 0 && published[0].value.provenance.uncertainty == 4,
	        "explicit publication must preserve only selected facts and original evidence provenance");
	const auto public_reader = runtime.read_ship(13);
	require(public_reader.size() == 1 && public_reader[0].value.fields.size() == 1 &&
	            std::get<std::int64_t>(public_reader[0].value.fields.at(amount)) == 7 &&
	            runtime.read_console(13).empty() && runtime.read_ship(21).empty(),
	        "explicitly published evidence is visible to every same-entity reader while private Console evidence "
	        "and other entities remain isolated");
	input.provenance.observed_tick = 1;
	input.provenance.revision = 2;
	input.fields[amount] = std::int64_t{9};
	runtime.write_console(11, input, 1);
	require(std::get<std::int64_t>(runtime.read_ship(12).back().value.fields.at(amount)) == 9,
	        "live publication must follow supported subsequent observations");
	runtime.stop_publication(publication);
	input.provenance.observed_tick = 2;
	input.provenance.revision = 3;
	input.fields[amount] = std::int64_t{12};
	runtime.write_console(11, input, 2);
	require(std::get<std::int64_t>(runtime.read_ship(12).back().value.fields.at(amount)) == 9,
	        "ending publication must freeze retained evidence rather than refresh it from private truth");
	input.provenance.observed_tick = 0;
	input.provenance.revision = 1;
	require(!runtime.write_console(11, input, 2), "delayed old reports must not replace newer evidence");
	CoordinationInput message;
	message.sender_system = 1003;
	message.origin = ControlOrigin::AI;
	message.destination = {MessageDestinationKind::Controller, 1001};
	message.payload = input;
	message.payload.provenance = {1003, 2, 4, 0, EvidenceKind::Intent};
	message.payload.fields.erase(note);
	message.delay = 2;
	message.request_key = 5;
	const auto message_id = runtime.send(message, 2);
	require(runtime.advance(3).empty(), "Coordination cannot deliver before its simulation-time delay");
	runtime.set_human_control(12, true);
	runtime.set_system_human_control(1001, true);
	const auto queued = runtime.save();
	KnowledgeRuntime resumed(definition);
	resumed.restore(queued);
	const auto delivered = runtime.advance(4), delivered_resumed = resumed.advance(4);
	require(delivered.size() == 1 && delivered[0].message.id == message_id && delivered[0].notify_human &&
	            delivered_resumed.size() == 1 && runtime.save() == resumed.save(),
	        "pending AI reports must resolve current human control at delivery and resume without rerouting or "
	        "duplication");
	runtime.set_system_human_control(1001, false);
	require(runtime.receive(1001).size() == 1 && runtime.receive(1002).empty() && runtime.receive(1001).empty(),
	        "addressed work survives handover but never enters another Controller's inbox");
	require(runtime.message_history(12).size() == 1 && !runtime.message_history(12)[0].notify_human,
	        "reconnecting message history must not replay old human notification cues");
	runtime.private_memory(1001).values[amount] = std::int64_t{99};
	require(runtime.private_memory(1002).values.empty(),
	        "private AI memory must remain independent on a shared Console");
	message.destination = {MessageDestinationKind::Console, 12};
	message.request_key = 0;
	message.payload.provenance.observed_tick = 4;
	message.payload.provenance.revision = 5;
	runtime.send(message, 4);
	runtime.advance(6);
	require(runtime.receive(1001).size() == 1 && runtime.receive(1002).size() == 1 && runtime.receive(1001).empty(),
	        "Console-wide delivery retains independent Controller consumption cursors");
	message.destination = {MessageDestinationKind::Console, 21};
	rejects(
	    [&]
	    {
		    runtime.send(message, 6);
	    },
	    "invalid cross-ship destinations must fail instead of widening into broadcasts");
	require(runtime.stale(runtime.read_ship(12).back(), 5),
	        "retained shared records must become stale using their observation time");
}
} // namespace

int main()
{
	try
	{
		equipment_and_resources();
		overload_and_recovery();
		generation_and_thermal_recovery();
		resource_forecasts_and_fractional_overdrive();
		prepaid_fractional_work();
		trees();
		knowledge();
		std::cout
		    << "Gameplay systems integration passed: composed equipment, resources, trees and scoped Coordination\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
