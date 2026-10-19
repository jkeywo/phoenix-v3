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
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
Catalog load(const Json &definition)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	pack.files["definitions.json"] = json_bytes(definition);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	return resolve_packs({read_pack(write_pack(pack))});
}
Json fixture(bool summaries = true)
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"),
	                       "synthetic summary binding fixture");
	data["schema"] = 4;
	data["consoles"][0]["widgets"] = Json::array();
	data["knowledge"] = Json::parse(R"json({
		"roles":["test.crew"],
		"schemas":[{"id":"test.telemetry","producers":["test.crew"],"readers":["test.crew"],
			"fields":[{"id":"requested","type":"integer"}]}]
	})json");
	data["trees"] = Json::parse(R"json([
		{"id":"test.summary-base","context":"system","initial":"control",
		 "record_bindings":[{"name":"known.requested","schema":"test.telemetry","field":"requested","scope":"console"}],
		 "summary_bindings":[{"name":"intent.power","channel":"obsolete"},{"name":"intent.cooling","channel":"cooling"}],
		 "states":[
			{"id":"control","initial":"enabled","running":[
				{"action":"set_destination","arguments":[{"observation":"self.system"},{"type":"region","id":1},100000,0]},
				{"action":"set_speed","arguments":[{"observation":"self.system"},{"observation":"intent.power"}]}],
			 "transitions":[{"target":"wrong-legacy","conditions":[{"left":{"observation":"summary.level"},"op":"present"}]}]},
			{"id":"enabled","parent":"control","running":[{"action":"set_enabled","arguments":[{"observation":"self.system"},true]}],
			 "transitions":[{"target":"disabled","conditions":[{"left":{"observation":"intent.cooling"},"op":"eq","right":0}]}]},
			{"id":"disabled","parent":"control","running":[{"action":"set_enabled","arguments":[{"observation":"self.system"},false]}],
			 "transitions":[{"target":"enabled","conditions":[{"left":{"observation":"intent.cooling"},"op":"eq","right":1}]}]},
			{"id":"wrong-legacy","running":[{"action":"set_speed","arguments":[{"observation":"self.system"},0]}]}
		 ]},
		{"id":"test.summary-child","extends":"test.summary-base",
		 "summary_bindings":[{"name":"intent.power","channel":"power","type":"integer"}]}
	])json");
	if (!summaries)
		data["trees"][0]["states"][0]["running"][1]["arguments"][1] = 1000;
	auto &ship = data["entities"][0];
	ship.erase("console");
	ship["stations"] = Json::parse(R"json([
		{"id":"bridge","consoles":[
			{"id":"main","definition":"test.console","systems":["engine","steering","radar"],
			 "knowledge_roles":["test.crew"]}
		]}
	])json");
	ship["systems"]["engine"]["kind"] = "engine";
	ship["controllers"] = {{"engine", "test.summary-child"}};
	ship["default_preset"] = "assisted";
	ship["presets"] = Json::parse(R"json([
		{"id":"assisted","consoles":{"main":{"direct":["steering","radar"],"delegated":["engine"],
		 "summary_intents":[
			{"system":"engine","channel":"power","levels":[1000,3000],"default":1000},
			{"system":"engine","channel":"cooling","levels":[0,1],"default":1}
		 ]}}}
	])json");
	ship["presets"][0]["consoles"]["main"]["summary_intents"][0]["level_labels"] =
	    Json::array({"Cruise policy", "Fast policy"});
	if (!summaries)
		ship["presets"][0]["consoles"]["main"].erase("summary_intents");
	data["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.ship"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	data["maps"][0]["regions"] =
	    Json::array({{{"id", "goal"}, {"shape", "circle"}, {"x_mm", 100000}, {"y_mm", 0}, {"radius_mm", 100}}});
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
	bool failed = false;
	try
	{
		(void)load(source);
	}
	catch (const std::exception &)
	{
		failed = true;
	}
	require(failed, message);
}
} // namespace
int main()
{
	try
	{
		const auto source = fixture();
		const auto catalog = load(source);
		const auto bindings = gameplay_summary_bindings(catalog.trees.at("test.summary-child"), catalog);
		require(bindings.size() == 2 && bindings[0].name == "intent.power" && bindings[0].channel == "power" &&
		            bindings[1].channel == "cooling",
		        "Summary inheritance did not replace named binding at its stable slot");
		const auto &program = catalog.tree_programs.at("test.summary-child");
		const auto offset = static_cast<std::size_t>(GameplayTreeObservation::Count) +
		                    gameplay_record_bindings(catalog.trees.at("test.summary-child"), catalog).size();
		require(program.registry.observations.at(offset).name == "intent.power" &&
		            program.registry.observations.at(offset + 1).name == "intent.cooling",
		        "Summary observations were not appended after record observations");
		World world;
		world.load(catalog, "test.map");
		require(world.claim(100, 1), "Could not claim summary experiment Console");
		const auto controls = world.observe(100)->gameplay.at("controls");
		require(std::any_of(controls.begin(), controls.end(),
		                    [](const auto &c)
		                    {
			                    return c.at("label") == "Cruise policy";
		                    }),
		        "Authored summary labels must reach player controls rather than exposing only numeric levels");
		advance(world);
		require(engine(world).requested == 1000 && engine(world).enabled,
		        "Named default summary values did not reach ordinary delegated AI");
		require(world.submit(100, {1, "summary.power", 3000, "engine"}) &&
		            world.submit(100, {1, "summary.cooling", 0, "engine"}),
		        "Independent authored summary intents were not admitted");
		advance(world);
		require(engine(world).requested == 3000 && !engine(world).enabled,
		        "Two channels collapsed into the same summary value");
		require(world.submit(100, {1, "summary.cooling", 1, "engine"}), "Cooling summary change was refused");
		advance(world);
		require(engine(world).requested == 3000 && engine(world).enabled,
		        "Changing cooling intent overwrote independent power intent");
		World resumed;
		resumed.restore(catalog, world.save());
		resumed.clear_claims();
		require(resumed.claim(100, 1), "Summary continuation could not reclaim Console");
		advance(resumed);
		require(engine(resumed).requested == 3000 && engine(resumed).enabled,
		        "Restoring summary continuation lost its named channels");
		const auto navigation = load(fixture(false));
		World destination;
		destination.load(navigation, "test.map");
		require(destination.claim(100, 1), "Could not claim destination-only summary experiment");
		advance(destination);
		bool has_coordinates = false;
		const auto navigation_state = destination.save();
		for (const auto &body : navigation_state.at("bodies"))
			for (const auto &system : body.value("gameplay", Json::array()))
				if (system.at("id") == "engine")
					has_coordinates = system.at("summary").contains("destination.x") &&
					                  system.at("summary").contains("destination.y") &&
					                  system.at("summary").size() == 2;
		require(has_coordinates, "Destination-only experiment did not execute its ordinary destination command");
		require(engine(destination).requested == 1000,
		        "Destination coordinate channels were exposed as legacy summary.level");
		auto bad = source;
		bad["trees"][1]["summary_bindings"][0]["name"] = "known.requested";
		refused(bad, "Production loader admitted summary/record observation name collision");
		bad = source;
		bad["trees"][1]["summary_bindings"][0]["type"] = "boolean";
		refused(bad, "Production loader admitted noninteger summary binding");
		bad = source;
		bad["trees"][0]["context"] = "scenario";
		refused(bad, "Production loader admitted summary binding outside System context");
		bad = source;
		bad["trees"][1]["summary_bindings"][0]["channel"] = "unrelated";
		refused(bad, "Production loader admitted custom summary channel absent from delegated controller contract");
		bad = source;
		bad["trees"][1]["summary_bindings"][0]["channel"] = "destination.x";
		refused(bad, "Production loader admitted reserved destination channel binding");
		bad = source;
		bad["entities"][0]["presets"][0]["consoles"]["main"]["summary_intents"][0]["channel"] = "destination.y";
		refused(bad, "Production loader admitted reserved destination summary control");
		bad = source;
		bad["entities"][0]["presets"][0]["consoles"]["main"]["summary_intents"][0]["levels"] =
		    Json::array({1000, 1000});
		refused(bad, "Production loader admitted duplicate summary levels");
		bad = source;
		bad["trees"][1]["summary_bindings"].push_back(bad["trees"][1]["summary_bindings"][0]);
		refused(bad, "Production loader admitted duplicate summary binding names");
		std::cout << "Named summary binding production integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
