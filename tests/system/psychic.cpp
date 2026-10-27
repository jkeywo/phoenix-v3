#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include "../support/ftl_fixture.hpp"
#include <iostream>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	auto &e = source["entities"][0];
	e["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                           {"capacity", 1000000},
	                                           {"initial", 1000000},
	                                           {"charge_rate", 2000},
	                                           {"discharge_rate", 2000},
	                                           {"operation_heat_ppm", 100000}}})}};
	const Json profiles = {{"cloak", {{"kind", "cloak"}, {"label", "Cloak"}, {"strength_permille", 900}}},
	                       {"scramble",
	                        {{"kind", "scramble"},
	                         {"label", "Ghost ships"},
	                         {"strength_permille", 600},
	                         {"ghost_count", 2},
	                         {"scan_ticks", 6},
	                         {"speed_mm_s", 1000},
	                         {"offset_mm", 20000},
	                         {"spacing_mm", 10000},
	                         {"recovery_ticks", 5}}},
	                       {"ward", {{"kind", "ward"}, {"label", "Ward"}, {"strength_permille", 800}}}};
	if (!source.contains("effects"))
		source["effects"] = Json::array();
	for (const auto &[id, profile] : profiles.items())
	{
		auto native = profile;
		const auto kind = profile.at("kind").get<std::string>();
		native["id"] = "test.psychic." + id;
		native["kind"] = "psychic";
		native["psychic_kind"] = kind;
		native.erase("recovery_ticks");
		source["effects"].push_back(native);
		source["effects"].push_back({{"id", "test.ability." + id},
		                             {"kind", "ability"},
		                             {"label", profile.at("label")},
		                             {"sustained", true},
		                             {"target", kind == "cloak"  ? "self"
		                                        : kind == "ward" ? "ally"
		                                                         : "contact"},
		                             {"energy_per_second", 100},
		                             {"recovery_ticks", profile.value("recovery_ticks", 100)},
		                             {"recovery_on", "stop"},
		                             {"shortage", "proportional"},
		                             {"require_healthy", true},
		                             {"psychic_effect", "test.psychic." + id}});
	}
	Json commands = Json::array(
	         {{{"id", "apparatus"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"choir"}}}}),
	     seats = Json::array();
	e["personnel"] = {
	    {"departments", Json::array({{{"id", "signals"}, {"label", "Signals"}, {"station", "operations"}}})},
	    {"traits", Json::array()},
	    {"duties", Json::array()},
	    {"officers", Json::array()}};
	for (const auto &c : e["control_layout"]["controllers"])
		e["personnel"]["duties"].push_back({{"id", c.at("id")},
		                                    {"label", c.at("id")},
		                                    {"department", "signals"},
		                                    {"compartment", "fore"},
		                                    {"console", c.at("console")},
		                                    {"controller", c.at("id")}});
	for (const auto *team : {"team-a", "team-b"})
		e["personnel"]["duties"].push_back({{"id", std::string(team) + "-duty"},
		                                    {"label", team},
		                                    {"department", "signals"},
		                                    {"compartment", "fore"},
		                                    {"team", team}});
	e["personnel"]["duties"].push_back({{"id", "conductor"},
	                                    {"label", "Choir conductor"},
	                                    {"department", "signals"},
	                                    {"compartment", "fore"},
	                                    {"controller", "choir"},
	                                    {"console", "drive"}});
	for (const auto &id : {"cloak", "scramble", "ward"})
	{
		std::string key = id;
		commands.push_back({{"id", key + "-enabled"}, {"action", "intent_boolean"}, {"consumers", Json::array()}});
		commands.push_back({{"id", key + "-target"},
		                    {"action", "intent_integer"},
		                    {"minimum", 0},
		                    {"maximum", 1000000000},
		                    {"consumers", Json::array()}});
		seats.push_back({{"duty", key},
		                 {"controller", "choir"},
		                 {"console", "drive"},
		                 {"enabled", key + "-enabled"},
		                 {"target", key + "-target"}});
		e["personnel"]["duties"].push_back(
		    {{"id", key}, {"label", key}, {"department", "signals"}, {"compartment", "fore"}, {"choir", "choir"}});
		e["personnel"]["officers"].push_back({{"id", key},
		                                      {"name", key},
		                                      {"department", "signals"},
		                                      {"compartment", "fore"},
		                                      {"duty", key},
		                                      {"abilities", {"test.ability." + key}},
		                                      {"traits", Json::array()}});
	}
	e["systems"]["choir"] = {
	    {"kind", "psychic"},
	    {"compartment", "fore"},
	    {"power_store", "cell"},
	    {"range_mm", 300000},
	    {"arc_mdeg", 90000},
	    {"supported_abilities", {"test.ability.cloak", "test.ability.scramble", "test.ability.ward"}},
	    {"seats", seats}};
	e["systems"]["radar"]["capabilities"] = {{"basic_identity", 0}, {"compartment_condition", 4}};
	for (auto &c : e["control_layout"]["controllers"])
		if (c.at("id") == "radar")
			c["commands"].push_back({{"id", "target"}, {"action", "sensor_target"}, {"consumers", {"radar"}}});
	e["control_layout"]["controllers"].push_back(
	    {{"id", "choir"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
	e["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("choir");
	e["stations"][0]["consoles"][0]["feeds"].push_back("choir");
	e["stations"][0]["consoles"][1]["feeds"] = Json::array();
	source["maps"][0]["factions"] = Json::array({{{"id", "a"}, {"label", "A"}}, {{"id", "b"}, {"label", "B"}}});
	source["maps"][0]["relationships"] = Json::array({{{"a", "a"}, {"b", "b"}, {"relation", "hostile"}}});
	source["maps"][0]["entities"][0]["faction"] = "a";
	source["maps"][0]["entities"][1]["faction"] = "b";
	source["maps"][0]["entities"][2]["faction"] = "a";
	return source;
}
Json &scramble_shapes(Json &choir)
{
	for (auto &[key, contribution] : choir["state"]["contributions"].items())
		if (contribution.at("ability") == "test.ability.scramble")
			return contribution["shapes"];
	throw std::runtime_error("Missing native scramble contribution");
}
const Json &scramble_shapes(const Json &choir)
{
	for (const auto &[key, contribution] : choir.at("state").at("contributions").items())
		if (contribution.at("ability") == "test.ability.scramble")
			return contribution.at("shapes");
	throw std::runtime_error("Missing native scramble contribution");
}
void ticks(World &w, int n = 1)
{
	while (n--)
		w.tick();
}
void command(World &w, std::uint64_t claim, EntityId ship, const std::string &ctrl, const std::string &channel,
             std::int64_t value)
{
	const auto observed = w.observe(claim);
	for (const auto &c : observed->gameplay.at("controllers"))
		if (c.at("id") == ctrl)
		{
			OperatingCommand request{ship, {}, value, {}, ctrl, channel, c.at("generation")};
			if (c.at("commands").at(channel).at("action") == "ability_activate")
				request.request_id = c.at("commands").at(channel).at("request_id").get<std::uint64_t>() + 1;
			require(w.submit(claim, request), "admitted psychic or equipment command");
			return;
		}
	throw std::runtime_error("missing Controller " + ctrl);
}
std::vector<EntityId> ghosts(const World &w, std::uint64_t claim)
{
	std::vector<EntityId> out;
	const auto observed = w.observe(claim);
	for (const auto &c : observed->contacts)
		if (c.id > 3 && c.id <= 1000000000ULL)
			out.push_back(c.id);
	return out;
}
void claim(World &w)
{
	require(w.claim(101, 1, "drive"), "claim source");
	require(w.claim(102, 2, "drive"), "claim victim");
	require(w.claim(103, 3, "drive"), "claim ally");
	require(w.claim(104, 2, "observer"), "claim unfed Console");
}

Json strain_fixture(bool concurrent = false)
{
	auto source = fixture();
	source["properties"] = Json::parse(R"json([
 {"id":"test.strain","kind":"quantity","unit":"strain","default":0,"maximum":1000000},
 {"id":"test.capacity","kind":"attribute","unit":"strain","default":3000,"maximum":1000000},
 {"id":"test.gain","kind":"attribute","unit":"strain","default":2000,"maximum":1000000},
 {"id":"test.rest","kind":"attribute","unit":"strain","default":4500,"maximum":1000000},
 {"id":"test.resume","kind":"attribute","unit":"permille","default":250,"maximum":1000},
 {"id":"test.state","kind":"enum","default":"ready","choices":{"ready":"Ready","recovering":"Recovering"}}
 ])json");
	const auto rules = Json::parse(R"json([
 {"id":"test.strain.gain-op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.strain","amount":0,"saturate":true}]},
 {"id":"test.strain.rest-op","kind":"operations","operations":[{"kind":"property_rate","target":"owner","property":"test.strain","amount":0,"saturate":true}]},
 {"id":"test.strain.overload-op","kind":"operations","operations":[
 {"kind":"property_set","target":"owner","property":"test.strain","value":0},
 {"kind":"property_set","target":"owner","property":"test.state","value":"recovering"}]},
 {"id":"test.strain.resume-op","kind":"operations","operations":[{"kind":"property_set","target":"owner","property":"test.state","value":"ready"}]},
 {"id":"test.strain.gain-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.strain.gain-op",
 "when":{"op":"gt","args":[{"fact":"owner.completed_work","family":"psychic"},{"value":0,"unit":"permille"}]},
 "values":{"0":{"amount":{"op":"ratio","args":[{"fact":"owner.completed_work","family":"psychic"},{"value":1000,"unit":"permille"},{"fact":"owner.property","property":"test.gain"}]}}}},
 {"id":"test.strain.rest-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.strain.rest-op",
 "when":{"op":"eq","args":[{"fact":"owner.completed_work","family":"psychic"},{"value":0,"unit":"permille"}]},
 "values":{"0":{"amount":{"op":"multiply","args":[{"fact":"owner.property","property":"test.rest"},{"value":-1}]}}}},
 {"id":"test.strain.overload-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.strain.overload-op",
 "when":{"op":"all","args":[
 {"op":"ge","args":[{"fact":"owner.property","property":"test.strain"},{"fact":"owner.property","property":"test.capacity"}]},
 {"op":"ne","args":[{"fact":"owner.property","property":"test.state","view":"base"},{"value":"recovering","enum":"test.state"}]}]},
 "values":{"0":{"value":{"fact":"owner.property","property":"test.capacity"}}}},
 {"id":"test.strain.resume-rule","kind":"rule","trigger":"tick","phase":"periodic","effect":"test.strain.resume-op",
 "when":{"op":"all","args":[
 {"op":"eq","args":[{"fact":"owner.property","property":"test.state","view":"base"},{"value":"recovering","enum":"test.state"}]},
 {"op":"le","args":[{"fact":"owner.property","property":"test.strain"},{"op":"ratio","args":[{"fact":"owner.property","property":"test.resume"},{"value":1000,"unit":"permille"},{"fact":"owner.property","property":"test.capacity"}]}]}]}}
 ])json");
	for (const auto &rule : rules)
		source["effects"].push_back(rule);
	for (auto &ability : source["effects"])
		if (ability.value("kind", std::string{}) == "ability")
		{
			ability["family"] = "psychic";
			ability["recovery_ticks"] = 30;
			ability["requirements"] = {{"op", "eq"},
			                           {"args", Json::array({{{"fact", "owner.property"}, {"property", "test.state"}},
			                                                 {{"value", "ready"}, {"enum", "test.state"}}})}};
			ability["observations"] = {"test.strain", "test.capacity", "test.resume", "test.state"};
		}
	auto &ship = source["entities"][0];
	for (auto &officer : ship["personnel"]["officers"])
	{
		officer["properties"] = Json::object();
		for (const auto &d : source["properties"])
			officer["properties"][d.at("id").get<std::string>()] = Json::object();
		officer["rules"] = Json::array();
		for (const auto *id :
		     {"test.strain.gain-rule", "test.strain.rest-rule", "test.strain.overload-rule", "test.strain.resume-rule"})
			officer["rules"].push_back({{"id", id}, {"rule", id}, {"eligibility", "lifetime"}});
	}
	if (concurrent)
	{
		for (auto &seat : ship["systems"]["choir"]["seats"])
			seat["ability"] = "test.ability." + seat.at("duty").get<std::string>();
		Json extra;
		for (const auto &effect : source["effects"])
			if (effect.at("id") == "test.ability.cloak")
				extra = effect;
		extra["id"] = "test.ability.extra";
		source["effects"].push_back(extra);
		ship["personnel"]["officers"][0]["abilities"].push_back("test.ability.extra");
		ship["systems"]["choir"]["supported_abilities"].push_back("test.ability.extra");
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller.at("id") == "choir")
				controller["commands"].push_back(
				    {{"id", "extra-enabled"}, {"action", "intent_boolean"}, {"consumers", Json::array()}});
		ship["ability_controls"] = {{"extra",
		                             {{"source", "officer"},
		                              {"owner", "cloak"},
		                              {"ability", "test.ability.extra"},
		                              {"apparatus", "choir"},
		                              {"controller", "choir"},
		                              {"console", "drive"},
		                              {"enabled", "extra-enabled"}}}};
	}
	return source;
}
Json strain_value(const World &world, const std::string &id = "test.strain", const std::string &officer = "cloak")
{
	const auto saved = world.save();
	for (const auto &row : saved.at("custom_properties"))
		if (row.at("entity") == 1 && row.at("officer") == officer)
			return row.at("values").at(id);
	throw std::runtime_error("Missing psychic officer properties");
}
Json strain_grant(const World &world, const std::string &ability = "test.ability.cloak")
{
	const auto saved = world.save();
	for (const auto &ship : saved.at("abilities").at("ships"))
		if (ship.at("ship") == 1)
			for (const auto &[id, grant] : ship.at("state").at("grants").items())
				if (grant.at("definition") == ability && grant.at("owner") == "cloak")
					return grant;
	throw std::runtime_error("Missing psychic officer ability grant");
}
std::int64_t strain_output(const World &world)
{
	const auto saved = world.save();
	std::int64_t output = 0;
	for (const auto &ship : saved.at("abilities").at("ships"))
		if (ship.at("ship") == 1)
			for (const auto &work : ship.at("state").at("work"))
				if (work.at("owner") == "cloak" && work.at("family") == "psychic")
					output += work.at("output").get<std::int64_t>();
	return output;
}
Json ability_property_ai_contract()
{
	Json trace = Json::array();
	for (int mode = 0; mode < 3; ++mode)
	{
		auto source = strain_fixture(true);
		auto &ship = source["entities"][0];
		ship["personnel"]["officers"][0]["properties"]["test.rest"] = 0;
		source["effects"].push_back(
		    {{"id", "test.ai.capacity"},
		     {"kind", "modifier"},
		     {"contributions", {{"capacity", {{"property", "test.capacity"}, {"delta_permille", 1000}}}}}});
		source["effects"].push_back(
		    {{"id", "test.ai.apply"},
		     {"kind", "operations"},
		     {"operations",
		      Json::array(
		          {{{"kind", "apply"}, {"target", "owner"}, {"effect", "test.ai.capacity"}, {"grant", "capacity"}}})}});
		source["effects"].push_back(
		    {{"id", "test.ai.activate"}, {"kind", "rule"}, {"trigger", "activate"}, {"effect", "test.ai.apply"}});
		ship["personnel"]["officers"][0]["rules"].push_back(
		    {{"id", "capacity"}, {"rule", "test.ai.activate"}, {"eligibility", "lifetime"}});
		if (mode == 1)
			for (auto &ability : source["effects"])
				if (ability.at("id") == "test.ability.extra")
					std::erase_if(ability["observations"].get_ref<Json::array_t &>(),
					              [](const auto &id)
					              {
						              return id == "test.capacity";
					              });
		if (mode == 2)
			ship["personnel"]["officers"][0]["properties"].erase("test.capacity");
		source["trees"].push_back(Json::parse(R"json({
   "id":"test.ai.property-reader","context":"controller",
   "command_bindings":[{"channel":"apparatus","type":"boolean"}],
   "ability_property_bindings":[{"endpoint":"extra","property":"test.capacity"}],
   "initial":"waiting","states":[
    {"id":"waiting","transitions":[{"target":"observed","conditions":[{"left":{"observation":"ability.extra.property.test.capacity"},"op":"eq","right":6000}]}]},
    {"id":"observed","entry":[{"action":"write.apparatus","arguments":[false]}]}
   ]})json"));
		if (mode != 0)
			source["trees"].back()["states"][0]["transitions"][0]["conditions"][0]["right"] = 0;
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller.at("id") == "choir")
			{
				controller.erase("fallback");
				controller["tree"] = "test.ai.property-reader";
			}
		for (auto &profile : ship["control_layout"]["profiles"])
		{
			std::erase_if(profile["consoles"]["drive"]["human"].get_ref<Json::array_t &>(),
			              [](const auto &id)
			              {
				              return id == "choir";
			              });
			profile["consoles"]["drive"]["ai"] = Json::array({"choir"});
		}
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		ticks(world, 4);
		const auto view = world.observe(101);
		std::int64_t apparatus = -1;
		Json property;
		for (const auto &controller : view->gameplay.at("controllers"))
			if (controller.at("id") == "choir")
				apparatus = controller.at("commands").at("apparatus").at("value");
		for (const auto &ability : view->gameplay.at("abilities"))
			if (ability.at("control") == "extra")
				for (const auto &row : ability.at("properties"))
					if (row.at("id") == "test.capacity")
						property = row;
		if (mode == 0)
		{
			require(
			    apparatus == 0 && !property.is_null() && property.at("value") == 6000,
			    "Permitted effective custom property drives ordinary AI action and matches the human ability board");
			require(strain_value(world, "test.capacity") == 3000,
			        "Human and AI read effective capacity without exposing its private stored base");
			for (int malformed = 0; malformed < 2; ++malformed)
			{
				auto invalid = view->gameplay;
				for (auto &ability : invalid["abilities"])
					if (ability["control"] == "extra")
						for (auto &row : ability["properties"])
							if (row["id"] == "test.capacity")
							{
								if (malformed == 0)
									row["value"] = "not-an-integer";
								else
									row["kind"] = "invented";
							}
				bool refused = false;
				try
				{
					(void)network::bounded_gameplay(invalid);
				}
				catch (const std::exception &)
				{
					refused = true;
				}
				require(refused, "Malformed typed custom property rows are rejected by bounded projection");
			}
			auto private_fields = view->gameplay;
			for (auto &ability : private_fields["abilities"])
				for (auto &row : ability["properties"])
					row["base"] = 123;
			const auto bounded = network::bounded_gameplay(private_fields);
			for (const auto &ability : bounded.at("abilities"))
				for (const auto &row : ability.at("properties"))
					require(!row.contains("base"), "Bounded projection excludes injected private property bases");
		}
		else
			require(apparatus == 1 && property.is_null(),
			        "Unobserved or undeclared typed property remains Missing and cannot cause an AI action");
		auto saved = world.save();
		saved.erase("packs");
		trace.push_back(std::move(saved));
	}
	return trace;
}

Json strain_contract()
{
	auto data = strain_fixture();
	const auto catalog = load(data);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, 101, 1, "choir", "cloak-enabled", 1);
	std::uint64_t first_activation = 0;
	int overload_tick = 0;
	for (int i = 0; i < 60; ++i)
	{
		ticks(world);
		const auto grant = strain_grant(world);
		first_activation = std::max(first_activation, grant.at("activation").get<std::uint64_t>());
		if (strain_value(world, "test.state") == "recovering")
		{
			overload_tick = i + 1;
			break;
		}
	}
	require(overload_tick > 0 && first_activation > 0, "Authored actual psychic work reaches its recovery latch");
	World resumed;
	resumed.restore(catalog, world.save());
	ticks(world);
	ticks(resumed);
	require(strain_grant(world).at("phase") != "active" && world.observe(101)->gameplay.at("abilities").size() > 0,
	        "Recovery requirement ends the old native psychic execution at the next operating boundary");
	bool retained_intent = false;
	const auto overloaded_view = world.observe(101);
	for (const auto &row : overloaded_view->gameplay.at("abilities"))
		if (row.at("ability") == "test.ability.cloak")
			retained_intent = row.at("requested");
	require(retained_intent, "Overload retains the requested sustained power");
	const auto previous = strain_value(world).get<std::int64_t>();
	ticks(world);
	ticks(resumed);
	require(strain_value(world).get<std::int64_t>() < previous && strain_output(world) == 0,
	        "No completed psychic output recovers strain instead of charging retained intent");
	bool ready = false;
	for (int i = 0; i < 40; ++i)
	{
		ticks(world);
		ticks(resumed);
		if (strain_value(world, "test.state") == "ready")
		{
			ready = true;
			break;
		}
	}
	require(ready && strain_value(world).get<std::int64_t>() <= 750 && strain_grant(world).at("phase") != "active",
	        "Recovery clears at authored threshold while normal longer cooldown still prevents activation");
	bool restarted = false;
	for (int i = 0; i < 40; ++i)
	{
		ticks(world);
		ticks(resumed);
		if (strain_grant(world).at("phase") == "active")
		{
			restarted = true;
			break;
		}
	}
	require(restarted && strain_grant(world).at("activation").get<std::uint64_t>() > first_activation,
	        "Still-requested power restarts as a fresh activation after ordinary cooldown");
	require(world.save() == resumed.save(),
	        "Strain interruption, work receipts, cooldown and automatic fresh restart continue exactly");
	const auto observed = world.observe(101)->gameplay.at("abilities");
	bool local_property = false;
	for (const auto &ability : observed)
		if (ability.at("ability") == "test.ability.cloak")
			for (const auto &property : ability.at("properties"))
				local_property |= property.at("id") == "test.strain";
	require(local_property, "Permitted ability status publishes authored strain properties");
	require(network::bounded_gameplay(world.observe(101)->gameplay).at("abilities") == observed,
	        "Typed authorised ability properties survive bounded network projection without changing values");
	require(!world.observe(104)->gameplay.contains("abilities") || world.observe(104)->gameplay.at("abilities").empty(),
	        "Unfed unrelated Console does not receive private psychic properties");
	auto concurrent = strain_fixture(true);
	const auto paired_catalog = load(concurrent);
	World paired;
	paired.load(paired_catalog, "test.map");
	claim(paired);
	command(paired, 101, 1, "choir", "cloak-enabled", 1);
	command(paired, 101, 1, "choir", "extra-enabled", 1);
	ticks(paired, 4);
	require(strain_output(paired) == 2000,
	        "Two independently active abilities report additive frozen work for the same officer");
	const auto paired_before = strain_value(paired).get<std::int64_t>();
	ticks(paired);
	require(strain_value(paired).get<std::int64_t>() - paired_before == 200,
	        "Concurrent actual psychic outputs add strain gain rates");
	auto partial = strain_fixture();
	partial["entities"][0]["resources"]["stores"][0]["discharge_rate"] = 50;
	const auto partial_catalog = load(partial);
	World limited;
	limited.load(partial_catalog, "test.map");
	claim(limited);
	command(limited, 101, 1, "choir", "cloak-enabled", 1);
	ticks(limited, 4);
	const auto partial_output = strain_output(limited);
	const auto partial_before = strain_value(limited).get<std::int64_t>();
	ticks(limited);
	require(partial_output > 0 && partial_output < 1000 &&
	            strain_value(limited).get<std::int64_t>() - partial_before == strain_output(limited) / 10,
	        "Fractional native funding charges strain from actual delivered psychic output");
	auto empty = strain_fixture();
	empty["entities"][0]["resources"]["stores"][0]["initial"] = 0;
	empty["entities"][0]["personnel"]["officers"][0]["properties"]["test.strain"] = 1000;
	const auto empty_catalog = load(empty);
	World unfunded;
	unfunded.load(empty_catalog, "test.map");
	claim(unfunded);
	command(unfunded, 101, 1, "choir", "cloak-enabled", 1);
	ticks(unfunded, 4);
	require(strain_output(unfunded) == 0 && strain_value(unfunded).get<std::int64_t>() < 1000,
	        "Funding outage produces no psychic strain and permits idle recovery");
	auto travelling = strain_fixture();
	travelling["entities"][0]["personnel"]["officers"][0]["properties"]["test.strain"] = 2000;
	for (auto &duty : travelling["entities"][0]["personnel"]["duties"])
		if (duty.at("id") == "radar")
			duty["compartment"] = "aft";
	const auto travel_catalog = load(travelling);
	World moving;
	moving.load(travel_catalog, "test.map");
	claim(moving);
	OperatingCommand assign{1, "personnel.assign", 0, "cloak"};
	assign.text = "radar";
	assign.generation = moving.observe(101)->gameplay.at("personnel").at("revision");
	require(moving.submit(101, assign), "Psychic reassignment is admitted through Personnel");
	ticks(moving, 2);
	require(strain_value(moving).get<std::int64_t>() < 2000 && strain_value(moving).get<std::int64_t>() > 0,
	        "Officer lifetime rules recover the same strain balance while travelling to another duty");
	auto discrete = strain_fixture(true);
	for (auto &controller : discrete["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "choir")
			for (auto &setting : controller["commands"])
				if (setting.at("id") == "extra-enabled")
					setting["action"] = "ability_activate";
	const auto discrete_catalog = load(discrete);
	World requested;
	requested.load(discrete_catalog, "test.map");
	claim(requested);
	command(requested, 101, 1, "choir", "extra-enabled", 1);
	bool discrete_overload = false;
	std::uint64_t original_request_activation = 0;
	for (int i = 0; i < 60; ++i)
	{
		ticks(requested);
		original_request_activation =
		    std::max(original_request_activation,
		             strain_grant(requested, "test.ability.extra").at("activation").get<std::uint64_t>());
		if (strain_value(requested, "test.state") == "recovering")
		{
			discrete_overload = true;
			break;
		}
	}
	require(discrete_overload,
	        "A discrete request may sustain psychic work until its shared requirement interrupts it");
	ticks(requested);
	const auto retained = strain_grant(requested, "test.ability.extra");
	require(retained.at("retained_request") == true && retained.at("phase") != "active",
	        "Requirement interruption retains an accepted sustained request without leaving its old execution active");
	World requested_resumed;
	requested_resumed.restore(discrete_catalog, requested.save());
	bool request_restarted = false;
	for (int i = 0; i < 60; ++i)
	{
		ticks(requested);
		ticks(requested_resumed);
		if (strain_grant(requested, "test.ability.extra").at("activation").get<std::uint64_t>() >
		    original_request_activation)
		{
			request_restarted = true;
			break;
		}
	}
	require(request_restarted && requested.save() == requested_resumed.save(),
	        "Consumed sustained request survives recovery and restore and starts one fresh activation without another "
	        "request");
	bool recovery_again = false;
	for (int i = 0; i < 60; ++i)
	{
		ticks(requested);
		if (strain_value(requested, "test.state") == "recovering")
		{
			recovery_again = true;
			break;
		}
	}
	require(recovery_again, "Repeated use can enter another recovery break");
	ticks(requested);
	command(requested, 101, 1, "choir", "extra-enabled", 0);
	ticks(requested, 60);
	require(strain_grant(requested, "test.ability.extra").at("phase") != "active" &&
	            strain_grant(requested, "test.ability.extra").at("retained_request") == false,
	        "Ordinary cancel clears a retained sustained request during recovery");

	auto failed_preparation = discrete;
	failed_preparation["effects"].push_back(
	    {{"id", "test.strain.impossible"},
	     {"kind", "operations"},
	     {"costs", Json::array({{{"payer", "source"},
	                             {"store", "energy:cell"},
	                             {"resource", "fixture.resource.energy"},
	                             {"amount", 1000001}}})},
	     {"operations",
	      Json::array(
	          {{{"kind", "property_add"}, {"target", "source"}, {"property", "test.strain"}, {"amount", 1}}})}});
	for (auto &ability : failed_preparation["effects"])
		if (ability.at("id") == "test.ability.extra")
		{
			ability["preparation_ticks"] = 1;
			ability["activation_effect"] = "test.strain.impossible";
		}
	const auto failed_catalog = load(failed_preparation);
	World failed;
	failed.load(failed_catalog, "test.map");
	claim(failed);
	command(failed, 101, 1, "choir", "extra-enabled", 1);
	ticks(failed, 4);
	require(strain_grant(failed, "test.ability.extra").at("activation") == 0 &&
	            strain_grant(failed, "test.ability.extra").at("phase") != "active",
	        "Failed activation after a sustained request's preparation does not start native work");
	const auto failed_view = failed.observe(101);
	bool recorded_failure = false;
	for (const auto &controller : failed_view->gameplay.at("controllers"))
		if (controller.at("id") == "choir")
		{
			const auto &receipt = controller.at("commands").at("extra-enabled");
			recorded_failure = receipt.at("accepted") == false && receipt.at("consumed") == receipt.at("revision");
		}
	require(recorded_failure,
	        "Original sustained request retains its identity until prepared activation records failure");
	auto trace = world.save();
	trace.erase("packs");
	return Json::array({trace, ability_property_ai_contract()});
}

} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json strain_trace;
		try
		{
			strain_trace = strain_contract();
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error(std::string("authored strain: ") + error.what());
		}
		{
			auto assisted = fixture();
			auto &layout = assisted["entities"][0]["control_layout"];
			layout["station_complexity"] = true;
			for (auto &c : layout["controllers"])
				if (c["id"] == "choir")
				{
					c.erase("fallback");
					c["tree"] = "test.protective-choir";
					c["commands"][0]["individual_stars"] = 3;
				}
			auto manual = layout["profiles"][0];
			manual["stars"] = 2;
			auto full = manual;
			full["id"] = "full";
			full["stars"] = 3;
			auto simple = manual;
			simple["id"] = "simple";
			simple["stars"] = 1;
			std::erase_if(simple["consoles"]["drive"]["human"].get_ref<Json::array_t &>(),
			              [](const auto &id)
			              {
				              return id == "choir";
			              });
			simple["consoles"]["drive"]["ai"] = {"choir"};
			layout["profiles"] = Json::array({simple, manual, full});
			layout["default_profile"] = "simple";
			assisted["trees"].push_back(Json::parse(R"json({"id":"test.protective-choir","context":"controller",
 "command_bindings":[{"channel":"apparatus","type":"boolean"},{"channel":"ward-enabled","type":"boolean"},{"channel":"ward-target"},{"channel":"scramble-enabled","type":"boolean"},{"channel":"cloak-enabled","type":"boolean"}],
 "initial":"protect","states":[{"id":"protect","running":[
 {"action":"write.apparatus","arguments":[true]}, {"action":"write.ward-enabled","arguments":[true]},
 {"action":"write.ward-target","arguments":[{"observation":"own.id"}]},
 {"action":"write.scramble-enabled","arguments":[false]}, {"action":"write.cloak-enabled","arguments":[false]}]}]})json"));
			const auto fitted = load(assisted);
			World ship;
			ship.load(fitted, "test.map");
			claim(ship);
			ticks(ship, 3);
			const auto value = [&](const std::string &field)
			{
				const auto observation = ship.observe(101);
				for (const auto &c : observation->gameplay.at("controllers"))
					if (c.at("id") == "choir")
						return c.at("commands").at(field).at("value").get<std::int64_t>();
				return std::int64_t{-1};
			};
			require(value("ward-target") == 1 && value("ward-enabled") == 1 && value("scramble-enabled") == 0,
			        "one-star protective policy wards self without offensive power");
			require(ship.submit(101, {1, "preset", 1}), "take two-star psychic control");
			ticks(ship);
			command(ship, 101, 1, "choir", "ward-enabled", 0);
			ticks(ship);
			require(value("ward-enabled") == 0, "two-star player psychic choice persists");
			require(ship.submit(101, {1, "preset", 2}), "take full apparatus control");
			ticks(ship);
			command(ship, 101, 1, "choir", "apparatus", 0);
			ticks(ship);
			require(ship.submit(101, {1, "preset", 1}), "return apparatus to assistance");
			ticks(ship);
			require(value("apparatus") == 1, "hidden apparatus override resets on downgrade");
			require(ship.submit(101, {1, "preset", 0}), "return to protective policy");
			ticks(ship, 3);
			require(value("ward-target") == 1 && value("ward-enabled") == 1, "downgrade restores self-ward");
		}
		{
			const auto local = load(fixture());
			World moved;
			moved.load(local, "test.map");
			claim(moved);
			ticks(moved);
			command(moved, 101, 1, "choir", "scramble-target", 2);
			command(moved, 101, 1, "choir", "scramble-enabled", 1);
			ticks(moved, 3);
			require(ghosts(moved, 102).size() == 2, "projection is live before immediate Personnel interruption");
			OperatingCommand relieve{1, "personnel.relieve", 0, "scramble"};
			relieve.generation = moved.observe(101)->gameplay.at("personnel").at("revision");
			require(moved.submit(101, relieve), "queue relief of a live psychic source");
			ticks(moved);
			const auto interrupted = moved.save();
			bool retained_fade = false;
			for (const auto &reception : interrupted.at("psychic").at("receptions"))
				if (reception.at("source") == 1 && reception.at("observer") == 2)
				{
					retained_fade = true;
					require(reception.at("ended").get<std::int64_t>() >= 0,
					        "committed source reassignment retires live reception while retaining fading evidence");
				}
			require(retained_fade, "interrupted projection retains dated fade state");
			World resumed;
			resumed.restore(local, interrupted);
			require(resumed.save() == interrupted,
			        "save at the first tick boundary after Personnel interruption restores exactly");
			ticks(moved, 8);
			ticks(resumed, 8);
			require(resumed.save() == moved.save(),
			        "interrupted projection fade and officer recovery continue exactly");
		}
		for (bool explicit_removal : {false, true})
		{
			auto authored = fixture();
			auto &ship = authored["entities"][0];
			Json temporary;
			for (const auto &effect : authored["effects"])
				if (effect.at("id") == "test.ability.scramble")
					temporary = effect;
			temporary["id"] = "test.ability.temporary-scramble";
			temporary["label"] = "Temporary projection";
			authored["effects"].push_back(temporary);
			authored["effects"].push_back({{"id", "test.temporary-psychic-grant"},
			                               {"kind", "ability_grant"},
			                               {"ability", "test.ability.temporary-scramble"},
			                               {"duration_ticks", explicit_removal ? 50 : 7}});
			authored["effects"].push_back({{"id", "test.grant-psychic"},
			                               {"kind", "operations"},
			                               {"operations", Json::array({{{"kind", "apply"},
			                                                            {"effect", "test.temporary-psychic-grant"},
			                                                            {"grant", "projection"},
			                                                            {"officer", "visitor"}}})}});
			authored["effects"].push_back(
			    {{"id", "test.remove-psychic"},
			     {"kind", "operations"},
			     {"operations", Json::array({{{"kind", "remove"}, {"grant", "projection"}, {"officer", "visitor"}}})}});
			ship["personnel"]["officers"].push_back({{"id", "visitor"},
			                                         {"name", "Visitor"},
			                                         {"department", "signals"},
			                                         {"compartment", "fore"},
			                                         {"traits", Json::array()}});
			ship["systems"]["choir"]["supported_abilities"].push_back("test.ability.temporary-scramble");
			for (auto &controller : ship["control_layout"]["controllers"])
				if (controller.at("id") == "choir")
				{
					controller["commands"].push_back(
					    {{"id", "temporary-enabled"}, {"action", "intent_boolean"}, {"consumers", Json::array()}});
					controller["commands"].push_back({{"id", "temporary-target"},
					                                  {"action", "intent_integer"},
					                                  {"minimum", 0},
					                                  {"maximum", 1000000000},
					                                  {"consumers", Json::array()}});
				}
			ship["ability_controls"] = {{"temporary-projection",
			                             {{"source", "officer"},
			                              {"owner", "visitor"},
			                              {"ability", "test.ability.temporary-scramble"},
			                              {"apparatus", "choir"},
			                              {"controller", "choir"},
			                              {"console", "drive"},
			                              {"enabled", "temporary-enabled"},
			                              {"target", "temporary-target"}}}};
			Json states = Json::parse(R"json([
 {"id":"wait","transitions":[{"target":"grant","conditions":[{"left":{"observation":"tick"},"op":"ge","right":1}]}]},
 {"id":"grant","entry":[{"action":"apply_effect_group","arguments":[{"type":"entity","id":1},{"type":"entity","id":1},"test.grant-psychic"]}]}
])json");
			if (explicit_removal)
			{
				authored["effects"].push_back({{"id", "test.ability.psychic-manager"},
				                               {"kind", "ability"},
				                               {"label", "Projection manager"},
				                               {"target", "self"},
				                               {"preparation_ticks", 6},
				                               {"start_effect", "test.grant-psychic"},
				                               {"activation_effect", "test.remove-psychic"}});
				ship["systems"]["radar"]["abilities"] = {"test.ability.psychic-manager"};
				ship["ability_controls"]["manager"] = {
				    {"source", "equipment"}, {"owner", "radar"},   {"ability", "test.ability.psychic-manager"},
				    {"controller", "choir"}, {"console", "drive"}, {"enabled", "manager"}};
				for (auto &controller : ship["control_layout"]["controllers"])
					if (controller.at("id") == "choir")
						controller["commands"].push_back(
						    {{"id", "manager"}, {"action", "ability_activate"}, {"consumers", Json::array()}});
			}
			else
			{
				authored["trees"].push_back({{"id", "test.temporary-psychic"},
				                             {"context", "scenario"},
				                             {"initial", "wait"},
				                             {"states", states}});
				authored["maps"][0]["scenario"] = {{"tree", "test.temporary-psychic"}};
			}
			const auto local = load(authored);
			World granted;
			granted.load(local, "test.map");
			claim(granted);
			if (explicit_removal)
				command(granted, 101, 1, "choir", "manager", 1);
			ticks(granted, 3);
			command(granted, 101, 1, "choir", "temporary-target", 2);
			command(granted, 101, 1, "choir", "temporary-enabled", 1);
			ticks(granted, 2);
			require(ghosts(granted, 102).size() == 2,
			        "temporary native psychic grant creates ordinary moving contacts");
			const auto active = granted.save();
			World resumed;
			resumed.restore(local, active);
			if (!explicit_removal)
			{
				std::uint64_t expiry = 0;
				for (const auto &owner : active.at("abilities").at("ships"))
					if (owner.at("ship") == 1)
						for (const auto &[key, grant] : owner.at("state").at("grants").items())
							if (grant.at("definition") == "test.ability.temporary-scramble")
								expiry = grant.at("expiry").get<std::uint64_t>();
				const auto count = static_cast<int>(expiry - active.at("tick").get<std::uint64_t>());
				require(count > 0 && count <= 5, "temporary native grant exposes a bounded future expiry");
				ticks(granted, count);
				ticks(resumed, count);
				const auto boundary = granted.save();
				require(boundary.at("tick") == expiry, "snapshot is captured exactly at the expiry boundary");
				World exact;
				exact.restore(local, boundary);
				require(exact.save() == boundary, "exact-expiry boundary snapshot restores before next-phase cleanup");
				const auto observed = granted.observe(101);
				for (const auto &controller : observed->gameplay.at("controllers"))
					if (controller.at("id") == "choir")
						require(!granted.submit(
						            101,
						            OperatingCommand{
						                1, {}, 1, {}, "choir", "temporary-enabled", controller.at("generation")}),
						        "an expired native ability cannot admit a fresh activation at the boundary");
				const auto energy = [](const World &world) -> std::int64_t
				{
					const auto saved = world.save();
					for (const auto &body : saved.at("bodies"))
						if (body.at("body").at("id") == 1)
							return body.at("resources")
							    .at("network")
							    .at("stores")[0]
							    .at("stock")
							    .at("quantities")
							    .value("fixture.resource.energy", std::int64_t{0});
					throw std::runtime_error("Missing native ability energy store");
				};
				const auto before_cleanup = energy(granted);
				for (auto *run : {&granted, &resumed, &exact})
					ticks(*run);
				require(granted.save() == resumed.save() && granted.save() == exact.save() &&
				            energy(granted) == before_cleanup && ghosts(granted, 102).empty(),
				        "next-phase expiry cleanup emits no additional native work or payment after restore");
				ticks(granted, 5 - count);
				ticks(resumed, 5 - count);
			}
			else
			{
				ticks(granted, 6);
				ticks(resumed, 6);
			}
			require(granted.save() == resumed.save() && ghosts(granted, 102).empty(),
			        "temporary psychic expiry/removal retires native work with exact continued fading and recovery");
			require(!granted.observe(102)->gameplay.at("psychic").at("fading").empty(),
			        "withdrawn temporary projection retains observer-local fading evidence");
		}
		const auto source = fixture();
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map");
		claim(w);
		ticks(w);
		command(w, 101, 1, "choir", "scramble-target", 2);
		command(w, 101, 1, "choir", "scramble-enabled", 1);
		command(w, 101, 1, "choir", "cloak-enabled", 1);
		ticks(w, 3);
		require(ghosts(w, 102).size() == 2, "authored moving ghost group received");
		require(ghosts(w, 104).empty(), "unfed Console cannot observe ghosts");
		const auto first = w.save();
		ticks(w, 3);
		require(first.at("psychic").at("choirs")[0] != w.save().at("psychic").at("choirs")[0], "apparent ships move");
		require(ghosts(w, 103).empty(), "outside interference arc remains unaffected");
		const auto observed = w.observe(101);
		require(observed->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") == 900,
		        "concurrent cloak receives funded work");
		const auto victim = w.observe(102);
		require(std::none_of(victim->contacts.begin(), victim->contacts.end(),
		                     [](const auto &c)
		                     {
			                     return c.id == 1;
		                     }),
		        "cloak reduces actual contact acquisition");
		const auto pending = w.save();
		World resumed;
		resumed.restore(catalog, pending);
		ticks(w, 5);
		ticks(resumed, 5);
		require(w.save() == resumed.save(), "exact continuation during moving projection");
		command(w, 102, 2, "choir", "ward-target", 2);
		command(w, 102, 2, "choir", "ward-enabled", 1);
		ticks(w, 2);
		require(ghosts(w, 102).empty(), "self ward suppresses ghost reception");
		command(w, 102, 2, "choir", "ward-enabled", 0);
		ticks(w, 2);
		require(ghosts(w, 102).size() == 2, "ward loss restores undisproved projection");
		const auto ghost = ghosts(w, 102).at(0);
		command(w, 102, 2, "radar", "target", ghost);
		ticks(w, 7);
		const auto remaining = ghosts(w, 102);
		require(std::find(remaining.begin(), remaining.end(), ghost) == remaining.end(),
		        "detailed scan dispels apparent ship");
		ticks(w, 10);
		require(ghosts(w, 102).size() == 1, "dispel receipt prevents recreation within activation");
		const auto shot_target = ghosts(w, 102).at(0);
		command(w, 102, 2, "gun", "target", shot_target);
		bool hit = false;
		for (int i = 0; i < 40; ++i)
		{
			ticks(w);
			if (ghosts(w, 102).empty())
			{
				hit = true;
				break;
			}
		}
		require(hit, "actual paid weapon trajectory dispels a ghost");
		require(!w.observe(102)->gameplay.at("psychic").at("fading").empty(), "dispel fades instead of exploding");
		const auto post_hit = w.save();
		World after_hit;
		after_hit.restore(catalog, post_hit);
		ticks(w, 4);
		ticks(after_hit, 4);
		require(w.save() == after_hit.save(), "continuation preserves projectile and dispel receipts");
		command(w, 101, 1, "choir", "apparatus", 0);
		ticks(w);
		require(w.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") == 0,
		        "disabled apparatus supplies no power");
		command(w, 101, 1, "choir", "apparatus", 1);
		ticks(w);
		const auto roster = w.observe(101)->gameplay.at("personnel");
		command(w, 101, 1, "choir", "scramble-enabled", 1);
		OperatingCommand relieve{1, "personnel.relieve", 0, "scramble"};
		relieve.generation = roster.at("revision");
		require(w.submit(101, relieve), "relieve psychic officer through Personnel");
		ticks(w);
		require(!w.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats")[1].at("enabled").get<bool>(),
		        "reassignment clears previous power intent");
		const auto save = w.save();
		auto invalid = save;
		invalid["psychic"]["receptions"][0]["observer"] = 999;
		bool refused = false;
		try
		{
			w.restore(catalog, invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && w.save() == save, "malformed ghost save refused without writes");
		const auto own = w.observe(101);
		const auto clean = network::bounded_gameplay(own->gameplay);
		(void)clean;
		require(clean.at("psychic") == own->gameplay.at("psychic"), "psychic board survives permitted projection");
		const auto wire = network::observation_json(*w.observe(102));
		const auto remote = network::read_observation(wire, catalog);
		require(remote.gameplay.at("sensors") == w.observe(102)->gameplay.at("sensors"),
		        "ghost evidence survives network allowlists");
		{
			auto limited_source = fixture();
			limited_source["entities"][0]["resources"]["stores"][0]["discharge_rate"] = 50;
			const auto limited_catalog = load(limited_source);
			World limited;
			limited.load(limited_catalog, "test.map");
			claim(limited);
			ticks(limited);
			command(limited, 101, 1, "choir", "cloak-enabled", 1);
			command(limited, 101, 1, "choir", "scramble-target", 2);
			command(limited, 101, 1, "choir", "scramble-enabled", 1);
			ticks(limited, 3);
			const auto seats = limited.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats");
			require(seats[0].at("work") > 0 && seats[0].at("work") < 900 && seats[1].at("work") < 600,
			        "concurrent powers share limited actual supply");
		}
		{
			auto friendly_source = fixture();
			friendly_source["maps"][0]["entities"][2]["x_mm"] = 80000;
			friendly_source["maps"][0]["entities"][2]["y_mm"] = 20000;
			const auto friendly_catalog = load(friendly_source);
			World friendly;
			friendly.load(friendly_catalog, "test.map");
			claim(friendly);
			ticks(friendly);
			command(friendly, 101, 1, "choir", "scramble-target", 2);
			command(friendly, 101, 1, "choir", "scramble-enabled", 1);
			ticks(friendly, 2);
			require(ghosts(friendly, 103).size() == 2, "friendly receivers within footprint receive ghosts");
			command(friendly, 101, 1, "choir", "ward-target", 3);
			command(friendly, 101, 1, "choir", "ward-enabled", 1);
			ticks(friendly, 2);
			require(ghosts(friendly, 103).empty() && ghosts(friendly, 102).size() == 2,
			        "ward protects one allied ship independently of victim");
		}

		{
			auto injured_source = fixture();
			injured_source["entities"][0]["personnel"]["officers"][1]["condition"] = "minor";
			const auto injured_catalog = load(injured_source);
			World injured;
			injured.load(injured_catalog, "test.map");
			claim(injured);
			ticks(injured);
			command(injured, 101, 1, "choir", "scramble-target", 2);
			command(injured, 101, 1, "choir", "scramble-enabled", 1);
			ticks(injured, 3);
			require(ghosts(injured, 102).empty(), "injured specialist cannot project through healthy apparatus");
		}
		{
			auto duplicate_source = fixture();
			duplicate_source["entities"][0]["personnel"]["officers"][2]["abilities"] = {"test.ability.scramble"};
			auto &e = duplicate_source["entities"][0];
			e["systems"]["choir2"] = e["systems"]["choir"];
			e["systems"]["choir2"]["seats"] = Json::array({e["systems"]["choir"]["seats"][2]});
			e["systems"]["choir"]["seats"].erase(2);
			for (auto &d : e["personnel"]["duties"])
				if (d["id"] == "ward")
					d["choir"] = "choir2";
			for (auto &c : e["control_layout"]["controllers"])
				if (c["id"] == "choir")
					c["commands"][0]["consumers"].push_back("choir2");
			e["stations"][0]["consoles"][0]["feeds"].push_back("choir2");
			const auto duplicate_catalog = load(duplicate_source);
			World duplicate;
			duplicate.load(duplicate_catalog, "test.map");
			claim(duplicate);
			ticks(duplicate);
			command(duplicate, 101, 1, "choir", "scramble-target", 2);
			command(duplicate, 101, 1, "choir", "scramble-enabled", 1);
			command(duplicate, 101, 1, "choir", "ward-target", 2);
			command(duplicate, 101, 1, "choir", "ward-enabled", 1);
			ticks(duplicate, 3);
			require(ghosts(duplicate, 102).size() == 2, "overlapping scramblers use one strongest contribution");
			const auto overlapping = duplicate.save();
			for (const auto &g : overlapping.at("psychic").at("receptions"))
				if (g.at("source") == 1 && g.at("observer") == 2)
					require(g.at("choir") == "choir", "equal-strength choirs use stable ordering");
			command(duplicate, 101, 1, "choir", "ward-target", 3);
			ticks(duplicate, 8);
			require(ghosts(duplicate, 102).size() == 2 && ghosts(duplicate, 103).size() == 2,
			        "duplicate powers can serve independent victims after recovery");
		}
		// Outer-band apparent contacts share detection rules without granting scan evidence.
		{
			auto authored = fixture();
			auto projector = authored["entities"][0];
			projector["id"] = "test.projector";
			authored["entities"][0]["systems"]["radar"]["range_mm"] = 1000;
			authored["entities"][0]["systems"]["radar"]["long_range_mm"] = 400000;
			authored["entities"].push_back(projector);
			authored["maps"][0]["entities"][0]["definition"] = "test.projector";
			const auto local = load(authored);
			World outer;
			outer.load(local, "test.map");
			claim(outer);
			ticks(outer);
			command(outer, 101, 1, "choir", "scramble-target", 2);
			command(outer, 101, 1, "choir", "scramble-enabled", 1);
			command(outer, 101, 1, "choir", "cloak-enabled", 1);
			ticks(outer, 3);
			const auto observed = outer.observe(102);
			std::vector<EntityId> apparent;
			for (const auto &c : observed->bearing_contacts)
			{
				require(c.id != 1, "outer band respects psychic concealment");
				if (c.id > 3 && c.id <= 1000000000ULL)
					apparent.push_back(c.id);
			}
			require(apparent.size() == 2 && ghosts(outer, 102).empty(),
			        "outer apparent contacts expose bearings without positions");
			command(outer, 102, 2, "radar", "target", apparent.front());
			ticks(outer, 8);
			const auto scanned = outer.observe(102);
			for (const auto &c : scanned->gameplay.at("sensors").at("contacts"))
				if (std::find(apparent.begin(), apparent.end(), c.at("id").get<EntityId>()) != apparent.end())
					require(c.at("findings").empty(), "outer apparent contacts grant no detailed scan findings");
			command(outer, 101, 1, "choir", "cloak-enabled", 0);
			ticks(outer, 2);
			const auto visible = outer.observe(102);
			require(std::any_of(visible->bearing_contacts.begin(), visible->bearing_contacts.end(),
			                    [](const auto &c)
			                    {
				                    return c.id == 1;
			                    }),
			        "uncloaked real contacts return as outer-band bearings");
		}
		// Sensor admission and capability acquisition must match real contacts.
		for (bool hide_ships : {false, true})
		{
			auto authored = fixture();
			auto &radar = authored["entities"][0]["systems"]["radar"];
			radar["capabilities"] = Json::object();
			if (hide_ships)
				radar["shows"] = {"asteroid"};
			// The projector keeps a ship feed; the receiving hull filters ships.
			if (hide_ships)
			{
				auto enabled = authored;
				enabled["entities"][0]["systems"]["radar"]["shows"] = {"ship"};
				authored["entities"].push_back(enabled["entities"][0]);
				authored["entities"].back()["id"] = "test.projector";
				authored["maps"][0]["entities"][0]["definition"] = "test.projector";
				const auto filtered = load(authored);
				World view;
				view.load(filtered, "test.map");
				claim(view);
				ticks(view);
				command(view, 101, 1, "choir", "scramble-target", 2);
				command(view, 101, 1, "choir", "scramble-enabled", 1);
				ticks(view, 3);
				require(ghosts(view, 102).empty(), "ship filter excludes apparent contacts");
				require(view.observe(102)->gameplay.at("sensors").at("contacts").empty(),
				        "ship filter excludes apparent evidence");
			}
			else
			{
				const auto local = load(authored);
				World sensors;
				sensors.load(local, "test.map");
				claim(sensors);
				ticks(sensors);

				command(sensors, 101, 1, "choir", "scramble-target", 2);
				command(sensors, 101, 1, "choir", "scramble-enabled", 1);
				ticks(sensors, 3);
				require(ghosts(sensors, 102).size() == 2, "unidentified apparent bodies remain targetable");
				const auto observed = sensors.observe(102);
				for (const auto &c : observed->gameplay.at("sensors").at("contacts"))
					require(!c.at("findings").contains("basic_identity"), "identity requires the sensor capability");
			}
		}
		{
			auto authored = fixture();
			auto &e = authored["entities"][0];
			e["systems"]["radar2"] = e["systems"]["radar"];
			e["publications"] = Json::array({{{"id", "scan"},
			                                  {"source_console", "drive"},
			                                  {"schema", "phoenix.sensor.findings"},
			                                  {"subject", 4},
			                                  {"fields", {"payload"}},
			                                  {"live", false}}});
			e["stations"][0]["consoles"][0]["feeds"].push_back("radar2");
			for (auto &c : e["control_layout"]["controllers"])
				if (c.at("id") == "radar")
					c["commands"].push_back(
					    {{"id", "target2"}, {"action", "sensor_target"}, {"consumers", {"radar2"}}});
			const auto local = load(authored);
			World scans;
			scans.load(local, "test.map");
			claim(scans);
			ticks(scans);
			command(scans, 101, 1, "choir", "scramble-target", 2);
			command(scans, 101, 1, "choir", "scramble-enabled", 1);
			ticks(scans, 2);
			const auto target = ghosts(scans, 102).at(0);
			command(scans, 102, 2, "radar", "target", target);
			ticks(scans, 3);
			command(scans, 102, 2, "radar", "target", 0);
			command(scans, 102, 2, "radar", "target2", target);
			ticks(scans, 3);
			require(ghosts(scans, 102).size() == 2, "switching scanners does not combine their exposure");
			const auto middle = scans.save();
			World continued;
			continued.restore(local, middle);
			ticks(scans, 3);
			ticks(continued, 3);
			require(ghosts(scans, 102).size() == 1, "second scanner completes its own detailed scan");
			require(scans.save() == continued.save(), "per-scanner exposure survives exact continuation");
			const auto evidence = scans.observe(102)->gameplay.at("sensors").at("contacts");
			bool finding = false;
			for (const auto &c : evidence)
				if (c.at("id") == target)
					finding = c.at("findings").contains("finding:dispelled");
			require(finding, "dispel finding uses retained sensor evidence");
			require(scans.submit(102, OperatingCommand{2, "publish.scan", 0, {}}),
			        "publish apparent finding through ordinary knowledge");
			ticks(scans);
			const auto shared = scans.observe(104)->gameplay.at("sensors");
			require(shared.at("scanners").empty() && !shared.at("contacts").empty(),
			        "unfed Console receives only deliberately shared finding");
			bool shared_finding = false;
			for (const auto &c : shared.at("contacts"))
				if (c.at("id") == target)
					shared_finding = c.at("findings").contains("finding:dispelled");
			require(shared_finding, "apparent finding uses the existing publication and evidence path");
		}
		{
			auto authored = fixture();
			auto &e = authored["entities"][0];
			Json orders = Json::array();
			for (const auto &c : e["control_layout"]["controllers"])
				if (c.at("id") == "choir")
					for (const auto &ch : c.at("commands"))
						if (ch.at("id") != "apparatus")
							orders.push_back(ch);
			e["control_layout"]["controllers"].push_back(
			    {{"id", "orders"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", orders}});
			e["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("orders");
			e["personnel"]["duties"].push_back({{"id", "planner"},
			                                    {"label", "Planner"},
			                                    {"department", "signals"},
			                                    {"compartment", "fore"},
			                                    {"controller", "orders"},
			                                    {"console", "drive"}});
			e["systems"]["choir"]["planning_controller"] = "orders";
			const auto local = load(authored);
			World reassigned;
			reassigned.load(local, "test.map");
			claim(reassigned);
			ticks(reassigned);
			command(reassigned, 101, 1, "choir", "scramble-enabled", 1);
			command(reassigned, 101, 1, "orders", "scramble-enabled", 1);
			command(reassigned, 101, 1, "orders", "cloak-enabled", 1);
			OperatingCommand relieve{1, "personnel.relieve", 0, "scramble"};
			relieve.generation = reassigned.observe(101)->gameplay.at("personnel").at("revision");
			require(reassigned.submit(101, relieve), "relieve officer with queued planning and execution writes");
			ticks(reassigned);
			const auto observation = reassigned.observe(101);
			for (const auto &c : observation->gameplay.at("controllers"))
				if (c.at("id") == "orders" || c.at("id") == "choir")
				{
					require(c.at("commands").at("scramble-enabled").at("value") == 0,
					        "reset clears both command owners");
					if (c.at("id") == "orders")
						require(c.at("commands").at("cloak-enabled").at("value") == 1,
						        "reset preserves unrelated seat writes");
				}
			const auto current = reassigned.save();
			auto old = current;
			old["psychic"].erase("version");
			bool rejected = false;
			try
			{
				reassigned.restore(local, old);
			}
			catch (const std::exception &ex)
			{
				rejected = std::string(ex.what()).find("start a new session") != std::string::npos;
			}
			require(rejected && reassigned.save() == current, "old psychic saves fail clearly without writes");
		}
		for (bool boundary : {false, true})
		{
			auto authored = fixture();
			for (auto &effect : authored["effects"])
				if (effect.at("id") == "test.psychic.scramble")
				{
					effect["speed_mm_s"] = 1001;
					effect["turn_mdeg_s"] = 1;
				}
			if (boundary)
			{
				authored["maps"][0]["entities"][0]["x_mm"] = 999900000;
				authored["maps"][0]["entities"][1]["x_mm"] = 999999990;
				authored["maps"][0]["entities"][2]["x_mm"] = -999900000;
			}
			const auto local = load(authored);
			World motion;
			motion.load(local, "test.map");
			claim(motion);
			ticks(motion);
			command(motion, 101, 1, "choir", "scramble-target", 2);
			command(motion, 101, 1, "choir", "scramble-enabled", 1);
			ticks(motion, 2);
			auto saved = motion.save();
			Json position;
			for (auto &body : saved["bodies"])
				if (body["body"]["id"] == 2)
				{
					position = body["body"]["position"];
					body["body"]["velocity"] = {{"x", 1001}, {"y", 0}};
					body["body"]["remainder"] = {{"x", 19}, {"y", -19}};
				}
			Json shapes;
			for (auto &choir : saved["psychic"]["choirs"])
				if (choir["ship"] == 1)
				{
					for (auto &shape : scramble_shapes(choir))
					{
						shape["x"] = position["x"];
						shape["y"] = position["y"];
						shape["vx"] = 1001;
						shape["vy"] = 0;
						shape["mx"] = 19;
						shape["my"] = -19;
						shape["heading"] = 0;
					}
					shapes = scramble_shapes(choir);
				}
			for (auto &g : saved["psychic"]["receptions"])
				if (g["source"] == 1)
					g["last"] = shapes[g["member"].get<std::size_t>()];
			motion.restore(local, saved);
			for (int n = 0; n < 24; ++n)
			{
				ticks(motion);
				const auto next = motion.save();
				Json actual;
				for (const auto &body : next.at("bodies"))
					if (body.at("body").at("id") == 2)
						actual = body.at("body");
				for (const auto &choir : next.at("psychic").at("choirs"))
					if (choir.at("ship") == 1)
						for (const auto &shape : scramble_shapes(choir))
							require(shape.at("x") == actual.at("position").at("x") &&
							            shape.at("y") == actual.at("position").at("y") &&
							            shape.at("mx") == actual.at("remainder").at("x") &&
							            shape.at("my") == actual.at("remainder").at("y"),
							        "apparent and ordinary motion share fractional integration and world bounds");
			}
		}
		{
			auto authored = fixture();
			fit_ftl(authored, "operations", "drive");
			for (const auto *id : {"jump", "jump-feed"})
				authored["entities"][0]["systems"][id]["compartment"] = "fore";
			for (auto &duty : authored["entities"][0]["personnel"]["duties"])
				if (duty.at("id") == "jump" || duty.at("id") == "jump-feed")
					duty["compartment"] = "fore";
			const auto local = load(authored);
			World departure;
			departure.load(local, "test.map");
			claim(departure);
			ticks(departure, 4);
			const auto jump_command = [&](bool depart)
			{
				const auto observation = departure.observe(101);
				for (const auto &console : observation->additional_consoles)
					if (console.console == "navigator")
						for (const auto &controller : console.gameplay.at("controllers"))
							if (controller.at("id") == "jump")
							{
								OperatingCommand c{1,
								                   depart ? "ftl.depart" : "system_enabled",
								                   depart ? 0 : 1,
								                   depart ? "jump" : "",
								                   "jump",
								                   depart ? "" : "enabled"};
								c.console = "navigator";
								c.generation = controller.at("generation");
								require(departure.submit(101, c), "choir ship jump command admitted");
								return;
							}
				throw std::runtime_error("Missing choir ship Navigator");
			};
			jump_command(false);
			ticks(departure, 650);
			command(departure, 101, 1, "choir", "scramble-target", 2);
			command(departure, 101, 1, "choir", "scramble-enabled", 1);
			command(departure, 101, 1, "choir", "cloak-enabled", 1);
			ticks(departure, 2);
			jump_command(true);
			ticks(departure);
			const auto completed = departure.save();
			require(!departure.observe(101)->gameplay.at("mission_report").is_null(), "choir ship departed");
			for (const auto &choir : completed.at("psychic").at("choirs"))
				require(choir.at("ship") != 1, "retired choir omitted from active equipment save");
			World resumed;
			resumed.restore(local, completed);
			require(resumed.save() == completed, "departed choir report restores without changing saved state");
			ticks(departure, 8);
			ticks(resumed, 8);
			require(resumed.save() == departure.save(), "departed choir and ghost continuation remains exact");
			const auto stable = resumed.save();
			for (int damage = 0; damage < 3; ++damage)
			{
				auto invalid = completed;
				if (damage == 0)
					invalid["psychic"]["choirs"].erase(invalid["psychic"]["choirs"].begin());
				else
					for (auto &body : invalid["bodies"])
						if (body.at("body").at("id") == 1)
						{
							if (damage == 1)
								body.erase("mission_report");
							else
								body["mission_report"]["tick"] = completed.at("tick").get<std::uint64_t>() + 1;
						}
				bool rejected = false;
				try
				{
					resumed.restore(local, invalid);
				}
				catch (const std::exception &)
				{
					rejected = true;
				}
				require(rejected && resumed.save() == stable,
				        "missing active choir or unverified departure rejected transactionally");
			}
		}
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			// ZIP timestamps vary between fixture builds; compare simulation state.
			auto trace = w.save();
			trace.erase("packs");
			std::cout << sha256(json_bytes(Json::array({trace, strain_trace}))) << '\n';
		}
		else
			std::cout << "Psychic choir system checks passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
