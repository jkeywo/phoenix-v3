#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

import phoenix.simulation;

namespace
{
using namespace phoenix;
using phoenix::test::load;
using phoenix::test::require;
Json fixture()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "Controller preferences");
	data["schema"] = 4;
	data["consoles"][0]["widgets"] = Json::array();
	data["trees"] = Json::parse(R"json([
 {"id":"test.summary-base","context":"controller","initial":"control",
  "command_bindings":[{"channel":"speed"},{"channel":"enabled","type":"boolean"},{"channel":"destination","type":"boolean","action":"destination"}],
  "command_reads":[{"name":"intent.power","controller":"preferences","channel":"obsolete"},{"name":"intent.cooling","controller":"preferences","channel":"cooling","type":"boolean"}],
  "states":[{"id":"control","running":[
   {"action":"write.destination","arguments":[true,100000,0]},
   {"action":"write.speed","arguments":[{"observation":"intent.power"}]},
   {"action":"write.enabled","arguments":[{"observation":"intent.cooling"}]}]}]},
 {"id":"test.summary-child","extends":"test.summary-base",
  "command_reads":[{"name":"intent.power","controller":"preferences","channel":"power","type":"integer"},{"name":"intent.cooling","controller":"preferences","channel":"cooling","type":"boolean"}]}
 ])json");
	auto &ship = data["entities"][0];
	ship.erase("console");
	ship["motion_model"] = "inertial";
	ship["systems"]["engine"]["kind"] = "engine";
	ship["stations"] = Json::parse(R"json([
 {"id":"bridge","consoles":[{"id":"main","definition":"test.console","systems":[],"feeds":["engine","steering","radar"]}]}
 ])json");
	ship["control_layout"] = Json::parse(R"json({"schema":1,"default_profile":"assisted",
 "controllers":[
  {"id":"preferences","console":"main","fallback":"hold","commands":[
   {"id":"power","action":"intent_integer","initial":1000,"minimum":1000,"maximum":3000,"consumers":[],
    "choices":[{"value":1000,"label_key":"Cruise policy"},{"value":3000,"label_key":"Fast policy"}]},
   {"id":"cooling","action":"intent_boolean","initial":1,"consumers":[]}]},
  {"id":"actuator","console":"main","tree":"test.summary-child","commands":[
   {"id":"speed","action":"speed_mm_s","consumers":["engine"]},
   {"id":"enabled","action":"engine_enabled","initial":1,"consumers":["engine"]},
   {"id":"destination","action":"destination","initial":0,"arguments":[0,0],"consumers":["engine"]}]}],
 "profiles":[{"id":"assisted","consoles":{"main":{"human":["preferences"],"ai":["actuator"]}}}]
 })json");
	data["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.ship"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	return data;
}
SystemView engine(const World &world)
{
	const auto observation = world.observe(100);
	require(observation.has_value(), "Summary experiment lost its claimed Console");
	const auto found = std::find_if(observation->systems.begin(), observation->systems.end(),
	                                [](const auto &system)
	                                {
		                                return system.id == "engine";
	                                });
	require(found != observation->systems.end(), "Summary experiment lost fitted engine telemetry");
	return *found;
}
void advance(World &world)
{
	for (int i = 0; i < 4; ++i)
		world.tick();
}
void refused(Json source, const char *message)
{
	phoenix::test::rejects(
	    [&]
	    {
		    (void)load(source);
	    },
	    message);
}
void workshop_channel_authoring()
{
	auto source = fixture();
	auto drive = source["entities"][0]["systems"]["engine"];
	drive["id"] = "test.drive";
	source["equipment"] = Json::array({drive});
	source["assemblies"] = Json::parse(R"json([{"id":"test.drive-assembly",
 "systems":{"engine":{"template":"test.drive"},"provider":{"kind":"generator","generator":"supply"}},
 "resources":{"stores":[{"id":"reserve","capacity":100,"initial":10,"charge_rate":100,"discharge_rate":100}],
 "generators":[{"id":"supply","store":"reserve","maximum_rate":100,"response_rate":100}]}}])json");
	source["assemblies"][0]["services"] = {
	    {"pump_power", {{"kind", "power"}, {"direction", "input"}, {"target", "reserve"}}}};
	source["assemblies"][0]["resources"]["coolant_sources"] =
	    Json::array({{{"id", "pump"}, {"initial", 10}, {"removal_rate", 10}, {"power_store", "$pump_power"}},
	                 {{"id", "passive"}, {"initial", 10}, {"removal_rate", 10}}});
	source["assemblies"][0]["systems"]["pump_control"] = {
	    {"kind", "coolant"}, {"power_store", "$pump_power"}, {"energy_per_second", 1}, {"cooling_link", "cooling"}};
	source["assemblies"][0]["resources"]["cooling_links"] = Json::array(
	    {{{"id", "cooling"}, {"source", "pump"}, {"store", "reserve"}, {"capacity_rate", 10}, {"allocation_rate", 1}}});

	const auto archive = write_pack(phoenix::test::fixture_pack(json_bytes(source), "test-core"));
	WorkshopDocument document;
	document.load_dependency(archive);
	const auto id = document.create_entity("test.ship");
	auto &ship = document.definitions()["entities"][0];
	ship["systems"] = {{"engine", nullptr}};
	ship["assemblies"] = Json::array({{{"id", "drive"}, {"assembly", "test.drive-assembly"}},
	                                  {{"id", "spare"}, {"assembly", "test.drive-assembly"}}});
	ship["stations"] = source["entities"][0]["stations"];
	ship["stations"][0]["consoles"][0]["feeds"] = {
	    "drive.engine",       "spare.engine",       "drive.provider", "spare.provider",
	    "drive.pump_control", "spare.pump_control", "steering",       "radar"};
	ship["control_layout"] = source["entities"][0]["control_layout"];
	for (auto &command : ship["control_layout"]["controllers"][1]["commands"])
		command["consumers"] = {"drive.engine", "spare.engine"};
	ship["control_layout"]["controllers"][1]["tree"] = "workshop.policy";
	ship["control_layout"]["controllers"][0]["commands"][0]["id"] = "custom_policy";
	document.definitions()["trees"].push_back(
	    {{"id", "workshop.policy"},
	     {"extends", "test.summary-child"},
	     {"command_reads",
	      Json::array({{{"name", "intent.power"}, {"controller", "preferences"}, {"channel", "custom_policy"}},
	                   {{"name", "intent.cooling"},
	                    {"controller", "preferences"},
	                    {"channel", "cooling"},
	                    {"type", "boolean"}}})}});
	const auto before = document.definitions();
	const auto preview = document.preview_equipment();
	const auto bindings = controller_command_reads(preview.trees.at("workshop.policy"), preview);
	require(bindings.size() == 2 && bindings[0].channel == "custom_policy" && bindings[1].channel == "cooling" &&
	            document.definitions() == before,
	        "Workshop preview must preserve inherited Controller reads without editing the draft");
	const auto exported = document.export_pack();
	WorkshopDocument reopened;
	reopened.load_dependency(archive);
	reopened.load_pack(exported);
	const auto verified = reopened.verify();
	const auto &network = verified.resource_definitions.at(id);
	require(network.coolant_sources.at(network.coolant_source_ids.at("drive.pump")).power_store ==
	                network.store_ids.at("drive.reserve") &&
	            network.coolant_sources.at(network.coolant_source_ids.at("drive.passive")).power_store == no_resource,
	        "Workshop Controller export must preserve paid and passive dependency pump bindings");
	require(controller_command_reads(verified.trees.at("workshop.policy"), verified)[0].channel == "custom_policy",
	        "Custom preference reads must survive production export/reopen");
	WorkshopDocument downstream;
	downstream.rename_pack("test-downstream");
	downstream.load_dependency(archive);
	downstream.load_dependency(exported);
	downstream.create_entity(id);
	auto &child = downstream.definitions()["entities"][0];
	child["control_layout"] = ship["control_layout"];
	child["control_layout"]["controllers"][0]["commands"][0]["id"] = "second_policy";
	child["control_layout"]["controllers"][1]["tree"] = "downstream.policy";
	downstream.definitions()["trees"].push_back(
	    {{"id", "downstream.policy"},
	     {"extends", "workshop.policy"},
	     {"command_reads",
	      Json::array({{{"name", "intent.power"}, {"controller", "preferences"}, {"channel", "second_policy"}},
	                   {{"name", "intent.cooling"},
	                    {"controller", "preferences"},
	                    {"channel", "cooling"},
	                    {"type", "boolean"}}})}});
	const auto downstream_preview = downstream.preview_equipment();
	require(controller_command_reads(downstream_preview.trees.at("downstream.policy"), downstream_preview)[0].channel ==
	            "second_policy",
	        "Downstream composed dependency must preserve local Controller binding override");
	ship["control_layout"]["controllers"][0]["commands"][0]["id"] = "unsupported_policy";
	phoenix::test::rejects(
	    [&]
	    {
		    (void)document.export_pack();
	    },
	    "Unsupported imported Controller channel must refuse export");
	require(ship["control_layout"]["controllers"][0]["commands"][0]["id"] == "unsupported_policy",
	        "Refused export must retain the draft");
}
bool preference(World &world, const char *channel, std::int64_t value)
{
	const auto view = world.observe(100);
	for (const auto &controller : view->gameplay.at("controllers"))
		if (controller.at("id") == "preferences")
		{
			OperatingCommand command{1, "", value, {}};
			command.controller = "preferences";
			command.channel = channel;
			command.generation = controller.at("generation").get<std::uint64_t>();
			return world.submit(100, command);
		}
	return false;
}
} // namespace
int main()
{
	try
	{
		workshop_channel_authoring();
		const auto source = fixture();
		const auto catalog = load(source);
		const auto bindings = controller_command_reads(catalog.trees.at("test.summary-child"), catalog);
		require(bindings.size() == 2 && bindings[0].channel == "power" && bindings[1].channel == "cooling",
		        "Named Controller read inheritance must replace a stable slot");
		World world;
		world.load(catalog, "test.map");
		require(world.claim(100, 1), "Could not claim preference Console");
		const auto controls = world.observe(100)->gameplay.at("controls");
		require(std::any_of(controls.begin(), controls.end(),
		                    [](const auto &c)
		                    {
			                    return c.value("label_key", std::string{}) == "Cruise policy";
		                    }),
		        "Authored preference labels must reach controls");
		require(!preference(world, "power", 2000), "In-range unlisted preference choice must be refused");
		advance(world);
		require(engine(world).requested == 1000 && engine(world).enabled,
		        "Default preferences must feed ordinary Controller AI");
		require(preference(world, "power", 3000) && preference(world, "cooling", 0),
		        "Independent preference changes refused");
		advance(world);
		require(engine(world).requested == 3000 && !engine(world).enabled, "Independent preference channels collapsed");
		require(preference(world, "cooling", 1), "Cooling preference change refused");
		advance(world);
		require(engine(world).requested == 3000 && engine(world).enabled, "Cooling change overwrote power preference");
		auto forged = world.save();
		forged["bodies"][0]["console_controllers"]["controllers"][0]["commands"][0]["value"] = 2000;
		const auto before = world.save();
		phoenix::test::rejects(
		    [&]
		    {
			    world.restore(catalog, forged);
		    },
		    "Saved unlisted preference choice must be refused");
		require(world.save() == before, "Refused preference restore preserves live World");
		World resumed;
		resumed.restore(catalog, world.save());
		advance(world);
		advance(resumed);
		require(world.save() == resumed.save(), "Preference and AI continuation must restore exactly");
		auto bad = source;
		bad["trees"][1]["command_reads"][0]["type"] = "boolean";
		refused(bad, "Mismatched preference type admitted");
		bad = source;
		bad["trees"][0]["context"] = "scenario";
		refused(bad, "Controller reads outside Controller context admitted");
		bad = source;
		bad["trees"][1]["command_reads"][0]["channel"] = "unrelated";
		refused(bad, "Unknown preference channel admitted");
		bad = source;
		bad["trees"][1]["command_reads"][0]["controller"] = "foreign";
		refused(bad, "Foreign Controller read admitted");
		bad = source;
		bad["trees"][1]["command_reads"].push_back(bad["trees"][1]["command_reads"][0]);
		refused(bad, "Duplicate read names admitted");
		bad = source;
		bad["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["choices"][1]["value"] = 1000;
		refused(bad, "Duplicate preference choices admitted");
		bad = source;
		bad["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["choices"][1]["value"] = 4000;
		refused(bad, "Out of range preference choice admitted");
		std::cout << "Controller preference production integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
