#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include "../support/active_effects.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
import phoenix.simulation;
import phoenix.gameplay_scenario;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	auto &ship = source["entities"][0];
	source["maps"][0]["entities"] = Json::array({source["maps"][0]["entities"][0]});
	source["resources"] = Json::array(
	    {{{"id", "fixture.resource.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}}});
	source["properties"] = Json::parse(R"json([
 {"id":"test.work","kind":"quantity","unit":"work","default":0,"maximum":1000000000},
 {"id":"test.events","kind":"quantity","unit":"events","default":0,"maximum":1000000000},
 {"id":"test.fuel","kind":"quantity","unit":"fuel","default":100,"maximum":1000000},
 {"id":"test.rate","kind":"attribute","unit":"permille","default":1000,"maximum":100000},
 {"id":"test.scale","kind":"attribute","unit":"permille","default":1000,"maximum":100000},
 {"id":"test.allowed","kind":"flag","default":true}
 ])json");
	Json properties = Json::object();
	for (const auto &definition : source["properties"])
		properties[definition.at("id").get<std::string>()] = Json::object();
	ship["properties"] = properties;
	ship["systems"]["radar"]["properties"] = properties;
	ship["combat"]["decks"][0]["compartments"][0]["properties"] = properties;
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"resource", "fixture.resource.energy"},
	                                              {"capacity", 1000},
	                                              {"initial", 1000},
	                                              {"charge_rate", 1000},
	                                              {"discharge_rate", 1000},
	                                              {"stop_heat", 100000},
	                                              {"recover_heat", 50000}}})}};
	ship["personnel"] = {
	    {"departments", Json::array({{{"id", "watch"}, {"label", "Watch"}, {"station", "operations"}}})},
	    {"traits", Json::array()},
	    {"duties", Json::array()},
	    {"officers", Json::array({{{"id", "a"},
	                               {"name", "A"},
	                               {"department", "watch"},
	                               {"compartment", "fore"},
	                               {"traits", Json::array()},
	                               {"properties", properties}}})}};
	for (const auto &controller : ship["control_layout"]["controllers"])
		ship["personnel"]["duties"].push_back({{"id", controller.at("id")},
		                                       {"label", controller.at("id")},
		                                       {"department", "watch"},
		                                       {"compartment", "fore"},
		                                       {"console", controller.at("console")},
		                                       {"controller", controller.at("id")}});
	for (const auto *team : {"team-a", "team-b"})
		ship["personnel"]["duties"].push_back({{"id", std::string(team) + "-duty"},
		                                       {"label", team},
		                                       {"department", "watch"},
		                                       {"compartment", "fore"},
		                                       {"team", team}});
	source["effects"] = Json::parse(R"json([
 {"id":"test.process.start","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.events","amount":1}]},
 {"id":"test.process.pause","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.events","amount":10}]},
 {"id":"test.process.resume","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.events","amount":100}]},
 {"id":"test.process.stop","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.events","amount":1000}]},
 {"id":"test.process.work","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.work","amount":0}]},
 {"id":"test.process.tick","kind":"process","label":"Synthetic work","native":"effects","start_effect":"test.process.start","work_effect":"test.process.work",
 "work_values":{"0":{"amount":{"fact":"event.work"}}}}
 ])json");
	return source;
}
Json &effect(Json &source, const std::string &id)
{
	for (auto &definition : source["effects"])
		if (definition.at("id") == id)
			return definition;
	throw std::runtime_error("Missing process fixture template " + id);
}
void ticks(World &world, int count = 1)
{
	while (count-- > 0)
		world.tick();
}
Json owner(const World &world, const std::string &equipment = {}, const std::string &officer = {}, int compartment = -1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("processes"))
		if (row.at("entity") == 1 && row.at("equipment") == equipment && row.at("officer") == officer &&
		    row.at("compartment") == compartment)
			return row.at("state");
	throw std::runtime_error("Missing process owner " + equipment + "/" + officer + "/" + std::to_string(compartment));
}
Json state(const World &world, const std::string &slot, const std::string &equipment = {},
           const std::string &officer = {}, int compartment = -1)
{
	return owner(world, equipment, officer, compartment).at("processes").at(slot);
}
Json property(const World &world, const std::string &id, const std::string &equipment = {},
              const std::string &officer = {}, int compartment = -1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("custom_properties"))
		if (row.at("entity") == 1 && row.at("equipment") == equipment && row.at("officer") == officer &&
		    row.at("compartment") == compartment)
			return row.at("values").at(id);
	throw std::runtime_error("Missing process property owner");
}
Json body(const World &world)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("bodies"))
		if (row.at("body").at("id") == 1)
			return row;
	throw std::runtime_error("Missing process body");
}
std::int64_t energy(const World &world, std::size_t index = 0)
{
	return body(world)
	    .at("resources")
	    .at("network")
	    .at("stores")
	    .at(index)
	    .at("stock")
	    .at("quantities")
	    .value("fixture.resource.energy", std::int64_t{0});
}
Json portable(Json saved)
{
	saved.erase("packs");
	return saved;
}
void controls(Json &source, const Json &endpoints)
{
	auto &ship = source["entities"][0];
	ship["process_controls"] = endpoints;
	Json commands = Json::array();
	for (const auto &[id, binding] : endpoints.items())
		commands.push_back({{"id", binding.at("enabled")},
		                    {"action", "intent_boolean"},
		                    {"initial", 1},
		                    {"consumers", Json::array()}});
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "processes"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["drive"]["human"].push_back("processes");
	ship["personnel"]["duties"].push_back({{"id", "processes"},
	                                       {"label", "Processes"},
	                                       {"department", "watch"},
	                                       {"compartment", "fore"},
	                                       {"console", "drive"},
	                                       {"controller", "processes"}});
}
void command(World &world, const std::string &controller, const std::string &channel, std::int64_t value)
{
	const auto view = world.observe(101);
	require(view.has_value(), "Process fixture retains its claimed Console");
	for (const auto &row : view->gameplay.at("controllers"))
		if (row.at("id") == controller)
		{
			require(world.submit(101, OperatingCommand{1, {}, value, {}, controller, channel, row.at("generation")}),
			        "Ordinary process control command admitted");
			return;
		}
	throw std::runtime_error("Missing process Controller " + controller);
}
void scenario(Json &source, const std::vector<std::pair<int, std::string>> &groups)
{
	Json states = Json::array();
	for (std::size_t index = 0; index < groups.size(); ++index)
	{
		Json waiting{{"id", "wait" + std::to_string(index)},
		             {"transitions", Json::array({{{"target", "apply" + std::to_string(index)},
		                                           {"conditions", Json::array({{{"left", {{"observation", "tick"}}},
		                                                                        {"op", "ge"},
		                                                                        {"right", groups[index].first}}})}}})}};
		Json action{{"id", "apply" + std::to_string(index)},
		            {"entry", Json::array({{{"action", "apply_effect_group"},
		                                    {"arguments", Json::array({{{"type", "entity"}, {"id", 1}},
		                                                               {{"type", "entity"}, {"id", 1}},
		                                                               groups[index].second})}}})}};
		if (index + 1 < groups.size())
			action["transitions"] = Json::array({{{"target", "wait" + std::to_string(index + 1)}}});
		states.push_back(waiting);
		states.push_back(action);
	}
	source["trees"].push_back(
	    {{"id", "test.process.scenario"}, {"context", "scenario"}, {"initial", "wait0"}, {"states", states}});
	source["maps"][0]["scenario"] = {{"tree", "test.process.scenario"}};
}
Json four_owners_and_travel()
{
	auto source = fixture();
	auto &ship = source["entities"][0];
	ship["processes"] = {{"actor", {{"process", "test.process.tick"}}},
	                     {"disabled", {{"process", "test.process.tick"}, {"requested", false}}}};
	ship["systems"]["radar"]["processes"] = {{"equipment", {{"process", "test.process.tick"}}}};
	ship["combat"]["decks"][0]["compartments"][0]["processes"] = {{"compartment", {{"process", "test.process.tick"}}}};
	ship["personnel"]["officers"][0]["processes"] = {{"officer", {{"process", "test.process.tick"}}},
	                                                 {"qualified", {{"process", "test.process.qualified"}}}};
	auto qualified = effect(source, "test.process.tick");
	qualified["id"] = "test.process.qualified";
	qualified["requirements"] = {
	    {"op", "all"},
	    {"args", Json::array({{{"fact", "owner.present"}}, {{"fact", "owner.healthy"}}, {{"fact", "owner.on_duty"}}})}};
	source["effects"].push_back(qualified);
	for (auto &duty : ship["personnel"]["duties"])
		if (duty.at("id") == "radar")
			duty["compartment"] = "aft";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim all-owner process fixture");
	ticks(world, 3);
	require(property(world, "test.work").get<std::int64_t>() > 0 &&
	            property(world, "test.work", "radar").get<std::int64_t>() > 0 &&
	            property(world, "test.work", {}, "a").get<std::int64_t>() > 0 &&
	            property(world, "test.work", {}, {}, 0).get<std::int64_t>() > 0,
	        "Explicit actor, equipment, officer and compartment processes all autostart through native owners");
	require(state(world, "disabled").at("requested") == false && state(world, "disabled").at("admitted") == false &&
	            property(world, "test.events") == 1,
	        "Explicit initially-disabled process performs no entry or work while independent autostart continues");
	require(state(world, "qualified", {}, "a").at("admitted") == false && property(world, "test.events", {}, "a") == 1,
	        "Officer process has no implicit duty requirement; separately authored qualification blocks entry");
	const auto run = state(world, "officer", {}, "a").at("run");
	const auto before = property(world, "test.work", {}, "a").get<std::int64_t>();
	OperatingCommand assign{1, "personnel.assign", 0, "a"};
	assign.text = "radar";
	assign.generation = world.observe(101)->gameplay.at("personnel").at("revision");
	require(world.submit(101, assign), "Process owner reassignment uses ordinary Personnel command");
	ticks(world, 2);
	require(property(world, "test.work", {}, "a").get<std::int64_t>() > before &&
	            state(world, "officer", {}, "a").at("run") == run &&
	            state(world, "qualified", {}, "a").at("admitted") == false,
	        "Travel preserves default officer work and run identity while explicit presence requirement stays blocked");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 12);
	ticks(resumed, 12);
	require(state(world, "qualified", {}, "a").at("admitted") == true && property(world, "test.events", {}, "a") == 2,
	        "Arrival starts the explicitly qualified peer without restarting the independent officer process");
	require(world.save() == resumed.save(), "All-owner process identities and travelling eligibility continue exactly");
	return portable(world.save());
}
Json lifecycle_and_exclusions()
{
	auto source = fixture();
	auto &process = effect(source, "test.process.tick");
	process["requirements"] = {{"fact", "owner.property"}, {"property", "test.allowed"}};
	process["entry_costs"] = Json::array({{{"payer", "owner"}, {"property", "test.fuel"}, {"amount", 3}}});
	process["pause_effect"] = "test.process.pause";
	process["resume_effect"] = "test.process.resume";
	process["stop_effect"] = "test.process.stop";
	source["entities"][0]["processes"] = {{"main", {{"process", "test.process.tick"}}}};
	controls(source,
	         {{"main", {{"source", "entity"}, {"process", "main"}, {"controller", "processes"}, {"enabled", "main"}}}});
	source["effects"].push_back(
	    {{"id", "test.block"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_set"}, {"target", "protected"}, {"property", "test.allowed"}, {"value", false}}})}});
	source["effects"].push_back(
	    {{"id", "test.unblock"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_set"}, {"target", "protected"}, {"property", "test.allowed"}, {"value", true}}})}});
	scenario(source, {{4, "test.block"}, {9, "test.unblock"}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "Claim process lifecycle fixture");
	ticks(world, 3);
	const auto run = state(world, "main").at("run");
	require(property(world, "test.fuel") == 97 && property(world, "test.events") == 1,
	        "Entry costs and start effect commit once before continuous work");
	ticks(world, 4);
	require(state(world, "main").at("phase") == "paused" && state(world, "main").at("run") == run &&
	            property(world, "test.fuel") == 97,
	        "Temporary unmet requirement pauses the same paid run");
	const auto work = property(world, "test.work");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world);
	ticks(resumed);
	require(property(world, "test.work") == work, "Blocked process commits no work effects");
	ticks(world, 5);
	ticks(resumed, 5);
	require(state(world, "main").at("phase") == "running" && state(world, "main").at("run") == run &&
	            property(world, "test.fuel") == 97 && property(world, "test.events") == 111,
	        "Resuming preserves identity and entry payment while executing authored pause/resume effects once");
	command(world, "processes", "main", 0);
	command(resumed, "processes", "main", 0);
	ticks(world);
	ticks(resumed);
	require(state(world, "main").at("phase") == "off" && property(world, "test.events") == 1111,
	        "Explicit stop terminates the run and emits its stop consequence once");
	command(world, "processes", "main", 1);
	command(resumed, "processes", "main", 1);
	ticks(world);
	ticks(resumed);
	require(state(world, "main").at("run") != run && property(world, "test.fuel") == 94,
	        "A deliberate later start has a fresh run and pays entry again");
	require(world.save() == resumed.save(), "Paused and terminated process lifecycle continues exactly");
	auto conflict = fixture();
	auto first = effect(conflict, "test.process.tick");
	first["id"] = "test.exclusive";
	first["exclusions"] = Json::array({{{"id", "shared"}, {"scope", "source"}}});
	conflict["effects"].push_back(first);
	conflict["entities"][0]["processes"] = {{"first", {{"process", "test.exclusive"}}},
	                                        {"second", {{"process", "test.exclusive"}}},
	                                        {"independent", {{"process", "test.process.tick"}}}};
	controls(
	    conflict,
	    {{"first", {{"source", "entity"}, {"process", "first"}, {"controller", "processes"}, {"enabled", "first"}}}});
	const auto shared = load(conflict);
	World exclusive;
	exclusive.load(shared, "test.map");
	require(exclusive.claim(101, 1, "drive"), "Claim process exclusion fixture");
	ticks(exclusive, 2);
	require(state(exclusive, "first").at("admitted") == true && state(exclusive, "second").at("admitted") == false &&
	            state(exclusive, "independent").at("admitted") == true,
	        "Explicit exclusions reject only the conflicting process while independent peer continues");
	command(exclusive, "processes", "first", 0);
	ticks(exclusive, 2);
	require(state(exclusive, "second").at("admitted") == true,
	        "Stopping the exclusion holder admits the still-requested peer");
	return Json::array({portable(world.save()), portable(exclusive.save())});
}
#include "../support/process_runtime_cases.hpp"
#include "../support/process_native_cases.hpp"
#include "../support/process_presentation_cases.hpp"
} // namespace
int main(int argc, char **argv)
{
	try
	{
		std::vector<std::string> failures;
		const auto run = [&](const char *name, const auto &test) -> Json
		{
			try
			{
				return test();
			}
			catch (const std::exception &error)
			{
				failures.push_back(std::string(name) + ": " + error.what());
				return nullptr;
			}
		};
		const Json trace = {run("four owners and travel", four_owners_and_travel),
		                    run("lifecycle and exclusions", lifecycle_and_exclusions),
		                    run("mixed funding and rollback", mixed_funding_and_rollback),
		                    run("bounded process prices", bounded_process_prices),
		                    run("unpaid exit consequences", unpaid_exit_consequences),
		                    run("temporary grants", temporary_grants),
		                    run("granted owner selectors", granted_owner_selectors),
		                    run("competing and invalid grants", competing_and_invalid_grants),
		                    run("active process removal", active_process_removal),
		                    run("maintained process grants", maintained_process_grants),
		                    run("process fractional rate lifetime", process_fractional_rate_lifetime),
		                    run("generation boundaries", generation_boundaries),
		                    run("native payments and modifiers", native_work_payments_and_modifiers),
		                    run("producer electrical inputs", producer_electrical_inputs),
		                    run("basic propulsion work", basic_propulsion_work),
		                    run("destroyed process owner", destroyed_process_owner),
		                    run("thermal and cooling", thermal_and_cooling),
		                    run("competing thermal work", competing_thermal_work),
		                    run("processor commitments", processor_commitments),
		                    run("permitted process information", permitted_process_information),
		                    run("process rule visibility", process_rule_visibility)};
		if (!failures.empty())
		{
			std::string message;
			for (const auto &failure : failures)
				message += failure + "\n";
			throw std::runtime_error(message);
		}
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << '\n';
		else
			std::cout << "Shared process integration checks passed\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
