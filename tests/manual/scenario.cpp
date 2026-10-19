#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import phoenix.content;
import phoenix.gameplay_scenario;

namespace
{
using namespace phoenix;
void scenario_check(bool valid, const char *message)
{
	if (!valid)
		throw std::runtime_error(message);
}
Json scenario_action(std::string action, Json arguments)
{
	return {{"action", std::move(action)}, {"arguments", std::move(arguments)}};
}
Catalog scenario_fixture(bool limited_instances = false, bool limited_actions = false,
                         bool incompatible_publication = false, bool unsupported_required_field = false)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic scenario integration");
	auto &map = data["maps"][0];
	map["regions"] =
	    Json::array({{{"id", "arrival"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 100}},
	                 {{"id", "gate"},
	                  {"shape", "rectangle"},
	                  {"x_mm", 5000},
	                  {"y_mm", 0},
	                  {"half_width_mm", 100},
	                  {"half_height_mm", 100}}});
	map["scenario"] = {
	    {"tree", "test.route"},
	    {"bindings",
	     {{"actor", {{"slot", "test.slot"}}},
	      {"objective", "arrival"},
	      {"region", "arrival"},
	      {"event", "traffic-ready"}}},
	    {"parameters", {{"traffic_count", 2}}},
	    {"spawn_groups",
	     Json::array(
	         {{{"id", "traffic"},
	           {"entities",
	            Json::array(
	                {{{"definition", "test.ship"}, {"x_mm", 20000}, {"y_mm", 3000}, {"heading_mdeg", 180000}},
	                 {{"definition", "test.variant"}, {"x_mm", 21000}, {"y_mm", 3000}, {"heading_mdeg", 180000}}})}}})},
	    {"objectives", Json::array({{{"id", "reach-arrival"},
	                                 {"type", "reach_region"},
	                                 {"region", "arrival"},
	                                 {"actor", {{"slot", "test.slot"}}},
	                                 {"recipients", {{"ship", {{"slot", "test.slot"}}}, {"consoles", {"main"}}}},
	                                 {"title_key", "test.arrival"}},
	                                {{"id", "reach-gate"},
	                                 {"type", "reach_region"},
	                                 {"region", "gate"},
	                                 {"actor", {{"slot", "test.slot"}}},
	                                 {"recipients", {{"ship", {{"slot", "test.slot"}}}}},
	                                 {"title_key", "test.gate"}}})},
	    {"events", {"traffic-ready"}},
	    {"publications", {"advisory"}},
	    {"situations", Json::array({{{"id", "traffic-watch"},
	                                 {"tree", "test.watch"},
	                                 {"parameters", {{"traffic_count", 7}}},
	                                 {"bindings", {{"event", "traffic-ready"}}}}})}};
	const auto self = Json{{"observation", "self.entity"}};
	const auto state = Json{{"observation", "objective.state"}};
	data["trees"] = Json::array(
	    {{{"id", "test.route"},
	      {"context", "scenario"},
	      {"initial", "waiting"},
	      {"states",
	       Json::array(
	           {{{"id", "waiting"},
	             {"entry",
	              Json::array({scenario_action("spawn_group", Json::array({"traffic", "traffic"})),
	                           scenario_action("add_objective", Json::array({"reach-arrival", self, "arrival"})),
	                           scenario_action("start_situation", Json::array({"traffic-watch", "watch-one"}))})},
	             {"transitions",
	              Json::array({{{"target", "finished"},
	                            {"conditions", Json::array({{{"left", state}, {"op", "eq"}, {"right", 2}}})}}})}},
	            {{"id", "finished"},
	             {"entry",
	              Json::array(
	                  {scenario_action("publish_scenario_message", Json::array({"advisory", self, 3, true, "arrived"})),
	                   scenario_action("finish_scenario", Json::array({true, "region-reached"}))})}}})}},
	     {{"id", "test.watch"},
	      {"context", "scenario"},
	      {"parameters", {{"traffic_count", {{"type", "integer"}, {"default", 1}}}}},
	      {"initial", "watching"},
	      {"states",
	       Json::array(
	           {{{"id", "watching"},
	             {"running",
	              Json::array({scenario_action(
	                  "emit_scenario_event",
	                  Json::array({"traffic-ready", {{"parameter", "traffic_count"}}, true, "watching"}))})}}})}}});
	data["knowledge"] = {
	    {"roles", Json::array()},
	    {"schemas",
	     Json::array({{{"id", "advisory"},
	                   {"producers", {"phoenix.scenario"}},
	                   {"readers", {"phoenix.crew"}},
	                   {"ship_publishable", true},
	                   {"fields", Json::array({{{"id", "amount"}, {"type", "integer"}, {"publishable", true}},
	                                           {{"id", "flag"}, {"type", "boolean"}, {"publishable", true}},
	                                           {{"id", "detail"}, {"type", "text"}, {"publishable", true}}})}}})}};
	if (limited_instances)
	{
		map["scenario"]["limits"] = {{"instances", 1}};
		auto extra = map["scenario"]["situations"][0];
		extra["id"] = "unused-watch";
		map["scenario"]["situations"].push_back(std::move(extra));
	}
	if (limited_actions)
		map["scenario"]["limits"]["actions"] = 1;
	if (incompatible_publication)
		data["knowledge"]["schemas"][0]["fields"][0]["type"] = "boolean";
	if (unsupported_required_field)
		data["knowledge"]["schemas"][0]["fields"].push_back(
		    {{"id", "extra"}, {"type", "integer"}, {"required", true}, {"publishable", true}});
	pack.files["definitions.json"] = json_bytes(data);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	return resolve_packs({read_pack(write_pack(pack))});
}
std::vector<BodyView> scenario_bodies(std::int64_t ship_x)
{
	return {{1, "test.variant", 0, 0, 0, 0, 0}, {2, "test.ship", ship_x, 0, 0, 0, 0}};
}
void scenario_admission_and_continuation(const Catalog &catalog)
{
	GameplayScenarioBridge original;
	original.load(catalog, "test.map");
	original.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
	original.step();
	scenario_check(original.controllers().size() == 2,
	               "Scenario start_situation did not create independent controller");
	auto requests = original.pending_spawns();
	scenario_check(requests.size() == 1 && requests.front().entities.size() == 2,
	               "Compiled scenario failed to request whole spawn group");
	scenario_check(original.runtime().spawned_entities().empty(),
	               "Scenario invented physical spawn before World confirmation");
	const auto main = requests.front().instance;
	const auto objective = original.runtime().objective_handle(main, "arrival");
	scenario_check(objective && original.runtime().objective_status(*objective) == ObjectiveStatus::Active,
	               "Scenario add_objective did not bind active instance");
	GameplayScenarioBridge pending;
	pending.restore(catalog, "test.map", original.save());
	scenario_check(pending.pending_spawns().size() == 1 && pending.pending_spawns().front().id == requests.front().id,
	               "Pending spawn did not survive recovery with its identity");
	const std::array<EntityId, 2> actual_ids{10, 11};
	original.confirm_spawn(requests.front().id, actual_ids);
	pending.confirm_spawn(requests.front().id, actual_ids);
	const auto replay = pending.execute(requests.front().action, GameplayTreeAction::SpawnGroup,
	                                    {state_tree::Value::string("traffic"), state_tree::Value::string("traffic")});
	scenario_check(replay.result.status == ScenarioActionStatus::Succeeded &&
	                   replay.result.entities == std::vector<EntityId>({10, 11}) && pending.pending_spawns().empty(),
	               "Replayed spawn did not return original physical IDs");
	original.step();
	pending.step();
	scenario_check(original.runtime().spawned_entities().size() == 2 &&
	                   pending.runtime().spawned_entities().size() == 2,
	               "Repeated scenario evaluation duplicated spawn");
	const std::array crossing{ScenarioMotionSegment{2, {-2000, 0}, {2000, 0}}};
	original.evaluate(crossing);
	pending.evaluate(crossing);
	scenario_check(original.runtime().objective_status(*objective) == ObjectiveStatus::Completed,
	               "High-speed circle crossing was missed");
	const auto publication_count = pending.runtime().pending_publications().size();
	pending.evaluate(crossing);
	scenario_check(pending.runtime().pending_publications().size() == publication_count,
	               "Repeated physical completion duplicated publication");
	scenario_check(!pending.runtime().objectives().at(*objective).completion_published &&
	                   !pending.runtime().objectives().at(*objective).completion_credited,
	               "Physical completion forged publication or completion credit");
	GameplayScenarioBridge resumed;
	resumed.restore(catalog, "test.map", pending.save());
	scenario_check(resumed.runtime().pending_publications().size() == publication_count &&
	                   resumed.pending_spawns().empty(),
	               "Resume lost pending publication or resurrected completed spawn");
	for (auto *bridge : {&original, &resumed})
	{
		auto bodies = scenario_bodies(2000);
		bodies.push_back({10, "test.ship", 20000, 3000, 0, 0, 180000});
		bodies.push_back({11, "test.variant", 21000, 3000, 0, 0, 180000});
		bridge->begin_tick(1, {{"test.slot", 2}}, bodies);
		bridge->step();
	}
	scenario_check(original.runtime().instances().at(main).finished &&
	                   original.runtime().instances().at(main).succeeded,
	               "Objective completion did not branch scenario to success");
	const auto events = resumed.runtime().events("traffic-ready");
	scenario_check(events.size() == 1 && std::get<std::int64_t>(events.front().payload.at("amount")) == 7,
	               "Independent situation parameter override was not applied");
	scenario_check(original.save() == resumed.save(),
	               "Resumed scenario continuation differs from uninterrupted execution");
	const auto unmodified = resumed.save();
	auto forged = unmodified;
	for (auto &r : forged["receipts"])
		if (r.at("action") == static_cast<unsigned>(GameplayTreeAction::SpawnGroup))
		{
			r["action"] = static_cast<unsigned>(GameplayTreeAction::AddObjective);
			break;
		}
	bool rejected = false;
	try
	{
		resumed.restore(catalog, "test.map", forged);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	scenario_check(rejected && resumed.save() == unmodified, "Forged saved action kind changed live scenario receipts");
	for (const auto &p : resumed.runtime().pending_publications())
		if (p.objective == *objective && p.status == ObjectiveStatus::Completed)
		{
			scenario_check(p.recipients.audience == ScenarioAudience::Consoles &&
			                   p.recipients.consoles == std::vector<std::string>{"main"},
			               "Objective publication widened recipient scope");
			resumed.runtime().acknowledge_publication(p.id);
		}
	resumed.runtime().acknowledge_credit(*objective);
	scenario_check(resumed.runtime().objectives().at(*objective).completion_published &&
	                   resumed.runtime().objectives().at(*objective).completion_credited,
	               "Explicit publication/credit acknowledgements were not retained");
}
void scenario_geometry_limits_and_references(const Catalog &catalog)
{
	ScenarioRuntime runtime(catalog.scenarios.at("test.map"));
	runtime.begin_tick(0, {{"test.slot", 2}}, {{1, {0, 0}}, {2, {2000, 0}}});
	const auto instance = runtime.create_instance("direct");
	const auto inside =
	    runtime
	        .add_objective({instance, 1, 0}, "reach-arrival", "inside", ScenarioActor{ScenarioActorKind::Entity, 1, {}})
	        .value;
	scenario_check(runtime.objective_status(inside) == ObjectiveStatus::Completed,
	               "Starting inside objective did not complete at activation");
	const auto gate = runtime.add_objective({instance, 1, 1}, "reach-gate", "gate").value;
	const std::array crossing{ScenarioMotionSegment{2, {2000, 0}, {8000, 0}}};
	runtime.evaluate(crossing);
	scenario_check(runtime.objective_status(gate) == ObjectiveStatus::Completed,
	               "High-speed rectangle crossing was missed");
	auto token = ScenarioActionToken{instance, 5, 3, {10}, {1}};
	const auto first = runtime.spawn_entity(token, "test.ship", {30000, 0}, 0, "first");
	token.call_activations = {2};
	const auto second = runtime.spawn_entity(token, "test.ship", {31000, 0}, 0, "second");
	scenario_check(first.value != second.value && runtime.pending_spawns().size() == 2,
	               "Distinct reusable caller activations collided");
	const auto failed = runtime.spawn_entity({instance, 9, 0}, "missing.definition", {}, 0, "bad");
	scenario_check(failed.status == ScenarioActionStatus::Failed && runtime.pending_spawns().size() == 2,
	               "Invalid spawn did not fail without physical effects");
	auto save = runtime.save();
	save["objectives"][0]["credited"] = true;
	save["objectives"][0]["status"] = static_cast<unsigned>(ObjectiveStatus::Active);
	bool rejected = false;
	try
	{
		runtime.restore(save);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	scenario_check(rejected && runtime.objective_status(inside) == ObjectiveStatus::Completed,
	               "Malformed continuation changed live scenario state");
	const ScenarioRegion tangent{"large", ScenarioRegionShape::Circle, {0, 0}, 1000000000, 0, 0};
	scenario_check(scenario_crosses(tangent, {-1000000000, 1000000000}, {1000000000, 1000000000}),
	               "Exact large-coordinate tangent was missed");
	const ScenarioRegion small{"small", ScenarioRegionShape::Circle, {0, 0}, 1, 0, 0};
	scenario_check(!scenario_crosses(small, {-1000000000, 2}, {1000000000, 2}),
	               "Large-coordinate near miss became false completion");
	auto limited = catalog.scenarios.at("test.map");
	limited.spawn_per_tick = 1;
	limited.spawned_population = 1;
	ScenarioRuntime bound(limited);
	bound.begin_tick(0, {{"test.slot", 2}}, {{2, {0, 0}}});
	const auto owner = bound.create_instance("bounded");
	scenario_check(bound.spawn_group({owner, 1, 0}, "traffic", {}, "too-many").status == ScenarioActionStatus::Failed &&
	                   bound.pending_spawns().empty(),
	               "Spawn budget admitted partial group");
}
} // namespace

int scenario_journey()
{
	const auto catalog = scenario_fixture();
	scenario_admission_and_continuation(catalog);
	scenario_geometry_limits_and_references(catalog);
	{
		GameplayScenarioBridge identity;
		identity.load(catalog, "test.map");
		const auto id = identity.runtime().instances().begin()->first;
		const auto before = identity.save();
		bool rejected = false;
		try
		{
			identity.execute({id, 0, 0}, GameplayTreeAction::SpawnGroup,
			                 {state_tree::Value::string("traffic"), state_tree::Value::string("bad")});
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		scenario_check(rejected && identity.save() == before, "Rejected zero activation left an unrestorable receipt");
		identity.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
		identity.step();
		const auto objective = *identity.runtime().objective_handle(id, "arrival");
		const auto forged = identity.execute(
		    {id, 999, 0}, GameplayTreeAction::SetObjectiveStatus,
		    {state_tree::Value::reference(state_tree::Type::Objective, objective), state_tree::Value::integer(2)});
		scenario_check(forged.result.status == ScenarioActionStatus::Failed &&
		                   identity.runtime().objective_status(objective) == ObjectiveStatus::Active,
		               "Scenario status action forged physical objective completion");
		const auto cancelled = identity.execute(
		    {id, 999, 1}, GameplayTreeAction::SetObjectiveStatus,
		    {state_tree::Value::reference(state_tree::Type::Objective, objective), state_tree::Value::integer(4)});
		scenario_check(cancelled.result.status == ScenarioActionStatus::Succeeded &&
		                   identity.runtime().objective_status(objective) == ObjectiveStatus::Cancelled,
		               "Scenario status action could not cancel owned active objective");
		GameplayScenarioBridge restored;
		restored.restore(catalog, "test.map", identity.save());
		scenario_check(restored.runtime().objective_status(objective) == ObjectiveStatus::Cancelled,
		               "Scenario lifecycle action did not survive recovery");
	}
	{
		const auto limited = scenario_fixture(true);
		GameplayScenarioBridge configs;
		configs.load(limited, "test.map");
		scenario_check(configs.controllers().size() == 1, "Reusable declarations consumed retained situation capacity");
		configs.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
		configs.step();
		scenario_check(configs.controllers().size() == 1, "Dynamic situation ignored configured instance limit");
	}
	{
		const auto limited = scenario_fixture(false, true);
		GameplayScenarioBridge transaction;
		transaction.load(limited, "test.map");
		transaction.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
		const auto before = transaction.save();
		bool rejected = false;
		try
		{
			transaction.step();
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		scenario_check(rejected && transaction.save() == before && transaction.pending_spawns().empty(),
		               "Refused multi-effect tick consumed one-shot tasks or partially committed effects");
	}
	{
		bool rejected = false;
		try
		{
			scenario_fixture(false, false, true);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		scenario_check(rejected, "Production loader admitted incompatible scenario publication payload schema");
	}
	{
		bool rejected = false;
		try
		{
			scenario_fixture(false, false, false, true);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		scenario_check(rejected, "Production loader admitted scenario message with unpopulated required payload field");
	}
	std::cout << "Scenario production-loader integration passed\n";
	return 0;
}

void repeated_sequence()
{
	using namespace phoenix;
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "sequence fixture");
	auto &map = data["maps"][0];
	map["regions"] = Json::parse(R"([
		{"id":"a","shape":"circle","x_mm":0,"y_mm":0,"radius_mm":100},
		{"id":"b","shape":"circle","x_mm":5000,"y_mm":0,"radius_mm":100}])");
	map["scenario"] = Json::parse(R"({"tree":"test.sequence","bindings":{"actor":{"slot":"test.slot"}},
		"limits":{"objectives":1,"actions":1}, "objectives":[
		{"id":"a","type":"reach_region","region":"a","actor":{"slot":"test.slot"},"recipients":{"ship":{"slot":"test.slot"}},"title_key":"test.a"},
		{"id":"b","type":"reach_region","region":"b","actor":{"slot":"test.slot"},"recipients":{"ship":{"slot":"test.slot"}},"title_key":"test.b"}],
		"objective_sequences":[{"id":"patrol","objectives":["a","b"],"repeat":true}]})");
	data["trees"] = Json::parse(R"([{"id":"test.sequence","context":"scenario","initial":"run","states":[
		{"id":"run","entry":[{"action":"start_objective_sequence","arguments":["patrol",{"observation":"self.entity"},"route"]}]}]}])");
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	pack.files["definitions.json"] = json_bytes(data);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	const auto catalog = resolve_packs({read_pack(write_pack(pack))});
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	bridge.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
	bridge.step();
	const auto id = bridge.runtime().objectives().begin()->first;
	for (std::uint64_t leg = 0; leg < 100; ++leg)
	{
		const auto x = leg % 2 == 0 ? 0 : 5000;
		bridge.begin_tick(leg + 1, {{"test.slot", 2}}, scenario_bodies(x - 2000));
		bridge.step();
		scenario_check(bridge.runtime().objectives().at(id).sequence_index == leg % 2 &&
		                   bridge.runtime().objectives().at(id).cycle == leg / 2,
		               "sequence must advance in order with a distinct cycle");
		const std::array wrong{ScenarioMotionSegment{2, {20000, 0}, {21000, 0}}};
		bridge.evaluate(wrong);
		const std::array dwelling{ScenarioMotionSegment{2, {x, 0}, {x, 0}}};
		bridge.evaluate(dwelling);
		scenario_check(bridge.runtime().objectives().at(id).status == ObjectiveStatus::Active,
		               "unrelated movement or dwelling must not complete a sequence leg without entry");
		const std::array crossing{ScenarioMotionSegment{2, {x - 2000, 0}, {x + 2000, 0}}};
		bridge.evaluate(crossing);
		scenario_check(bridge.runtime().objectives().at(id).status == ObjectiveStatus::Completed,
		               "executed crossing must complete the current sequence leg");
		GameplayScenarioBridge resumed;
		resumed.restore(catalog, "test.map", bridge.save());
		scenario_check(resumed.save() == bridge.save(), "pending sequence completion must round trip");
		for (auto *current : {&bridge, &resumed})
		{
			for (const auto &p : current->runtime().pending_publications())
				current->runtime().acknowledge_publication(p.id);
			current->runtime().acknowledge_credit(id);
		}
		scenario_check(resumed.save() == bridge.save(), "completion acknowledgements must survive recovery");
		// The next leg (including every wrap to cycle N+1) runs from recovered state.
		bridge.restore(catalog, "test.map", resumed.save());
		const auto saved = bridge.save();
		scenario_check(saved.at("runtime").at("objectives").size() == 1 &&
		                   saved.at("runtime").at("actions").size() == 1 && saved.at("receipts").size() == 1,
		               "repetition must not consume objective or journal population");
	}
}

int main()
{
	try
	{
		repeated_sequence();
		return scenario_journey();
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
