#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.workshop_test;

using namespace phoenix;
namespace
{
using phoenix::test::require;
WorkshopTestRequest fixture()
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "disposable-test"}};
	const Json definitions{
	    {"schema", 1},
	    {"entities",
	     Json::array(
	         {{{"id", "disposable.ship"},
	           {"render",
	            {{"mode", "camera"}, {"images", Json::array({"disposable-test:assets/pixel.png"})}, {"scale", 1}}},
	           {"systems",
	            {{"engine", {{"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}}},
	             {"steering", {{"turn_mdeg_s", 90000}}}}},
	           {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 1000}}}}})},
	    {"maps",
	     Json::array({{{"id", "disposable.map"},
	                   {"entities", Json::array()},
	                   {"ship_slots", Json::array({{{"id", "bridge"},
	                                                {"x_mm", 0},
	                                                {"y_mm", 0},
	                                                {"heading_mdeg", 0},
	                                                {"allowed_ships", Json::array({"disposable.ship"})}}})}}})}};
	pack.files["definitions.json"] = json_bytes(definitions);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	WorkshopTestRequest result;
	result.pack = write_pack(pack);
	result.mission = "disposable.map";
	result.slot = "bridge";
	result.ship = "disposable.ship";
	return result;
}
WorkshopTestRequest operation(WorkshopTestAction action)
{
	WorkshopTestRequest request;
	request.action = action;
	return request;
}
void directed_template_roundtrip()
{
	auto source = phoenix::test::controller_session_fixture();
	source["effects"] = Json::parse(R"json([
	 {"id":"test.designated","kind":"targeting_policy","label":"Designated contact","policy":"designated"},
	 {"id":"test.scan-base","kind":"directed_mode","label":"Focused scan","operation":"scan",
	  "targeting":["test.designated"],"range_mm":40000,"capabilities":{"basic_identity":7}},
	 {"id":"test.scan-detail","extends":"test.scan-base","label":"Detailed scan","capabilities":{"shield_sectors":11}},
	 {"id":"test.damage","kind":"operations","operations":[{"kind":"damage","target":"protected","damage":2}]},
	 {"id":"test.burning","kind":"periodic","duration_ticks":20,"period_ticks":5,"effect":"test.damage"},
	 {"id":"test.ignite","kind":"operations","operations":[{"kind":"apply","target":"protected","effect":"test.burning","grant":"burning"}]},
	 {"id":"test.payload","kind":"payload","bindings":[{"id":"penetration","outcome":"compartment","positive_damage":true,"effect":"test.ignite"}]}
	])json");
	source["entities"][0]["systems"]["scanner"] = {
	    {"kind", "radar"},
	    {"range_mm", 40000},
	    {"directed", {{"modes", {"test.scan-base", "test.scan-detail"}}, {"targeting", {"test.designated"}}}}};
	WorkshopDocument document;
	document.load_pack(phoenix::test::archive(source));
	require(document.definitions().at("entities")[0].at("control_layout").at("controllers")[0] ==
	            source.at("entities")[0].at("control_layout").at("controllers")[0],
	        "Workshop retains authored Controller fallback instead of orphaned generated policy references");
	const auto preview = document.preview_definition(WorkshopDefinitionKind::Effect, "test.scan-detail");
	require(preview.at("operation") == "scan" && preview.at("targeting")[0] == "test.designated",
	        "Workshop resolves inherited directed operation and target policy references");
	const auto verified = document.verify();
	const auto closure = scenario_definitions(verified, "test.map");
	for (const auto *id : {"test.scan-base", "test.scan-detail", "test.designated"})
		require(std::find(closure.begin(), closure.end(), id) != closure.end(),
		        "scenario dependency closure retains shared directed mode and target policy templates");
	WorkshopDocument reopened;
	reopened.load_pack(document.export_pack());
	const auto result = reopened.verify();
	require(compile_directed(result, "test.controllable").at("scanner").modes.size() == 2 &&
	            compile_payload(result, "test.payload").bindings.size() == 1 &&
	            compile_periodic_effect(result, "test.burning").period_ticks == 5,
	        "Workshop export and reopen preserve directed, payload and periodic catalogue contracts");
}
} // namespace
int main()
{
	try
	{
		directed_template_roundtrip();
		const auto snapshot = fixture();
		GameplayTestHost first, second;
		require(first.submit(snapshot),
		        "disposable test must construct an isolated World through production pack/load paths");
		require(second.submit(snapshot), "independent disposable hosts must support the same authored snapshot");
		first.advance(100);
		require(first.feedback().at("tick") == 2 && second.feedback().at("tick") == 0,
		        "advancing a disposable host must not tick another World");
		require(first.submit(operation(WorkshopTestAction::Pause)), "pause must address the existing disposable World");
		first.advance(250);
		require(first.feedback().at("tick") == 2, "paused disposable test must not advance");
		require(first.submit(operation(WorkshopTestAction::Step)) && first.feedback().at("tick") == 3,
		        "step must execute exactly one real production tick");
		first.advance(100);
		require(first.feedback().at("tick") == 3, "single step must leave the test paused");
		require(first.submit(operation(WorkshopTestAction::Pause)), "pause action toggles resume");
		first.advance(50);
		require(first.feedback().at("tick") == 4, "resumed test must advance from the retained state");
		auto invalid = snapshot;
		invalid.slot = "missing";
		require(!first.submit(std::move(invalid)) && first.feedback().at("tick") == 4,
		        "invalid replacement must report the slot error without replacing the current disposable World");
		require(first.submit(operation(WorkshopTestAction::Restart)) && first.feedback().at("tick") == 0,
		        "restart must rebuild the original exported snapshot rather than the failed replacement");
		require(first.submit(operation(WorkshopTestAction::Stop)) && !first.feedback().at("running").get<bool>(),
		        "stop must dispose of the isolated World");
		require(!first.submit(operation(WorkshopTestAction::Step)), "step without a running test must fail clearly");
		WorkshopDocument document;
		document.load_pack(phoenix::test::archive(phoenix::test::controller_session_fixture()));
		auto &authored = document.definitions();
		authored["trees"] = Json::parse(R"json([
		 {"id":"test.base","context":"controller","initial":"run","command_bindings":[{"channel":"speed"}],
		  "parameters":{"target":{"type":"integer","default":4000}},
		  "states":[{"id":"run","running":[{"action":"write.speed","arguments":[{"parameter":"target"}]}]}]},
		 {"id":"test.child","extends":"test.base","arguments":{"target":6000}}])json");
		auto &layout = authored["entities"][0]["control_layout"];
		layout["default_profile"] = "automatic";
		layout["controllers"][0].erase("fallback");
		layout["controllers"][0]["tree"] = "test.child";
		const auto resolved_child = document.preview_tree(authored["trees"][1]);
		require(resolved_child.at("context") == "controller" &&
		            resolved_child.at("command_bindings")[0].at("channel") == "speed",
		        "Workshop preview resolves inherited Controller command vocabulary");
		const auto verified = document.verify();
		const auto child_program = gameplay_registry(resolved_child, verified, state_tree::Context::Controller);
		require(std::any_of(child_program.actions.begin(), child_program.actions.end(),
		                    [](const auto &action)
		                    {
			                    return action.name == "write.speed";
		                    }),
		        "Workshop palette uses production inherited Controller registry");
		WorkshopDocument reopened;
		reopened.load_pack(document.export_pack());
		require(reopened.definitions().at("entities")[0].at("control_layout") == layout &&
		            reopened.preview_tree(reopened.definitions().at("trees")[1]) == resolved_child,
		        "authored Controller profile and inherited tree survive export/reopen");
		WorkshopTestRequest controller_test;
		controller_test.pack = reopened.export_pack();
		controller_test.mission = "test.map";
		controller_test.slot = "test.slot";
		controller_test.ship = "test.controllable";
		require(first.submit(controller_test), "Workshop Test accepts reopened explicit Controller document");
		first.advance(100);
		require(first.feedback().at("tick") == 2 && first.feedback().at("host_diagnostics").empty(),
		        "reopened Controller tree executes through production disposable World");
		const auto feedback = first.feedback();
		const auto &controller_rows = feedback.at("controllers");
		const auto inspected = std::find_if(controller_rows.begin(), controller_rows.end(),
		                                    [](const auto &row)
		                                    {
			                                    return row.value("context", "") == "controller" &&
			                                           row.value("controller", "") == "propulsion";
		                                    });
		require(inspected != controller_rows.end(), "Workshop Test exposes explicit Controller inspection");
		require(inspected->at("console") == "drive" && inspected->at("operator") == "ai" &&
		            inspected->at("tree") == "test.child" && inspected->at("active_states") == Json::array({"run"}) &&
		            inspected->at("diagnostics").empty(),
		        "Workshop inspection identifies active inherited Controller and its effective operator");
		const auto &inspected_command = inspected->at("commands")[0];
		require(inspected_command.at("channel") == "speed" && inspected_command.at("value") == 6000 &&
		            inspected_command.at("revision").get<std::uint64_t>() > 0 &&
		            inspected_command.at("consumed") == inspected_command.at("revision"),
		        "Workshop inspection shows accepted Controller command consumed by production systems");
		auto &bad_tree = reopened.definitions()["trees"][0];
		bad_tree["command_bindings"][0]["channel"] = "foreign";
		phoenix::test::rejects(
		    [&]
		    {
			    reopened.verify();
		    },
		    "Workshop verification refuses foreign Controller channel");
		phoenix::test::rejects(
		    [&]
		    {
			    reopened.export_pack();
		    },
		    "Workshop export refuses invalid Controller document");
		controller_test.pack = phoenix::test::archive(reopened.definitions());
		require(!first.submit(controller_test) && first.feedback().at("tick") == 2,
		        "invalid Controller Test replacement retains previous disposable World");
		std::cout << "disposable Workshop integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
