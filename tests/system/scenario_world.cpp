#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.simulation;

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
Catalog fixture()
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic World scenario");
	data["entities"][0]["systems"]["engine"] = {{"max_speed_mm_s", 100000}, {"acceleration_mm_s2", 2000000}};
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
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	pack.files["definitions.json"] = json_bytes(data);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	return resolve_packs({read_pack(write_pack(pack))});
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

int main()
{
	try
	{
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
		std::cout << "World scenario spawn/objective/recovery integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
