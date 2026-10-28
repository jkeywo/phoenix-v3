#include "../support/active_effects.hpp"
#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <limits>
import phoenix.content.effects;
import phoenix.effects.rules;
import phoenix.simulation;
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
		    rule["trigger"] = "tick";
	    },
	    "require periodic phase");
	invalid(
	    [](Json &rule)
	    {
		    rule["phase"] = "periodic";
	    },
	    "other triggers use instant or maintained phase");
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
	    "unknown property");
	invalid(
	    [](Json &rule)
	    {
		    rule["phase"] = "periodic";
		    rule["trigger"] = "tick";
		    rule["funding"] = {
		        {"shortage", "proportional"},
		        {"costs",
		         Json::array({{{"payer", "source"},
		                       {"store", "energy:cell"},
		                       {"resource", "fixture.resource.energy"},
		                       {"per_second", {{"value", 1}, {"unit", "resource:fixture.resource.energy"}}}}})}};
	    },
	    "proportional funding requires maintained modifier applies only");
	auto source = fixture();
	auto &pre = source["effects"][1];
	pre["trigger"] = "pre_shield_damage";
	pre["damage_permille"] = {{"value", 1001}, {"unit", "permille"}};
	const auto compiled = compile_rule_catalog(load(source)).at("test.rule.transfer");
	require(!materialize_rule(compiled, context(compiled)),
	        "out-of-bounds pending damage transformation refuses the entire materialization");
}
Json runtime_fixture()
{
	auto source = combat_fixture();
	source["maps"][0]["entities"] = Json::array({source["maps"][0]["entities"][0]});
	auto &ship = source["entities"][0];
	ship["resources"] = {
	    {"stores",
	     Json::array(
	         {{{"id", "cell"}, {"capacity", 1000}, {"initial", 1000}, {"charge_rate", 200}, {"discharge_rate", 200}},
	          {{"id", "marker"},
	           {"capacity", 1000},
	           {"initial", 0},
	           {"charge_rate", 1000},
	           {"discharge_rate", 1000}}})}};
	source["effects"] = Json::parse(R"json([
      {"id":"test.runtime.mod","kind":"modifier","contributions":{"range":{"property":"sensors","delta_permille":1000}}},
      {"id":"test.runtime.apply","kind":"operations","operations":[
        {"kind":"apply","target":"target","equipment":"$target","effect":"test.runtime.mod","grant":"range"}]},
      {"id":"test.runtime.maintain","kind":"rule","trigger":"maintain","phase":"maintained","effect":"test.runtime.apply"},
      {"id":"test.runtime.conditional","kind":"rule","trigger":"maintain","phase":"maintained",
        "when":{"fact":"source.enabled"},"effect":"test.runtime.apply"},
      {"id":"test.runtime.enter-op","kind":"operations","operations":[
        {"kind":"add","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
      {"id":"test.runtime.exit-op","kind":"operations","operations":[
        {"kind":"add","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":10}]},
      {"id":"test.runtime.enter","kind":"rule","trigger":"enter","effect":"test.runtime.enter-op"},
      {"id":"test.runtime.exit","kind":"rule","trigger":"exit","effect":"test.runtime.exit-op"}
    ])json");
	return source;
}
Json &definition(Json &source, const std::string &id)
{
	for (auto &effect : source["effects"])
		if (effect.at("id") == id)
			return effect;
	throw std::runtime_error("missing runtime rule fixture definition " + id);
}
Json body(const World &world, EntityId id = 1)
{
	const auto snapshot = world.save();
	for (const auto &row : snapshot.at("bodies"))
		if (row.at("body").at("id") == id)
			return row;
	throw std::runtime_error("missing runtime rule fixture body");
}
std::int64_t quantity(const World &world, std::size_t index = 0, EntityId id = 1)
{
	return body(world, id)
	    .at("resources")
	    .at("network")
	    .at("stores")
	    .at(index)
	    .at("stock")
	    .at("quantities")
	    .value("fixture.resource.energy", std::int64_t{0});
}
Json contributions(const World &world, EntityId id = 1)
{
	return modifier_contributions(world.save(), id);
}
Json portable(Json snapshot)
{
	snapshot.erase("packs");
	return snapshot;
}
void advance(World &world, int count = 1)
{
	while (count-- > 0)
		world.tick();
}
void control(World &world, const std::string &owner, const std::string &channel, std::int64_t value)
{
	const auto observed = world.observe(101);
	require(observed.has_value(), "rule fixture command retains its claimed Console");
	for (const auto &controller : observed->gameplay.at("controllers"))
		if (controller.at("id") == owner)
		{
			require(world.submit(101, OperatingCommand{1, {}, value, {}, owner, channel, controller.at("generation")}),
			        "ordinary Controller admits rule fixture command");
			return;
		}
	throw std::runtime_error("missing runtime rule fixture Controller " + owner + ": " +
	                         observed->gameplay.at("controllers").dump());
}
Json maintained_sources()
{
	auto source = runtime_fixture();
	source["entities"][0]["systems"]["radar"]["rules"] =
	    Json::array({{{"id", "constant"}, {"rule", "test.runtime.maintain"}},
	                 {{"id", "conditional"}, {"rule", "test.runtime.conditional"}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim maintained rule fixture");
	advance(world, 2);
	const auto both = contributions(world);
	require(both.size() == 2 && both[0].at("source") != both[1].at("source"),
	        "two bindings of one modifier have independent maintained source identities");
	advance(world, 3);
	require(contributions(world) == both,
	        "maintained reevaluation preserves contribution identities without accumulating grants");
	control(world, "radar", "enabled", 0);
	advance(world, 2);
	const auto remaining = contributions(world);
	require(remaining.size() == 1 &&
	            (remaining[0].at("id") == both[0].at("id") || remaining[0].at("id") == both[1].at("id")),
	        "conditional source withdrawal removes its own contribution and retains its peer");
	World resumed;
	resumed.restore(catalog, world.save());
	require(contributions(resumed) == remaining,
	        "restore rebuilds maintained contribution identity without another tick");
	control(world, "radar", "enabled", 1);
	control(resumed, "radar", "enabled", 1);
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save() && contributions(world).size() == 2,
	        "fresh eligibility restores the withdrawn source with exact continued state");
	return portable(world.save());
}
Json funding_leases()
{
	Json trace = Json::array();
	for (const auto &policy : {std::string("pause"), std::string("stop"), std::string("proportional")})
	{
		auto source = runtime_fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["radar"]["energy_per_second"] = 80;
		ship["systems"]["radar"]["power_store"] = "cell";
		ship["systems"]["radar"]["rules"] = Json::array({{{"id", "paid"}, {"rule", "test.runtime.maintain"}}});
		definition(source, "test.runtime.maintain")["funding"] = {
		    {"shortage", policy},
		    {"costs", Json::array({{{"payer", "target"},
		                            {"store", "energy:cell"},
		                            {"resource", "fixture.resource.energy"},
		                            {"per_second", {{"value", 400}, {"unit", "resource:fixture.resource.energy"}}}}})}};
		auto untreated = source;
		untreated["entities"][0]["systems"]["radar"].erase("rules");
		const auto catalog = load(source);
		World world, baseline;
		world.load(catalog, "test.map");
		baseline.load(load(untreated), "test.map");
		require(world.claim(101, 1, "drive"), "claim funded rule fixture");
		require(contributions(world).empty() && quantity(world) == 1000,
		        "paid maintained source has no unpaid initial output");
		advance(world);
		advance(baseline);
		require(contributions(world).empty(),
		        "first upkeep payment leases the next native phase rather than free current output");
		const auto leased = world.save();
		World resumed;
		resumed.restore(catalog, leased);
		advance(world, 5);
		advance(resumed, 5);
		advance(baseline, 5);
		require(world.save() == resumed.save(), "funded output lease and prepaid work restore exactly");
		if (policy == "proportional")
			require(quantity(world) == 940 && quantity(baseline) == 976 && contributions(world).size() == 1,
			        "proportional upkeep shares finite native flow and pays each leased quantum once");
		else
			require(quantity(world) == quantity(baseline) && quantity(baseline) == 976 && contributions(world).empty(),
			        "indivisible pause or stop requotes partial funding without spending the native peer's work");
		World restored_live;
		restored_live.restore(catalog, world.save());
		require(restored_live.save() == world.save(),
		        "live proportional modifier values and maintained flags survive restore before reevaluation");
		trace.push_back(portable(world.save()));
	}
	return trace;
}
Json quantity_upkeep()
{
	Json trace = Json::array();
	for (const auto fractional : {false, true})
	{
		auto source = runtime_fixture();
		source["properties"] = Json::array({{{"id", "test.work.charge"},
		                                     {"kind", "quantity"},
		                                     {"default", 3},
		                                     {"minimum", 2},
		                                     {"maximum", 10},
		                                     {"unit", "charge"}}});
		auto &radar = source["entities"][0]["systems"]["radar"];
		radar["properties"] = {{"test.work.charge", Json::object()}};
		radar["rules"] = Json::array({{{"id", "paid"}, {"rule", "test.runtime.maintain"}}});
		auto &rule = definition(source, "test.runtime.maintain");
		Json costs = Json::array({{{"payer", "source"},
		                           {"property", "test.work.charge"},
		                           {"per_second", {{"value", fractional ? 1 : 10}, {"unit", "charge"}}}},
		                          {{"payer", "target"},
		                           {"store", "energy:cell"},
		                           {"resource", "fixture.resource.energy"},
		                           {"per_second", {{"value", 20}, {"unit", "resource:fixture.resource.energy"}}}}});
		if (!fractional)
			costs.push_back({{"payer", "target"},
			                 {"property", "test.work.charge"},
			                 {"per_second", {{"value", 10}, {"unit", "charge"}}}});
		rule["funding"] = {{"shortage", "pause"}, {"costs", costs}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		advance(world);
		const auto snapshot = world.save();
		bool found = false;
		for (const auto &row : snapshot.at("custom_properties"))
			if (row.at("entity") == 1 && row.at("equipment") == "radar")
			{
				found = true;
				require(row.at("values").at("test.work.charge") == 2,
				        "upkeep consumes only spendable quantity above its authored minimum");
			}
		require(found && quantity(world) == 999, "mixed quantity and native electrical work commit one exact bundle");
		World resumed;
		resumed.restore(catalog, snapshot);
		advance(world, 24);
		advance(resumed, 24);
		require(world.save() == resumed.save(), "quantity work credits restore with exact mixed-cost continuation");
		require(quantity(world) == (fractional ? 980 : 999),
		        "canonical quantity aliases coalesce and prepaid fractions bound electrical work before allocation");
		const auto paused = contributions(world);
		require(paused.size() == 1 && paused[0].at("delta_permille") == 0,
		        "depleted property zeros the maintained contribution while pause retains its native grant identity");
		bool stopped_output = false;
		const auto depleted_snapshot = world.save();
		for (const auto &owner : depleted_snapshot.at("rules").at("ships"))
			if (owner.at("entity") == 1)
				for (const auto &[key, state] : owner.at("state").at("executions").items())
					if (state.at("binding") == "paid")
						stopped_output = state.at("output") == 0 && state.at("lease_permille") == 0;
		require(stopped_output, "depleted quantity leaves no unpaid maintained output or future paid lease");
		trace.push_back(portable(world.save()));
	}
	return trace;
}
Json invalid_runtime_quote()
{
	auto source = runtime_fixture();
	source["entities"][0]["systems"]["radar"]["rules"] =
	    Json::array({{{"id", "invalid"}, {"rule", "test.runtime.maintain"}}});
	auto &rule = definition(source, "test.runtime.maintain");
	rule["costs"] = Json::array({{{"payer", "target"},
	                              {"store", "energy:cell"},
	                              {"resource", "fixture.resource.energy"},
	                              {"amount", {{"value", 5}, {"unit", "resource:fixture.resource.energy"}}}}});
	rule["values"] = {
	    {"0",
	     {{"delta_permille",
	       {{"op", "ratio"},
	        {"args",
	         Json::array(
	             {{{"fact", "source.resource"}, {"store", "energy:cell"}, {"resource", "fixture.resource.energy"}},
	              {{"fact", "target.resource"}, {"store", "energy:marker"}, {"resource", "fixture.resource.energy"}},
	              {{"value", 1000}, {"unit", "permille"}}})}}}}}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 4);
	require(quantity(world) == 1000 && quantity(world, 1) == 0 && contributions(world).empty(),
	        "invalid native rule arithmetic commits neither an entry cost nor a partial maintained effect");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save(), "uncommittable native rule remains deterministic across restore");
	return portable(world.save());
}
Json retry_mixed_input_reservations()
{
	Json trace = Json::array();
	for (const bool property : {false, true})
		for (const auto &policy : {std::string("pause"), std::string("proportional")})
		{
			auto source = runtime_fixture();
			auto &ship = source["entities"][0];
			Json shared;
			if (property)
			{
				source["properties"] = Json::array({{{"id", "test.work.charge"},
				                                     {"kind", "quantity"},
				                                     {"default", 1},
				                                     {"maximum", 1},
				                                     {"unit", "charge"}}});
				ship["systems"]["radar"]["properties"] = {{"test.work.charge", Json::object()}};
				shared = {{"payer", "target"},
				          {"property", "test.work.charge"},
				          {"per_second", {{"value", 20}, {"unit", "charge"}}}};
			}
			else
			{
				auto &mag = ship["systems"]["mag"];
				mag["capacity"] = 1;
				mag["initial"] = 1;
				mag["ammunition_type"] = "test.runtime.supply";
				mag["reload"] = {{"strategy", "none"}};
				shared = {{"payer", "target"},
				          {"store", "mag"},
				          {"resource", "test.runtime.supply"},
				          {"per_second", {{"value", 20}, {"unit", "resource:test.runtime.supply"}}}};
			}
			auto earlier = definition(source, "test.runtime.maintain");
			earlier["id"] = "test.work.first";
			earlier["funding"] = {
			    {"shortage", policy},
			    {"costs",
			     Json::array({shared,
			                  {{"payer", "target"},
			                   {"store", "energy:marker"},
			                   {"resource", "fixture.resource.energy"},
			                   {"per_second", {{"value", 20}, {"unit", "resource:fixture.resource.energy"}}}}})}};
			auto later = earlier;
			later["id"] = "test.work.second";
			later["funding"]["costs"][1]["store"] = "energy:cell";
			source["effects"].push_back(earlier);
			source["effects"].push_back(later);
			ship["systems"]["radar"]["rules"] = Json::array(
			    {{{"id", "a-first"}, {"rule", "test.work.first"}}, {{"id", "b-second"}, {"rule", "test.work.second"}}});
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map");
			advance(world);
			require(quantity(world) == 999 && quantity(world, 1) == 0,
			        "an electrically unfunded earlier bundle releases its provisional input for a funded later bundle");
			const auto saved = world.save();
			if (property)
			{
				bool consumed = false;
				for (const auto &row : saved.at("custom_properties"))
					if (row.at("entity") == 1 && row.at("equipment") == "radar")
						consumed = row.at("values").at("test.work.charge") == 0;
				require(consumed, "retried funded bundle consumes the last shared quantity exactly once");
			}
			else
			{
				std::int64_t remaining = 0;
				const auto saved_body = body(world);
				for (const auto &[name, stock] : saved_body.at("stocks").items())
					remaining += stock.at("quantities").value("test.runtime.supply", std::int64_t{0});
				require(remaining == 0, "retried funded bundle consumes the last shared stock exactly once");
			}
			World resumed;
			resumed.restore(catalog, saved);
			advance(world);
			advance(resumed);
			const auto live = contributions(world);
			require(live.size() == 1 && live[0].at("delta_permille") == 1000,
			        "only the funded later bundle receives its next-phase maintained lease");
			advance(world, 2);
			advance(resumed, 2);
			require(world.save() == resumed.save() && quantity(world) == 999,
			        "allocation retries preserve exact credits and continuation without charging an unfunded bundle");
			trace.push_back(portable(world.save()));
		}
	return trace;
}
Json retry_survivor_capacity()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["discharge_rate"] = 100;
	source["properties"] = Json::array(
	    {{{"id", "test.work.charge"}, {"kind", "quantity"}, {"default", 100}, {"maximum", 100}, {"unit", "charge"}}});
	ship["systems"]["radar"]["properties"] = {{"test.work.charge", Json::object()}};
	const Json energy = {{"payer", "target"},
	                     {"store", "energy:cell"},
	                     {"resource", "fixture.resource.energy"},
	                     {"per_second", {{"value", 100}, {"unit", "resource:fixture.resource.energy"}}}};
	auto survivor = definition(source, "test.runtime.maintain");
	survivor["id"] = "test.work.survivor";
	survivor["funding"] = {{"shortage", "proportional"},
	                       {"costs", Json::array({energy,
	                                              {{"payer", "target"},
	                                               {"property", "test.work.charge"},
	                                               {"per_second", {{"value", 20}, {"unit", "charge"}}}}})}};
	auto loser = survivor;
	loser["id"] = "test.work.loser";
	loser["funding"] = {{"shortage", "pause"}, {"costs", Json::array({energy})}};
	source["effects"].push_back(survivor);
	source["effects"].push_back(loser);
	ship["systems"]["radar"]["rules"] = Json::array(
	    {{{"id", "a-survivor"}, {"rule", "test.work.survivor"}}, {{"id", "b-loser"}, {"rule", "test.work.loser"}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world);
	require(quantity(world) == 995,
	        "a proportional survivor uses all flow released by an indivisible competitor's rejection");
	const auto saved = world.save();
	bool paid_full = false;
	for (const auto &owner : saved.at("rules").at("ships"))
		if (owner.at("entity") == 1)
			for (const auto &[key, state] : owner.at("state").at("executions").items())
				if (state.at("binding") == "a-survivor")
					paid_full = state.at("lease_permille") == 1000;
	require(paid_full, "retired competitor resets provisional output cap before the survivor's final lease");
	World resumed;
	resumed.restore(catalog, saved);
	advance(world, 3);
	advance(resumed, 3);
	const auto live = contributions(world);
	require(world.save() == resumed.save() && quantity(world) == 980 && live.size() == 1 &&
	            live[0].at("delta_permille") == 1000,
	        "full survivor output, exact mixed payment and request-set convergence continue after restore");
	return portable(world.save());
}
Json prepaid_inputs()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["mag"]["capacity"] = 100;
	ship["systems"]["mag"]["initial"] = 100;
	ship["systems"]["mag"]["ammunition_type"] = "test.runtime.supply";
	ship["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	ship["systems"]["radar"]["rules"] = Json::array({{{"id", "bundle"}, {"rule", "test.runtime.maintain"}}});
	definition(source, "test.runtime.maintain")["funding"] = {
	    {"shortage", "pause"},
	    {"costs", Json::array({{{"payer", "target"},
	                            {"store", "energy:cell"},
	                            {"resource", "fixture.resource.energy"},
	                            {"per_second", {{"value", 3}, {"unit", "resource:fixture.resource.energy"}}}},
	                           {{"payer", "target"},
	                            {"store", "mag"},
	                            {"resource", "test.runtime.supply"},
	                            {"per_second", {{"value", 7}, {"unit", "resource:test.runtime.supply"}}}}})}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 7);
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 13);
	advance(resumed, 13);
	require(world.save() == resumed.save(),
	        "multi-input fractional work credits continue exactly across a nonperiod boundary");
	const auto stocks = body(world).at("stocks");
	std::int64_t supply = 0;
	for (const auto &[name, stock] : stocks.items())
		supply += stock.at("quantities").value("test.runtime.supply", std::int64_t{0});
	require(quantity(world) == 997 && supply == 93 && contributions(world).size() == 1,
	        "twenty funded ticks consume precisely one second of both required inputs without rounding each tick up");
	return portable(world.save());
}
Json region_initial_and_spawn()
{
	auto source = runtime_fixture();
	source["maps"][0]["regions"] =
	    Json::array({{{"id", "start"},
	                  {"shape", "circle"},
	                  {"x_mm", 0},
	                  {"y_mm", 0},
	                  {"radius_mm", 30000},
	                  {"rules", Json::array({{{"id", "entry"}, {"rule", "test.runtime.enter"}}})}}});
	source["trees"].push_back(Json::parse(R"json({"id":"test.runtime.spawn","context":"scenario","initial":"wait",
      "states":[{"id":"wait","transitions":[{"target":"spawn","conditions":[
        {"left":{"observation":"tick"},"op":"ge","right":5}]}]},
      {"id":"spawn","entry":[{"action":"spawn_entity","arguments":["test.combat","",25000,0,0,"visitor"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.runtime.spawn"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 3);
	require(quantity(world, 1) == 1, "initial sampled membership emits entry exactly once");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 7);
	advance(resumed, 7);
	require(world.save() == resumed.save() && quantity(world, 1) == 1,
	        "restore does not replay initial entry while scenario spawning continues identically");
	std::int64_t entries = 0;
	const auto snapshot = world.save();
	for (const auto &row : snapshot.at("bodies"))
		entries += quantity(world, 1, row.at("body").at("id"));
	require(world.inspect().size() == 2 && entries == 2,
	        "scenario-spawned inside actor emits one new independent entry");
	return portable(world.save());
}
Json filtered_modifier_lifetime()
{
	auto source = runtime_fixture();
	definition(source, "test.runtime.mod")["contributions"] = {
	    {"scan", {{"property", "sensors"}, {"role", "scan"}, {"delta_permille", 1000}}},
	    {"drive", {{"property", "propulsion"}, {"role", "drive"}, {"delta_permille", 500}}}};
	definition(source, "test.runtime.apply")["operations"][0]["role"] = "scan";
	source["entities"][0]["systems"]["radar"]["rules"] =
	    Json::array({{{"id", "filtered"}, {"rule", "test.runtime.maintain"}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 2);
	const auto selected = contributions(world);
	require(selected.size() == 1 && selected[0].at("name") == "scan" && selected[0].at("equipment") == "radar",
	        "mixed-role template applies only its explicitly selected compatible native contribution");
	World resumed;
	resumed.restore(catalog, world.save());
	require(contributions(resumed) == selected,
	        "filtered native grant survives immediate restore against its full mixed-role template");
	advance(world, 5);
	advance(resumed, 5);
	require(world.save() == resumed.save() && contributions(world) == selected,
	        "filtered contribution retains identity across later pruning and maintained reevaluation");
	return portable(world.save());
}
Json exact_owner_siblings()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["other-radar"] = ship["systems"]["radar"];
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "other-radar"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"other-radar"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("other-radar");
	source["effects"].push_back(Json::parse(R"json({"id":"test.runtime.remove-owner","kind":"operations",
        "operations":[{"kind":"remove","target":"owner","scope":"owner"}]})json"));
	source["effects"].push_back(Json::parse(R"json({"id":"test.runtime.remove-disabled-owner","kind":"rule",
        "trigger":"tick","phase":"periodic","period_ticks":1,"when":{"op":"not","args":[{"fact":"owner.enabled"}]},
        "effect":"test.runtime.remove-owner"})json"));
	definition(source, "test.runtime.mod")["rules"] =
	    Json::array({{{"id", "self"}, {"rule", "test.runtime.remove-disabled-owner"}}});
	source["effects"].push_back(
	    Json::parse(R"json({"id":"test.runtime.apply-siblings","kind":"operations","operations":[
        {"kind":"apply","target":"protected","equipment":"radar","effect":"test.runtime.mod","grant":"shared"},
        {"kind":"apply","target":"protected","equipment":"other-radar","effect":"test.runtime.mod","grant":"shared"}]})json"));
	source["trees"].push_back(
	    Json::parse(R"json({"id":"test.runtime.siblings-tree","context":"scenario","initial":"apply",
        "states":[{"id":"apply","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":1},{"type":"entity","id":1},"test.runtime.apply-siblings"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.runtime.siblings-tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim exact native owner removal");
	advance(world, 3);
	const auto siblings = contributions(world);
	require(siblings.size() == 2 && siblings[0].at("source") == siblings[1].at("source") &&
	            siblings[0].at("id") != siblings[1].at("id"),
	        "one parent and local grant can identify separate fitted native modifier applications");
	Json survivor;
	for (const auto &row : siblings)
		if (row.at("equipment") == "other-radar")
			survivor = row;
	control(world, "radar", "enabled", 0);
	advance(world, 3);
	require(contributions(world) == Json::array({survivor}),
	        "owner-scoped self-removal selects the disabled native application without deleting same-source siblings");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save() && contributions(world) == Json::array({survivor}),
	        "exact owner metadata and surviving sibling continue without cross-removal or replay");
	return portable(world.save());
}
Json funded_entry_once()
{
	Json trace = Json::array();
	for (bool affordable : {true, false})
	{
		auto source = runtime_fixture();
		auto &ship = source["entities"][0];
		ship["resources"]["stores"][0]["initial"] = 100;
		ship["systems"]["radar"]["rules"] = Json::array({{{"id", "startup"}, {"rule", "test.runtime.maintain"}}});
		auto &rule = definition(source, "test.runtime.maintain");
		rule["costs"] = Json::array(
		    {{{"payer", "target"},
		      {"store", "energy:cell"},
		      {"resource", "fixture.resource.energy"},
		      {"amount", {{"value", affordable ? 5 : 200}, {"unit", "resource:fixture.resource.energy"}}}}});
		rule["funding"] = {
		    {"shortage", "pause"},
		    {"costs", Json::array({{{"payer", "target"},
		                            {"store", "energy:cell"},
		                            {"resource", "fixture.resource.energy"},
		                            {"per_second", {{"value", 400}, {"unit", "resource:fixture.resource.energy"}}}}})}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive"), "claim funded startup projection");
		// Initial electrical allowance is established by the native first resource phase.
		advance(world, 2);
		const auto initial = contributions(world);
		const auto diagnostic = [&](const char *phase)
		{
			const auto view = world.observe(101);
			Json native = Json::array(), states = Json::array();
			for (const auto &system : view->systems)
				if (system.id == "radar")
					native.push_back({{"enabled", system.enabled},
					                  {"requested", system.requested},
					                  {"range", system.effective_range},
					                  {"telemetry", system.telemetry}});
			const auto snapshot = world.save();
			for (const auto &owner : snapshot.at("rules").at("ships"))
				if (owner.at("entity") == 1)
					for (const auto &[key, state] : owner.at("state").at("executions").items())
						states.push_back({{"binding", state.at("binding")},
						                  {"active", state.at("active")},
						                  {"output", state.at("output")},
						                  {"blocker", state.at("blocker")},
						                  {"lease_tick", state.at("lease_tick")},
						                  {"lease_permille", state.at("lease_permille")},
						                  {"credits", state.at("credits")}});
			return std::string(phase) + " affordable=" + std::to_string(affordable) +
			       " quantity=" + std::to_string(quantity(world)) + " rows=" + contributions(world).dump() +
			       " native=" + native.dump() + " states=" + states.dump();
		};
		bool inoperative = false;
		const auto observed = world.observe(101);
		for (const auto &system : observed->systems)
			if (system.id == "radar")
				inoperative = system.effective_range == 200000;
		if (!inoperative)
			throw std::runtime_error(diagnostic("paused startup supplied unpaid native output"));
		if (!(quantity(world) == (affordable ? 95 : 100) && initial.size() == (affordable ? 1 : 0)))
			throw std::runtime_error(diagnostic("funded entry cost or initial ownership mismatch"));
		World resumed;
		resumed.restore(catalog, world.save());
		require(world.save() == resumed.save(),
		        "paused startup ownership restores immediately without repaying its entry");
		advance(world, 5);
		advance(resumed, 5);
		if (!(world.save() == resumed.save() && quantity(world) == (affordable ? 95 : 100) &&
		      contributions(world) == initial))
			throw std::runtime_error(diagnostic("blocked upkeep changed startup ownership or stock"));
		trace.push_back(portable(world.save()));
	}
	return trace;
}
Json finite_maintained_grants()
{
	Json trace = Json::array();
	for (const auto &kind : {std::string("shield"), std::string("ability"), std::string("periodic")})
	{
		auto source = runtime_fixture();
		auto &ship = source["entities"][0];
		// A body-level periodic condition requires an authored native destructible target.
		ship["durability"] = 100;
		ship["rules"] = Json::array({{{"id", "finite"}, {"rule", "test.runtime.maintain"}}});
		Json operation = {
		    {"kind", "apply"}, {"target", "target"}, {"effect", "test.finite.effect"}, {"grant", "finite"}};
		if (kind == "shield")
			source["effects"].push_back(
			    Json::parse(R"json({"id":"test.finite.effect","kind":"shield","duration_ticks":4,
                "contributions":{"cover":{"capacity":10,"initial":10,"warmup_ticks":1,"recharge_per_second":0,
                    "off_recharge_per_second":0,"sustain_energy_per_second":0}}})json"));
		else if (kind == "ability")
		{
			source["effects"].push_back(
			    Json::parse(R"json({"id":"test.finite.ability","kind":"ability","label":"Finite grant",
                "target":"self","activation_effect":"test.runtime.enter-op"})json"));
			source["effects"].push_back({{"id", "test.finite.effect"},
			                             {"kind", "ability_grant"},
			                             {"ability", "test.finite.ability"},
			                             {"duration_ticks", 4}});
			operation["equipment"] = "radar";
		}
		else
		{
			source["effects"].push_back({{"id", "test.finite.tick"},
			                             {"kind", "rule"},
			                             {"trigger", "tick"},
			                             {"phase", "periodic"},
			                             {"period_ticks", 2},
			                             {"effect", "test.runtime.enter-op"}});
			source["effects"].push_back({{"id", "test.finite.effect"},
			                             {"kind", "periodic"},
			                             {"duration_ticks", 4},
			                             {"rules", Json::array({{{"id", "pulse"}, {"rule", "test.finite.tick"}}})}});
		}
		definition(source, "test.runtime.apply")["operations"] = Json::array({operation,
		                                                                      {{"kind", "apply"},
		                                                                       {"target", "target"},
		                                                                       {"equipment", "radar"},
		                                                                       {"effect", "test.runtime.mod"},
		                                                                       {"grant", "live"}}});
		definition(source, "test.runtime.maintain")["values"] = {
		    {"1",
		     {{"delta_permille",
		       {{"op", "ratio"},
		        {"args",
		         Json::array(
		             {{{"fact", "target.resource"}, {"store", "energy:cell"}, {"resource", "fixture.resource.energy"}},
		              {{"value", 1000}, {"unit", "resource:fixture.resource.energy"}},
		              {{"value", 1000}, {"unit", "permille"}}})}}}}}};
		source["effects"].push_back(Json::parse(R"json({"id":"test.finite.change","kind":"operations","operations":[
            {"kind":"consume","target":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":1}]})json"));
		source["trees"].push_back(
		    Json::parse(R"json({"id":"test.finite.change-tree","context":"scenario","initial":"wait",
            "states":[{"id":"wait","transitions":[{"target":"change","conditions":[
                {"left":{"observation":"tick"},"op":"ge","right":5}]}]},
            {"id":"change","entry":[{"action":"apply_effect_group","arguments":[
                {"type":"entity","id":1},{"type":"entity","id":1},"test.finite.change"]}]}]})json"));
		source["maps"][0]["scenario"] = {{"tree", "test.finite.change-tree"}};
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		advance(world, 2);
		const auto instances = [&](const World &instance)
		{
			Json result = Json::array();
			const auto saved = instance.save();
			if (kind == "shield")
				result = body(instance).at("shields").at("contributions");
			else if (kind == "periodic")
				result = saved.at("active_effects").at("conditions");
			else
				for (const auto &owner : saved.at("abilities").at("ships"))
					if (owner.at("ship") == 1)
						for (const auto &[id, grant] : owner.at("state").at("grants").items())
							if (grant.at("definition") == "test.finite.ability")
								result.push_back(grant);
			return result;
		};
		const auto applied = instances(world);
		require(applied.size() == 1, "maintained source creates one finite native application");
		const auto expiry = applied[0].at("expiry");
		World resumed;
		resumed.restore(catalog, world.save());
		require(instances(resumed) == applied, "finite native lifetime restores without refreshing its expiry");
		advance(world);
		advance(resumed);
		require(world.save() == resumed.save() && instances(world).size() == 1 &&
		            instances(world)[0].at("expiry") == expiry,
		        "static maintained reevaluation does not refresh an already applied finite native lifetime");
		advance(world, 6);
		advance(resumed, 6);
		require(world.save() == resumed.save() && instances(world).empty() && quantity(world) == 999 &&
		            contributions(world).size() == 1,
		        "live modifier updates do not recreate finite grants after their original native lifetime expires");
		const auto marker = quantity(world, 1);
		advance(world, 4);
		advance(resumed, 4);
		require(world.save() == resumed.save() && instances(world).empty() && quantity(world, 1) == marker,
		        "unchanged maintained source cannot recreate expired grants or repeat their independent consequences");
		trace.push_back(portable(world.save()));
	}
	return trace;
}
Json nested_owner_retirement()
{
	auto source = runtime_fixture();
	for (const auto &id : {std::string("a"), std::string("b"), std::string("c")})
		source["effects"].push_back(
		    {{"id", "test.chain." + id},
		     {"kind", "modifier"},
		     {"contributions", {{"scan", {{"property", "sensors"}, {"delta_permille", 100}}}}}});
	source["effects"].push_back(Json::parse(R"json({"id":"test.chain.remove-owner","kind":"operations",
        "operations":[{"kind":"remove","target":"owner","scope":"owner"}]})json"));
	source["effects"].push_back(Json::parse(R"json({"id":"test.chain.remove-later","kind":"rule","trigger":"tick",
        "phase":"periodic","period_ticks":8,"effect":"test.chain.remove-owner"})json"));
	for (const auto &child : {std::string("b"), std::string("c")})
	{
		source["effects"].push_back({{"id", "test.chain.apply-" + child},
		                             {"kind", "operations"},
		                             {"operations", Json::array({{{"kind", "apply"},
		                                                          {"target", "owner"},
		                                                          {"equipment", "$owner"},
		                                                          {"effect", "test.chain." + child},
		                                                          {"grant", "child"}}})}});
		source["effects"].push_back({{"id", "test.chain.maintain-" + child},
		                             {"kind", "rule"},
		                             {"trigger", "maintain"},
		                             {"phase", "maintained"},
		                             {"effect", "test.chain.apply-" + child}});
	}
	definition(source, "test.chain.a")["rules"] =
	    Json::array({{{"id", "child"}, {"rule", "test.chain.maintain-b"}},
	                 {{"id", "retire"}, {"rule", "test.chain.remove-later"}}});
	definition(source, "test.chain.b")["rules"] = Json::array({{{"id", "child"}, {"rule", "test.chain.maintain-c"}}});
	source["effects"].push_back(Json::parse(R"json({"id":"test.chain.start","kind":"operations","operations":[
        {"kind":"apply","target":"protected","equipment":"radar","effect":"test.chain.a","grant":"parent"}]})json"));
	source["trees"].push_back(Json::parse(R"json({"id":"test.chain.tree","context":"scenario","initial":"apply",
        "states":[{"id":"apply","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":1},{"type":"entity","id":1},"test.chain.start"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.chain.tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	for (int tick = 0; tick < 6 && contributions(world).size() < 3; ++tick)
		world.tick();
	require(contributions(world).size() == 3,
	        "independent native parent establishes two levels of owned maintained children before retirement");
	World resumed;
	resumed.restore(catalog, world.save());
	bool retired = false;
	for (int tick = 0; tick < 12 && !retired; ++tick)
	{
		world.tick();
		resumed.tick();
		bool parent = false;
		for (const auto &row : contributions(world))
			parent = parent || row.at("definition") == "test.chain.a";
		if (!parent)
		{
			require(contributions(world).empty() && world.save() == resumed.save(),
			        "late parent self-removal retires existing child dependency clocks to a fixed point at the same "
			        "boundary");
			World boundary;
			boundary.restore(catalog, world.save());
			require(
			    boundary.save() == world.save(),
			    "boundary after native parent and descendant withdrawal immediately restores without orphan clocks");
			advance(world, 3);
			advance(resumed, 3);
			advance(boundary, 3);
			require(world.save() == resumed.save() && world.save() == boundary.save() && contributions(world).empty(),
			        "retired dependency chain cannot recreate native grants or replay work after continuation");
			retired = true;
		}
	}
	require(retired, "native periodic parent self-removal executes after child ownership becomes active");
	return portable(world.save());
}
Json native_output_facts()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["resources"]["stores"][0]["initial"] = 0;
	ship["systems"]["radar"]["power_store"] = "cell";
	ship["systems"]["radar"]["energy_per_second"] = 80;
	ship["systems"]["radar"]["rules"] =
	    Json::array({{{"id", "native-output"}, {"rule", "test.runtime.maintain"}, {"equipment", "mount"}}});
	definition(source, "test.runtime.mod")["contributions"]["range"]["property"] = "traverse";
	definition(source, "test.runtime.maintain")["when"] = {
	    {"op", "all"},
	    {"args",
	     Json::array(
	         {{{"op", "eq"},
	           {"args", Json::array({{{"fact", "source.output"}}, {{"value", 0}, {"unit", "permille"}}})}},
	          {{"op", "eq"},
	           {"args", Json::array({{{"fact", "target.output"}}, {{"value", 1000}, {"unit", "permille"}}})}}})}};
	source["effects"].push_back(Json::parse(R"json({"id":"test.runtime.power-refill","kind":"operations","operations":[
        {"kind":"add","target":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":1000}]})json"));
	source["trees"].push_back(Json::parse(R"json({"id":"test.runtime.power-tree","context":"scenario","initial":"wait",
        "states":[{"id":"wait","transitions":[{"target":"refill","conditions":[
            {"left":{"observation":"tick"},"op":"ge","right":3}]}]},
        {"id":"refill","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":1},{"type":"entity","id":1},"test.runtime.power-refill"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.runtime.power-tree"}};
	source["entities"][0]["systems"]["radar"]["powerless_start"] = true;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	advance(world, 2);
	require(quantity(world) == 0 && contributions(world).size() == 1 &&
	            contributions(world)[0].at("equipment") == "mount",
	        "source output distinguishes an enabled powerless radar from its independently available target turret");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 6);
	advance(resumed, 6);
	require(world.save() == resumed.save() && quantity(world) > 0 && quantity(world) < 1000 &&
	            contributions(world).empty(),
	        "native funding changes the source output fact and withdraws its condition without borrowing the rule's "
	        "own lease");
	return portable(world.save());
}
Json periodic_stop_transition()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["mag"]["capacity"] = 100;
	ship["systems"]["mag"]["initial"] = 0;
	ship["systems"]["mag"]["ammunition_type"] = "test.runtime.supply";
	ship["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	ship["systems"]["radar"]["rules"] = Json::array({{{"id", "periodic"}, {"rule", "test.runtime.periodic"}}});
	source["effects"].push_back(Json::parse(R"json({"id":"test.runtime.periodic","kind":"rule","trigger":"tick",
        "phase":"periodic","period_ticks":3,"when":{"fact":"owner.enabled"},"effect":"test.runtime.enter-op",
        "funding":{"shortage":"stop","costs":[{"payer":"target","store":"mag","resource":"test.runtime.supply",
            "per_second":{"value":20,"unit":"resource:test.runtime.supply"}}]}})json"));
	source["effects"].push_back(Json::parse(R"json({"id":"test.runtime.refill","kind":"operations","operations":[
        {"kind":"add","target":"protected","store":"mag","resource":"test.runtime.supply","amount":10}]})json"));
	source["trees"].push_back(Json::parse(R"json({"id":"test.runtime.refill-tree","context":"scenario","initial":"wait",
        "states":[{"id":"wait","transitions":[{"target":"refill","conditions":[
            {"left":{"observation":"tick"},"op":"ge","right":4}]}]},
        {"id":"refill","entry":[{"action":"apply_effect_group","arguments":[
            {"type":"entity","id":1},{"type":"entity","id":1},"test.runtime.refill"]}]}]})json"));
	source["maps"][0]["scenario"] = {{"tree", "test.runtime.refill-tree"}};
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim stopped periodic source");
	advance(world, 8);
	const auto supply = [](const World &instance)
	{
		std::int64_t result = 0;
		const auto snapshot = body(instance);
		for (const auto &[name, stock] : snapshot.at("stocks").items())
			result += stock.at("quantities").value("test.runtime.supply", std::int64_t{0});
		return result;
	};
	require(
	    quantity(world, 1) == 0 && supply(world) == 10,
	    "refilling a stopped periodic source cannot restart output or work purchases without a fresh eligibility edge");
	control(world, "radar", "enabled", 0);
	advance(world);
	World resumed;
	resumed.restore(catalog, world.save());
	control(world, "radar", "enabled", 1);
	control(resumed, "radar", "enabled", 1);
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save() && quantity(world, 1) == 0 && supply(world) < 10,
	        "fresh periodic eligibility purchases a new lease and waits a whole cadence before producing output");
	advance(world);
	advance(resumed);
	require(world.save() == resumed.save() && quantity(world, 1) == 1,
	        "stopped periodic rule resumes once after the new full period with exactly continued funding");
	return portable(world.save());
}
Json queue_capacity()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["rules"] = Json::array();
	for (int i = 0; i < 64; ++i)
		ship["rules"].push_back({{"id", "post-" + std::to_string(i)}, {"rule", "test.runtime.saturated"}});
	source["effects"].push_back(
	    {{"id", "test.runtime.saturated"},
	     {"kind", "rule"},
	     {"trigger", "post_entity_damage"},
	     {"effect", "test.runtime.enter-op"},
	     {"costs", Json::array({{{"payer", "subject"},
	                             {"store", "energy:cell"},
	                             {"resource", "fixture.resource.energy"},
	                             {"amount", {{"value", 1}, {"unit", "resource:fixture.resource.energy"}}}}})},
	     {"values",
	      {{"0",
	        {{"amount",
	          {{"op", "divide"},
	           {"args", Json::array({{{"value", 1}, {"unit", "resource:fixture.resource.energy"}}, 0})}}}}}}}});
	Json damages = Json::array();
	for (int i = 0; i < 32; ++i)
		damages.push_back({{"kind", "damage"}, {"target", "protected"}, {"damage", 1}});
	Json tasks = Json::array();
	for (int i = 0; i < 5; ++i)
	{
		// Distinct authored definitions are distinct commands; identical writes
		// intentionally coalesce in the state-tree output journal.
		const auto id = "test.runtime.hit-batch." + std::to_string(i);
		source["effects"].push_back({{"id", id}, {"kind", "operations"}, {"operations", damages}});
		tasks.push_back(
		    {{"action", "apply_effect_group"},
		     {"arguments", Json::array({{{"type", "entity"}, {"id", 1}}, {{"type", "entity"}, {"id", 1}}, id})}});
	}
	source["trees"].push_back({{"id", "test.runtime.saturate"},
	                           {"context", "scenario"},
	                           {"initial", "hit"},
	                           {"states", Json::array({{{"id", "hit"}, {"entry", tasks}}})}});
	source["maps"][0]["scenario"] = {{"tree", "test.runtime.saturate"}};
	// Repeated native hits retain their ordinary compartment clamp semantics;
	// leave enough authored integrity for all admitted one-point hits.
	for (auto &compartment : ship["combat"]["decks"][0]["compartments"])
		compartment["hp"] = 1000;
	// This queue fixture measures admitted damage, without concurrent repair work.
	for (auto &controller : ship["control_layout"]["controllers"])
		if (ship["systems"][controller.at("id").get<std::string>()].at("kind") == "repair_team")
			controller["commands"][0]["initial"] = 0;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	const auto integrity = [](const World &instance)
	{
		std::int64_t total = 0;
		const auto snapshot = body(instance);
		for (const auto &compartment : snapshot.at("combat").at("compartments"))
			total += compartment.at("hp").get<std::int64_t>();
		return total;
	};
	const auto original = integrity(world);
	bool saturated = false;
	for (int tick = 0; tick < 5 && !saturated; ++tick)
	{
		world.tick();
		const auto snapshot = world.save();
		for (const auto &receipt : snapshot.at("rules").at("results"))
			saturated = saturated || receipt.at("result").get<std::string>().find("capacity") != std::string::npos;
	}
	if (!(saturated && integrity(world) == original - 160 && quantity(world) == 1000 && quantity(world, 1) == 0))
	{
		const auto snapshot = world.save();
		Json samples = Json::array();
		for (const auto &receipt : snapshot.at("rules").at("results"))
			if (samples.size() < 5 && receipt.at("result") != "committed")
				samples.push_back(receipt);
		throw std::runtime_error(
		    "bounded queue: saturated=" + std::to_string(saturated) + " integrity=" + std::to_string(integrity(world)) +
		    " original=" + std::to_string(original) + " energy=" + std::to_string(quantity(world)) +
		    " marker=" + std::to_string(quantity(world, 1)) +
		    " pending=" + std::to_string(snapshot.at("rules").at("pending").size()) +
		    " results=" + std::to_string(snapshot.at("rules").at("results").size()) + " failures=" + samples.dump());
	}
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 2);
	advance(resumed, 2);
	require(world.save() == resumed.save() && integrity(world) == original - 160 && quantity(world) == 1000 &&
	            quantity(world, 1) == 0,
	        "uncommittable saturated post jobs neither charge costs nor replay native damage after continuation");
	return portable(world.save());
}
Json boundary_departure_order()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	source["maps"][0]["entities"][0]["x_mm"] = 1000;
	ship["systems"]["engine"] = {
	    {"kind", "engine"}, {"compartment", "fore"}, {"max_speed_mm_s", 200000}, {"acceleration_mm_s2", 4000000}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "engine"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 200000}, {"consumers", {"engine"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("engine");
	source["effects"].push_back(Json::parse(R"json({"id":"test.boundary.arrival-op","kind":"operations","operations":[
        {"kind":"consume","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":10},
        {"kind":"add","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]})json"));
	source["effects"].push_back({{"id", "test.boundary.arrival"},
	                             {"kind", "rule"},
	                             {"trigger", "enter"},
	                             {"effect", "test.boundary.arrival-op"}});
	source["maps"][0]["regions"] =
	    Json::array({{{"id", "a-departure"},
	                  {"shape", "rectangle"},
	                  {"x_mm", 0},
	                  {"y_mm", 0},
	                  {"half_width_mm", 1000},
	                  {"half_height_mm", 1000},
	                  {"rules", Json::array({{{"id", "exit"}, {"rule", "test.runtime.exit"}}})}},
	                 {{"id", "b-arrival"},
	                  {"shape", "circle"},
	                  {"x_mm", 5000},
	                  {"y_mm", 0},
	                  {"radius_mm", 1000},
	                  {"rules", Json::array({{{"id", "enter"}, {"rule", "test.boundary.arrival"}}})}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim boundary departure propulsion");
	require(quantity(world, 1) == 0, "inclusive starting boundary does not invent an unbound entry consequence");
	advance(world);
	require(world.inspect().at(0).x >= 11000 && quantity(world, 1) == 1,
	        "zero-width boundary departure exits at fraction zero before entering another region along the executed "
	        "movement");
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save() && quantity(world, 1) == 1,
	        "ordered boundary departure and later arrival do not replay at the next phase or after restore");
	return portable(world.save());
}
Json swept_annulus()
{
	auto source = runtime_fixture();
	auto &ship = source["entities"][0];
	ship["systems"]["engine"] = {
	    {"kind", "engine"}, {"compartment", "fore"}, {"max_speed_mm_s", 200000}, {"acceleration_mm_s2", 4000000}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "engine"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 200000}, {"consumers", {"engine"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("engine");
	source["maps"][0]["regions"] = Json::array(
	    {{{"id", "ring"},
	      {"shape", "annulus"},
	      {"x_mm", 5000},
	      {"y_mm", 0},
	      {"radius_mm", 3000},
	      {"inner_radius_mm", 1000},
	      {"rules", Json::array({{{"id", "entry"}, {"rule", "test.runtime.enter"}},
	                             {{"id", "exit"}, {"rule", "test.runtime.exit"}},
	                             {{"id", "maintain"}, {"rule", "test.runtime.maintain"}, {"equipment", "radar"}}})}}});
	ship["resources"]["stores"].push_back(
	    {{"id", "order"}, {"capacity", 10}, {"initial", 0}, {"charge_rate", 1000}, {"discharge_rate", 1000}});
	definition(source, "test.runtime.enter-op")["operations"].push_back(
	    {{"kind", "fill"}, {"target", "subject"}, {"store", "energy:order"}, {"resource", "fixture.resource.energy"}});
	definition(source, "test.runtime.exit-op")["operations"].push_back({{"kind", "consume"},
	                                                                    {"target", "subject"},
	                                                                    {"store", "energy:order"},
	                                                                    {"resource", "fixture.resource.energy"},
	                                                                    {"amount", 1}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim annulus crossing propulsion");
	advance(world);
	require(world.inspect().at(0).x >= 10000 && contributions(world).empty() && quantity(world, 1) == 22 &&
	            quantity(world, 2) == 9,
	        "current motion phase executes enter exit enter exit in order while endpoint maintenance stays absent");
	control(world, "engine", "speed", 0);
	advance(world);
	World resumed;
	resumed.restore(catalog, world.save());
	advance(world, 3);
	advance(resumed, 3);
	require(world.save() == resumed.save() && quantity(world, 1) == 22 && contributions(world).empty(),
	        "annulus passage consequences remain exactly once after restore without leaking maintained grants");
	return portable(world.save());
}
#include "../support/rule_persistence_cases.hpp"
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto run = [](const char *name, const auto &test) -> Json
		{
			try
			{
				return test();
			}
			catch (const std::exception &error)
			{
				throw std::runtime_error(std::string(name) + ": " + error.what());
			}
		};
		const Json trace = {run("compiler", compiler_contract),
		                    run("maintained sources", maintained_sources),
		                    run("funding leases", funding_leases),
		                    run("quantity upkeep", quantity_upkeep),
		                    run("mixed reservation retry", retry_mixed_input_reservations),
		                    run("survivor capacity retry", retry_survivor_capacity),
		                    run("prepaid inputs", prepaid_inputs),
		                    run("invalid runtime quote", invalid_runtime_quote),
		                    run("initial and spawned membership", region_initial_and_spawn),
		                    run("swept annulus", swept_annulus),
		                    run("boundary departure order", boundary_departure_order),
		                    run("queue capacity", queue_capacity),
		                    run("periodic stop transition", periodic_stop_transition),
		                    run("native output facts", native_output_facts),
		                    run("filtered modifier lifetime", filtered_modifier_lifetime),
		                    run("exact owner siblings", exact_owner_siblings),
		                    run("funded entry once", funded_entry_once),
		                    run("finite maintained grants", finite_maintained_grants),
		                    run("nested owner retirement", nested_owner_retirement),
		                    run("rule persistence", rule_persistence_cases)};
		invalid_contracts();
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << '\n';
		else
			std::cout << "Shared rule integration checks passed\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
