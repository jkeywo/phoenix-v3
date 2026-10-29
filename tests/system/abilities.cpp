#include "../support/active_effects.hpp"
#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <string>
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.effects;
import phoenix.content.operations;
import phoenix.content.sensing;
import phoenix.simulation;
import phoenix.networking.projection;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	auto &ship = source["entities"][0];
	ship["resources"] = {
	    {"stores",
	     Json::array(
	         {{{"id", "cell"}, {"capacity", 200}, {"initial", 100}, {"charge_rate", 1000}, {"discharge_rate", 1000}},
	          {{"id", "marker"},
	           {"capacity", 200},
	           {"initial", 0},
	           {"charge_rate", 1000},
	           {"discharge_rate", 1000}}})}};
	source["effects"] = Json::parse(R"json([
 {"id":"test.ability.start","kind":"operations","operations":[
  {"kind":"consume","target":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":2}]},
 {"id":"test.ability.mark","kind":"operations","operations":[
  {"kind":"consume","target":"protected","store":"energy:cell","resource":"fixture.resource.energy","amount":7},
  {"kind":"add","target":"protected","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]},
 {"id":"test.ability.independent","kind":"modifier","duration_ticks":20,
  "contributions":{"range":{"property":"sensors","delta_permille":250}}},
 {"id":"test.ability.apply-independent","kind":"operations","operations":[
  {"kind":"apply","target":"protected","effect":"test.ability.independent","equipment":"radar","grant":"range"}]},
 {"id":"test.ability.pulse","kind":"ability","label":"Pulse","target":"self",
  "recovery_ticks":12,"recovery_on":"activation","activation_effect":"test.ability.mark"},
 {"id":"test.ability.prepared","kind":"ability","label":"Prepared pulse","target":"self",
  "preparation_ticks":4,"recovery_ticks":6,"recovery_on":"activation","interruption":"cancel",
  "start_effect":"test.ability.start","activation_effect":"test.ability.mark"},
 {"id":"test.ability.first","kind":"ability","label":"First channel","target":"self","sustained":true,
  "start_effect":"test.ability.start","exclusions":[{"id":"shared-apparatus","scope":"apparatus"}]},
 {"id":"test.ability.second","kind":"ability","label":"Second channel","target":"self","sustained":true,
  "start_effect":"test.ability.start","exclusions":[{"id":"shared-apparatus","scope":"apparatus"}]},
 {"id":"test.ability.temporary","kind":"ability","label":"Temporary pulse","target":"self",
  "recovery_ticks":30,"recovery_on":"activation","activation_effect":"test.ability.apply-independent"},
 {"id":"test.ability.temporary-grant","kind":"ability_grant","ability":"test.ability.temporary","duration_ticks":6},
 {"id":"test.ability.grant","kind":"operations","operations":[
  {"kind":"apply","target":"protected","effect":"test.ability.temporary-grant","grant":"temporary","officer":"a"}]}
])json");
	ship["systems"]["radar"]["abilities"] = {"test.ability.pulse", "test.ability.prepared", "test.ability.first",
	                                         "test.ability.second"};
	Json commands = Json::array();
	ship["ability_controls"] = Json::object();
	const auto endpoint = [&](const std::string &id, const std::string &ability, const std::string &kind,
	                          const std::string &owner, bool apparatus = false)
	{
		bool sustained = false;
		for (const auto &definition : source.at("effects"))
			if (definition.at("id") == ability)
				sustained = definition.value("sustained", false);
		commands.push_back(
		    {{"id", id}, {"action", sustained ? "intent_boolean" : "ability_activate"}, {"consumers", Json::array()}});
		Json binding = {{"source", kind},     {"owner", owner}, {"ability", ability}, {"controller", "abilities"},
		                {"console", "drive"}, {"enabled", id}};
		if (apparatus)
			binding["apparatus"] = "radar";
		ship["ability_controls"][id] = binding;
	};
	endpoint("equipment-pulse", "test.ability.pulse", "equipment", "radar");
	endpoint("equipment-prepared", "test.ability.prepared", "equipment", "radar");
	endpoint("first", "test.ability.first", "equipment", "radar", true);
	endpoint("second", "test.ability.second", "equipment", "radar", true);
	endpoint("duty-pulse", "test.ability.pulse", "duty", "radar");
	endpoint("officer-pulse", "test.ability.pulse", "duty", "mount");
	endpoint("temporary", "test.ability.temporary", "officer", "a");
	endpoint("independent-officer", "test.ability.pulse", "officer", "c");
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "abilities"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["drive"]["human"].push_back("abilities");
	ship["personnel"] = {
	    {"departments", Json::array({{{"id", "watch"}, {"label", "Watch"}, {"station", "operations"}}})},
	    {"traits", Json::array()},
	    {"duties", Json::array()},
	    {"officers", Json::array({{{"id", "a"},
	                               {"name", "A"},
	                               {"department", "watch"},
	                               {"compartment", "fore"},
	                               {"duty", "radar"},
	                               {"traits", Json::array()},
	                               {"abilities", {"test.ability.pulse"}}},
	                              {{"id", "b"},
	                               {"name", "B"},
	                               {"department", "watch"},
	                               {"compartment", "fore"},
	                               {"duty", "mount-second"},
	                               {"traits", Json::array()},
	                               {"abilities", {"test.ability.pulse"}}}})}};
	ship["personnel"]["officers"].push_back({{"id", "c"},
	                                         {"name", "C"},
	                                         {"department", "watch"},
	                                         {"compartment", "fore"},
	                                         {"traits", Json::array()},
	                                         {"abilities", {"test.ability.pulse"}}});
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
	return source;
}
void ticks(World &world, int count = 1)
{
	while (count-- > 0)
		world.tick();
}
void claim(World &world)
{
	require(world.claim(101, 1, "drive"), "claim ability Console");
	require(world.claim(102, 1, "observer"), "claim unfed ability observer");
	ticks(world);
}
bool has_ability(const World &world, const std::string &control)
{
	const auto observed = world.observe(101);
	for (const auto &row : observed->gameplay.at("abilities"))
		if (row.at("control") == control)
			return true;
	return false;
}
Json ability(const World &world, const std::string &control)
{
	const auto observed = world.observe(101);
	for (const auto &row : observed->gameplay.at("abilities"))
		if (row.at("control") == control)
			return row;
	throw std::runtime_error("Missing ability control " + control);
}
std::uint64_t command(World &world, const std::string &channel, bool enabled,
                      const std::string &controller = "abilities", bool expect_admitted = true,
                      std::uint64_t replay = 0)
{
	const auto observed = world.observe(101);
	for (const auto &owner : observed->gameplay.at("controllers"))
		if (owner.at("id") == controller)
		{
			OperatingCommand request{1, {}, enabled ? 1 : 0, {}, controller, channel, owner.at("generation")};
			const auto &setting = owner.at("commands").at(channel);
			if (setting.at("action") == "ability_activate")
				request.request_id = replay ? replay : setting.at("request_id").get<std::uint64_t>() + 1;
			const bool admitted = fixture_native_submit(world, 101, request);
			if (admitted != expect_admitted)
			{
				std::string detail;
				for (const auto &row : observed->gameplay.at("abilities"))
					if (row.at("control") == channel)
						detail += " phase=" + row.at("phase").get<std::string>() +
						          " blocker=" + row.at("blocker").get<std::string>();
				throw std::runtime_error("Ability command " + controller + "/" + channel + "=" +
				                         std::to_string(enabled ? 1 : 0) +
				                         " request=" + std::to_string(request.request_id) +
				                         (expect_admitted ? " was refused" : " was unexpectedly admitted") + detail);
			}
			return request.request_id;
		}
	throw std::runtime_error("Missing ability Controller " + controller);
}
void assign(World &world, const std::string &officer, const std::string &duty)
{
	OperatingCommand request{1, "personnel.assign", 0, officer};
	request.text = duty;
	request.generation = world.observe(101)->gameplay.at("personnel").at("revision");
	require(fixture_native_submit(world, 101, request), "ability source reassignment admitted");
	ticks(world);
}
std::int64_t quantity(const World &world, std::size_t index, EntityId actor = 1)
{
	const auto saved = world.save();
	for (const auto &row : saved.at("bodies"))
		if (row.at("body").at("id") == actor)
			return row.at("resources")
			    .at("network")
			    .at("stores")
			    .at(index)
			    .at("stock")
			    .at("quantities")
			    .value("fixture.resource.energy", std::int64_t{0});
	throw std::runtime_error("Missing ability actor stock");
}
Json without_packs(Json saved)
{
	saved.erase("packs");
	return saved;
}
void restore_refused(World &world, const Catalog &catalog, const Json &invalid, const std::string &reason = {})
{
	const auto before = world.save();
	bool rejected = false;
	try
	{
		world.restore(catalog, invalid);
	}
	catch (const std::exception &error)
	{
		rejected = reason.empty() || std::string(error.what()).find(reason) != std::string::npos;
	}
	require(rejected && world.save() == before, "invalid ability continuation refused without replacement writes");
}
Json compiler_catalogue_reload()
{
	auto source = fixture();
	const Json profile = {
	    {"id", "test.psychic.compiler"}, {"kind", "psychic"}, {"psychic_kind", "cloak"}, {"strength_permille", 300}};
	const Json definition = {{"id", "test.ability.compiler"},
	                         {"kind", "ability"},
	                         {"label", "Compiler cloak"},
	                         {"sustained", true},
	                         {"target", "self"},
	                         {"shortage", "proportional"},
	                         {"recovery_on", "stop"},
	                         {"recovery_ticks", 12},
	                         {"psychic_effect", "test.psychic.compiler"}};
	source["effects"].push_back(profile);
	source["effects"].push_back(definition);
	const auto pack = read_pack(write_pack(fixture_pack(json_bytes(source), "test-core")));
	const auto original = resolve_packs({pack});
	const auto compiled = [](const Catalog &catalog)
	{
		const auto shared = compile_ability_catalog(catalog);
		const auto directed = compile_directed_catalog(catalog);
		const auto personnel = compile_personnel(catalog, "test.combat", directed);
		const auto layout = compile_controllable_layout(catalog, "test.combat");
		const auto psychic = compile_psychic(catalog, "test.combat", shared, personnel, layout);
		const auto typed = compile_abilities(catalog, "test.combat", shared, personnel, layout, psychic);
		const auto standalone = compile_abilities(catalog, "test.combat");
		const auto controls = [](const AbilityDefinitionSet &set)
		{
			Json rows = Json::array();
			for (const auto &control : set.controls)
				rows.push_back({control.id, control.source, control.owner, control.ability, control.apparatus,
				                control.controller, control.enabled, control.target});
			return rows;
		};
		require(controls(typed) == controls(standalone) && typed.equipment_grants == standalone.equipment_grants,
		        "standalone and typed ability compilation preserve controls and source grants");
		const auto native = compile_psychic(catalog, "test.combat");
		require(psychic.has_power("test.ability.compiler") && native.has_power("test.ability.compiler") &&
		            psychic.power("test.ability.compiler").strength == native.power("test.ability.compiler").strength &&
		            typed.catalog->definitions.at("test.ability.compiler").recovery_ticks ==
		                standalone.catalog->definitions.at("test.ability.compiler").recovery_ticks,
		        "standalone and typed compilation resolve differing ability and native profile identities");
		return Json{{"recovery", typed.catalog->definitions.at("test.ability.compiler").recovery_ticks},
		            {"strength", psychic.power("test.ability.compiler").strength}};
	};
	const auto before = compiled(original);
	PackDraft patch;
	patch.manifest = {{"schema", 1},
	                  {"id", "ability-mod"},
	                  {"dependencies", {"test-core"}},
	                  {"overrides", {"test.psychic.compiler", "test.ability.compiler"}}};
	auto changed_profile = profile;
	changed_profile["strength_permille"] = 850;
	auto changed_ability = definition;
	changed_ability["recovery_ticks"] = 27;
	patch.files["definitions.json"] =
	    json_bytes({{"schema", 4}, {"effects", Json::array({changed_profile, changed_ability})}});
	const auto replacement = resolve_packs({pack, read_pack(write_pack(patch))});
	const auto after = compiled(replacement);
	require(before == Json({{"recovery", 12}, {"strength", 300}}) &&
	            after == Json({{"recovery", 27}, {"strength", 850}}) && compiled(original) == before,
	        "authored catalogue replacement updates ability and profile without changing the original catalogue");
	changed_ability["psychic_effect"] = "test.psychic.missing";
	patch.files["definitions.json"] =
	    json_bytes({{"schema", 4}, {"effects", Json::array({changed_profile, changed_ability})}});
	bool rejected = false;
	try
	{
		const auto invalid = resolve_packs({pack, read_pack(write_pack(patch))});
		(void)compiled(invalid);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected, "replacement catalogue cannot reuse a previously valid native profile reference");
	return Json::array({before, after});
}
Json recovery_deadline_bounds()
{
	auto source = fixture();
	for (auto &definition : source["effects"])
	{
		if (definition.at("id") == "test.ability.pulse")
			definition["recovery_ticks"] = 3;
		if (definition.at("id") == "test.ability.first")
			definition["recovery_ticks"] = 1000;
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "equipment-pulse", true);
	ticks(world);
	auto invalid = world.save();
	bool found = false;
	for (auto &ship : invalid["abilities"]["ships"])
		if (ship.at("ship") == 1)
			for (auto &[key, deadline] : ship["state"]["recovery"].items())
				if (key == "equipment:radar:test.ability.pulse")
				{
					deadline = invalid.at("tick").get<std::uint64_t>() + 4;
					found = true;
				}
	require(found, "activated short ability creates its source recovery ledger entry");
	restore_refused(world, catalog, invalid);
	return without_packs(world.save());
}
Json apparatus_free_officer()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "independent-officer", true);
	command(world, "equipment-pulse", true);
	ticks(world);
	require(quantity(world, 0) == 86 && quantity(world, 1) == 2 &&
	            ability(world, "independent-officer").at("owner") == "c",
	        "unassigned apparatus-free officer and equipment independently execute one shared ability template");
	ticks(world, 15);
	require(quantity(world, 1) == 2, "independent sources retain once-only activation receipts through recovery");
	return without_packs(world.save());
}
Json request_replay()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	const auto original = command(world, "equipment-pulse", true);
	ticks(world);
	const auto committed = world.save();
	World continued;
	continued.restore(catalog, committed);
	for (auto *run : {&world, &continued})
	{
		command(*run, "equipment-pulse", true, "abilities", true, original);
		ticks(*run);
		require(quantity(*run, 1) == 1 && quantity(*run, 0) == 93,
		        "replaying a committed request identity cannot pay or activate twice after restore");
		ticks(*run, 15);
		const auto next = command(*run, "equipment-pulse", true);
		require(next > original, "another activation receives a fresh discrete request identity");
		ticks(*run);
		require(quantity(*run, 1) == 2 && quantity(*run, 0) == 86,
		        "a new request activates after recovery without a Stop toggle gesture");
		const auto observation = run->observe(101);
		for (const auto &controller : observation->gameplay.at("controllers"))
			if (controller.at("id") == "abilities")
			{
				const auto &receipt = controller.at("commands").at("equipment-pulse");
				require(receipt.at("accepted") == true && receipt.at("consumed") == receipt.at("revision") &&
				            receipt.at("request_id") == next,
				        "the ordinary Controller request exposes its physical execution outcome");
			}
	}
	require(world.save() == continued.save(), "request receipts and new activations continue exactly after restore");
	return without_packs(world.save());
}
Json prepared_and_once()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "equipment-pulse", true);
	ticks(world);
	require(quantity(world, 0) == 93 && quantity(world, 1) == 1,
	        "equipment-only instant ability atomically pays and applies through shared stock operations");
	ticks(world, 15);
	require(quantity(world, 1) == 1, "consumed one-shot request cannot repeat a completed activation");
	command(world, "equipment-prepared", true);
	ticks(world);
	require(ability(world, "equipment-prepared").at("phase") == "preparing" && quantity(world, 0) == 91,
	        "preparation pays only its explicit start step");
	command(world, "equipment-prepared", false);
	ticks(world, 5);
	require(quantity(world, 0) == 91 && quantity(world, 1) == 1,
	        "cancelled preparation neither refunds completed start work nor applies activation effects");
	command(world, "equipment-prepared", true);
	ticks(world);
	const auto pending = world.save();
	World resumed;
	resumed.restore(catalog, pending);
	ticks(world, 6);
	ticks(resumed, 6);
	require(world.save() == resumed.save() && quantity(world, 0) == 82 && quantity(world, 1) == 2,
	        "preparation and final atomic activation survive exact continuation without repaying start work");
	const auto stable = world.save();
	auto invalid = stable;
	invalid["abilities"]["ships"][0]["state"]["next_generation"] = 0;
	restore_refused(world, catalog, invalid);
	invalid = stable;
	invalid["abilities"]["ships"].erase(0);
	restore_refused(world, catalog, invalid);
	return without_packs(world.save());
}
Json simultaneous_starts()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "first", true);
	command(world, "second", true);
	ticks(world);
	require(ability(world, "first").at("phase") == "active" && ability(world, "second").at("phase") != "active" &&
	            quantity(world, 0) == 98,
	        "same-tick excluded starts commit only the first stable execution and its cost");
	command(world, "first", false);
	ticks(world, 3);
	require(ability(world, "second").at("phase") != "active" && !ability(world, "second").at("requested").get<bool>() &&
	            quantity(world, 0) == 98,
	        "losing queued activation is cleared rather than starting automatically when the winner stops");
	command(world, "second", true);
	ticks(world);
	require(ability(world, "second").at("phase") == "active" && quantity(world, 0) == 96,
	        "losing ability can start only after a new explicit request");
	return without_packs(world.save());
}
Json exclusions_and_source_recovery()
{
	const auto catalog = load(fixture());
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "first", true);
	ticks(world);
	require(ability(world, "first").at("phase") == "active" && quantity(world, 0) == 98,
	        "first sustained ability owns its explicitly bound apparatus exclusion");
	command(world, "second", true, "abilities", false);
	ticks(world);
	require(ability(world, "first").at("phase") == "active" && ability(world, "second").at("phase") != "active" &&
	            quantity(world, 0) == 98,
	        "conflicting activation cannot replace the running ability or spend its start cost");
	command(world, "second", false);
	command(world, "first", false);
	ticks(world);
	command(world, "second", true);
	ticks(world);
	require(ability(world, "second").at("phase") == "active" && quantity(world, 0) == 96,
	        "explicit stop releases exclusions for a later admitted start");
	command(world, "second", false);
	command(world, "duty-pulse", true);
	ticks(world);
	const auto original_ready = ability(world, "duty-pulse").at("ready");
	assign(world, "a", "mount");
	assign(world, "b", "radar");
	command(world, "officer-pulse", true, "abilities", false);
	ticks(world);
	require(ability(world, "officer-pulse").at("ready") == original_ready && quantity(world, 1) == 1,
	        "officer recovery follows the source across reassignment and a second grant handle");
	command(world, "duty-pulse", true);
	ticks(world);
	require(quantity(world, 1) == 2,
	        "replacement officer uses their own readiness rather than inheriting the seat timer");
	const auto saved = world.save();
	World continued;
	continued.restore(catalog, saved);
	ticks(world, 15);
	ticks(continued, 15);
	require(world.save() == continued.save(), "source-owned recovery and exclusions restore exactly");
	return without_packs(world.save());
}
Json &effect(Json &source, const std::string &id)
{
	for (auto &row : source["effects"])
		if (row.at("id") == id)
			return row;
	throw std::runtime_error("Missing synthetic ability definition");
}
Json ai_ability_facts()
{
	auto source = fixture();
	// Passive contacts have a default observation Console but no Controller layout.
	// They must coexist with AI ability snapshots without requiring EntityControls.
	source["entities"].push_back(
	    {{"id", "test.passive-contact"},
	     {"systems", Json::object()},
	     {"render", {{"mode", "camera"}, {"images", {"test-core:assets/pixel.png"}}, {"scale", 1}}},
	     {"collision", {{"shape", "circle"}, {"radius_mm", 100}, {"mass_grams", 0}}}});
	source["maps"][0]["entities"].push_back(
	    {{"id", 99}, {"definition", "test.passive-contact"}, {"x_mm", 5000000}, {"y_mm", 0}, {"heading_mdeg", 0}});
	source["trees"].push_back(Json::parse(R"json({
 "id":"test.ability-policy","context":"controller","initial":"start",
 "ability_bindings":["equipment-prepared"],
 "command_bindings":[{"channel":"equipment-prepared","action":"ability_activate","type":"boolean"},{"channel":"seen"}],
 "states":[
  {"id":"start","entry":[{"action":"write.equipment-prepared","arguments":[true]},{"action":"write.seen","arguments":[1]}],
   "transitions":[{"target":"preparing","conditions":[{"left":{"observation":"ability.equipment-prepared.phase"},"op":"eq","right":"preparing"}]}]},
  {"id":"preparing","entry":[{"action":"write.seen","arguments":[2]}],
   "transitions":[{"target":"recovering","conditions":[{"left":{"observation":"ability.equipment-prepared.phase"},"op":"eq","right":"recovering"}]}]},
  {"id":"recovering","entry":[{"action":"write.seen","arguments":[3]}],
   "transitions":[{"target":"ready","conditions":[{"left":{"observation":"ability.equipment-prepared.ready"},"op":"eq","right":true}]}]},
  {"id":"ready","entry":[{"action":"write.seen","arguments":[4]}]}
 ]})json"));
	for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "abilities")
		{
			controller.erase("fallback");
			controller["tree"] = "test.ability-policy";
			controller["commands"].push_back({{"id", "seen"},
			                                  {"action", "intent_integer"},
			                                  {"minimum", 0},
			                                  {"maximum", 4},
			                                  {"consumers", Json::array()}});
		}
	for (auto &profile : source["entities"][0]["control_layout"]["profiles"])
	{
		Json human = Json::array();
		for (const auto &controller : profile["consoles"]["drive"]["human"])
			if (controller != "abilities")
				human.push_back(controller);
		profile["consoles"]["drive"]["human"] = human;
		profile["consoles"]["drive"]["ai"].push_back("abilities");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	bool preparing = false, recovering = false, ready = false;
	for (int i = 0; i < 30; ++i)
	{
		ticks(world);
		const auto observed = world.observe(101);
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("id") == "abilities")
			{
				const auto seen = controller.at("commands").at("seen").at("value").get<int>();
				preparing |= seen == 2;
				recovering |= seen == 3;
				ready |= seen == 4;
			}
	}
	require(preparing && recovering && ready && quantity(world, 1) == 1,
	        "ordinary AI tree observes typed preparation, recovery and readiness facts and activates once");
	auto foreign = source;
	foreign["entities"][0]["systems"]["mount"]["abilities"] = {"test.ability.first"};
	foreign["entities"][0]["ability_controls"]["foreign"] = {
	    {"source", "equipment"}, {"owner", "mount"},      {"ability", "test.ability.first"},
	    {"apparatus", "mount"},  {"controller", "radar"}, {"console", "drive"},
	    {"enabled", "foreign"}};
	for (auto &controller : foreign["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "radar")
			controller["commands"].push_back(
			    {{"id", "foreign"}, {"action", "intent_boolean"}, {"consumers", Json::array()}});
	for (auto &tree : foreign["trees"])
		if (tree.at("id") == "test.ability-policy")
			tree["ability_bindings"].push_back("foreign");
	bool rejected = false;
	try
	{
		(void)load(foreign);
	}
	catch (const std::exception &error)
	{
		rejected = std::string(error.what()).find("ability observation must name an owned") != std::string::npos;
	}
	require(rejected, "Controller compilation rejects a foreign ability endpoint binding before simulation");
	return without_packs(world.save());
}
void emitter(Json &source, const std::string &start, const std::string &activation, int preparation = 0)
{
	Json ability = {{"id", "test.ability.manager"},
	                {"kind", "ability"},
	                {"label", "Grant manager"},
	                {"target", "self"},
	                {"preparation_ticks", preparation}};
	if (!start.empty())
		ability["start_effect"] = start;
	if (!activation.empty())
		ability["activation_effect"] = activation;
	source["effects"].push_back(ability);
	auto &ship = source["entities"][0];
	ship["systems"]["radar"]["abilities"].push_back("test.ability.manager");
	ship["ability_controls"]["manager"] = {
	    {"source", "equipment"},     {"owner", "radar"},   {"ability", "test.ability.manager"},
	    {"controller", "abilities"}, {"console", "drive"}, {"enabled", "manager"}};
	for (auto &controller : ship["control_layout"]["controllers"])
		if (controller.at("id") == "abilities")
			controller["commands"].push_back(
			    {{"id", "manager"}, {"action", "ability_activate"}, {"consumers", Json::array()}});
}
Json removal_owner_identity()
{
	auto source = fixture();
	source["effects"].push_back({{"id", "test.ability.mixed-grants"},
	                             {"kind", "operations"},
	                             {"operations", Json::array({{{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"effect", "test.ability.independent"},
	                                                          {"grant", "shared"},
	                                                          {"equipment", "radar"}},
	                                                         {{"kind", "apply"},
	                                                          {"target", "protected"},
	                                                          {"effect", "test.ability.temporary-grant"},
	                                                          {"grant", "shared"},
	                                                          {"officer", "a"}}})}});
	source["effects"].push_back(
	    {{"id", "test.ability.remove-modifier"},
	     {"kind", "operations"},
	     {"operations",
	      Json::array({{{"kind", "remove"}, {"target", "protected"}, {"grant", "shared"}, {"equipment", "radar"}}})}});
	emitter(source, "test.ability.mixed-grants", "test.ability.remove-modifier", 3);
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "manager", true);
	ticks(world);
	const auto count_modifiers = [](const World &world)
	{
		const auto saved = world.save();
		for (const auto &body : saved.at("bodies"))
			if (body.at("body").at("id") == 1)
				return phoenix::test::modifier_applications(saved, 1).size();
		throw std::runtime_error("Missing mixed-grant source body");
	};
	require(count_modifiers(world) == 1 && has_ability(world, "temporary"),
	        "one stable source can grant a modifier and officer ability with the same local name");
	ticks(world, 3);
	require(count_modifiers(world) == 0 && has_ability(world, "temporary"),
	        "equipment modifier removal must not select a same-parent officer ability grant");
	return without_packs(world.save());
}
Json partial_funding()
{
	Json trace = Json::array();
	for (const auto &[prepared, policy] : std::vector<std::pair<bool, std::string>>{
	         {true, "pause"}, {false, "pause"}, {false, "stop"}, {true, "proportional"}, {false, "proportional"}})
	{
		auto source = fixture();
		auto &ship = source["entities"][0];
		// Every tick offers ten whole energy units against twenty required. Tiny
		// rates can legitimately pulse fully funded work as whole units accrue.
		ship["resources"]["stores"].push_back({{"id", "z-ability-cell"},
		                                       {"capacity", 1000},
		                                       {"initial", 1000},
		                                       {"charge_rate", 200},
		                                       {"discharge_rate", 200}});
		ship["systems"]["radar"]["power_store"] = "z-ability-cell";
		ship["systems"]["radar"]["energy_per_second"] = 0;
		ship["systems"]["native-load"] = {{"kind", "radar"},   {"compartment", "fore"}, {"range_mm", 1},
		                                  {"shows", {"ship"}}, {"power_store", "cell"}, {"energy_per_second", 20}};
		ship["control_layout"]["controllers"].push_back(
		    {{"id", "native-load"},
		     {"console", "drive"},
		     {"fallback", "hold"},
		     {"commands", Json::array({{{"id", "enabled"},
		                                {"action", "system_enabled"},
		                                {"initial", 1},
		                                {"consumers", {"native-load"}}}})}});
		for (auto &profile : ship["control_layout"]["profiles"])
			profile["consoles"]["drive"]["human"].push_back("native-load");
		ship["personnel"]["duties"].push_back({{"id", "native-load"},
		                                       {"label", "Native load"},
		                                       {"department", "watch"},
		                                       {"compartment", "fore"},
		                                       {"controller", "native-load"},
		                                       {"console", "drive"}});
		if (policy != "proportional")
		{
			ship["systems"]["shared-load"] = ship["systems"]["native-load"];
			ship["systems"]["shared-load"]["power_store"] = "z-ability-cell";
			ship["systems"]["shared-load"]["energy_per_second"] = 80;
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "shared-load"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands", Json::array({{{"id", "enabled"},
			                                {"action", "system_enabled"},
			                                {"initial", 1},
			                                {"consumers", {"shared-load"}}}})}});
			for (auto &profile : ship["control_layout"]["profiles"])
				profile["consoles"]["drive"]["human"].push_back("shared-load");
			ship["personnel"]["duties"].push_back({{"id", "shared-load"},
			                                       {"label", "Shared native load"},
			                                       {"department", "watch"},
			                                       {"compartment", "fore"},
			                                       {"controller", "shared-load"},
			                                       {"console", "drive"}});
		}
		auto &definition = effect(source, prepared ? "test.ability.prepared" : "test.ability.first");
		definition.erase("start_effect");
		definition.erase("activation_effect");
		definition["shortage"] = policy;
		definition["preparation_ticks"] = prepared ? 100 : 0;
		definition[prepared ? "preparation_energy_per_second" : "energy_per_second"] = 400;
		const auto control = prepared ? "equipment-prepared" : "first";
		const auto catalog = load(source);
		World world, baseline;
		world.load(catalog, "test.map");
		baseline.load(catalog, "test.map");
		claim(world);
		claim(baseline);
		command(world, control, true);
		ticks(world, 20);
		ticks(baseline, 20);
		const auto status = ability(world, control);
		const auto context =
		    std::string(prepared ? " prepared/" : " sustained/") + policy +
		    " main=" + std::to_string(quantity(world, 0)) + "/baseline=" + std::to_string(quantity(baseline, 0)) +
		    " low=" + std::to_string(quantity(world, 2)) + "/baseline=" + std::to_string(quantity(baseline, 2)) +
		    " progress=" + status.at("progress").dump() + " output=" + status.at("output").dump() +
		    " phase=" + status.at("phase").dump();
		require(quantity(world, 0) == quantity(baseline, 0) && quantity(baseline, 0) < 100,
		        ("ability funding requote preserves real independent native consumption" + context).c_str());
		if (policy == "proportional")
		{
			require(
			    quantity(world, 2) == 800 && (prepared ? status.at("progress").get<std::int64_t>() == 10000
			                                           : status.at("output").get<std::int64_t>() == 500),
			    ("proportional ability work consumes and advances only its funded native fraction" + context).c_str());
		}
		else
		{
			require(quantity(world, 2) == quantity(baseline, 2) && quantity(baseline, 2) < 1000 &&
			            status.at("progress") == 0 && status.at("output") == 0 && status.at("phase") != "active",
			        ("pause/stop spends no ability share and preserves ordinary same-bank work after requote" + context)
			            .c_str());
		}
		const auto observed = world.observe(101);
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("id") == "abilities")
			{
				const auto &receipt = controller.at("commands").at(control);
				require(
				    receipt.at("accepted") == true && receipt.at("consumed") == receipt.at("revision"),
				    "funding shortage preserves the consumed admitted start outcome and reports operation separately");
			}
		const auto saved = world.save();
		World resumed;
		resumed.restore(catalog, saved);
		ticks(world, 3);
		ticks(resumed, 3);
		require(world.save() == resumed.save(),
		        "native fractional funding and paused work continue exactly after restore");
		trace.push_back(without_packs(world.save()));
	}
	return trace;
}
Json atomic_required_groups()
{
	Json trace = Json::array();
	for (bool prepared : {false, true})
	{
		auto source = fixture();
		source["effects"].push_back({{"id", "test.ability.impossible"},
		                             {"kind", "operations"},
		                             {"operations", Json::array({{{"kind", "add"},
		                                                          {"store", "energy:marker"},
		                                                          {"resource", "fixture.resource.energy"},
		                                                          {"amount", 1}},
		                                                         {{"kind", "consume"},
		                                                          {"store", "energy:cell"},
		                                                          {"resource", "fixture.resource.energy"},
		                                                          {"amount", 200}}})}});
		auto &definition = effect(source, prepared ? "test.ability.prepared" : "test.ability.pulse");
		definition["start_effect"] = "test.ability.start";
		definition["activation_effect"] = "test.ability.impossible";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		const auto control = prepared ? "equipment-prepared" : "equipment-pulse";
		command(world, control, true);
		ticks(world, prepared ? 7 : 1);
		require(quantity(world, 0) == (prepared ? 98 : 100) && quantity(world, 1) == 0 &&
		            ability(world, control).at("ready") == 0,
		        "failed required activation commits no partial stock/effects/recovery; only prior prepared work stays "
		        "paid");
		const auto observation = world.observe(101);
		for (const auto &controller : observation->gameplay.at("controllers"))
			if (controller.at("id") == "abilities")
			{
				const auto &outcome = controller.at("commands").at(control);
				require(outcome.at("accepted") == false && outcome.at("revision") == outcome.at("consumed"),
				        "failed activation records its consumed request outcome");
			}
		ticks(world, 5);
		require(quantity(world, 0) == (prepared ? 98 : 100), "failed request cannot silently retry startup payment");
		trace.push_back(without_packs(world.save()));
	}
	return trace;
}
void schedule_grants(Json &source, const std::vector<std::pair<int, std::string>> &schedule)
{
	Json states = Json::array();
	for (std::size_t i = 0; i < schedule.size(); ++i)
	{
		const auto wait = "wait-" + std::to_string(i), apply = "apply-" + std::to_string(i);
		states.push_back(
		    {{"id", wait},
		     {"transitions", Json::array({{{"target", apply},
		                                   {"conditions", Json::array({{{"left", {{"observation", "tick"}}},
		                                                                {"op", "ge"},
		                                                                {"right", schedule[i].first}}})}}})}});
		Json action = {{"id", apply},
		               {"entry", Json::array({{{"action", "apply_effect_group"},
		                                       {"arguments", Json::array({{{"type", "entity"}, {"id", 1}},
		                                                                  {{"type", "entity"}, {"id", 1}},
		                                                                  schedule[i].second})}}})}};
		if (i + 1 < schedule.size())
			action["transitions"] = Json::array({{{"target", "wait-" + std::to_string(i + 1)}}});
		states.push_back(action);
	}
	source["trees"].push_back(
	    {{"id", "test.ability-grants"}, {"context", "scenario"}, {"initial", "wait-0"}, {"states", states}});
	source["maps"][0]["scenario"] = {{"tree", "test.ability-grants"}};
}
Json temporary_grants()
{
	auto authored = fixture();
	emitter(authored, "test.ability.grant", "test.ability.grant", 3);
	const auto catalog = load(authored);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "manager", true);
	ticks(world);
	const auto generation = ability(world, "temporary").at("generation");
	command(world, "temporary", true);
	ticks(world);
	const auto ready = ability(world, "temporary").at("ready");
	require(ability(world, "temporary").at("phase") == "recovering", "temporary officer grant activates normally");
	ticks(world, 2);
	require(ability(world, "temporary").at("generation") == generation &&
	            ability(world, "temporary").at("ready") == ready,
	        "emitter activation refreshes its start grant without replacing identity or recovery");
	const auto middle = world.save();
	World continued;
	continued.restore(catalog, middle);
	ticks(world, 8);
	ticks(continued, 8);
	require(world.save() == continued.save(), "temporary lifetime and independent effects restore exactly");
	bool independent = false;
	const auto expired = world.save();
	for (const auto &row : expired.at("bodies"))
		if (row.at("body").at("id") == 1)
			independent =
			    !phoenix::test::modifier_applications(world.save(), row.at("body").at("id").get<std::uint64_t>())
			         .empty();
	require(independent && !has_ability(world, "temporary"),
	        "grant expiry withdraws the ability while its independent applied effect remains");
	command(world, "manager", true);
	ticks(world, 4);
	require(ability(world, "temporary").at("generation") != generation &&
	            ability(world, "temporary").at("ready") == ready,
	        "another emitter request grants a fresh lifetime without clearing source recovery");
	command(world, "temporary", true, "abilities", false);
	ticks(world);
	require(ability(world, "temporary").at("ready") == ready, "temporary regrant cannot bypass source-owned recovery");
	const auto other = world.observe(102)->gameplay;
	require(!other.contains("abilities") || other.at("abilities").empty(),
	        "unbound Console cannot inspect ability controls");
	const auto allowed = network::bounded_gameplay(world.observe(101)->gameplay);
	require(allowed.at("abilities") == world.observe(101)->gameplay.at("abilities"),
	        "standard ability board survives network projection");
	return without_packs(world.save());
}
Json late_grant_replacement()
{
	auto source = fixture();
	effect(source, "test.ability.temporary")["activation_effect"] = "test.ability.mark";
	source["effects"].push_back(
	    {{"id", "test.ability.replace-grant"},
	     {"kind", "operations"},
	     {"operations", Json::array({{{"kind", "remove"}, {"grant", "temporary"}, {"officer", "a"}},
	                                 {{"kind", "apply"},
	                                  {"effect", "test.ability.temporary-grant"},
	                                  {"grant", "temporary"},
	                                  {"officer", "a"}}})}});
	emitter(source, "test.ability.grant", "test.ability.replace-grant");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "manager", true);
	ticks(world);
	const auto old_generation = ability(world, "temporary").at("generation").get<std::int64_t>();
	const auto observed = world.observe(101);
	for (const auto &controller : observed->gameplay.at("controllers"))
		if (controller.at("id") == "abilities")
		{
			OperatingCommand stale{1, {}, 1, {}, "abilities", "temporary", controller.at("generation")};
			stale.request_id = controller.at("commands").at("temporary").at("request_id").get<std::uint64_t>() + 1;
			stale.arguments = {old_generation};
			require(fixture_native_submit(world, 101, stale),
			        "old generation request is admitted before another activation replaces its grant");
		}
	command(world, "manager", true);
	ticks(world);
	require(ability(world, "temporary").at("generation").get<std::int64_t>() != old_generation &&
	            quantity(world, 1) == 0,
	        "remove/regrant before prepared snapshot consumption cannot execute a stale selected lifetime");
	ticks(world, 2);
	require(quantity(world, 1) == 0, "stale request is not retried against the replacement lifetime");
	command(world, "temporary", true);
	ticks(world);
	require(quantity(world, 1) == 1 && quantity(world, 0) == 93, "fresh replacement request executes normally");
	return without_packs(world.save());
}
Json interruption_and_expiry()
{
	auto authored = fixture();
	effect(authored, "test.ability.prepared")["preparation_ticks"] = 6;
	effect(authored, "test.ability.prepared")["interruption"] = "pause";
	const auto catalog = load(authored);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "equipment-prepared", true);
	ticks(world, 2);
	const auto progress = ability(world, "equipment-prepared").at("progress");
	command(world, "enabled", false, "radar");
	ticks(world, 3);
	require(ability(world, "equipment-prepared").at("progress") == progress && quantity(world, 0) == 98 &&
	            quantity(world, 1) == 0,
	        "apparatus loss pauses preparation without advancing work or repaying startup");
	command(world, "enabled", true, "radar");
	ticks(world, 8);
	require(quantity(world, 0) == 91 && quantity(world, 1) == 1,
	        "restored apparatus resumes retained preparation and pays activation once");
	Json trace = Json::array({without_packs(world.save())});
	for (bool sustained : {false, true})
	{
		auto temporary = fixture();
		auto &definition = effect(temporary, "test.ability.temporary");
		definition["sustained"] = sustained;
		for (auto &controller : temporary["entities"][0]["control_layout"]["controllers"])
			if (controller.at("id") == "abilities")
				for (auto &channel : controller["commands"])
					if (channel.at("id") == "temporary")
						channel["action"] = sustained ? "intent_boolean" : "ability_activate";
		definition["preparation_ticks"] = sustained ? 0 : 10;
		definition["activation_effect"] = "test.ability.mark";
		definition["recovery_on"] = sustained ? "stop" : "activation";
		effect(temporary, "test.ability.temporary-grant")["duration_ticks"] = 5;
		schedule_grants(temporary, {{1, "test.ability.grant"}});
		const auto local = load(temporary);
		World granted;
		granted.load(local, "test.map");
		claim(granted);
		ticks(granted, 2);
		command(granted, "temporary", true);
		ticks(granted);
		require(ability(granted, "temporary").at("phase") == (sustained ? "active" : "preparing"),
		        "temporary grant begins the requested preparation or sustained operation");
		const auto pending = granted.save();
		if (sustained)
		{
			auto invalid = pending;
			for (auto &owner : invalid["abilities"]["ships"])
				if (owner.at("ship") == 1)
				{
					auto &state = owner["state"];
					Json duplicate;
					for (const auto &[key, grant] : state.at("grants").items())
						if (grant.at("definition") == "test.ability.temporary" && grant.at("phase") == "active")
							duplicate = grant;
					require(!duplicate.is_null(), "temporary grant is active before duplicate-source save validation");
					const auto parent = duplicate.at("parent").get<std::string>() + ".second";
					const auto id = "temporary:" + parent + ":" + duplicate.at("source").get<std::string>() + ":" +
					                duplicate.at("owner").get<std::string>();
					duplicate["parent"] = parent;
					duplicate["id"] = id;
					duplicate["generation"] = state.at("next_generation");
					state["next_generation"] = state.at("next_generation").get<std::uint64_t>() + 1;
					duplicate["activation"] = state.at("next_activation");
					state["next_activation"] = state.at("next_activation").get<std::uint64_t>() + 1;
					state["grants"][id] = duplicate;
				}
			restore_refused(granted, local, invalid, "Duplicate saved running source ability");
		}
		World resumed;
		resumed.restore(local, pending);
		ticks(granted, 12);
		ticks(resumed, 12);
		require(
		    granted.save() == resumed.save() && !has_ability(granted, "temporary") &&
		        quantity(granted, 1) == (sustained ? 1 : 0),
		    "grant expiry cancels preparation or stops active work with exact continuation and no later activation");
		trace.push_back(without_packs(granted.save()));
	}
	return trace;
}
Json explicit_group_costs()
{
	Json trace = Json::array();
	for (const auto &[prepared, stock] : std::vector<std::pair<bool, int>>{{false, 100}, {false, 6}, {true, 6}})
	{
		auto source = fixture();
		source["entities"][0]["resources"]["stores"][0]["initial"] = stock;
		source["effects"].push_back(Json::parse(R"json({"id":"test.batch.start","kind":"operations",
            "costs":[{"payer":"source","store":"cell","resource":"fixture.resource.energy","amount":3}],
            "operations":[{"kind":"add","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]})json"));
		source["effects"].push_back(Json::parse(R"json({"id":"test.batch.finish","kind":"operations",
            "costs":[{"payer":"owner","store":"energy:cell","resource":"fixture.resource.energy","amount":4}],
            "operations":[{"kind":"add","target":"subject","store":"energy:marker","resource":"fixture.resource.energy","amount":2}]})json"));
		auto &ability_definition = effect(source, prepared ? "test.ability.prepared" : "test.ability.pulse");
		ability_definition["start_effect"] = "test.batch.start";
		ability_definition["activation_effect"] = "test.batch.finish";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		const auto endpoint = prepared ? "equipment-prepared" : "equipment-pulse";
		command(world, endpoint, true);
		ticks(world);
		if (prepared)
			require(quantity(world, 0) == 3 && quantity(world, 1) == 1 &&
			            ability(world, endpoint).at("phase") == "preparing",
			        "prepared start pays its own explicit cost and consequence before a later required activation");
		else if (stock == 100)
			require(quantity(world, 0) == 93 && quantity(world, 1) == 3 && ability(world, endpoint).at("ready") > 0,
			        "instant start and activation commit both explicit costs and independent outputs once");
		else
			require(quantity(world, 0) == 6 && quantity(world, 1) == 0 && ability(world, endpoint).at("ready") == 0,
			        "aliases of one physical reserve share a batch balance and insufficient combined cost rolls back "
			        "all instant work");
		World resumed;
		resumed.restore(catalog, world.save());
		ticks(world, 8);
		ticks(resumed, 8);
		require(world.save() == resumed.save(),
		        "explicit group payments and prepared activation outcome restore without replay");
		if (stock == 6)
		{
			require(quantity(world, 0) == (prepared ? 3 : 6) && quantity(world, 1) == (prepared ? 1 : 0) &&
			            ability(world, endpoint).at("ready") == 0,
			        "failed required activation preserves prior preparation payments but creates no recovery or "
			        "automatic retries");
			const auto observed = world.observe(101);
			for (const auto &owner : observed->gameplay.at("controllers"))
				if (owner.at("id") == "abilities")
					require(owner.at("commands").at(endpoint).at("accepted") == false,
					        "combined explicit cost failure is exposed as the consumed request outcome");
		}
		trace.push_back(without_packs(world.save()));
	}
	return trace;
}
Json separate_group_limits()
{
	auto source = fixture();
	Json operations = Json::array();
	for (int i = 0; i < 16; ++i)
		operations.push_back({{"kind", "add"},
		                      {"target", "subject"},
		                      {"store", "energy:marker"},
		                      {"resource", "fixture.resource.energy"},
		                      {"amount", 1}});
	for (const auto &[id, amount] :
	     std::vector<std::pair<std::string, int>>{{"test.limit.start", 3}, {"test.limit.finish", 4}})
		source["effects"].push_back({{"id", id},
		                             {"kind", "operations"},
		                             {"operations", operations},
		                             {"costs", Json::array({{{"payer", "source"},
		                                                     {"store", "energy:cell"},
		                                                     {"resource", "fixture.resource.energy"},
		                                                     {"amount", amount}}})}});
	effect(source, "test.ability.pulse")["start_effect"] = "test.limit.start";
	effect(source, "test.ability.pulse")["activation_effect"] = "test.limit.finish";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	claim(world);
	command(world, "equipment-pulse", true);
	ticks(world);
	require(quantity(world, 0) == 93 && quantity(world, 1) == 32,
	        "start and activation retain their approved combined operation bound while committing one atomic batch");
	effect(source, "test.limit.finish")["operations"].push_back(operations[0]);
	rejects(
	    [&]
	    {
		    (void)load(source);
	    },
	    "ability batching cannot relax the combined thirty-two operation authoring limit");
	effect(source, "test.limit.finish")["operations"].erase(16);
	for (int i = 16; i < 33; ++i)
		effect(source, "test.limit.start")["operations"].push_back(operations[0]);
	rejects(
	    [&]
	    {
		    (void)load(source);
	    },
	    "ability batching cannot relax an individual group's native operation limit");
	return without_packs(world.save());
}
Json rich_ability_context()
{
	Json trace = Json::array();
	for (const auto &[officer, legacy] :
	     std::vector<std::pair<bool, bool>>{{false, false}, {true, false}, {false, true}, {true, true}})
	{
		auto source = fixture();
		const auto endpoint = officer ? "independent-officer" : "equipment-pulse";
		auto &ship = source["entities"][0];
		const auto recipient_channel = std::string(endpoint) + "-recipient";
		effect(source, "test.ability.pulse")["target"] = "contact";
		for (auto &[id, binding] : ship["ability_controls"].items())
			if (binding.at("ability") == "test.ability.pulse")
			{
				const auto channel = id + "-recipient";
				binding["target"] = channel;
				for (auto &controller : ship["control_layout"]["controllers"])
					if (controller.at("id") == "abilities")
						controller["commands"].push_back({{"id", channel},
						                                  {"action", "intent_integer"},
						                                  {"minimum", 0},
						                                  {"maximum", 1000000000},
						                                  {"consumers", Json::array()}});
			}
		Json start_cost = {{"payer", legacy ? "protected" : "source"},
		                   {"store", "cell"},
		                   {"resource", "fixture.resource.energy"},
		                   {"amount", 3}};
		Json finish_cost = {{"payer", legacy ? "attacker" : "owner"},
		                    {"store", "energy:cell"},
		                    {"resource", "fixture.resource.energy"},
		                    {"amount", 4}};
		if (!officer)
			start_cost["equipment"] = "$source";
		source["effects"].push_back({{"id", "test.context.start"},
		                             {"kind", "operations"},
		                             {"costs", Json::array({start_cost})},
		                             {"operations", Json::array({{{"kind", "add"},
		                                                          {"target", legacy ? "protected" : "subject"},
		                                                          {"store", "energy:marker"},
		                                                          {"resource", "fixture.resource.energy"},
		                                                          {"amount", 1}}})}});
		source["effects"].push_back({{"id", "test.context.finish"},
		                             {"kind", "operations"},
		                             {"costs", Json::array({finish_cost})},
		                             {"operations", Json::array({{{"kind", "add"},
		                                                          {"target", legacy ? "attacker" : "target"},
		                                                          {"store", "energy:marker"},
		                                                          {"resource", "fixture.resource.energy"},
		                                                          {"amount", 2}},
		                                                         {{"kind", "apply"},
		                                                          {"target", legacy ? "attacker" : "target"},
		                                                          {"equipment", officer ? "radar" : "$source"},
		                                                          {"effect", "test.ability.independent"},
		                                                          {"grant", "context"}}})}});
		source["effects"].push_back(Json::parse(R"json({"id":"test.context.downstream-group","kind":"operations",
            "costs":[{"payer":"source","store":"energy:cell","resource":"fixture.resource.energy","amount":1}],
            "operations":[{"kind":"add","target":"owner","store":"energy:marker","resource":"fixture.resource.energy","amount":1}]})json"));
		source["effects"].push_back(Json::parse(R"json({"id":"test.context.downstream","kind":"rule","trigger":"tick",
            "phase":"periodic","period_ticks":1,"when":{"op":"eq","args":[{"fact":"source.entity"},{"fact":"instigator.entity"}]},
            "effect":"test.context.downstream-group"})json"));
		effect(source, "test.ability.independent")["rules"] =
		    Json::array({{{"id", "provenance"}, {"rule", "test.context.downstream"}}});
		effect(source, "test.ability.pulse")["start_effect"] = "test.context.start";
		effect(source, "test.ability.pulse")["activation_effect"] = "test.context.finish";
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		claim(world);
		const auto observed = world.observe(101);
		bool submitted = false;
		for (const auto &owner : observed->gameplay.at("controllers"))
			if (owner.at("id") == "abilities")
				submitted = fixture_native_submit(
				    world, 101, OperatingCommand{1, {}, 2, {}, "abilities", recipient_channel, owner.at("generation")});
		require(submitted, "ordinary Console selects an observed cross-ship ability recipient");
		ticks(world);
		command(world, endpoint, true);
		ticks(world);
		const auto source_paid = legacy ? 97 : 93;
		const auto recipient_paid = legacy ? 96 : 100;
		require(quantity(world, 0, 1) == source_paid && quantity(world, 0, 2) == recipient_paid &&
		            quantity(world, 1, 1) == 1 && quantity(world, 1, 2) == 2,
		        "ability source costs, caster-subject output and selected-recipient output resolve to distinct "
		        "physical ships");
		bool provenance = false;
		const auto committed = world.save();
		for (const auto &origin : committed.at("rules").at("origins"))
			if (origin.at("target") == 2 &&
			    origin.at("source").get<std::string>().find("ability.") != std::string::npos)
			{
				const auto &context = origin.at("context");
				provenance = context.at("source").at("entity") == 1 && context.at("owner").at("entity") == 1 &&
				             context.at("instigator").at("entity") == 1 && context.at("subject").at("entity") == 1 &&
				             context.at("target").at("entity") == 2 &&
				             context.at("source").at(officer ? "officer" : "equipment") == (officer ? "c" : "radar");
			}
		require(provenance, "downstream native effect provenance retains the performing officer or equipment and rich "
		                    "caster/recipient roles");
		bool modifier_on_recipient = false;
		for (const auto &row : committed.at("bodies"))
			if (row.at("body").at("id") == 2)
				for (const auto &modifier :
				     modifier_contributions(world.save(), row.at("body").at("id").get<std::uint64_t>()))
					modifier_on_recipient = modifier_on_recipient || modifier.at("equipment") == "radar";
		require(modifier_on_recipient, "equipment source reference selects the matching recipient fitting while "
		                               "retaining original source identity");
		World resumed;
		resumed.restore(catalog, committed);
		ticks(world, 4);
		ticks(resumed, 4);
		require(world.save() == resumed.save() && quantity(world, 0, 1) < source_paid &&
		            quantity(world, 0, 2) == recipient_paid && quantity(world, 1, 2) > 2,
		        "independent downstream rules continue charging original source and publishing recipient output after "
		        "exact restore");
		trace.push_back(without_packs(world.save()));
	}
	return trace;
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
		const Json trace = {run("compiler catalogue reload", compiler_catalogue_reload),
		                    run("recovery deadline bounds", recovery_deadline_bounds),
		                    run("apparatus-free officer", apparatus_free_officer),
		                    run("request replay", request_replay),
		                    run("preparation", prepared_and_once),
		                    run("simultaneous starts", simultaneous_starts),
		                    run("source recovery and exclusions", exclusions_and_source_recovery),
		                    run("AI ability facts", ai_ability_facts),
		                    run("partial funding", partial_funding),
		                    run("atomic required groups", atomic_required_groups),
		                    run("explicit group costs", explicit_group_costs),
		                    run("ability group limits", separate_group_limits),
		                    run("rich ability context", rich_ability_context),
		                    run("removal owner identity", removal_owner_identity),
		                    run("late grant replacement", late_grant_replacement),
		                    run("temporary grants", temporary_grants),
		                    run("interruption and expiry", interruption_and_expiry)};
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << '\n';
		else
			std::cout << "Shared ability system checks passed\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
