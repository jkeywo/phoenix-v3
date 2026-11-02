#include "../support/fixtures.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import phoenix.content.catalog;
import phoenix.content.packs;
import phoenix.content.verification;
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
                         bool incompatible_publication = false, bool unsupported_required_field = false,
                         bool task_planning = false, bool task_workload = false, bool task_completion = false)
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
	if (task_planning)
	{
		map["scenario"]["tree"] = "test.task-main";
		map["scenario"]["fact_bindings"] = Json::parse(R"([
			{"id":"force","kind":"clock","field":"tick"}])");
		map["scenario"]["task_plan"] = Json::parse(R"({"root":"seize","tasks":[
			{"id":"seize","objectives":["reach-arrival"],"methods":[
				{"id":"screen","requires":{"fact":"force","op":"ge","value":3},
				 "maintain":{"fact":"force","op":"ge","value":3},"children":[{"id":"boarder","task":"board"}]},
				{"id":"alone","requires":{"fact":"force","op":"ge","value":1},"situation":"traffic-watch"}]},
			{"id":"board","methods":[{"id":"dock","situation":"traffic-watch"}]}]})");
		map["scenario"]["task_plan"]["tasks"].erase(1);
		map["scenario"]["task_plan"]["random_seed"] = true;
		data["tasks"] = Json::parse(R"([{"id":"test.task-board","parameters":{"situations":["run"]},
			"methods":[{"id":"dock","situation":"run"}]}])");
		map["scenario"]["task_plan"]["imports"] = Json::parse(R"([
			{"id":"board","template":"test.task-board","arguments":{"situations":{"run":"traffic-watch"}}}])");
		data["trees"].push_back(Json::parse(R"({"id":"test.task-main","context":"scenario","initial":"idle",
			"scenario_facts":[{"name":"force.available","fact":"force","type":"integer"}],
			"states":[{"id":"idle"}]})"));
		if (task_workload)
		{
			auto &task = map["scenario"]["task_plan"]["tasks"][0];
			task["participants"] = {{"first", {{"entity", 1}}}, {"second", {{"slot", "test.slot"}}}};
			task["methods"][1]["latest_ticks"] = 10;
			task["methods"][1]["attention"] = Json::parse(R"([
				{"participant":"first","console":"main","amount":60},
				{"participant":"second","console":"main","amount":60}])");
		}
		if (task_completion)
		{
			map["scenario"]["task_plan"]["tasks"][0]["complete_when"] = {{"fact", "force"}, {"value", 4}};
			map["scenario"]["task_plan"]["imports"][0]["arguments"]["situations"]["run"] = "setup-only";
			map["scenario"]["situations"].push_back(
			    {{"id", "setup-only"}, {"tree", "test.setup-only"}, {"start", false}});
			data["trees"].push_back(Json::parse(
			    R"json({"id":"test.setup-only","context":"scenario","initial":"publish","states":[{"id":"publish","entry":[{"action":"finish_scenario","arguments":[true,"setup resolved"]}]}]})json"));
		}
	}
	phoenix::test::materialise_fixture_widgets(data);
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
	phoenix::test::materialise_fixture_widgets(data);
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

void task_planning_and_repair()
{
	const auto catalog = scenario_fixture(false, false, false, false, true);
	const auto &definition = catalog.scenarios.at("test.map");
	const ScenarioFacts one{{"force", state_tree::Value::integer(1)}};
	const ScenarioFacts three{{"force", state_tree::Value::integer(3)}};
	const auto rejected = plan_scenario_task(definition, "seize", {});
	scenario_check(!rejected.feasible && rejected.nodes.empty(), "Missing task evidence must fail closed");
	const auto single = plan_scenario_task(definition, "seize", one);
	scenario_check(single.feasible && single.nodes.size() == 1 && single.nodes[0].method == "alone",
	               "preferred method must backtrack without creating its rejected children");
	const auto fleet = plan_scenario_task(definition, "seize", three);
	scenario_check(fleet.feasible && fleet.nodes.size() == 2 && fleet.nodes[0].method == "screen",
	               "feasible method must decompose its real child task");
	auto varied = definition;
	varied.tasks.at("seize").vary_methods = true;
	std::set<std::string> selected;
	for (std::uint64_t seed = 1; seed <= 32; ++seed)
		selected.insert(plan_scenario_task(varied, "seize", three, seed).nodes[0].method);
	scenario_check(selected.size() == 2, "Seeded alternatives must vary while retaining feasibility");
	auto limited = definition;
	limited.task_expansions = 1;
	const auto exhausted = plan_scenario_task(limited, "seize", three);
	scenario_check(!exhausted.feasible && exhausted.exhausted && exhausted.nodes.empty(),
	               "bounded search must not expose a partially selected plan");
	GameplayScenarioBridge early;
	early.load(catalog, "test.map");
	const auto early_main = early.runtime().instances().begin()->first;
	early.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
	early.evaluate_conditions(0, {{early_main, {}}});
	early.step();
	const auto early_request = early.execute({early_main, 98, 0}, GameplayTreeAction::RequestTask,
	                                         {state_tree::Value::string("board"), state_tree::Value::string("early")});
	scenario_check(early_request.result.status == ScenarioActionStatus::Succeeded,
	               "independent work may be admitted before the root is feasible");
	early.begin_tick(1, {{"test.slot", 2}}, scenario_bodies(-2000));
	early.evaluate_conditions(1, {{early_main, {}}});
	early.step();
	scenario_check(early.tasks().size() == 1 && early.tasks()[0].situation != 0,
	               "an infeasible root must not stall independent admitted tasks");
	early.begin_tick(2, {{"test.slot", 2}}, scenario_bodies(-2000));
	early.evaluate_conditions(2, {{early_main, one}});
	early.step();
	scenario_check(early.tasks().size() == 2 && early.tasks()[0].path.starts_with("request.") &&
	                   early.tasks()[1].path == "root",
	               "the root must become eligible without replacing earlier admitted work");
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	scenario_check(bridge.prepare_task_seed(17), "A new task scenario accepts one host-selected setup seed");
	const auto main = bridge.runtime().instances().begin()->first;
	bridge.begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
	bridge.evaluate_conditions(0, {{main, three}});
	bridge.step();
	scenario_check(!bridge.prepare_task_seed(18), "An admitted plan cannot reroll its scenario seed");
	scenario_check(
	    bridge.tasks().size() == 2 && bridge.controllers().size() == 2 && bridge.pending_spawns().empty(),
	    "committed task must execute through an independent scenario tree, without invented physical spawns");
	const auto &fields = bridge.controllers().at(main).program.registry.observations;
	const auto force = std::find_if(fields.begin(), fields.end(),
	                                [](const auto &field)
	                                {
		                                return field.name == "force.available";
	                                });
	scenario_check(force != fields.end() &&
	                   bridge.observations(main).at(static_cast<std::size_t>(force - fields.begin())).number == 3,
	               "scenario trees must receive only their explicitly compiled authoritative fact bindings");
	const auto original_child = bridge.tasks()[1].situation;
	GameplayScenarioBridge resumed;
	resumed.restore(catalog, "test.map", bridge.save());
	scenario_check(resumed.save() == bridge.save(), "task plan, child identity and tree receipts must round trip");
	for (auto *current : {&bridge, &resumed})
	{
		current->begin_tick(1, {{"test.slot", 2}}, scenario_bodies(-2000));
		current->evaluate_conditions(1, {{main, one}});
		current->step();
		scenario_check(current->tasks()[0].method == "alone" && current->tasks()[0].situation != original_child,
		               "loss of screening capability must replan the affected task");
		scenario_check(current->tasks()[1].status == ScenarioTaskStatus::Superseded &&
		                   current->runtime().instances().contains(original_child),
		               "superseded task must retain its old situation and effect receipts");
	}
	scenario_check(resumed.save() == bridge.save(), "replanning must continue identically after recovery");
	scenario_check(bridge.runtime().events("traffic-ready").size() == 2,
	               "a superseded tree must not emit another event after replanning");
	const auto before_request = bridge.tasks().size();
	const auto requested = bridge.execute({main, 99, 0}, GameplayTreeAction::RequestTask,
	                                      {state_tree::Value::string("board"), state_tree::Value::string("extra")});
	scenario_check(requested.result.status == ScenarioActionStatus::Succeeded &&
	                   bridge.tasks().size() == before_request + 1,
	               "state trees must be able to request a bounded authored follow-on task");
	const auto replayed = bridge.execute({main, 99, 0}, GameplayTreeAction::RequestTask,
	                                     {state_tree::Value::string("board"), state_tree::Value::string("extra")});
	scenario_check(replayed.result.value == requested.result.value && bridge.tasks().size() == before_request + 1,
	               "replayed task admission must not duplicate work");
	resumed.restore(catalog, "test.map", bridge.save());
	scenario_check(resumed.save() == bridge.save(), "pending follow-on task admission must survive restore");
	const auto after = bridge.save();
	auto invalid = after;
	invalid["task_plan"]["nodes"][0]["method"] = "missing";
	bool refused = false;
	try
	{
		bridge.restore(catalog, "test.map", invalid);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	scenario_check(refused && bridge.save() == after, "malformed task continuation must be rejected transactionally");
}

void task_physical_completion()
{
	const auto catalog = scenario_fixture(false, false, false, false, true, false, true);
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	const auto main = bridge.runtime().instances().begin()->first;
	for (ScenarioTick tick = 0; tick < 2; ++tick)
	{
		bridge.begin_tick(tick, {{"test.slot", 2}}, scenario_bodies(-2000));
		bridge.evaluate_conditions(tick, {{main, {{"force", state_tree::Value::integer(3)}}}});
		bridge.step();
	}
	scenario_check(bridge.tasks()[1].status == ScenarioTaskStatus::Succeeded &&
	                   bridge.tasks()[0].status == ScenarioTaskStatus::Running,
	               "Completed child setup must not award an unmet compound physical outcome");
	bridge.begin_tick(2, {{"test.slot", 2}}, scenario_bodies(-2000));
	bridge.evaluate_conditions(2, {{main, {{"force", state_tree::Value::integer(4)}}}});
	bridge.step();
	scenario_check(bridge.tasks()[0].status == ScenarioTaskStatus::Succeeded,
	               "A compound task resolves when its committed physical predicate becomes true");
}
void task_attention_windows()
{
	const auto catalog = scenario_fixture(false, false, false, false, true, true);
	const ScenarioFacts facts{{"force", state_tree::Value::integer(1)}};
	GameplayScenarioBridge shared, separate;
	for (auto *bridge : {&shared, &separate})
	{
		bridge->load(catalog, "test.map");
		bridge->begin_tick(0, {{"test.slot", 2}}, scenario_bodies(-2000));
		bridge->evaluate_conditions(0, {{bridge->runtime().instances().begin()->first, facts}});
	}
	shared.supply_task_operators(std::array{ScenarioTaskOperator{1, "main", 7}, ScenarioTaskOperator{2, "main", 7}});
	separate.supply_task_operators(std::array{ScenarioTaskOperator{1, "main", 7}, ScenarioTaskOperator{2, "main", 8}});
	shared.step();
	separate.step();
	scenario_check(shared.tasks()[0].status == ScenarioTaskStatus::Waiting &&
	                   separate.tasks()[0].status == ScenarioTaskStatus::Running,
	               "attention must aggregate actual people rather than assume one operator per Console");
	GameplayScenarioBridge restored;
	restored.restore(catalog, "test.map", shared.save());
	for (auto *bridge : {&shared, &restored})
	{
		bridge->begin_tick(10, {{"test.slot", 2}}, scenario_bodies(-2000));
		bridge->supply_task_operators(
		    std::array{ScenarioTaskOperator{1, "main", 7}, ScenarioTaskOperator{2, "main", 7}});
		bridge->step();
		scenario_check(bridge->tasks()[0].status == ScenarioTaskStatus::Running,
		               "a core task cannot be delayed indefinitely by a workload forecast");
	}
	scenario_check(shared.save() == restored.save(), "task scheduling windows must continue exactly after restore");
}

int main()
{
	try
	{
		task_planning_and_repair();
		task_attention_windows();
		task_physical_completion();
		repeated_sequence();
		return scenario_journey();
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
