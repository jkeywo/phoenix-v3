#include "../support/active_effects.hpp"
#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <string>
#include <vector>
import phoenix.simulation;
import phoenix.gameplay_scenario;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture(const Json &effects = Json::array(), const std::vector<std::string> &groups = {})
{
	auto source = combat_fixture();
	source["resources"] = Json::array(
	    {{{"id", "fixture.resource.energy"}, {"label", "Energy"}, {"tags", {"energy"}}, {"sizes", {{"energy", 1}}}}});
	source["properties"] = Json::parse(R"json([
 {"id":"test.quantity","kind":"quantity","unit":"points","default":10,"maximum":1000},
 {"id":"test.growth","kind":"quantity","unit":"points","default":0,"maximum":1000},
 {"id":"test.decline","kind":"quantity","unit":"points","default":20,"maximum":1000},
 {"id":"test.attribute","kind":"attribute","unit":"points","default":-20,"minimum":-1000,"maximum":1000},
 {"id":"test.sample","kind":"attribute","unit":"points","default":0,"minimum":-1000,"maximum":1000},
 {"id":"test.flag","kind":"flag","default":true},
 {"id":"test.mode","kind":"enum","default":"a","choices":{"a":"A","b":"B","c":"C"}},
 {"id":"test.event","kind":"quantity","unit":"points","default":0,"maximum":1000},
 {"id":"test.events","kind":"quantity","unit":"points","default":0,"maximum":1000},
 {"id":"test.chain","kind":"quantity","unit":"points","default":0,"maximum":1000},
 {"id":"test.flag-sample","kind":"quantity","unit":"points","default":0,"maximum":1000},
 {"id":"test.mode-sample","kind":"quantity","unit":"points","default":0,"maximum":1000}
 ])json");
	auto &ship = source["entities"][0];
	ship["properties"] = Json::object();
	for (const auto &d : source["properties"])
		ship["properties"][d.at("id").get<std::string>()] = Json::object();
	ship["systems"]["radar"]["properties"] = {{"test.quantity", Json::object()}, {"test.flag", false}};
	ship["combat"]["decks"][0]["compartments"][0]["properties"] = {{"test.quantity", 23}};
	ship["personnel"] = {
	    {"departments", Json::array({{{"id", "watch"}, {"label", "Watch"}, {"station", "operations"}}})},
	    {"traits", Json::array()},
	    {"duties", Json::array()},
	    {"officers", Json::array({{{"id", "a"},
	                               {"name", "A"},
	                               {"department", "watch"},
	                               {"compartment", "fore"},
	                               {"traits", Json::array()},
	                               {"properties", {{"test.quantity", 17}, {"test.flag", false}}}}})}};
	for (const auto &controller : ship["control_layout"]["controllers"])
		ship["personnel"]["duties"].push_back({{"id", controller.at("id")},
		                                       {"label", controller.at("id")},
		                                       {"department", "watch"},
		                                       {"compartment", "fore"},
		                                       {"console", controller.at("console")},
		                                       {"controller", controller.at("id")}});
	for (const auto *team : {"team-a", "team-b"})
		ship["personnel"]["duties"].push_back({{"id", std::string(team) + "-duty"},
		                                       {"label", team},
		                                       {"department", "watch"},
		                                       {"compartment", "fore"},
		                                       {"team", team}});
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"resource", "fixture.resource.energy"},
	                                              {"capacity", 100},
	                                              {"initial", 30},
	                                              {"charge_rate", 1000},
	                                              {"discharge_rate", 1000}}})}};
	source["effects"] = effects;
	if (!groups.empty())
	{
		Json entry = Json::array();
		for (const auto &group : groups)
			entry.push_back({{"action", "apply_effect_group"},
			                 {"arguments",
			                  Json::array({{{"type", "entity"}, {"id", 1}}, {{"type", "entity"}, {"id", 2}}, group})}});
		source["trees"].push_back(
		    {{"id", "test.properties.scenario"},
		     {"context", "scenario"},
		     {"initial", "wait"},
		     {"states", Json::array({{{"id", "wait"},
		                              {"transitions",
		                               Json::array({{{"target", "apply"},
		                                             {"conditions", Json::array({{{"left", {{"observation", "tick"}}},
		                                                                          {"op", "ge"},
		                                                                          {"right", 1}}})}}})}},
		                             {{"id", "apply"}, {"entry", entry}}})}});
		source["maps"][0]["scenario"] = {{"tree", "test.properties.scenario"}};
	}
	return source;
}
Catalog content(const Json &source)
{
	return resolve_packs({read_pack(write_pack(fixture_pack(json_bytes(source), "test-core")))});
}
void ticks(World &world, int count = 1)
{
	while (count-- > 0)
		world.tick();
}
Json property_row(const World &world, EntityId entity = 1, const std::string &equipment = {},
                  const std::string &officer = {}, int compartment = -1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("custom_properties"))
		if (row.at("entity") == entity && row.at("equipment") == equipment && row.at("officer") == officer &&
		    row.at("compartment") == compartment)
			return row;
	throw std::runtime_error("Missing custom property owner entity=" + std::to_string(entity) + " equipment=" +
	                         equipment + " officer=" + officer + " compartment=" + std::to_string(compartment) +
	                         " rows=" + saved.at("custom_properties").dump());
}
Json value(const World &world, const std::string &property, EntityId entity = 1, const std::string &equipment = {},
           const std::string &officer = {}, int compartment = -1)
{
	return property_row(world, entity, equipment, officer, compartment).at("values").at(property);
}
std::int64_t energy(const World &world, EntityId entity = 1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("bodies"))
		if (row.at("body").at("id") == entity)
			return row.at("resources")
			    .at("network")
			    .at("stores")
			    .at(0)
			    .at("stock")
			    .at("quantities")
			    .value("fixture.resource.energy", std::int64_t{0});
	throw std::runtime_error("Missing test energy owner");
}
Json portable(Json saved)
{
	saved.erase("packs");
	return saved;
}
void malformed_property_capture(World &world, const Catalog &catalog, const std::string &property, bool enumeration)
{
	const auto stable = world.save();
	auto malformed = stable;
	bool changed = false;
	for (auto &job : malformed["rules"]["pending"])
		for (auto &fact : job["context"]["captured"])
			if (fact.at("key") == property && fact.at("value_type") == fact.at("type"))
			{
				if (enumeration)
					fact["text"] = "undeclared-choice";
				else
					fact["number"] = 1001;
				changed = true;
			}
	require(changed, "Malformed capture regression reaches a typed pending property event");
	bool refused = false;
	try
	{
		world.restore(catalog, malformed);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused && world.save() == stable,
	        "Pending property before/after values enforce numeric bounds and enum domains atomically");
}

Json declarations_and_restore()
{
	const auto catalog = content(fixture());
	World world;
	world.load(catalog, "test.map");
	require(value(world, "test.quantity") == 10 && value(world, "test.quantity", 1, "radar") == 10 &&
	            value(world, "test.quantity", 1, {}, "a") == 17 && value(world, "test.quantity", 1, {}, {}, 0) == 23,
	        "Actor, equipment, officer and compartment declarations initialise explicit defaults and overrides");
	require(!property_row(world, 1, "radar").at("values").contains("test.attribute"),
	        "Undeclared property remains missing");
	const auto saved = world.save();
	World restored;
	restored.restore(catalog, saved);
	ticks(world, 4);
	ticks(restored, 4);
	require(world.save() == restored.save(), "Four native property owner kinds resume without reinitialising");
	auto malformed = saved;
	for (auto &row : malformed["custom_properties"])
		if (row["entity"] == 1 && row["equipment"] == "radar")
			row["values"]["test.attribute"] = 1;
	const auto before = restored.save();
	bool refused = false;
	try
	{
		restored.restore(catalog, malformed);
	}
	catch (const std::exception &)
	{
		refused = true;
	}
	require(refused && restored.save() == before, "Undeclared saved property is refused atomically");
	return portable(world.save());
}
Json atomic_payments()
{
	const auto effects = Json::parse(R"json([
 {"id":"test.pay","kind":"operations","costs":[
 {"payer":"protected","property":"test.quantity","amount":3},
 {"payer":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":2}],
 "operations":[{"kind":"property_add","target":"attacker","property":"test.quantity","amount":4},
 {"kind":"property_set","target":"protected","equipment":"radar","property":"test.flag","value":true},
 {"kind":"property_set","target":"protected","officer":"a","property":"test.quantity","value":12}]},
 {"id":"test.rollback","kind":"operations","costs":[{"payer":"protected","property":"test.quantity","amount":2}],
 "operations":[{"kind":"property_add","target":"protected","property":"test.quantity","amount":4},
 {"kind":"consume","target":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":999}]},
 {"id":"test.self-fund","kind":"operations","costs":[{"payer":"protected","property":"test.quantity","amount":50}],
 "operations":[{"kind":"property_add","target":"protected","property":"test.quantity","amount":50}]},
 {"id":"test.alias","kind":"operations","costs":[{"payer":"protected","property":"test.quantity","amount":4},
 {"payer":"target","property":"test.quantity","amount":4}],
 "operations":[{"kind":"property_add","target":"protected","property":"test.event","amount":1}]},
 {"id":"test.missing","kind":"operations","operations":[
 {"kind":"property_add","target":"protected","property":"test.event","amount":1},
 {"kind":"property_add","target":"protected","equipment":"radar","property":"test.attribute","amount":1}]},
 {"id":"test.lower-bound","kind":"operations","costs":[{"payer":"protected","property":"test.quantity","amount":3},
 {"payer":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":1}],
 "operations":[{"kind":"property_add","target":"protected","property":"test.event","amount":1}]}
 ])json");
	auto source = fixture(
	    effects, {"test.pay", "test.rollback", "test.self-fund", "test.alias", "test.missing", "test.lower-bound"});
	source["properties"][0]["minimum"] = 5;
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 2);
	require(value(world, "test.quantity") == 7 && energy(world) == 28 && value(world, "test.quantity", 2) == 14,
	        "Mixed property/resource entry costs pay once and failed later groups conserve both balances");
	require(value(world, "test.flag", 1, "radar") == true && value(world, "test.quantity", 1, {}, "a") == 12,
	        "Explicit equipment and officer property operations resolve native owners");
	require(value(world, "test.event") == 0,
	        "Aliased quantity costs cannot overspend and missing properties roll back earlier mutations");
	require(world.save().at("scenario").at("receipts").size() == 6,
	        "Every requested test transaction has a scenario receipt");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 3);
	ticks(resumed, 3);
	require(world.save() == resumed.save() && value(world, "test.quantity") == 7,
	        "Paid one-off work does not replay on continuation");
	return portable(world.save());
}
Json rate_and_change_events()
{
	auto source = fixture(Json::parse(R"json([
 {"id":"test.event-write","kind":"operations","operations":[
 {"kind":"property_add","target":"protected","property":"test.event","amount":1},
 {"kind":"property_add","target":"protected","property":"test.event","amount":2}]},
 {"id":"test.grow-op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.growth","amount":7}]},
 {"id":"test.decline-op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.decline","amount":-3,"saturate":true}]},
 {"id":"test.grow","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.grow-op"},
 {"id":"test.decline-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.decline-op"},
 {"id":"test.event-op","kind":"operations","operations":[{"kind":"property_add","target":"subject","property":"test.events","amount":1}]},
 {"id":"test.event-rule","kind":"rule","trigger":"property_changed","effect":"test.event-op",
 "when":{"op":"all","args":[
 {"op":"eq","args":[{"fact":"event.property_before","property":"test.event"},{"value":0,"unit":"points"}]},
 {"op":"eq","args":[{"fact":"event.property_after","property":"test.event"},{"value":3,"unit":"points"}]}]}},
 {"id":"test.chain-op","kind":"operations","operations":[{"kind":"property_add","target":"subject","property":"test.chain","amount":1}]},
 {"id":"test.chain-rule","kind":"rule","trigger":"property_changed","effect":"test.chain-op",
 "when":{"op":"eq","args":[{"fact":"event.property_after","property":"test.events"},{"value":1,"unit":"points"}]}}
 ])json"),
	                      {"test.event-write"});
	source["entities"][0]["rules"] = Json::array({{{"id", "grow"}, {"rule", "test.grow"}},
	                                              {{"id", "decline"}, {"rule", "test.decline-rule"}},
	                                              {{"id", "changed"}, {"rule", "test.event-rule"}},
	                                              {{"id", "chain"}, {"rule", "test.chain-rule"}}});
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 2);
	require(value(world, "test.event") == 3 && value(world, "test.events") == 0 && value(world, "test.chain") == 0,
	        "Successful transaction coalesces property changes without recursive execution");
	malformed_property_capture(world, catalog, "test.event", false);
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world);
	ticks(resumed);
	require(value(world, "test.events") == 1 && value(world, "test.chain") == 0,
	        "One coalesced before/after event executes at next scheduled phase");
	ticks(world);
	ticks(resumed);
	require(value(world, "test.chain") == 1, "Chained property reaction defers to a later phase");
	ticks(world, 17);
	ticks(resumed, 17);
	const auto grown = value(world, "test.growth").get<std::int64_t>();
	const auto declined = value(world, "test.decline").get<std::int64_t>();
	const auto remainder = property_row(world).at("remainders");
	ticks(world, World::ticks_per_second);
	ticks(resumed, World::ticks_per_second);
	require(value(world, "test.growth") == grown + 7 && value(world, "test.decline") == declined - 3 &&
	            property_row(world).at("remainders") == remainder,
	        "Fractional positive and negative rates conserve exact integer work across whole seconds");
	require(world.save() == resumed.save(), "Queued property events and fractional remainders continue exactly");
	const auto stable = resumed.save();
	for (bool oversized : {false, true})
	{
		auto malformed = stable;
		for (auto &row : malformed["custom_properties"])
			if (row["entity"] == 1 && row["equipment"] == "" && row["officer"] == "" && row["compartment"] == -1)
			{
				if (oversized)
					for (int i = 0; i < 8193; ++i)
						row["remainders"]["oversized-" + std::to_string(i)] = 0;
				else
					row["remainders"]["invalid-rate"] = World::ticks_per_second;
			}
		bool refused = false;
		try
		{
			resumed.restore(catalog, malformed);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && resumed.save() == stable,
		        "Out-of-range or oversized rate remainders reject continuation atomically");
	}
	return portable(world.save());
}
void enabled(World &world, bool value)
{
	const auto view = world.observe(101);
	for (const auto &controller : view->gameplay.at("controllers"))
		if (controller.at("id") == "radar")
		{
			require(
			    world.submit(
			        101, OperatingCommand{1, {}, value ? 1 : 0, {}, "radar", "enabled", controller.at("generation")}),
			    "Native equipment enable command admitted");
			return;
		}
	throw std::runtime_error("Missing rate fixture radar Controller");
}
Json rate_owner_lifetimes()
{
	auto source = fixture(Json::parse(R"json([
 {"id":"test.rate.op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.growth","amount":7}]},
 {"id":"test.rate.rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.rate.op"},
 {"id":"test.rate.finite","kind":"modifier","duration_ticks":5,"contributions":{"number":{"property":"test.attribute","delta_permille":1}},"rules":[{"id":"rate","rule":"test.rate.rule"}]},
 {"id":"test.rate.start","kind":"operations","operations":[{"kind":"apply","target":"protected","effect":"test.rate.finite","grant":"rate"}]}
 ])json"),
	                      {"test.rate.start"});
	auto &states = source["trees"].back()["states"];
	const auto entry = states[1].at("entry");
	const auto transition = [](const char *target, int tick)
	{
		return Json::array(
		    {{{"target", target},
		      {"conditions", Json::array({{{"left", {{"observation", "tick"}}}, {"op", "ge"}, {"right", tick}}})}}});
	};
	states[1]["transitions"] = transition("second", 10);
	states.push_back({{"id", "second"}, {"entry", entry}, {"transitions", transition("third", 20)}});
	states.push_back({{"id", "third"}, {"entry", entry}});
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 4);
	require(!property_row(world).at("remainders").empty(),
	        "Finite native rule source owns fractional property work while alive");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 28);
	ticks(resumed, 28);
	require(property_row(world).at("remainders").empty() && modifier_applications(world.save(), 1).empty(),
	        "Repeated finite rule-source retirement releases its rate remainders rather than leaking owner rows");
	if (world.save() != resumed.save())
		throw std::runtime_error("Rate remainder retirement follows native source expiry through continuation: " +
		                         Json::diff(world.save(), resumed.save()).dump());
	auto paused = fixture(Json::parse(R"json([
 {"id":"test.pause.op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.growth","amount":7}]},
 {"id":"test.pause.rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.pause.op","when":{"fact":"source.enabled"}}
 ])json"));
	paused["entities"][0]["systems"]["radar"]["properties"]["test.growth"] = Json::object();
	paused["entities"][0]["systems"]["radar"]["rules"] = Json::array({{{"id", "rate"}, {"rule", "test.pause.rule"}}});
	const auto paused_catalog = content(paused);
	World parked;
	parked.load(paused_catalog, "test.map");
	require(parked.claim(101, 1, "drive"), "Claim rate pause fixture");
	ticks(parked, 3);
	const auto balance = value(parked, "test.growth", 1, "radar");
	const auto fraction = property_row(parked, 1, "radar").at("remainders");
	require(!fraction.empty(), "Sub-unit active rate work has a persisted remainder");
	enabled(parked, false);
	ticks(parked, 3);
	require(value(parked, "test.growth", 1, "radar") == balance &&
	            property_row(parked, 1, "radar").at("remainders") == fraction,
	        "Temporary condition ineligibility preserves already-earned fractional rate work without new output");
	World parked_resumed;
	parked_resumed.restore(paused_catalog, parked.save());
	enabled(parked, true);
	enabled(parked_resumed, true);
	ticks(parked, 20);
	ticks(parked_resumed, 20);
	require(value(parked, "test.growth", 1, "radar").get<std::int64_t>() == balance.get<std::int64_t>() + 7 &&
	            parked.save() == parked_resumed.save(),
	        "Reeligible rate source resumes its fractional work once with exact continuation");
	return portable(world.save());
}

Json distinct_officer_events()
{
	auto source = fixture(Json::parse(R"json([
 {"id":"test.officer-change","kind":"operations","operations":[
 {"kind":"property_set","target":"protected","officer":"a","property":"test.flag","value":true},
 {"kind":"property_set","target":"protected","officer":"b","property":"test.flag","value":true}]},
 {"id":"test.officer-tick-op","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.quantity","amount":1}]},
 {"id":"test.officer-tick","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.officer-tick-op"},
 {"id":"test.officer-mod","kind":"modifier","duration_ticks":30,
 "contributions":{"attribute":{"property":"test.attribute","delta_permille":500}},
 "rules":[{"id":"native-owner","rule":"test.officer-tick"}]},
 {"id":"test.officer-apply","kind":"operations","operations":[{"kind":"apply","target":"subject","effect":"test.officer-mod","grant":"property-trigger"}]},
 {"id":"test.officer-listener","kind":"rule","trigger":"property_changed","effect":"test.officer-apply",
 "when":{"fact":"event.property_after","property":"test.flag"}},
 {"id":"test.explicit-officer","kind":"operations","operations":[
 {"kind":"apply","target":"protected","officer":"a","effect":"test.officer-mod","grant":"remove-me"},
 {"kind":"remove","target":"protected","officer":"a","grant":"remove-me"}]}
 ])json"),
	                      {"test.explicit-officer", "test.officer-change"});
	auto &officers = source["entities"][0]["personnel"]["officers"];
	officers[0]["properties"]["test.attribute"] = -10;
	auto second = officers[0];
	second["id"] = "b";
	second["name"] = "B";
	second["properties"]["test.quantity"] = 23;
	officers.push_back(second);
	source["entities"][0]["rules"] = Json::array({{{"id", "flag-changes"}, {"rule", "test.officer-listener"}}});
	source["effects"].push_back(
	    {{"id", "test.rebound-op"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_add"}, {"target", "target"}, {"property", "test.quantity"}, {"amount", 1}}})}});
	source["effects"].push_back({{"id", "test.rebound"},
	                             {"kind", "rule"},
	                             {"trigger", "tick"},
	                             {"phase", "periodic"},
	                             {"effect", "test.rebound-op"}});
	officers[0]["rules"] = Json::array(
	    {{{"id", "equipment"}, {"rule", "test.rebound"}, {"equipment", "radar"}, {"eligibility", "lifetime"}},
	     {{"id", "officer"}, {"rule", "test.rebound"}, {"officer", "b"}, {"eligibility", "lifetime"}}});
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 3);
	require(value(world, "test.quantity", 1, "radar").get<std::int64_t>() > 10,
	        "Officer-owned rules explicitly rebind to equipment without retaining the original officer selector");
	const auto receipts = world.save().at("scenario").at("receipts");
	require(
	    receipts.size() == 2 && receipts.at(0).at("status") == static_cast<unsigned>(ScenarioActionStatus::Succeeded),
	    "Explicit officer modifier apply and remove resolve the same native contribution rather than an ability grant");
	const auto applications = modifier_applications(world.save(), 1);
	require(applications.size() == 2 && applications[0].at("source") != applications[1].at("source"),
	        "One listener retains independent application identities for two officer property-change subjects");
	bool first = false, second_owner = false;
	for (const auto &application : applications)
	{
		first |= application.at("equipment") == "officer:a";
		second_owner |= application.at("equipment") == "officer:b";
	}
	require(first && second_owner, "Triggered custom modifiers retain their native officer target identities");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 4);
	ticks(resumed, 4);
	require(value(world, "test.quantity", 1, {}, "a").get<std::int64_t>() > 17 &&
	            value(world, "test.quantity", 1, {}, "b").get<std::int64_t>() > 23,
	        "Native effect rules use the actual officer owner rather than a fabricated equipment identity");
	require(world.save() == resumed.save(), "Officer-targeted modifier event sources continue exactly after restore");
	return portable(world.save());
}
Json compartment_event_continuation()
{
	auto source = fixture(Json::parse(R"json([
 {"id":"test.compartment-hit","kind":"operations","operations":[{"kind":"damage","target":"protected","damage":1}]},
 {"id":"test.compartment-write","kind":"operations","operations":[{"kind":"property_add","target":"subject","property":"test.quantity","amount":1}]},
 {"id":"test.compartment-post","kind":"rule","trigger":"post_compartment_damage","effect":"test.compartment-write"},
 {"id":"test.compartment-mark","kind":"operations","operations":[{"kind":"property_add","target":"owner","property":"test.events","amount":1}]},
 {"id":"test.compartment-changed","kind":"rule","trigger":"property_changed","effect":"test.compartment-mark",
 "when":{"op":"present","args":[{"fact":"event.property_after","property":"test.quantity"}]}}
 ])json"),
	                      {"test.compartment-hit"});
	source["entities"][0]["combat"]["decks"][0]["compartments"][1]["properties"] = {{"test.quantity", 31}};
	source["entities"][0]["rules"] = Json::array({{{"id", "post-hit"}, {"rule", "test.compartment-post"}},
	                                              {{"id", "changed"}, {"rule", "test.compartment-changed"}}});
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	bool pending = false;
	for (int tick = 0; tick < 8 && !pending; ++tick)
	{
		ticks(world);
		const auto saved = world.save();
		for (const auto &job : saved.at("rules").at("pending"))
			if (job.at("rule") == "test.compartment-changed" &&
			    job.at("context").at("subject").at("compartment").get<int>() >= 0)
				pending = true;
	}
	require(pending, "Reached native damage outcome produces a queued compartment property-change event");
	require(value(world, "test.quantity", 1, {}, {}, 0).get<std::int64_t>() > 23 ||
	            value(world, "test.quantity", 1, {}, {}, 1).get<std::int64_t>() > 31,
	        "Damage hook mutates the reached native compartment property");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 3);
	ticks(resumed, 3);
	require(value(world, "test.events").get<std::int64_t>() > 0 && world.save() == resumed.save(),
	        "Pending compartment property event validates its native kind and continues exactly once after restore");
	return portable(world.save());
}

Json overrides()
{
	auto source = fixture(Json::parse(R"json([
 {"id":"test.low","kind":"modifier","duration_ticks":8,"contributions":{
 "number":{"property":"test.attribute","delta_permille":250},
 "flag":{"property":"test.flag","value":true,"priority":1},
 "mode":{"property":"test.mode","value":"b","priority":1}}},
 {"id":"test.high","kind":"modifier","duration_ticks":8,"contributions":{
 "number":{"property":"test.attribute","delta_permille":250},
 "flag":{"property":"test.flag","value":false,"priority":2},
 "mode":{"property":"test.mode","value":"c","priority":2}}},
 {"id":"test.newest","kind":"modifier","duration_ticks":8,"contributions":{
 "flag":{"property":"test.flag","value":true,"priority":2},
 "mode":{"property":"test.mode","value":"b","priority":2}}},
 {"id":"test.install","kind":"operations","operations":[
 {"kind":"apply","target":"protected","effect":"test.low","grant":"low"},
 {"kind":"apply","target":"protected","effect":"test.high","grant":"high"},
 {"kind":"apply","target":"protected","effect":"test.newest","grant":"newest"},
 {"kind":"apply","target":"protected","effect":"test.high","grant":"high"},
 {"kind":"property_set","target":"protected","property":"test.flag","value":false},
 {"kind":"property_set","target":"protected","property":"test.mode","value":"c"}]},
 {"id":"test.sample-op","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.sample","value":0}]},
 {"id":"test.sample-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.sample-op",
 "values":{"0":{"value":{"fact":"owner.property","property":"test.attribute"}}}},
 {"id":"test.flag-one","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.flag-sample","value":1}]},
 {"id":"test.flag-zero","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.flag-sample","value":0}]},
 {"id":"test.flag-on","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.flag-one","when":{"fact":"owner.property","property":"test.flag"}},
 {"id":"test.flag-off","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.flag-zero","when":{"op":"not","args":[{"fact":"owner.property","property":"test.flag"}]}},
 {"id":"test.mode-one","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.mode-sample","value":1}]},
 {"id":"test.mode-zero","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.mode-sample","value":0}]},
 {"id":"test.mode-b","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.mode-one","when":{"op":"eq","args":[{"fact":"owner.property","property":"test.mode"},{"value":"b","enum":"test.mode"}]}},
 {"id":"test.mode-other","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.mode-zero","when":{"op":"ne","args":[{"fact":"owner.property","property":"test.mode"},{"value":"b","enum":"test.mode"}]}},
 {"id":"test.mode-event","kind":"rule","trigger":"property_changed","effect":"test.mode-one","when":{"op":"eq","args":[{"fact":"event.property_after","property":"test.mode"},{"value":"b","enum":"test.mode"}]}}
 ])json"),
	                      {"test.install"});
	source["entities"][0]["rules"] = Json::array();
	for (const auto *id :
	     {"test.sample-rule", "test.flag-on", "test.flag-off", "test.mode-b", "test.mode-other", "test.mode-event"})
		source["entities"][0]["rules"].push_back({{"id", id}, {"rule", id}});
	const auto catalog = content(source);
	World world;
	world.load(catalog, "test.map");
	ticks(world, 2);
	malformed_property_capture(world, catalog, "test.mode", true);
	ticks(world, 2);
	require(value(world, "test.sample") == -30, "Signed custom attributes combine additive percentage contributions");
	require(value(world, "test.flag") == false && value(world, "test.mode") == "c" &&
	            value(world, "test.flag-sample") == 1 && value(world, "test.mode-sample") == 1,
	        "Priority then newest application wins typed overrides while masked writes update current base");
	require(modifier_applications(world.save(), 1).size() == 3,
	        "Refreshing a contribution preserves identity and does not create a fourth application");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world, 10);
	ticks(resumed, 10);
	require(value(world, "test.sample") == -20 && value(world, "test.flag-sample") == 0 &&
	            value(world, "test.mode-sample") == 0,
	        "Expired overrides expose current base and numeric attributes return to their base");
	require(world.save() == resumed.save(), "Typed override precedence and expiry continue exactly");
	return portable(world.save());
}
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
		const Json trace = {run("declarations", declarations_and_restore),
		                    run("atomic payments", atomic_payments),
		                    run("rate and change events", rate_and_change_events),
		                    run("rate owner lifetimes", rate_owner_lifetimes),
		                    run("distinct officer subjects", distinct_officer_events),
		                    run("compartment event continuation", compartment_event_continuation),
		                    run("overrides", overrides)};
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << '\n';
		else
			std::cout << "Custom property integration checks passed\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
