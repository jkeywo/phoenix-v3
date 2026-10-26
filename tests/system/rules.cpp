#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <limits>
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	// This compiler fixture declares its resource independently of fitted energy stores.
	source["resources"] = Json::array({{{"id", "fixture.resource.energy"},
	                                    {"label", "Test energy"},
	                                    {"tags", {"energy"}},
	                                    {"sizes", {{"energy", 1}}}}});
	source["effects"] = Json::parse(R"json([
      {"id":"test.rule.group","kind":"operations","payer":"owner","energy":1,
       "costs":[{"payer":"target","store":"energy:cell","resource":"fixture.resource.energy","amount":2}],
       "operations":[{"kind":"add","target":"target","store":"energy:cell","resource":"fixture.resource.energy","amount":1}]},
      {"id":"test.rule.transfer","kind":"rule","trigger":"activate","effect":"test.rule.group",
       "when":{"op":"all","args":[{"fact":"source.exists"},{"fact":"target.exists"}]},
       "costs":[{"payer":"source","store":"energy:cell","resource":"fixture.resource.energy",
                 "amount":{"value":3,"unit":"resource:fixture.resource.energy"}}],
       "energy":{"op":"ratio","args":[
           {"fact":"source.resource","store":"energy:cell","resource":"fixture.resource.energy"},
           {"fact":"target.resource","store":"energy:cell","resource":"fixture.resource.energy"},
           {"value":10,"unit":"energy"}]},
       "values":{"0":{"amount":{"op":"ratio","args":[
           {"fact":"source.resource","store":"energy:cell","resource":"fixture.resource.energy"},
           {"fact":"target.resource","store":"energy:cell","resource":"fixture.resource.energy"},
           {"value":20,"unit":"resource:fixture.resource.energy"}]}}}}
    ])json");
	return source;
}
RuleContext context(const RuleDefinition &rule, std::int64_t numerator = 80, std::int64_t denominator = 40)
{
	RuleContext result;
	for (const auto &fact : rule.facts)
	{
		if (fact.kind == RuleFactKind::Exists)
			result.facts.push_back(state_tree::Value::boolean(true));
		else if (fact.kind == RuleFactKind::ResourceQuantity && fact.role == RuleRole::Source)
			result.facts.push_back(state_tree::Value::integer(numerator));
		else if (fact.kind == RuleFactKind::ResourceQuantity && fact.role == RuleRole::Target)
			result.facts.push_back(state_tree::Value::integer(denominator));
		else
			throw std::runtime_error("unexpected rule fixture fact binding");
	}
	return result;
}
Json compiler_contract()
{
	const auto source = fixture();
	const auto pack = read_pack(write_pack(fixture_pack(json_bytes(source), "test-core")));
	const auto catalog = resolve_packs({pack});
	const auto rules = compile_rule_catalog(catalog);
	const auto &rule = rules.at("test.rule.transfer");
	require(rule.facts.size() == 4, "repeated source and target facts share stable typed context slots");
	const auto resolved = materialize_rule(rule, context(rule));
	require(resolved && resolved->energy == 20 && resolved->operations.size() == 1 &&
	            resolved->operations[0].amount == 40 && resolved->costs.size() == 2 &&
	            resolved->costs[0].payer == "target" && resolved->costs[0].amount == 2 &&
	            resolved->costs[1].payer == "source" && resolved->costs[1].amount == 3,
	        "typed ratio values materialize native operations while retaining distinct explicit costs");
	require(!materialize_rule(rule, context(rule, 80, 0)) &&
	            !materialize_rule(rule, context(rule, std::numeric_limits<std::int64_t>::max(), 1)),
	        "invalid arithmetic refuses the entire materialization instead of returning partial costs or effects");
	auto unavailable = context(rule);
	unavailable.facts.pop_back();
	require(!materialize_rule(rule, unavailable), "incomplete indexed context cannot materialize a rule");
	require(rule.effects.energy == 1 && rule.effects.operations[0].amount == 1 && rule.effects.costs.size() == 1,
	        "successful and failed evaluations leave the compiled effect template unchanged");
	PackDraft patch;
	patch.manifest = {
	    {"schema", 1}, {"id", "rule-mod"}, {"dependencies", {"test-core"}}, {"overrides", {"test.rule.transfer"}}};
	auto changed = source.at("effects")[1];
	changed["values"]["0"]["amount"]["args"][2]["value"] = 30;
	patch.files["definitions.json"] = json_bytes({{"schema", 4}, {"effects", Json::array({changed})}});
	const auto replacement = resolve_packs({pack, read_pack(write_pack(patch))});
	const auto modified = compile_rule_catalog(replacement).at("test.rule.transfer");
	const auto overridden = materialize_rule(modified, context(modified));
	require(overridden && overridden->operations[0].amount == 60 &&
	            materialize_rule(rule, context(rule))->operations[0].amount == 40,
	        "mod rule replacement changes authored values without mutating the original compiled catalogue");
	return {{"original", resolved->operations[0].amount}, {"override", overridden->operations[0].amount}};
}
void invalid_contracts()
{
	const auto invalid = [](const auto &mutate, const char *diagnostic)
	{
		auto source = fixture();
		mutate(source["effects"][1]);
		rejected(
		    [&]
		    {
			    (void)compile_rule_catalog(load(source));
		    },
		    diagnostic);
	};
	invalid(
	    [](Json &rule)
	    {
		    rule["energy"] = {{"value", 4}, {"unit", "ticks"}};
	    },
	    "units");
	invalid(
	    [](Json &rule)
	    {
		    rule["trigger"] = "pre_shield_damage";
		    rule["when"] = {{"op", "gt"},
		                    {"args", Json::array({{{"fact", "event.absorbed"}}, {{"value", 0}, {"unit", "damage"}}})}};
	    },
	    "unavailable before resolution");
	invalid(
	    [](Json &rule)
	    {
		    rule["energy"] = {{"fact", "source.property"}, {"property", "invented-property"}};
	    },
	    "unknown native property");
	invalid(
	    [](Json &rule)
	    {
		    rule["phase"] = "periodic";
		    rule["funding"] = {
		        {"shortage", "proportional"},
		        {"costs",
		         Json::array({{{"payer", "source"},
		                       {"store", "energy:cell"},
		                       {"resource", "fixture.resource.energy"},
		                       {"per_second", {{"value", 1}, {"unit", "resource:fixture.resource.energy"}}}}})}};
	    },
	    "proportional funding requires maintained modifier applies only");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto trace = compiler_contract();
		invalid_contracts();
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << '\n';
		else
			std::cout << "Shared rule foundation checks passed\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
