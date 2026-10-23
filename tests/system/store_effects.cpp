#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
import phoenix.simulation;
import phoenix.gameplay_scenario;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json source(const Json &effects, const std::vector<std::string> &groups)
{
	auto data = combat_fixture();
	data["resources"] = Json::array(
	    {{{"id", "fixture.resource.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}}});
	data["entities"][0]["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                                             {"resource", "fixture.resource.energy"},
	                                                             {"capacity", 20},
	                                                             {"initial", 3},
	                                                             {"charge_rate", 100},
	                                                             {"discharge_rate", 100}},
	                                                            {{"id", "other"},
	                                                             {"resource", "fixture.resource.energy"},
	                                                             {"capacity", 10},
	                                                             {"initial", 0},
	                                                             {"charge_rate", 100},
	                                                             {"discharge_rate", 100}}})}};
	data["effects"] = effects;
	Json entry = Json::array();
	for (const auto &group : groups)
		entry.push_back({{"action", "apply_effect_group"}, {"arguments", Json::array({1, 2, group})}});
	// Entity literals use explicit typed syntax in the state-tree input grammar.
	for (auto &task : entry)
	{
		task["arguments"][0] = {{"type", "entity"}, {"id", 1}};
		task["arguments"][1] = {{"type", "entity"}, {"id", 2}};
	}
	data["trees"].push_back(
	    {{"id", "test.store-effects"},
	     {"context", "scenario"},
	     {"initial", "wait"},
	     {"states",
	      Json::array(
	          {{{"id", "wait"},
	            {"transitions",
	             Json::array({{{"target", "apply"},
	                           {"conditions",
	                            Json::array({{{"left", {{"observation", "tick"}}}, {"op", "ge"}, {"right", 1}}})}}})}},
	           {{"id", "apply"}, {"entry", entry}}})}});
	data["maps"][0]["scenario"] = {{"tree", "test.store-effects"}};
	return data;
}
Json &body(Json &saved, EntityId id)
{
	for (auto &row : saved["bodies"])
		if (row["body"]["id"] == id)
			return row;
	throw std::runtime_error("Missing store-effect test body");
}
Json stock(const World &world, EntityId id, std::size_t index = 0)
{
	auto saved = world.save();
	return body(saved, id).at("resources").at("network").at("stores").at(index).at("stock");
}
std::int64_t energy(const World &world, EntityId id, std::size_t index = 0)
{
	return stock(world, id, index).at("quantities").value("fixture.resource.energy", std::int64_t{0});
}
Catalog catalog(const Json &data)
{
	return resolve_packs({read_pack(write_pack(fixture_pack(json_bytes(data), "test-core")))});
}
Json ordinary_operations()
{
	const auto data = source(Json::parse(R"json([
 {"id":"test.range","kind":"modifier","duration_ticks":8,
  "contributions":{"range":{"property":"sensors","delta_permille":1000}}},
 {"id":"test.stock","kind":"operations","operations":[
  {"kind":"fill","store":"energy:cell","resource":"fixture.resource.energy"},
  {"kind":"consume","store":"energy:cell","resource":"fixture.resource.energy","amount":4},
  {"kind":"add","store":"energy:cell","resource":"fixture.resource.energy","amount":1},
  {"kind":"set","store":"energy:cell","resource":"fixture.resource.energy","amount":6},
  {"kind":"transfer","from_store":"energy:cell","store":"energy:cell","resource":"fixture.resource.energy","amount":2}]},
 {"id":"test.rollback","kind":"operations","operations":[
  {"kind":"apply","effect":"test.range","equipment":"radar","grant":"bonus"},
  {"kind":"add","store":"energy:other","resource":"fixture.resource.energy","amount":2},
  {"kind":"consume","store":"energy:cell","resource":"fixture.resource.energy","amount":100}]}
])json"),
	                         {"test.stock", "test.rollback"});
	const auto content = catalog(data);
	World world;
	world.load(content, "test.map", {});
	world.tick();
	const auto pending = world.save();
	World resumed;
	resumed.restore(content, pending);
	for (auto *run : {&world, &resumed})
		run->tick();
	require(energy(world, 1) == 8 && energy(world, 2) == 1 && energy(world, 1, 1) == 0,
	        "trusted fill/consume/add/set/transfer must share staged stock and conserve transfer quantity");
	auto committed = world.save();
	require(body(committed, 1).at("property_effects").at("contributions").empty(),
	        "failed mixed modifier/store group must commit neither modifier nor earlier stock addition");
	const auto &receipts = committed.at("scenario").at("receipts");
	require(receipts.size() == 2 &&
	            receipts[0].at("status") == static_cast<unsigned>(ScenarioActionStatus::Succeeded) &&
	            receipts[1].at("status") == static_cast<unsigned>(ScenarioActionStatus::Failed),
	        "production scenario records completed and failed stock-operation receipts");
	for (int i = 0; i < 4; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save() && energy(world, 1) == 8 && energy(world, 2) == 1,
	        "restored scenario effects cannot replay completed stock changes");
	return world.save();
}
Json committed_stock()
{
	auto data = source(Json::parse(R"json([
 {"id":"test.fill","kind":"operations","operations":[
  {"kind":"fill","store":"energy:cell","resource":"fixture.resource.energy"}]},
 {"id":"test.consume-claimed","kind":"operations","operations":[
  {"kind":"consume","store":"energy:cell","resource":"fixture.resource.energy","amount":16}]},
 {"id":"test.set-claimed","kind":"operations","operations":[
  {"kind":"set","store":"energy:cell","resource":"fixture.resource.energy","amount":16}]}
])json"),
	                   {"test.fill", "test.consume-claimed", "test.set-claimed"});
	data["recipes"] = Json::array({{{"id", "test.reserve-energy"},
	                                {"label", "Reserve energy output"},
	                                {"work", 100},
	                                {"inputs", Json::object()},
	                                {"outputs", {{"fixture.resource.energy", 5}}}}});
	data["entities"][0]["processors"] = Json::array({{{"id", "reserve-energy"},
	                                                  {"equipment", "radar"},
	                                                  {"recipe", "test.reserve-energy"},
	                                                  {"work_per_second", 20},
	                                                  {"outputs", {{"fixture.resource.energy", "energy:cell"}}}}});
	const auto content = catalog(data);
	World initial;
	initial.load(content, "test.map", {});
	initial.tick();
	auto saved = initial.save();
	require(!body(saved, 1)["resources"]["network"]["stores"][0]["stock"]["capacity_claims"].empty(),
	        "production batch processor creates its real receiving-space commitment");
	World world;
	world.restore(content, saved);
	world.tick();
	require(energy(world, 1) == 15 && !stock(world, 1).at("capacity_claims").empty(),
	        "fill/consume/set respect the pending processor output commitment");
	World resumed;
	resumed.restore(content, world.save());
	for (int i = 0; i < 3; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(world.save() == resumed.save(), "stock commitments and terminal receipts survive continuation");
	return world.save();
}
void pending_receipts()
{
	const auto data = source(Json::parse(R"json([{"id":"test.fill","kind":"operations","operations":[
 {"kind":"fill","store":"energy:cell","resource":"fixture.resource.energy"}]}])json"),
	                         {"test.fill"});
	const auto content = catalog(data);
	GameplayScenarioBridge bridge;
	bridge.load(content, "test.map");
	const ScenarioActionToken token{bridge.controllers().begin()->first, 100, 0};
	const std::vector<state_tree::Value> arguments{
	    {state_tree::Type::Entity, 1, {}}, {state_tree::Type::Entity, 2, {}}, {state_tree::Type::Text, 0, "test.fill"}};
	bridge.execute(token, GameplayTreeAction::ApplyEffectGroup, arguments);
	bridge.execute(token, GameplayTreeAction::ApplyEffectGroup, arguments);
	require(bridge.pending_effect_groups().size() == 1, "pending effect group must be idempotent by execution receipt");
	const auto saved = bridge.save();
	GameplayScenarioBridge resumed;
	resumed.restore(content, "test.map", saved);
	require(resumed.pending_effect_groups().size() == 1, "pending stock execution must survive bridge continuation");
	resumed.confirm_effect_group(token);
	resumed.execute(token, GameplayTreeAction::ApplyEffectGroup, arguments);
	require(resumed.pending_effect_groups().empty(), "committed stock receipt must not recreate pending execution");
	const auto before_rejection = resumed.save();
	auto malformed = saved;
	malformed["group_requests"][0]["definition"] = "test.missing";
	rejects(
	    [&]
	    {
		    resumed.restore(content, "test.map", malformed);
	    },
	    "unknown pending operation definition rejected");
	require(resumed.save() == before_rejection, "invalid pending definition preserves the previous continuation");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array({ordinary_operations(), committed_stock()});
		pending_receipts();
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			for (auto &state : trace)
				state.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "Shared stock effect operations and continuation passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
