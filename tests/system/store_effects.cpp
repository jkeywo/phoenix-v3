#include "../support/active_effects.hpp"
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
	require(phoenix::test::modifier_applications(committed, 1).empty(),
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
void paid_store_aliases()
{
	for (bool fail : {false, true})
	{
		auto data = source(Json::parse(R"json([
 {"id":"test.paid","kind":"operations","payer":"radar","energy":2,"operations":[
  {"kind":"set","store":"hold","resource":"fixture.resource.energy","amount":6},
  {"kind":"consume","store":"logistics:hold","resource":"fixture.resource.energy","amount":1},
  {"kind":"add","store":"supplies","resource":"fixture.resource.energy","amount":1},
  {"kind":"consume","store":"cell","resource":"fixture.resource.energy","amount":1}]}
])json"),
		                   {"test.paid"});
		data["effects"][0]["operations"][3]["amount"] = fail ? 2 : 1;
		data["resources"][0]["tags"].push_back("cargo");
		data["resources"][0]["sizes"]["cargo"] = 1;
		auto &ship = data["entities"][0];
		ship["systems"]["radar"]["power_store"] = "cell";
		ship["resources"]["stores"][0]["operation_heat_ppm"] = 1000000;
		ship["logistics"] = Json::parse(R"json({"controller":"radar","stores":[
 {"id":"hold","compartment":"fore","stock":{"id":"supplies","capacity_tag":"cargo","capacity":10,
  "filter":{"resources":["fixture.resource.energy"]},"initial":{"fixture.resource.energy":2}}}
 ]})json");
		const auto content = catalog(data);
		World world;
		world.load(content, "test.map", {});
		world.tick();
		World resumed;
		resumed.restore(content, world.save());
		world.tick();
		resumed.tick();
		auto saved = world.save();
		const auto &owner = body(saved, 1);
		require(energy(world, 1) == (fail ? 3 : 0) &&
		            owner.at("stocks").at("logistics:hold").at("quantities").at("fixture.resource.energy") ==
		                (fail ? 2 : 6) &&
		            owner.at("resources").at("network").at("stores")[0].at("heat") == (fail ? 0 : 2),
		        "aliases share one staged balance; entry payment, native heat and physical stock commit or roll back "
		        "together");
		require(saved == resumed.save(), "mixed paid stock operation continues identically after restore");
		for (const auto *field : {"charge_left", "discharge_left"})
		{
			auto malformed = saved;
			body(malformed, 1)["resources"]["network"]["stores"][0][field] = 6;
			rejects(
			    [&]
			    {
				    resumed.restore(content, malformed);
			    },
			    "saved network allowance cannot exceed a physical tick");
			require(resumed.save() == saved, "invalid allowance cannot replace the live World");
			body(malformed, 1)["resources"]["network"]["stores"][0].erase(field);
			rejects(
			    [&]
			    {
				    resumed.restore(content, malformed);
			    },
			    "missing network allowance must not silently reset payment capacity");
		}
	}
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
	require(bridge.pending_effects().size() == 1, "pending effect group must be idempotent by execution receipt");
	const auto saved = bridge.save();
	GameplayScenarioBridge resumed;
	resumed.restore(content, "test.map", saved);
	require(resumed.pending_effects().size() == 1, "pending stock execution must survive bridge continuation");
	resumed.confirm_effect(token);
	resumed.execute(token, GameplayTreeAction::ApplyEffectGroup, arguments);
	require(resumed.pending_effects().empty(), "committed stock receipt must not recreate pending execution");
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
void shared_effect_lifecycle()
{
	const auto data = source(Json::parse(R"json([{"id":"test.fill","kind":"operations","operations":[
 {"kind":"fill","store":"energy:cell","resource":"fixture.resource.energy"}]}])json"),
	                         {"test.fill"});
	const auto content = catalog(data);
	GameplayScenarioBridge bridge;
	bridge.load(content, "test.map");
	const auto instance = bridge.controllers().begin()->first;
	using state_tree::Type;
	using state_tree::Value;
	struct Action
	{
		GameplayTreeAction action;
		std::vector<Value> arguments;
	};
	// Admit the families in the reverse of their established physical phase order.
	const std::vector<Action> actions{
	    {GameplayTreeAction::ApplyEffectGroup,
	     {{Type::Entity, 1, {}}, {Type::Entity, 2, {}}, {Type::Text, 0, "test.fill"}}},
	    {GameplayTreeAction::ApplyModifierEffect,
	     {{Type::Entity, 1, {}},
	      {Type::Text, 0, "radar"},
	      {Type::Text, 0, "test.native-modifier"},
	      {Type::Text, 0, "range"}}},
	    {GameplayTreeAction::RefreshModifierEffect,
	     {{Type::Entity, 1, {}}, {Type::Text, 0, "radar"}, {Type::Text, 0, "range"}}},
	    {GameplayTreeAction::RemoveModifierEffect,
	     {{Type::Entity, 1, {}}, {Type::Text, 0, "radar"}, {Type::Text, 0, "range"}}},
	    {GameplayTreeAction::ApplyShieldEffect,
	     {{Type::Entity, 1, {}}, {Type::Text, 0, "test.native-shield"}, {Type::Text, 0, "shield"}}},
	    {GameplayTreeAction::RefreshShieldEffect, {{Type::Entity, 1, {}}, {Type::Text, 0, "shield"}}},
	    {GameplayTreeAction::RemoveShieldEffect, {{Type::Entity, 1, {}}, {Type::Text, 0, "shield"}}}};
	for (std::size_t i = 0; i < actions.size(); ++i)
	{
		const ScenarioActionToken token{instance, 200 + i, 0};
		bridge.execute(token, actions[i].action, actions[i].arguments);
		bridge.execute(token, actions[i].action, actions[i].arguments);
	}
	const auto saved = bridge.save();
	require(saved.at("shield_requests").size() == 3 && saved.at("modifier_requests").size() == 3 &&
	            saved.at("group_requests").size() == 1 && bridge.pending_effects().size() == 7,
	        "all authored effect verbs share one idempotent queue and retain their continuation representation");
	GameplayScenarioBridge resumed;
	resumed.restore(content, "test.map", saved);
	require(resumed.save() == saved,
	        "mixed pending families round-trip without changing receipts or serialized arguments");
	const auto pending = resumed.pending_effects();
	require(pending[0].kind == ScenarioEffectKind::Shield && pending[0].operation == ScenarioEffectOperation::Apply &&
	            pending[1].operation == ScenarioEffectOperation::Refresh &&
	            pending[2].operation == ScenarioEffectOperation::Remove &&
	            pending[3].kind == ScenarioEffectKind::Modifier && pending[6].kind == ScenarioEffectKind::Group,
	        "the unified dispatcher preserves shield/modifier/group phases and token order within each phase");
	for (const auto &request : pending)
		resumed.confirm_effect(request.token, request.kind == ScenarioEffectKind::Modifier ? "target removed" : "");
	for (std::size_t i = 0; i < actions.size(); ++i)
	{
		const auto receipt = resumed.execute({instance, 200 + i, 0}, actions[i].action, actions[i].arguments);
		require(receipt.result.status ==
		            (i >= 1 && i <= 3 ? ScenarioActionStatus::Failed : ScenarioActionStatus::Succeeded),
		        "shared confirmation preserves success and failure receipts for every authored verb");
	}
	require(resumed.pending_effects().empty(), "terminal receipts cannot re-enqueue any effect family");
	const auto terminal = resumed.save();
	for (const auto *field : {"shield_requests", "modifier_requests", "group_requests"})
	{
		auto malformed = saved;
		malformed[field].erase(malformed[field].begin());
		rejects(
		    [&]
		    {
			    resumed.restore(content, "test.map", malformed);
		    },
		    "missing pending request rejected for every family");
		require(resumed.save() == terminal, "failed mixed continuation replacement remains atomic");
	}
	auto malformed = saved;
	malformed["modifier_requests"][0]["equipment"] = "other";
	rejects(
	    [&]
	    {
		    resumed.restore(content, "test.map", malformed);
	    },
	    "pending modifier arguments must match their original receipt");
	require(resumed.save() == terminal, "mismatched pending arguments leave the live continuation unchanged");
}

} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array({ordinary_operations(), committed_stock()});
		pending_receipts();
		paid_store_aliases();
		shared_effect_lifecycle();
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
