#include "../support/fixtures.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import phoenix.simulation;
import phoenix.gameplay_scenario;
import phoenix.session.local;

namespace
{
using namespace phoenix;
void check(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
Json action(std::string name, Json args)
{
	return {{"action", std::move(name)}, {"arguments", std::move(args)}};
}
Catalog load_fixture(const Json &data)
{
	auto pack = phoenix::test::fixture_pack(json_bytes(data), "test-core");
	return resolve_packs({read_pack(write_pack(pack))});
}
void competition_resolution()
{
	Json map = {{"regions",
	             Json::array({{{"id", "finish"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 100}}})}};
	for (int actor = 1; actor <= 2; ++actor)
		map["scenario"]["objectives"].push_back({{"id", "race-" + std::to_string(actor)},
		                                         {"type", "reach_region"},
		                                         {"region", "finish"},
		                                         {"competition", "race"},
		                                         {"actor", {{"entity", actor}}},
		                                         {"recipients", {{"ship", {{"entity", actor}}}}},
		                                         {"title_key", "Finish"}});
	for (const bool tie : {false, true})
	{
		ScenarioRuntime runtime(compile_scenario(map, {}));
		const auto instance = runtime.create_instance("race");
		runtime.begin_tick(100, {}, {{1, {-1000, 0}}, {2, {-1000, 0}}});
		for (int actor = 1; actor <= 2; ++actor)
			runtime.add_objective({instance, 1, static_cast<std::uint32_t>(actor)}, "race-" + std::to_string(actor),
			                      "ship-" + std::to_string(actor));
		ScenarioRuntime restored(compile_scenario(map, {}));
		restored.restore(runtime.save());
		const std::vector<ScenarioMotionSegment> movement{{1, {-1000, 0}, {1000, 0}},
		                                                  {2, {-1000, 0}, {tie ? 1000 : -500, 0}}};
		for (auto *r : {&runtime, &restored})
		{
			r->evaluate(movement);
			check(r->mission_complete(), "Race did not finish after swept arrival");
			for (const auto &[id, objective] : r->objectives())
				check(objective.outcome == (tie                           ? "draw"
				                            : objective.actor.entity == 1 ? "won"
				                                                          : "lost"),
				      "Race result depends on iteration order");
		}
		check(runtime.save() == restored.save(), "Race recovery diverged");
		const auto count = runtime.pending_publications().size();
		runtime.evaluate(movement);
		check(count == runtime.pending_publications().size(), "Race result was published twice");
	}
}
void delayed_competition()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "delayed race fixture");
	auto &map = data["maps"][0];
	map["entities"] = Json::array();
	map["ship_slots"][0]["x_mm"] = -10000;
	map["ship_slots"].push_back(
	    {{"id", "other"}, {"allowed_ships", {"test.ship"}}, {"x_mm", -10000}, {"y_mm", 20000}, {"heading_mdeg", 0}});
	map["regions"] =
	    Json::array({{{"id", "finish"}, {"shape", "circle"}, {"x_mm", 10000}, {"y_mm", 0}, {"radius_mm", 1000}}});
	map["scenario"] = {{"tree", "test.race-a"},
	                   {"bindings", {{"actor", {{"slot", "test.slot"}}}}},
	                   {"situations", Json::array({{{"id", "other"},
	                                                {"tree", "test.race-b"},
	                                                {"bindings", {{"actor", {{"slot", "other"}}}}},
	                                                {"start", true}}})}};
	for (const auto &[name, slot] :
	     std::vector<std::pair<std::string, std::string>>{{"a", "test.slot"}, {"b", "other"}})
	{
		map["scenario"]["objectives"].push_back({{"id", name},
		                                         {"type", "reach_region"},
		                                         {"region", "finish"},
		                                         {"competition", "race"},
		                                         {"actor", {{"slot", slot}}},
		                                         {"recipients", {{"ship", {{"slot", slot}}}}},
		                                         {"title_key", "Finish"}});
		data["trees"].push_back(
		    {{"id", "test.race-" + name},
		     {"context", "scenario"},
		     {"initial", "wait"},
		     {"states",
		      Json::array({{{"id", "wait"},
		                    {"transitions",
		                     Json::array({{{"target", "run"},
		                                   {"conditions", Json::array({{{"left", {{"observation", "scenario.elapsed"}}},
		                                                                {"op", "ge"},
		                                                                {"right", 100}}})}}})}},
		                   {{"id", "run"},
		                    {"entry", Json::array({action(
		                                  "add_objective",
		                                  Json::array({name, {{"observation", "self.entity"}}, "finish"}))})}}})}});
	}
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}, {"other", "test.ship"}});
	for (int i = 0; i < 100; ++i)
		world.tick();
	check(world.save().at("scenario").at("runtime").at("objectives").empty(),
	      "race objective appeared before five seconds");
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	check(world.save() == resumed.save(), "five-second delay changed after recovery");
	const auto objectives = world.save().at("scenario").at("runtime").at("objectives");
	check(objectives.size() == 2 && objectives[0].at("created") == 100 && objectives[1].at("created") == 100,
	      "both race objectives must activate at five seconds");
}
Catalog fixture(bool fitted = false)
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic World scenario");
	data["entities"][0]["systems"]["engine"] = {{"max_speed_mm_s", 100000}, {"acceleration_mm_s2", 2000000}};
	if (fitted)
	{
		data["entities"][0]["systems"]["engine"]["kind"] = "engine";
		data["entities"][0]["systems"]["engine"]["power_store"] = "cell";
		data["entities"][0]["systems"]["engine"]["power_rate"] = 20;
		data["entities"][0]["resources"] = Json::parse(
		    R"({"stores":[{"id":"cell","capacity":1000,"initial":1000,"charge_rate":100,"discharge_rate":100}]})");
	}
	auto &map = data["maps"][0];
	map["entities"] = Json::array(
	    {{{"id", 1}, {"definition", "test.variant"}, {"x_mm", 1000000}, {"y_mm", 20000}, {"heading_mdeg", 0}}});
	map["ship_slots"][0]["x_mm"] = -2000;
	map["regions"] =
	    Json::array({{{"id", "destination"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 100}}});
	map["scenario"] = {
	    {"tree", "test.scenario"},
	    {"bindings", {{"actor", {{"slot", "test.slot"}}}}},
	    {"parameters", {{"spawn_y", 10000}}},
	    {"objectives", Json::array({{{"id", "reach"},
	                                 {"type", "reach_region"},
	                                 {"region", "destination"},
	                                 {"actor", {{"slot", "test.slot"}}},
	                                 {"recipients", {{"ship", {{"slot", "test.slot"}}}}},
	                                 {"title_key", "test.objective.reach"}}})},
	    {"situations",
	     Json::array({{{"id", "upper-traffic"}, {"tree", "test.spawner"}, {"parameters", {{"spawn_y", 10000}}}},
	                  {{"id", "lower-traffic"}, {"tree", "test.spawner"}, {"parameters", {{"spawn_y", -10000}}}}})}};
	data["trees"] = Json::array(
	    {{{"id", "test.scenario"},
	      {"context", "scenario"},
	      {"initial", "travelling"},
	      {"states",
	       Json::array(
	           {{{"id", "travelling"},
	             {"entry", Json::array({action("start_situation", Json::array({"upper-traffic", "upper"})),
	                                    action("start_situation", Json::array({"lower-traffic", "lower"})),
	                                    action("add_objective",
	                                           Json::array({"reach", {{"observation", "self.entity"}}, "arrival"}))})},
	             {"transitions",
	              Json::array({{{"target", "complete"},
	                            {"conditions", Json::array({{{"left", {{"observation", "objective.state"}}},
	                                                         {"op", "eq"},
	                                                         {"right", 2}}})}}})}},
	            {{"id", "complete"},
	             {"entry", Json::array({action("finish_scenario", Json::array({true, "arrived"}))})}}})}},
	     {{"id", "test.spawner"},
	      {"context", "scenario"},
	      {"parameters", {{"spawn_y", {{"type", "integer"}, {"default", 10000}}}}},
	      {"initial", "spawn"},
	      {"states",
	       Json::array(
	           {{{"id", "spawn"},
	             {"running",
	              Json::array({action(
	                  "spawn_entity",
	                  Json::array({"test.variant", "", 50000, {{"parameter", "spawn_y"}}, 0, "traffic"}))})}}})}}});
	return load_fixture(data);
}
Json reader_fixture()
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "scoped scenario record reader");
	data["schema"] = 4;
	data["consoles"][0]["widgets"] = Json::array();
	auto &ship = data["entities"][0];
	ship.erase("console");
	ship["stations"] = Json::parse(R"json([
		{"id":"bridge","consoles":[
			{"id":"main","definition":"test.console","systems":["engine","steering"],"knowledge_roles":["test.reader"]},
			{"id":"private","definition":"test.console","systems":["radar"],"knowledge_roles":["test.private"]}
		]}
	])json");
	data["knowledge"] = Json::parse(R"json({"roles":["test.reader","test.private"],"schemas":[
		{"id":"test.signal","producers":["phoenix.scenario"],"readers":["test.reader"],"ship_publishable":true,"stale_after_ticks":1,
		 "fields":[{"id":"amount","type":"integer","publishable":true},{"id":"flag","type":"boolean","publishable":true},{"id":"detail","type":"text","publishable":true}]}
	]})json");
	data["maps"][0]["scenario"] = Json::parse(R"json({
		"tree":"test.main-reader","bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"},
		"publications":["test.signal"],"situations":[
			{"id":"publisher","tree":"test.publisher","bindings":{"actor":{"slot":"test.slot"}}},
			{"id":"second","tree":"test.reader","start":true,"bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"}},
			{"id":"other-ship","tree":"test.reader","start":true,"bindings":{"actor":{"entity":1},"knowledge_console":"main"}},
			{"id":"expiry","tree":"test.expiry","start":true,"bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"}}
		]
	})json");
	data["trees"] = Json::parse(R"json([
		{"id":"test.reader","context":"scenario","initial":"wait","record_bindings":[
			{"name":"known.amount","schema":"test.signal","field":"amount","scope":"ship"},
			{"name":"known.flag","schema":"test.signal","field":"flag","scope":"ship"},
			{"name":"known.detail","schema":"test.signal","field":"detail","scope":"ship"},
			{"name":"known.console","schema":"test.signal","field":"amount","scope":"console"},
			{"name":"known.other","schema":"test.signal","field":"amount","scope":"ship","subject":1}
		],"states":[
			{"id":"wait","transitions":[{"target":"done","conditions":[
				{"left":{"observation":"known.amount"},"op":"eq","right":7},
				{"left":{"observation":"known.flag"},"op":"eq","right":true},
				{"left":{"observation":"known.detail"},"op":"eq","right":"ready"}
			]}]},
			{"id":"done","entry":[{"action":"finish_scenario","arguments":[true,"read permitted evidence"]}]}
		]},
		{"id":"test.main-reader","extends":"test.reader","overrides":[{"state":"wait","entry":[{"action":"start_situation","arguments":["publisher","publisher-instance"]}]}]},
		{"id":"test.publisher","context":"scenario","initial":"publish","states":[{"id":"publish","entry":[
			{"action":"publish_scenario_message","arguments":["test.signal",{"observation":"self.entity"},7,true,"ready"]}
		]}]},
		{"id":"test.expiry","extends":"test.reader","initial":"wait","overrides":[
			{"state":"wait","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]}]}
		],"states":[
			{"id":"wait","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]}]},
			{"id":"observed","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]},{"target":"done"}]},
			{"id":"done","entry":[{"action":"finish_scenario","arguments":[true,"stale evidence removed"]}]}
		]}
	])json");
	auto referenced_reader = data["trees"][0];
	referenced_reader["id"] = "test.read-signal";
	data["trees"].push_back(std::move(referenced_reader));
	data["trees"][0]["states"][0].erase("transitions");
	data["trees"][0]["states"][0]["running"] = Json::array({{{"call", "test.read-signal"}}});
	return data;
}
void spawned_equipment_continuation()
{
	const auto catalog = fixture(true);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	world.tick();
	world.tick();
	check(world.inspect().size() == 4 && world.scene().size() == 4,
	      "Scenario spawns must create bodies with equipment while keeping child identities private");
	std::vector<EntityId> spawned;
	const auto player = *world.ship_for_slot("test.slot");
	for (const auto &body : world.inspect())
		if (body.id != 1 && body.id != player)
			spawned.push_back(body.id);
	check(spawned.size() == 2, "Reusable situations did not create two independently fitted ships");
	for (std::size_t i = 0; i < spawned.size(); ++i)
	{
		check(world.claim(200 + i, spawned[i]), "Dynamically created equipment has no usable Console binding");
		check(world.submit(200 + i, {spawned[i], "speed_mm_s", 2000, "engine"}),
		      "Dynamically created equipment refused an ordinary command");
	}
	world.tick();
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	check(resumed.save() == saved, "Spawned equipment or resource state did not round trip");
	for (int tick = 0; tick < 4; ++tick)
	{
		world.tick();
		resumed.tick();
	}
	check(world.save() == resumed.save() && world.inspect().size() == 4,
	      "Dynamic equipment must execute once and preserve continuation after recovery");
	for (const auto id : spawned)
		for (const auto &body : world.inspect())
			if (body.id == id)
				check(body.vx > 0, "Spawned engine did not execute through the production equipment query");
}
void scenario_record_readers()
{
	const auto data = reader_fixture();
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	world.tick();
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 7; ++i)
	{
		world.tick();
		resumed.tick();
	}
	const auto state = world.save();
	bool main = false, second = false, other = false, expired = false;
	for (const auto &instance : state.at("scenario").at("runtime").at("instances"))
	{
		const bool success = instance.at("finished").get<bool>() && instance.at("succeeded").get<bool>();
		if (instance.at("name") == "main")
			main = success;
		if (instance.at("name") == "second")
			second = success;
		if (instance.at("name") == "other-ship")
			other = success;
		if (instance.at("name") == "expiry")
			expired = success;
	}
	check(main && second && expired && !other,
	      "Scenario readers lost typed evidence, scope/subject isolation, independent situations, or staleness");
	check(state == resumed.save(), "Scoped scenario readers diverged after World recovery");
	auto refused = [&](Json invalid, const char *message)
	{
		bool failed = false;
		try
		{
			(void)load_fixture(invalid);
		}
		catch (const std::exception &)
		{
			failed = true;
		}
		check(failed, message);
	};
	auto invalid = data;
	invalid["maps"][0]["scenario"]["bindings"].erase("knowledge_console");
	refused(invalid, "Scenario record reader without explicit Console was admitted");
	invalid = data;
	invalid["maps"][0]["scenario"]["bindings"]["knowledge_console"] = "private";
	refused(invalid, "Scenario record reader bypassed selected Console roles");
	invalid = data;
	invalid["maps"][0]["scenario"]["bindings"]["knowledge_console"] = "missing";
	refused(invalid, "Scenario record reader selected nonexistent Console");
	invalid = data;
	invalid["maps"][0]["scenario"]["situations"][1]["bindings"].erase("knowledge_console");
	refused(invalid, "Reusable situation omitted its explicit record reader");
	auto dynamic = data;
	dynamic["entities"].push_back(
	    {{"id", "test.unrelated-private"},
	     {"extends", "test.ship"},
	     {"stations",
	      Json::parse(
	          R"json([{"id":"bridge","consoles":[{"id":"main","definition":"test.console","systems":["engine","steering","radar"],"knowledge_roles":["test.private"]}]}])json")}});
	dynamic["maps"][0]["scenario"]["bindings"]["actor"] = {{"handle", "traffic"}};
	(void)load_fixture(dynamic);
	// Typed ingress also suppresses an older live record after the latest stream
	// record was withdrawn. This uses a compiled production reader and real keys.
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	const auto bodies = world.inspect();
	bridge.begin_tick(10, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	const auto readers = bridge.knowledge_readers();
	check(!readers.empty(), "Compiled scenario did not request scoped evidence");
	KnowledgeRecord live;
	live.value.schema = catalog.knowledge_definition.schema("test.signal");
	live.value.subject = readers[0].ship;
	live.value.provenance.observed_tick = 10;
	live.value.provenance.revision = 1;
	live.value.fields[catalog.knowledge_definition.field(live.value.schema, "amount")] = std::int64_t{7};
	auto withdrawn = live;
	withdrawn.value.withdrawn = true;
	withdrawn.value.provenance.revision = 2;
	std::vector<ScenarioKnowledgeSnapshot> snapshots{{readers[0], {}, {live}}};
	bridge.supply_knowledge(snapshots);
	snapshots[0].shared[0].value.fields.begin()->second = std::int64_t{99};
	const auto frozen = bridge.observations(readers[0].instance);
	const auto offset = static_cast<std::size_t>(GameplayTreeObservation::Count);
	check(frozen.at(offset).number == 7 && frozen.at(offset + 3).type == state_tree::Type::Missing &&
	          frozen.at(offset + 4).type == state_tree::Type::Missing,
	      "Scenario snapshot was mutable or ignored record scope/subject filters");
	bool replaced = false;
	try
	{
		bridge.supply_knowledge(snapshots);
	}
	catch (const std::exception &)
	{
		replaced = true;
	}
	check(replaced, "Scenario evidence was replaced within one immutable tick");
	bridge.begin_tick(11, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	snapshots[0].shared = {live, withdrawn};
	bridge.supply_knowledge(snapshots);
	const auto values = bridge.observations(readers[0].instance);
	check(values.at(static_cast<std::size_t>(GameplayTreeObservation::Count)).type == state_tree::Type::Missing,
	      "Withdrawn latest scenario evidence resurrected older history");
	bridge.begin_tick(12, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	auto independent = live;
	independent.value.provenance.source = 99;
	independent.value.provenance.observed_tick = 12;
	withdrawn.value.provenance.observed_tick = 12;
	snapshots[0].shared = {independent, withdrawn};
	bridge.supply_knowledge(snapshots);
	check(bridge.observations(readers[0].instance).at(offset).number == 7,
	      "Withdrawing one source suppressed an independent live scenario evidence stream");
	bridge.begin_tick(13, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	snapshots[0].shared = {live};
	bridge.supply_knowledge(snapshots);
	check(bridge.observations(readers[0].instance).at(offset).type == state_tree::Type::Missing,
	      "Scenario stale evidence remained available");
}
std::vector<Json> objective_records(const Observation &observation)
{
	std::vector<Json> out;
	for (const char *scope : {"records", "shared"})
		for (const auto &record : observation.gameplay.value(scope, Json::array()))
			if (record.value("schema", std::string{}) == "phoenix.objective")
				out.push_back(record);
	return out;
}
} // namespace

#include "../manual/scenario_probe.hpp"

int main(int argc, char **argv)
{
	try
	{
		if (argc == 6 && std::string(argv[1]) == "--smoke-gameover")
			return scenario_probe(argv[2], argv[3], std::stoi(argv[4]), argv[5]);
		if (argc == 5 && std::string(argv[1]) == "--smoke-pack")
			return scenario_probe(argv[2], argv[3], std::stoi(argv[4]));
		const auto catalog = fixture();
		World original;
		original.load(catalog, "test.map", {{"test.slot", "test.ship"}});
		const auto player = original.ship_for_slot("test.slot");
		check(player.has_value(), "Scenario actor slot did not resolve to a physical ship");
		check(original.claim(100, *player), "Scenario player Console claim failed");
		check(original.claim(101, 1), "Independent ship Console claim failed");
		check(original.submit(100, {*player, "speed_mm_s", 100000}), "High-speed experiment command was not admitted");
		original.tick();
		const auto saved = original.save();
		check(saved.contains("scenario"), "World did not persist its authoritative scenario continuation");
		World resumed;
		resumed.restore(catalog, saved);
		// Transport recovery clears/reclaims ordinary human authority explicitly;
		// both runs continue with identical current orders and physical state.
		for (auto *world : {&original, &resumed})
		{
			world->clear_claims();
			check(world->claim(100, *player) && world->claim(101, 1),
			      "Recovered scenario Console could not be reclaimed");
		}
		for (int i = 0; i < 3; ++i)
		{
			original.tick();
			resumed.tick();
		}
		const auto bodies = original.inspect();
		check(bodies.size() == 4, "Independent situation instances did not each construct one real ECS entity");
		std::vector<EntityId> spawned;
		bool upper = false, lower = false;
		for (const auto &b : bodies)
		{
			if (b.id == 1 || b.id == *player)
				continue;
			spawned.push_back(b.id);
			upper = upper || b.y == 10000;
			lower = lower || b.y == -10000;
			check(b.definition == "test.variant", "Physical spawn used the wrong verified entity definition");
		}
		check(spawned.size() == 2 && spawned[0] != spawned[1] && upper && lower,
		      "Real spawns lost stable identity or per-instance parameter placement");
		const auto player_view = original.observe(100), other_view = original.observe(101);
		check(player_view.has_value() && other_view.has_value(), "Scenario projections lost admitted Consoles");
		const auto records = objective_records(*player_view);
		check(!records.empty(), "Physically completed objective did not reach its permitted ship blackboard");
		check(objective_records(*other_view).empty(), "Scenario objective leaked to another ship");
		const auto continuation = original.save();
		const auto &scenario = continuation.at("scenario").at("runtime");
		check(scenario.at("objectives").size() == 1 &&
		          scenario.at("objectives")[0].at("status") == static_cast<unsigned>(ObjectiveStatus::Completed) &&
		          scenario.at("objectives")[0].at("completed_actor") == *player,
		      "Actual Flecs movement crossed the region without physical objective completion");
		bool succeeded = false;
		for (const auto &instance : scenario.at("instances"))
			if (instance.at("name") == "main")
				succeeded = instance.at("finished").get<bool>() && instance.at("succeeded").get<bool>();
		check(succeeded, "Scenario tree did not consume physical completion and finish successfully");
		check(continuation == resumed.save(), "Dynamic World/scenario resume differs from uninterrupted continuation");
		const auto publication_count = records.size();
		for (int i = 0; i < 3; ++i)
		{
			original.tick();
			resumed.tick();
		}
		check(original.inspect().size() == 4 && resumed.inspect().size() == 4,
		      "Completed one-shot spawn was replayed after recovery");
		check(objective_records(*original.observe(100)).size() == publication_count,
		      "Terminal objective repeatedly published completion");
		check(original.save() == resumed.save(),
		      "Later physical continuation diverged after restored scenario effects");
		scenario_record_readers();
		competition_resolution();
		delayed_competition();
		spawned_equipment_continuation();
		std::cout << "World scenario spawn/objective/recovery integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
