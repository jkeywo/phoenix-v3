#include "../support/fixtures.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
import phoenix.content.resources;
import phoenix.resources;
import phoenix.simulation;
#include "../support/machinery_baseline.hpp"
namespace
{
using namespace phoenix;
void check(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
Catalog fixture()
{
	auto data = Json::parse(R"json({"schema":4,"resources":[
 {"id":"test.ore","label":"Ore","tags":["cargo"],"sizes":{"cargo":2}},
 {"id":"test.reagent","label":"Reagent","tags":["cargo"],"sizes":{"cargo":1}},
 {"id":"test.product","label":"Product","tags":["cargo"],"sizes":{"cargo":3}},
 {"id":"test.waste","label":"Waste","tags":["cargo"],"sizes":{"cargo":1}}],
 "recipes":[
 {"id":"test.batch","label":"Batch","tags":["manufacture"],"inputs":{"test.ore":4,"test.reagent":2},"outputs":{"test.product":2,"test.waste":1},"work":4},
 {"id":"test.continuous","label":"Flow","tags":["manufacture"],"inputs":{"test.ore":4,"test.reagent":2},"outputs":{"test.product":2,"test.waste":2},"work":4},
 {"id":"test.free","label":"Free timed","tags":["manufacture"],"inputs":{},"outputs":{"test.product":2},"work":2}],
 "entities":[],"maps":[]})json");
	const auto pack = test::fixture_pack(json_bytes(data), "test-core");
	return resolve_packs({read_pack(write_pack(pack))});
}
StockStore store(const ResourceCatalogue &resources, const std::string &id, std::int64_t capacity,
                 const std::map<std::string, ResourceAmount> &initial = {})
{
	StockStoreDefinition definition;
	definition.id = id;
	definition.capacity_tag = "cargo";
	definition.capacity = capacity;
	definition.filter.tags = {"cargo"};
	definition.initial = initial;
	return StockStore(definition, resources);
}
ProcessorDefinition definition(const Catalog &catalog, bool continuous)
{
	return compile_processor(catalog,
	                         {{"recipe", continuous ? "test.continuous" : "test.batch"},
	                          {"recipes", {"test.batch", "test.continuous", "test.free"}},
	                          {"mode", continuous ? "continuous" : "batch"},
	                          {"work_per_second", 20},
	                          {"energy_per_second", 20},
	                          {"inputs", {{"test.ore", "input"}, {"test.reagent", "input"}}},
	                          {"outputs", {{"test.product", "output"}, {"test.waste", "output"}}}},
	                         "fabricator");
}
void batch_contracts(const Catalog &catalog, Json &trace)
{
	const auto resources = compile_resource_catalog(catalog);
	auto input = store(resources, "input", 100, {{"test.ore", 8}, {"test.reagent", 1}});
	auto output = store(resources, "output", 7);
	ProcessorStores stores{{"input", &input}, {"output", &output}};
	ResourceProcessor processor(definition(catalog, false));
	const auto before_input = input.save(), before_output = output.save();
	check(processor.prepare(stores, 1000) == 0 && input.save() == before_input && output.save() == before_output,
	      "short second ingredient must not consume first ingredient or reserve an output");
	check(input.add("test.reagent", 3), "add exact reagent");
	check(output.reserve_capacity("delivery", "test.waste", 1), "reserve concurrent delivery space");
	const auto blocked = input.save();
	check(processor.prepare(stores, 1000) == 0 && input.save() == blocked,
	      "required batch must not clip to available space");
	check(output.release_capacity("delivery"), "release only delivery claim");
	check(processor.prepare(stores, 1000) == 1000 && input.quantity("test.ore") == 4 &&
	          input.quantity("test.reagent") == 2,
	      "batch starts with all exact ingredients paid once");
	check(output.reserved_capacity() == 7 && output.quantity("test.product") == 0, "all products reserved before work");
	processor.advance(stores, 500);
	auto restored_input = store(resources, "input", 100), restored_output = store(resources, "output", 7);
	restored_input.restore(input.save());
	restored_output.restore(output.save());
	ProcessorStores resumed_stores{{"input", &restored_input}, {"output", &restored_output}};
	ResourceProcessor resumed(definition(catalog, false));
	resumed.restore(processor.save(), resumed_stores);
	for (auto *p : {&processor, &resumed})
	{
		auto &s = p == &processor ? stores : resumed_stores;
		check(p->prepare(s, 0) == 0, "zero rate cannot begin or pay work");
		check(p->prepare(s, 3500) == 3500, "paid batch resumes retained work");
		p->advance(s, 3500);
	}
	check(output.quantity("test.product") == 2 && output.quantity("test.waste") == 1 &&
	          output.capacity_claims().empty(),
	      "complete batch publishes every output exactly once");
	check(processor.save() == resumed.save() && input.save() == restored_input.save() &&
	          output.save() == restored_output.save(),
	      "paid batch continuation is exact");
	check(processor.prepare(stores, 1000) == 0 && processor.operating_rate(1000) == 0,
	      "full outputs prevent a new operation and operating payment");
	check(output.consume("test.product", 2) && output.consume("test.waste", 1), "clear completed products");
	check(processor.prepare(stores, 1000) == 1000, "start second paid batch");
	processor.advance(stores, 1000);
	check(processor.select(stores, "test.free") && input.quantity("test.ore") == 0 && output.capacity_claims().empty(),
	      "switch abandons consumed inputs and releases only unfulfilled outputs");
	check(processor.prepare(stores, 2000) == 2000, "explicit zero-input timed recipe prepares");
	processor.advance(stores, 2000);
	check(output.quantity("test.product") == 2, "free recipe still requires completed work");
	trace.push_back({{"processor", processor.save()}, {"input", input.save()}, {"output", output.save()}});
}
void continuous_contracts(const Catalog &catalog, Json &trace)
{
	const auto resources = compile_resource_catalog(catalog);
	auto input = store(resources, "input", 100, {{"test.ore", 1}, {"test.reagent", 1}});
	auto output = store(resources, "output", 8);
	ProcessorStores stores{{"input", &input}, {"output", &output}};
	ResourceProcessor processor(definition(catalog, true));
	check(processor.prepare(stores, 4000) == 1000, "continuous work limits progress to exact available ingredients");
	check(processor.operating_rate(4000) == 5, "partial supported work quotes proportional operating rate");
	processor.advance(stores, processor.funded_work(500));
	check(processor.state().progress == 500 && output.quantity("test.product") == 0 && output.reserved_capacity() == 4,
	      "fractional work retains paid inputs and required fractional product space");
	auto in2 = store(resources, "input", 100), out2 = store(resources, "output", 8);
	in2.restore(input.save());
	out2.restore(output.save());
	ProcessorStores stores2{{"input", &in2}, {"output", &out2}};
	ResourceProcessor resumed(definition(catalog, true));
	resumed.restore(processor.save(), stores2);
	for (auto *p : {&processor, &resumed})
	{
		auto &s = p == &processor ? stores : stores2;
		check(p->prepare(s, 500) == 500, "prepaid partial ingredient credit survives interruption");
		p->advance(s, 500);
		check(s.at("input")->add("test.ore", 3) && s.at("input")->add("test.reagent", 1),
		      "supply remaining ingredients");
		check(p->prepare(s, 3000) == 3000, "remaining exact continuous work prepares");
		p->advance(s, 3000);
	}
	check(input.quantity("test.ore") == 0 && input.quantity("test.reagent") == 0 &&
	          output.quantity("test.product") == 2 && output.quantity("test.waste") == 2,
	      "continuous ratios conserve exact stock");
	check(processor.save() == resumed.save() && input.save() == in2.save() && output.save() == out2.save(),
	      "continuous continuation matches");
	trace.push_back({{"processor", processor.save()}, {"input", input.save()}, {"output", output.save()}});
}
void cadence_and_shared_store(const Catalog &catalog, Json &trace)
{
	const auto resources = compile_resource_catalog(catalog);
	for (bool continuous : {false, true})
	{
		auto output = store(resources, "output", 100);
		auto input = store(resources, "input", 100);
		auto def = definition(catalog, continuous);
		def.initial_recipe = "test.free";
		ResourceProcessor processor(def);
		ProcessorStores stores{{"input", &input}, {"output", &output}};
		for (int tick = 0; tick < 4; ++tick)
		{
			const auto work = processor.work_budget(30);
			check(processor.prepare(stores, work) == work, "nondivisible work fully prepared");
			processor.advance(stores, processor.funded_work(1000));
		}
		check(output.quantity("test.product") == 6 && processor.state().progress == 0,
		      "nondivisible rate retains throughput across cycles");
		check(processor.prepare(stores, 6000) == 6000, "one tick can prepare several cycles");
		processor.advance(stores, 6000);
		check(output.quantity("test.product") == 12, "fast processor completes all supported cycles");
		trace.push_back({{"cadence", processor.save()}, {"output", output.save()}});
	}
	// A full shared store can still transform a partial exact ratio. The
	// full cycle grows occupancy and cannot fit; half a cycle can fit.
	auto shared = store(resources, "shared", 10, {{"test.reagent", 10}});
	auto def = definition(catalog, true);
	def.recipes = {RecipeDefinition{"shared", "Shared", {}, {{"test.reagent", 1}}, {{"test.waste", 2}}, 1}};
	def.initial_recipe = "shared";
	def.inputs = {{"test.reagent", "shared"}};
	def.outputs = {{"test.waste", "shared"}};
	ResourceProcessor processor(def);
	ProcessorStores stores{{"shared", &shared}};
	check(processor.prepare(stores, 1000) == 500,
	      "shared-store partial conversion uses capacity freed by its exact ingredient");
	processor.advance(stores, 500);
	check(shared.quantity("test.reagent") == 9 && shared.quantity("test.waste") == 1 && shared.occupied() == 10,
	      "shared-store conversion conserves quantities");
	trace.push_back({{"shared", shared.save()}, {"processor", processor.save()}});
	auto large = store(resources, "shared", resource_limit, {{"test.reagent", resource_limit}});
	def.recipes[0].inputs = {{"test.reagent", 999999999}};
	def.recipes[0].outputs = {{"test.waste", 1000000000}};
	ResourceProcessor bounded(def);
	ProcessorStores large_stores{{"shared", &large}};
	check(bounded.prepare(large_stores, 1000000) == 999,
	      "large near-equal shared-store ratio skips its infeasible capacity plateau exactly");
	bounded.advance(large_stores, 999);
	check(large.occupied() == resource_limit && large.quantity("test.waste") == 999000000,
	      "high-bound conversion remains exact");
	trace.push_back({{"large", large.save()}, {"processor", bounded.save()}});
	auto prepaid = store(resources, "shared", 1000000, {{"test.reagent", 1000000}});
	def.recipes[0].inputs = {{"test.reagent", 1000}};
	def.recipes[0].outputs = {{"test.waste", 1000}};
	ResourceProcessor interrupted(def);
	ProcessorStores prepaid_stores{{"shared", &prepaid}};
	check(interrupted.prepare(prepaid_stores, 1000) == 1000, "prepare equal-ratio committed input");
	interrupted.advance(prepaid_stores, 999);
	check(prepaid.add("test.reagent", 1), "fill prepaid-input headroom");
	check(interrupted.prepare(prepaid_stores, 1000000) == 0,
	      "prepaid equal-ratio capacity plateau terminates with exact blockage");
	auto unsupported = def;
	unsupported.recipes[0].work = 1000000000;
	unsupported.recipes[0].inputs = {{"test.reagent", 1000000000000LL}};
	unsupported.recipes[0].outputs = {{"test.waste", 499999999999LL}, {"test.product", 500000000001LL}};
	unsupported.outputs["test.product"] = "shared";
	bool rejected = false;
	try
	{
		ResourceProcessor invalid(unsupported);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	check(rejected, "unbounded continuous fractional ratio periods reject explicitly");
}
void world_contracts(Json &trace)
{
	auto source = test::combat_fixture();
	auto &ship = source["entities"][0];
	source["resources"] = Json::array();
	for (const auto *id : {"test.ore", "test.product"})
		source["resources"].push_back({{"id", id}, {"label", id}, {"tags", {"cargo"}}, {"sizes", {{"cargo", 1}}}});
	source["recipes"] = Json::array({{{"id", "test.flow"},
	                                  {"label", "Flow"},
	                                  {"inputs", {{"test.ore", 1}}},
	                                  {"outputs", {{"test.product", 2}}},
	                                  {"work", 2}}});
	const auto stock = [](const std::string &resource, int capacity, int initial)
	{
		return Json{{"capacity_tag", "cargo"},
		            {"capacity", capacity},
		            {"filter", {{"resources", {resource}}}},
		            {"initial", initial ? Json{{resource, initial}} : Json::object()}};
	};
	ship["logistics"] = {{"controller", "team-a"},
	                     {"units_per_second", 20},
	                     {"concurrency", 1},
	                     {"batch_size", 1},
	                     {"stores", Json::array({{{"id", "input"},
	                                              {"compartment", "aft"},
	                                              {"controller", "team-a"},
	                                              {"main", true},
	                                              {"stock", stock("test.ore", 20, 20)}},
	                                             {{"id", "output"},
	                                              {"compartment", "aft"},
	                                              {"controller", "team-a"},
	                                              {"stock", stock("test.product", 6, 0)}}})}};
	ship["processors"] = Json::array({{{"id", "flow"},
	                                   {"equipment", "team-a"},
	                                   {"recipe", "test.flow"},
	                                   {"mode", "continuous"},
	                                   {"work_per_second", 30},
	                                   {"energy_per_second", 40},
	                                   {"inputs", {{"test.ore", "logistics:input"}}},
	                                   {"outputs", {{"test.product", "logistics:output"}}}}});
	ship["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	ship["systems"]["team-a"]["power_store"] = "cell";
	ship["resources"] = {
	    {"stores",
	     Json::array(
	         {{{"id", "cell"}, {"capacity", 100}, {"initial", 100}, {"charge_rate", 20}, {"discharge_rate", 20}}})}};
	const auto pack = test::fixture_pack(json_bytes(source), "test-core");
	const auto catalog = resolve_packs({read_pack(write_pack(pack))});
	World world;
	world.load(catalog, "test.map");
	world.tick();
	const auto first = world.save();
	check(first.at("bodies")[0].at("processors").at("flow").at("progress") == 750,
	      "World continuous work receives its actual scarce energy grant");
	World resumed;
	resumed.restore(catalog, first);
	for (int tick = 0; tick < 12; ++tick)
	{
		world.tick();
		resumed.tick();
	}
	check(world.save() == resumed.save(), "World processor inputs, partial funding and outputs continue exactly");
	const auto full = world.save();
	const auto &body = full.at("bodies")[0];
	check(body.at("stocks").at("logistics:output").at("quantities").at("test.product") == 6,
	      "World continuous output reaches exact capacity");
	const auto energy = body.at("resources").at("network").at("stores")[0].at("stock").at("quantities");
	for (int tick = 0; tick < 4; ++tick)
		world.tick();
	check(world.save().at("bodies")[0].at("resources").at("network").at("stores")[0].at("stock").at("quantities") ==
	          energy,
	      "World blocked output stops new processor energy payment");
	trace.push_back(world.save());
	auto process_modified = source;
	process_modified["entities"][0]["processors"][0]["energy_per_second"] = 0;
	process_modified["entities"][0]["logistics"]["stores"][1]["stock"]["capacity"] = 100;
	process_modified["effects"] =
	    Json::array({{{"id", "test.processing-rate"},
	                  {"kind", "modifier"},
	                  {"contributions", {{"rate", {{"property", "processing"}, {"delta_permille", 10}}}}}}});
	process_modified["maps"][0]["scenario"] = {{"tree", "test.processing-tree"},
	                                           {"bindings", {{"actor", {{"entity", 1}}}}}};
	process_modified["trees"].push_back(
	    Json::parse(R"json({"id":"test.processing-tree","context":"scenario","initial":"wait",
	 "states":[{"id":"wait","transitions":[{"target":"apply","conditions":[{"left":{"observation":"tick"},"op":"ge","right":1}]}]},
	 {"id":"apply","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"team-a","test.processing-rate","rate"]}]}]})json"));
	const auto process_pack = test::fixture_pack(json_bytes(process_modified), "test-core");
	const auto process_catalog = resolve_packs({read_pack(write_pack(process_pack))});
	World process_world;
	process_world.load(process_catalog, "test.map");
	process_world.tick();
	process_world.tick();
	check(process_world.save().at("bodies")[0].at("processors").at("flow").at("progress") == 1015,
	      "processing modifier targets an actual team-owned generic processor at full precision");
	trace.push_back(process_world.save());
	auto long_source = source;
	const std::string long_input = "input." + std::string(122, 'i'), long_output = "output." + std::string(121, 'o'),
	                  long_processor = "processor." + std::string(118, 'p');
	long_source["resources"][0]["id"] = long_input;
	long_source["resources"][1]["id"] = long_output;
	long_source["recipes"][0]["inputs"] = {{long_input, 1}};
	long_source["recipes"][0]["outputs"] = {{long_output, 2}};
	auto &long_ship = long_source["entities"][0];
	long_ship["logistics"]["stores"][0]["stock"]["filter"]["resources"] = {long_input};
	long_ship["logistics"]["stores"][0]["stock"]["initial"] = {{long_input, 20}};
	long_ship["logistics"]["stores"][1]["stock"]["filter"]["resources"] = {long_output};
	long_ship["processors"][0]["id"] = long_processor;
	long_ship["processors"][0]["energy_per_second"] = 0;
	long_ship["processors"][0]["inputs"] = {{long_input, "logistics:input"}};
	long_ship["processors"][0]["outputs"] = {{long_output, "logistics:output"}};
	const auto long_pack = test::fixture_pack(json_bytes(long_source), "test-core");
	const auto long_catalog = resolve_packs({read_pack(write_pack(long_pack))});
	World long_world;
	long_world.load(long_catalog, "test.map");
	long_world.tick();
	const auto long_saved = long_world.save();
	check(long_saved.at("bodies")[0].at("stocks").at("logistics:output").at("quantities").at(long_output) == 1 &&
	          !long_saved.at("bodies")[0].at("stocks").at("logistics:output").at("capacity_claims").empty(),
	      "long authored processor and resource identities produce and reserve exact output");
	World long_resumed;
	long_resumed.restore(long_catalog, long_saved);
	long_world.tick();
	long_resumed.tick();
	check(long_world.save() == long_resumed.save() &&
	          long_world.save().at("bodies")[0].at("stocks").at("logistics:output").at("quantities").at(long_output) ==
	              3,
	      "long authored identities retain exact processor claims through continuation");
	trace.push_back(long_world.save());
	auto reload_source = test::combat_fixture();
	test::materialise_fixture_resources(reload_source);
	auto &magazine = reload_source["entities"][0]["systems"]["mag"];
	magazine["stock"]["capacity"] = 20;
	magazine["stock"]["initial"] = Json::object();
	magazine["processor"]["work_per_second"] = 30;
	const auto resource = magazine["stock"]["selection"].get<std::string>();
	for (auto &recipe : reload_source["recipes"])
		if (recipe.at("id") == magazine["processor"]["recipe"])
		{
			recipe["work"] = 2;
			recipe["outputs"] = {{resource, 1}};
		}
	const auto reload_pack = test::fixture_pack(json_bytes(reload_source), "test-core");
	const auto reload_catalog = resolve_packs({read_pack(write_pack(reload_pack))});
	World reload;
	reload.load(reload_catalog, "test.map");
	for (int tick = 0; tick < 4; ++tick)
		reload.tick();
	check(reload.save().at("bodies")[0].at("stocks").at("system:mag").at("quantities").at(resource) == 3,
	      "World magazine reload preserves nondivisible rate without applying its property twice");
	trace.push_back(reload.save());
	for (const auto delta : {-10, 10})
	{
		auto modified = reload_source;
		modified["entities"][0]["systems"]["mag"]["processor"]["work_per_second"] = 20;
		for (auto &recipe : modified["recipes"])
			recipe["work"] = 100;
		modified["effects"] =
		    Json::array({{{"id", "test.small-rate"},
		                  {"kind", "modifier"},
		                  {"contributions", {{"rate", {{"property", "reload"}, {"delta_permille", delta}}}}}}});
		modified["maps"][0]["scenario"] = {{"tree", "test.rate-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
		modified["trees"].push_back(Json::parse(R"json({"id":"test.rate-tree","context":"scenario","initial":"wait",
		 "states":[{"id":"wait","transitions":[{"target":"apply","conditions":[{"left":{"observation":"tick"},"op":"ge","right":1}]}]},
		 {"id":"apply","entry":[{"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"mag","test.small-rate","rate"]}]}]})json"));
		const auto modified_pack = test::fixture_pack(json_bytes(modified), "test-core");
		const auto modified_catalog = resolve_packs({read_pack(write_pack(modified_pack))});
		World modified_world;
		modified_world.load(modified_catalog, "test.map");
		modified_world.tick();
		modified_world.tick();
		check(modified_world.save().at("bodies")[0].at("processors").at("mag").at("progress") == 2000 + delta,
		      "small signed reload modifiers retain their authored permille precision");
		World continued;
		continued.restore(modified_catalog, modified_world.save());
		for (int tick = 0; tick < 3; ++tick)
		{
			modified_world.tick();
			continued.tick();
		}
		check(modified_world.save() == continued.save(), "small signed reload rate continuation remains exact");
		trace.push_back(modified_world.save());
	}
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto catalog = fixture();
		Json trace = Json::array();
		if (argc > 1 && std::string(argv[1]) == "--baseline-json")
		{
			world_contracts(trace);
			std::cout << phoenix::test::machinery_physical_trace(trace).dump() << '\n';
			return 0;
		}
		batch_contracts(catalog, trace);
		continuous_contracts(catalog, trace);
		cadence_and_shared_store(catalog, trace);
		world_contracts(trace);
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << trace.dump() << '\n';
		else
			std::cout << "Shared recipe processors passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
