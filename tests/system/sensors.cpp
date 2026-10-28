#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
import phoenix.content.controllers;
import phoenix.content.distribution;
import phoenix.simulation;
import phoenix.console;
import phoenix.session.execution;
import phoenix.networking.projection;
import phoenix.knowledge.read_models;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	auto &s = source["entities"][0];
	s["class_key"] = "Test cruiser";
	s["systems"]["radar"]["capabilities"] = {{"basic_identity", 0}, {"contact_arcs", 0}, {"own_arcs", 0}};
	s["systems"]["scanner-mount"] = s["systems"]["mount"];
	s["systems"]["scanner"] = {
	    {"kind", "radar"},
	    {"range_mm", 100000},
	    {"arc_mdeg", 12000},
	    {"turret", "scanner-mount"},
	    {"shows", {"ship"}},
	    {"compartment", "fore"},
	    {"capabilities", {{"shield_sectors", 4}, {"compartment_condition", 8}, {"findings", 0}}}};
	s["stations"][0]["consoles"][0]["feeds"].push_back("scanner");
	s["stations"][0]["consoles"][0]["feeds"].push_back("scanner-mount");
	s["stations"][0]["consoles"][1]["feeds"] = Json::array({"team-a", "team-b"});
	s["control_layout"]["controllers"].push_back(
	    {{"id", "scanner"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands", Json::array({{{"id", "target"}, {"action", "sensor_target"}, {"consumers", {"scanner"}}},
	                               {{"id", "enabled"},
	                                {"action", "system_enabled"},
	                                {"initial", 1},
	                                {"consumers", {"scanner", "scanner-mount"}}}})}});
	s["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("scanner");
	source["maps"][0]["sensor_findings"] = Json::array({{{"id", "engineering"},
	                                                     {"text_key", "Isolated conduit"},
	                                                     {"target_entity", 2},
	                                                     {"scan_ticks", 6},
	                                                     {"requires", {"findings", "compartment_condition"}}}});
	return source;
}
void ticks(World &w, int count)
{
	while (count--)
		w.tick();
}
void command(World &w, const std::string &channel, int value)
{
	const auto o = w.observe(101);
	for (const auto &c : o->gameplay.at("controllers"))
		if (c.at("id") == "scanner")
		{
			require(w.submit(101, OperatingCommand{1, {}, value, {}, "scanner", channel, c.at("generation")}),
			        "scanner Controller command admitted");
			return;
		}
	throw std::runtime_error("missing scanner");
}
Json scanner_state(const World &w)
{
	const auto saved = w.save();
	for (const auto &row : saved.at("sensors"))
		if (row.at("ship") == 1 && row.at("system") == "scanner")
			return row;
	throw std::runtime_error("missing saved scanner");
}
Json samples(const World &w, int target = 2)
{
	const auto saved = w.save();
	for (const auto &s : saved.at("sensors"))
		if (s.at("ship") == 1 && s.at("system") == "scanner")
			for (const auto &t : s.at("tracks"))
				if (t.at("target") == target)
					return t.at("state").at("samples");
	return Json::object();
}
void surface_contact_range()
{
	auto source = combat_fixture();
	auto &radar = source["entities"][0]["systems"]["radar"];
	radar["range_mm"] = 100000;
	radar["shows"] = {"planet"};
	radar["broad_capabilities"] = {{"basic_identity", 0}, {"contact_arcs", 0}, {"shield_sectors", 0}};
	auto &planet = source["entities"][1];
	planet["tags"] = {"planet"};
	planet["collision"]["radius_mm"] = 50000;
	auto &bodies = source["maps"][0]["entities"];
	bodies[1]["definition"] = "test.visual";
	bodies[1]["x_mm"] = 140000;
	bodies[2]["definition"] = "test.visual";
	bodies[2]["x_mm"] = 180000;
	bodies[2]["y_mm"] = 0;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive"), "claim radius-aware radar");
	world.tick();
	const auto view = *world.observe(101);
	require(view.contacts.size() == 1 && view.contacts[0].id == 2 && view.contacts[0].radius == 50000,
	        "radar detects a body's near surface beyond centre range, without revealing a farther body");
	const auto client = resolve_packs({ship_client_assets(catalog, view.own.definition)});
	const auto decoded = network::read_observation(network::observation_json(view), client);
	require(decoded.contacts[0].radius == 50000, "permitted contact footprint survives restricted network projection");
	require(projection::read_body(projection::body_json(view.contacts[0]), projection::BodyReadPolicy::SharedDisplay)
	                .radius == 50000,
	        "shared radar preserves the sensed footprint");
}
void independent_shield_scan()
{
	auto source = fixture();
	source["effects"] = Json::parse(R"json([{"id":"test.remote-shield","kind":"shield","duration_ticks":20,
  "contributions":{"bubble":{"capacity":12,"initial":9,"sustain_energy_per_second":0,
   "recharge_per_second":0,"off_recharge_per_second":0,"warmup_ticks":1}}}])json");
	source["maps"][0]["scenario"] = {{"tree", "test.remote-shield-tree"}, {"bindings", {{"actor", {{"entity", 2}}}}}};
	source["trees"].push_back(Json::parse(R"json({"id":"test.remote-shield-tree","context":"scenario","initial":"apply",
  "states":[{"id":"apply","entry":[{"action":"apply_shield_effect",
   "arguments":[{"observation":"self.entity"},"test.remote-shield","blessing"]}]}]})json"));
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "claim remote shield scan consoles");
	ticks(world, 1);
	command(world, "target", 2);
	ticks(world, 4);
	const auto acquired = samples(world).at("shield_sectors");
	const auto sectors = acquired.at("data");
	require(sectors.size() == 1 && sectors[0].at("contribution") == "bubble" && sectors[0].at("strength") == 9 &&
	            sectors[0].at("id").get<std::string>().starts_with("effect:"),
	        "Production scanner failed to acquire non-equipment shield contribution");
	require(!sectors[0].contains("source") && !sectors[0].contains("definition"),
	        "Remote shield scan disclosed private source metadata");
	require(world.observe(102)->gameplay.at("sensors").at("contacts").empty(),
	        "Unfed console acquired remote temporary shield finding");
	command(world, "enabled", 0);
	ticks(world, 20);
	const auto expired = world.save();
	for (const auto &body : expired.at("bodies"))
		if (body.at("body").at("id") == 2)
			require(body.at("shields").at("contributions").empty(), "Temporary scanned shield did not expire");
	require(samples(world).at("shield_sectors") == acquired,
	        "Expired remote shield rewrote a disabled scanner's retained finding");
	command(world, "enabled", 1);
	ticks(world, 1);
	require(samples(world).at("shield_sectors").at("data").empty(),
	        "Resumed scanner failed to replace stale shield finding through ordinary acquisition");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto catalog = load(fixture());
		World w;
		w.load(catalog, "test.map");
		require(w.claim(101, 1, "drive") && w.claim(102, 1, "observer"), "claim stations");
		ticks(w, 1);
		{
			// The client DAT intentionally contains no private officer assignments.
			// It must accept the host's bounded, bonus-adjusted public radar envelope.
			auto boosted = *w.observe(101);
			for (auto &system : boosted.systems)
				if (system.kind == OperatingSystem::Radar)
				{
					system.effective_range = system.maximum + system.maximum / 5;
					system.radial_ranges.assign(128, system.effective_range);
					system.reach_profile = {{360000, system.effective_range}};
					require(display::read_radar(display::radar_json(system)).effective_range == system.effective_range,
					        "shared radar accepts authoritative bonuses above nominal range");
				}
			const auto client = resolve_packs({ship_client_assets(catalog, boosted.own.definition)});
			const auto wire = network::observation_json(boosted);
			require(network::read_observation(wire, client).systems.size() == boosted.systems.size(),
			        "whole-ship DAT accepts bonus-adjusted sensor observations");
			auto invalid = wire;
			for (auto &system : invalid["systems"])
				if (system["kind"] == "radar")
				{
					system["effective_range"] = projection::sensor_range_limit + 1;
					system["reach_profile"] = Json::array();
				}
			bool rejected = false;
			try
			{
				(void)network::read_observation(invalid, client);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "excessive projected sensor range remains rejected");
		}
		require(w.observe(101)->contacts.at(0).definition == "Test cruiser",
		        "public class replaces private definition");
		require(w.observe(102)->contacts.empty() && w.observe(102)->gameplay.at("sensors").at("contacts").empty(),
		        "no unrestricted evidence on unfed console");
		require(samples(w).empty(), "unselected timed scanner reveals nothing");
		command(w, "target", 2);
		ticks(w, 4);
		require(!samples(w).at("shield_sectors").at("data").is_null(), "short capability acquired");
		require(samples(w).at("compartment_condition").at("data").is_null(), "long capability remains private");
		command(w, "enabled", 0);
		ticks(w, 1);
		const auto paused = samples(w);
		ticks(w, 5);
		require(samples(w) == paused, "disabled scanner retains unfinished exposure and snapshots");
		command(w, "enabled", 1);
		ticks(w, 5);
		require(!samples(w).at("compartment_condition").at("data").is_null(), "resumed long acquisition completes");
		require(samples(w).at("finding:engineering").at("data").at("text") == "Isolated conduit",
		        "authored information acquired");
		const auto acquired_observation = w.observe(101);
		for (const auto &contact : acquired_observation->gameplay.at("sensors").at("contacts"))
			require(!contact.at("findings").contains("findings"),
			        "knowledge records do not duplicate direct authored findings");
		auto damaged = w.save();
		for (auto &body : damaged["bodies"])
			if (body.at("body").at("id") == 2)
				body["combat"]["compartments"][0]["hp"] = 45;
		w.restore(catalog, damaged);
		ticks(w, 1);

		std::int64_t actual_hp = 0;
		const auto repaired = w.save();
		for (const auto &body : repaired.at("bodies"))
			if (body.at("body").at("id") == 2)
				actual_hp = body.at("combat").at("compartments")[0].at("hp");
		require(samples(w).at("compartment_condition").at("data")[0].at("hp") == actual_hp && actual_hp < 100,
		        "completed finding updates from current covered target state");
		command(w, "target", 3);
		ticks(w, 3);
		const auto traversal_snapshot = w.save();
		ticks(w, 19);
		require(samples(w, 3).at("compartment_condition").at("exposure") > 0 &&
		            samples(w, 3).at("compartment_condition").at("exposure") < 8,
		        "save includes genuinely partial acquisition");
		const auto snapshot = w.save();
		World restored;
		restored.restore(catalog, snapshot);
		ticks(w, 25);
		ticks(restored, 25);
		World slewing;
		slewing.restore(catalog, traversal_snapshot);
		ticks(slewing, 44);
		require(w.save() == slewing.save(), "saved turret traversal continues identically");
		require(w.save() == restored.save(), "traversal and partial scanning continue identically after restore");
		require(!samples(w, 3).at("compartment_condition").at("data").is_null(),
		        "actual turret traverses to second target");
		require(samples(w).at("compartment_condition").at("observed") <
		            samples(w, 3).at("compartment_condition").at("observed"),
		        "previous target retains stale timestamp");
		for (int variant = 0; variant < 3; ++variant)
		{
			auto incomplete = w.save();
			if (variant == 0)
				incomplete.erase("sensors");
			else if (variant == 1)
				incomplete["sensors"] = Json::array();
			else
				incomplete["sensors"].erase(incomplete["sensors"].begin());
			rejects(
			    [&]
			    {
				    World missing;
				    missing.restore(catalog, incomplete);
			    },
			    "A continuation must retain acquisition state for every fitted sensor");
		}
		auto hidden = fixture();
		hidden["entities"][0]["systems"]["radar"]["capabilities"] = Json::object();
		World unidentified;
		unidentified.load(load(hidden), "test.map");
		require(unidentified.claim(101, 1, "drive"), "claim unknown contacts");
		ticks(unidentified, 1);
		const auto contact = unidentified.observe(101)->contacts.at(0);
		require(contact.definition == "Unidentified contact" && contact.faction.empty() && contact.radar_sprite.empty(),
		        "unknown contact hides private identity");

		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 1000},
			                                              {"initial", 1000},
			                                              {"charge_rate", 200},
			                                              {"discharge_rate", 200}}})}};
			ship["systems"]["radar"]["powerless_start"] = true;
			ship["systems"]["radar"]["energy_per_second"] = 400;
			ship["systems"]["radar"]["power_store"] = "cell";
			World partial;
			partial.load(load(source), "test.map");
			require(partial.claim(101, 1, "drive"), "claim partially powered radar");
			ticks(partial, 1);
			const auto observation = partial.observe(101);
			require(!observation->contacts.empty() && observation->contacts[0].definition == "Unidentified contact",
			        "partial power detects contacts without acquiring instantaneous identity");
			for (const auto &contact : observation->gameplay.at("sensors").at("contacts"))
				require(!contact.at("findings").contains("basic_identity"),
				        "contact identity and detailed evidence obey the same acquisition power rule");
		}
		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 100},
			                                              {"initial", 0},
			                                              {"charge_rate", 100},
			                                              {"discharge_rate", 100}}})}};
			ship["systems"]["scanner"]["powerless_start"] = true;
			ship["systems"]["scanner"]["energy_per_second"] = 20;
			ship["systems"]["scanner"]["power_store"] = "cell";
			World unpowered;
			unpowered.load(load(source), "test.map");
			require(unpowered.claim(101, 1, "drive"), "claim unpowered scanner");
			command(unpowered, "target", 2);
			ticks(unpowered, 20);
			require(samples(unpowered).empty(), "unfunded scanner cannot acquire findings");
		}
		{
			auto source = fixture();
			source["entities"][0]["systems"]["scanner"]["range_mm"] = 59999;
			World outside;
			outside.load(load(source), "test.map");
			require(outside.claim(101, 1, "drive"), "claim range fixture");
			command(outside, "target", 2);
			ticks(outside, 10);
			require(samples(outside).empty(), "outside exact range cannot scan");
			source["entities"][0]["systems"]["scanner"]["range_mm"] = 60000;
			World boundary;
			boundary.load(load(source), "test.map");
			require(boundary.claim(101, 1, "drive"), "claim boundary");
			command(boundary, "target", 2);
			ticks(boundary, 8);
			require(!samples(boundary).at("compartment_condition").at("data").is_null(), "range boundary qualifies");
		}
		{
			auto source = fixture();
			source["trees"].push_back(Json::parse(
			    R"({"id":"test.scan","context":"controller","command_bindings":[{"channel":"target","action":"sensor_target"}],"initial":"scan","states":[{"id":"scan","running":[{"action":"write.target","arguments":[2]}]}]})"));
			source["entities"][0]["control_layout"]["controllers"].back()["tree"] = "test.scan";
			World ai;
			ai.load(load(source), "test.map");
			ticks(ai, 12);
			require(!samples(ai).at("compartment_condition").at("data").is_null(),
			        "AI uses same scan command admission");
		}
		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			ship["publications"] = Json::array({{{"id", "scan"},
			                                     {"source_console", "drive"},
			                                     {"schema", "phoenix.sensor.compartment_condition"},
			                                     {"subject", 2},
			                                     {"fields", {"payload"}},
			                                     {"live", false}}});
			World sharing;
			sharing.load(load(source), "test.map");
			require(sharing.claim(101, 1, "drive") && sharing.claim(102, 1, "observer"), "claim evidence fixture");
			command(sharing, "target", 2);
			ticks(sharing, 8);
			require(sharing.submit(101, OperatingCommand{1, "publish.scan", 0, {}}),
			        "deliberate finding publication admitted");
			ticks(sharing, 1);
			require(!sharing.observe(102)->gameplay.at("sensors").at("contacts").empty(),
			        "publication delivers authorized evidence");
			const auto projected = network::bounded_gameplay(sharing.observe(102)->gameplay);
			require(projected.at("sensors").at("scanners").empty(),
			        "network receiver gains evidence without private scanner state");
			source["entities"][0]["publications"][0]["schema"] = "phoenix.sensor.findings";
			World findings;
			findings.load(load(source), "test.map");
			require(findings.claim(101, 1, "drive") && findings.claim(102, 1, "observer"), "claim finding publication");
			command(findings, "target", 2);
			ticks(findings, 8);
			require(findings.submit(101, OperatingCommand{1, "publish.scan", 0, {}}), "publish authored finding");
			ticks(findings, 1);
			const auto shared_findings = network::bounded_gameplay(findings.observe(102)->gameplay).at("sensors");
			const auto &discovery = shared_findings.at("contacts")[0].at("findings").at("finding:engineering");
			require(discovery.at("data").at("text") == "Isolated conduit" && discovery.at("observed") == 6 &&
			            !discovery.at("live").get<bool>(),
			        "shared finding retains its acquisition timestamp instead of publication time");

			ship["systems"]["radar"]["capabilities"] = Json::object();
			ship["systems"]["scanner"]["capabilities"]["basic_identity"] = 4;
			ship["stations"][0]["consoles"][1]["feeds"].push_back("radar");
			ship["publications"][0]["schema"] = "phoenix.sensor.basic_identity";
			World identity;
			identity.load(load(source), "test.map");
			require(identity.claim(101, 1, "drive") && identity.claim(102, 1, "observer"), "claim shared identity");
			command(identity, "target", 2);
			ticks(identity, 8);
			require(identity.observe(102)->contacts[0].definition == "Unidentified contact",
			        "anonymous detection alone does not identify a contact");
			require(identity.submit(101, OperatingCommand{1, "publish.scan", 0, {}}), "publish acquired identity");
			ticks(identity, 1);
			const auto recipient = identity.observe(102);
			require(recipient->contacts[0].definition == recipient->gameplay.at("sensors")
			                                                 .at("contacts")[0]
			                                                 .at("findings")
			                                                 .at("basic_identity")
			                                                 .at("data")
			                                                 .at("class")
			                                                 .get<std::string>() &&
			            recipient->contacts[0].definition == "Test cruiser",
			        "deliberately shared identity resolves identically on radar and in contact details");
		}
		{
			auto source = fixture();
			source["maps"][0]["scenario"] = {{"tree", "test.sensor-events"}, {"events", {"available", "discovered"}}};
			source["trees"].push_back(Json::parse(
			    R"({"id":"test.sensor-events","context":"scenario","initial":"wait","states":[{"id":"wait","transitions":[{"target":"available","conditions":[{"left":{"elapsed":true},"op":"ge","right":5}]}]},{"id":"available","entry":[{"action":"emit_scenario_event","arguments":["available",0,true,""]}]}]})"));
			auto &f = source["maps"][0]["sensor_findings"][0];
			f["requires_event"] = "available";
			f["discovery_event"] = "discovered";
			World conditioned;
			const auto catalog = load(source);
			conditioned.load(catalog, "test.map");
			World resumed;
			resumed.restore(catalog, conditioned.save());
			require(resumed.save() == conditioned.save(), "event-only situation survives initial lobby snapshot");
			auto missing_controller = conditioned.save();
			missing_controller["scenario"]["controllers"] = Json::array();
			rejects(
			    [&]
			    {
				    resumed.restore(catalog, missing_controller);
			    },
			    "Event source exception must not admit a missing scenario Controller");
			require(conditioned.claim(101, 1, "drive"), "claim conditioned scan");
			command(conditioned, "target", 2);
			ticks(conditioned, 3);
			require(!samples(conditioned).contains("finding:engineering"),
			        "unavailable authored finding does not leak or accumulate");
			ticks(conditioned, 20);
			require(!samples(conditioned).at("finding:engineering").at("data").is_null(), "available finding acquires");
			require(conditioned.save().at("sensor_discoveries").size() == 1,
			        "discovery emitted once per ship and finding");
			resumed.restore(catalog, conditioned.save());
			ticks(conditioned, 5);
			ticks(resumed, 5);
			require(resumed.save() == conditioned.save(), "scenario and sensor event source continue after recovery");
		}

		{
			auto source = fixture();
			auto &ship = source["entities"][0];
			auto copy = ship["systems"]["scanner"];
			copy.erase("turret");
			copy["arc_mdeg"] = 360000;
			ship["systems"]["second-scanner"] = copy;
			ship["stations"][0]["consoles"][0]["feeds"].push_back("second-scanner");
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "second-scanner"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands",
			      Json::array({{{"id", "target"}, {"action", "sensor_target"}, {"consumers", {"second-scanner"}}}})}});
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("second-scanner");
			World concurrent;
			concurrent.load(load(source), "test.map");
			require(concurrent.claim(101, 1, "drive"), "claim concurrent scanners");
			command(concurrent, "target", 2);
			ticks(concurrent, 3);
			const auto observed = concurrent.observe(101);
			for (const auto &c : observed->gameplay.at("controllers"))
				if (c.at("id") == "second-scanner")
					require(concurrent.submit(
					            101, OperatingCommand{1, {}, 2, {}, "second-scanner", "target", c.at("generation")}),
					        "independent second scanner admitted");
			ticks(concurrent, 5);
			require(!samples(concurrent).at("compartment_condition").at("data").is_null(),
			        "first scanner completes independently");
			const auto saved = concurrent.save();
			for (const auto &scanner : saved.at("sensors"))
				if (scanner.at("ship") == 1 && scanner.at("system") == "second-scanner")
					require(scanner.at("tracks")[0]
					            .at("state")
					            .at("samples")
					            .at("compartment_condition")
					            .at("data")
					            .is_null(),
					        "second scanner cannot pool first scanner exposure");
			for (const auto &directed : saved.at("directed"))
				if (directed.at("ship") == 1 && directed.at("system") == "gun")
					require(directed.at("target") == 0, "scan targeting leaves weapon intent untouched");
		}

		{
			auto source = fixture();
			auto &systems = source["entities"][0]["systems"];
			systems["radar"]["range_mm"] = 30000;
			systems["radar"]["long_range_mm"] = 90000;
			systems["scanner"]["range_mm"] = 40000;
			const auto c = load(source);
			World long_range;
			long_range.load(c, "test.map");
			require(long_range.claim(101, 1, "drive") && long_range.claim(102, 1, "observer"),
			        "claim bearing consoles");
			ticks(long_range, 1);
			const auto observed = long_range.observe(101);
			require(observed->contacts.empty() && observed->bearing_contacts.size() == 2,
			        "outer band grants bearing only");
			require(long_range.observe(102)->bearing_contacts.empty(), "unfed Console receives no bearings");
			require(observed->bearing_contacts[0].id == 2 && observed->bearing_contacts[0].bearing_mdeg < 20 &&
			            std::abs(observed->bearing_contacts[1].bearing_mdeg - 90000) < 20,
			        "bearing geometry without range");
			const auto encoded = network::observation_json(*observed);
			for (const auto &b : encoded.at("bearing_contacts"))
				require(b.size() == 3 && !b.contains("x") && !b.contains("definition"),
				        "bearing wire allowlist excludes geometry and identity");
			require(network::read_observation(encoded, c).bearing_contacts.size() == 2,
			        "bearing projection round trip");

			Json shared{{"revision", 1},
			            {"console", "drive"},
			            {"widget", "radar"},
			            {"kind", "radar"},
			            {"own", encoded.at("own")},
			            {"camera", {0, 0, 1}},
			            {"contacts", Json::array()},
			            {"regions", Json::array()},
			            {"waypoint", Json::object()},
			            {"objectives", Json::array()},
			            {"radar", display::radar_json(observed->systems[0])},
			            {"bearing_contacts", encoded.at("bearing_contacts")}};
			shared["bearing_contacts"][0]["x"] = 60000;
			shared["bearing_contacts"][0]["definition"] = "private hull";
			const auto published = display::sanitise(shared);
			require(published.at("bearing_contacts")[0].size() == 3, "shared display strips hidden bearing fields");
			auto bad_wire = encoded;
			bad_wire["bearing_contacts"][0]["bearing_mdeg"] = 360000;
			bool malformed = false;
			try
			{
				network::read_observation(bad_wire, c);
			}
			catch (const std::exception &)
			{
				malformed = true;
			}
			require(malformed, "malformed bearing projection rejected");
			command(long_range, "target", 3);
			ticks(long_range, 30);
			require(samples(long_range, 3).empty(), "aiming at an out-of-range blip earns no findings");
			const auto selected = scanner_state(long_range).at("last_bearing");
			require(selected.at("id") == 3, "only selected scanner bearing is retained");
			const auto snapshot = long_range.save();
			for (const auto &row : snapshot.at("sensors"))
				if (row.at("system") == "radar")
					require(row.at("last_bearing").is_null() && !row.contains("bearings"),
					        "broad radar keeps no bearing history");
			auto legacy = snapshot;
			for (auto &row : legacy["sensors"])
			{
				const auto bearing = row.at("last_bearing");
				row.erase("last_bearing");
				row["bearings"] =
				    bearing.is_null()
				        ? Json::array()
				        : Json::array({bearing, Json{{"id", 2}, {"bearing_mdeg", 0}, {"observed_tick", 1}}});
			}
			World migrated;
			migrated.restore(c, legacy);
			require(migrated.save() == snapshot, "legacy bearing arrays retain only the selected target");
			auto invalid_legacy = legacy;
			for (auto &row : invalid_legacy["sensors"])
				if (row.at("ship") == 1 && row.at("system") == "scanner")
					row["bearings"][1]["bearing_mdeg"] = 360000;
			rejects(
			    [&]
			    {
				    World bad;
				    bad.restore(c, invalid_legacy);
			    },
			    "unselected legacy bearings must still be validated");
			auto no_history = snapshot;
			for (auto &row : no_history["sensors"])
				row.erase("last_bearing");
			World older;
			older.restore(c, no_history);
			require(scanner_state(older).at("last_bearing").is_null(),
			        "saves without bearing history remain supported");

			World resumed;
			resumed.restore(c, long_range.save());
			ticks(long_range, 5);
			ticks(resumed, 5);
			require(long_range.save() == resumed.save(), "bearing slew and history survive recovery");
			command(long_range, "target", 2);
			ticks(long_range, 1);
			require(scanner_state(long_range).at("last_bearing").at("id") == 2,
			        "new target captures its own permitted bearing");
			command(long_range, "target", 0);
			ticks(long_range, 1);
			require(scanner_state(long_range).at("last_bearing").is_null(), "clearing target clears retained bearing");
			command(long_range, "target", 3);
			ticks(long_range, 1);
			const auto loss_view = long_range.observe(101);
			for (const auto &controller : loss_view->gameplay.at("controllers"))
				if (controller.at("id") == "radar")
					require(long_range.submit(
					            101, OperatingCommand{1, {}, 0, {}, "radar", "enabled", controller.at("generation")}),
					        "disable broad radar for contact loss");
			ticks(long_range, 1);
			const auto lost = scanner_state(long_range).at("last_bearing");
			ticks(long_range, 5);
			require(long_range.observe(101)->bearing_contacts.empty() &&
			            scanner_state(long_range).at("last_bearing") == lost,
			        "contact loss retains bearing without refreshing its observation time");
			World lost_resumed;
			lost_resumed.restore(c, long_range.save());
			ticks(long_range, 3);
			ticks(lost_resumed, 3);
			require(long_range.save() == lost_resumed.save(), "lost bearing aiming resumes identically");

			auto fog = source;
			fog["maps"][0]["regions"] =
			    Json::array({{{"id", "cloud"},
			                  {"shape", "circle"},
			                  {"x_mm", 30000},
			                  {"y_mm", 0},
			                  {"radius_mm", 20000},
			                  {"nebula", {{"density", 100}, {"sensor_factor", 100}, {"colour", {.2, .3, .4}}}}}});
			World obscured;
			obscured.load(load(fog), "test.map");
			require(obscured.claim(101, 1, "drive"), "claim nebula test");
			const auto fog_contacts = obscured.observe(101)->bearing_contacts;
			require(std::none_of(fog_contacts.begin(), fog_contacts.end(),
			                     [](const auto &b)
			                     {
				                     return b.id == 2;
			                     }),
			        "nebula attenuates outer band");

			auto attenuated = fog;
			attenuated["entities"][0]["systems"]["radar"]["range_mm"] = 70000;
			attenuated["maps"][0]["regions"][0]["nebula"]["density"] = .01;
			attenuated["maps"][0]["regions"][0]["nebula"]["sensor_factor"] = 1;
			World faint;
			faint.load(load(attenuated), "test.map");
			require(faint.claim(101, 1, "drive"), "claim attenuated normal return");
			const auto faint_view = faint.observe(101);
			require(faint_view->bearing_contacts.size() == 1 && faint_view->bearing_contacts[0].id == 2,
			        "normal attenuation failure can still produce an outer-band bearing");
			auto narrow = source;
			narrow["entities"][0]["systems"]["radar"]["arc_mdeg"] = 90000;
			World directional;
			directional.load(load(narrow), "test.map");
			require(directional.claim(101, 1, "drive"), "claim directional outer radar");
			require(directional.observe(101)->bearing_contacts.size() == 1 &&
			            directional.observe(101)->bearing_contacts[0].id == 2,
			        "outer band respects physical arcs");

			auto powered = source;
			auto &ship = powered["entities"][0];
			ship["systems"]["radar"]["long_range_mm"] = 200000;
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 1000},
			                                              {"initial", 1000},
			                                              {"charge_rate", 200},
			                                              {"discharge_rate", 200}}})}};
			ship["systems"]["radar"]["powerless_start"] = true;
			ship["systems"]["radar"]["energy_per_second"] = 400;
			ship["systems"]["radar"]["power_store"] = "cell";
			World partial;
			partial.load(load(powered), "test.map");
			require(partial.claim(101, 1, "drive"), "claim partially powered outer radar");
			ticks(partial, 1);
			require(partial.observe(101)->bearing_contacts.size() == 2 && partial.observe(101)->contacts.empty(),
			        "partial power scales outer reach without details");
			const auto partial_view = partial.observe(101);
			for (const auto &c : partial_view->gameplay.at("controllers"))
				if (c.at("id") == "radar")
					require(partial.submit(101, OperatingCommand{1, {}, 0, {}, "radar", "enabled", c.at("generation")}),
					        "disable outer radar");
			ticks(partial, 1);
			require(partial.observe(101)->bearing_contacts.empty(), "disabled radar cannot track hidden targets");
			auto invalid = source;
			invalid["entities"][0]["systems"]["radar"]["long_range_mm"] = 30000;
			bool rejected = false;
			try
			{
				load(invalid);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "outer range must exceed normal range");
			auto outside = source;
			outside["maps"][0]["entities"][1]["x_mm"] = 90001;
			World beyond;
			beyond.load(load(outside), "test.map");
			require(beyond.claim(101, 1, "drive"), "claim outer boundary");
			require(beyond.observe(101)->bearing_contacts.size() == 1, "outside outer range is not detected");
		}
		{
			auto source = fixture();
			source["entities"][0]["systems"]["radar"]["range_mm"] = 30000;
			source["entities"][0]["systems"]["radar"]["long_range_mm"] = 120000;
			World focused;
			focused.load(load(source), "test.map");
			require(focused.claim(101, 1, "drive"), "claim bearing enquiry");
			ticks(focused, 1);
			require(focused.observe(101)->bearing_contacts.size() == 1, "focused scanner sees only actual beam");
			command(focused, "target", 3);
			ticks(focused, 40);
			const auto promoted = focused.observe(101);
			require(std::any_of(promoted->contacts.begin(), promoted->contacts.end(),
			                    [](const auto &b)
			                    {
				                    return b.id == 3;
			                    }) &&
			            std::none_of(promoted->bearing_contacts.begin(), promoted->bearing_contacts.end(),
			                         [](const auto &b)
			                         {
				                         return b.id == 3;
			                         }),
			        "scanner promotes same target without duplicate blip");
			require(!samples(focused, 3).at("compartment_condition").at("data").is_null(),
			        "focused scanner acquires from bearing designation");
			command(focused, "enabled", 0);
			ticks(focused, 2);
			const auto demoted = focused.observe(101);
			require(demoted->contacts.empty() && demoted->bearing_contacts.size() == 2,
			        "loss of resolving scanner restores bearing-only contacts");
			for (const auto &row : demoted->gameplay.at("sensors").at("contacts"))
				for (const auto &[cap, f] : row.at("findings").items())
					require(!f.at("live").get<bool>(), "outer return never refreshes retained details");
		}

		{
			auto source = fixture();
			source["entities"][0]["systems"]["radar"]["range_mm"] = 30000;
			source["entities"][0]["systems"]["radar"]["long_range_mm"] = 120000;
			auto &ship = source["entities"][0];
			ship["systems"]["drive-engine"] = {
			    {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 10000}, {"compartment", "fore"}};
			ship["systems"]["drive-steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}, {"compartment", "fore"}};
			ship["stations"][0]["consoles"][0]["feeds"].push_back("drive-engine");
			ship["stations"][0]["consoles"][0]["feeds"].push_back("drive-steering");
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "flight"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands",
			      Json::array({{{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"drive-engine"}}},
			                   {{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"drive-steering"}}}})}});
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("flight");
			source["maps"][0]["entities"].erase(0);
			source["maps"][0]["ship_slots"] = Json::array({{{"id", "test.slot"},
			                                                {"x_mm", 0},
			                                                {"y_mm", 0},
			                                                {"heading_mdeg", 0},
			                                                {"allowed_ships", {"test.combat"}}}});
			auto session = make_local_session();
			session->add_pack(archive(source));
			session->start({"test.map", {{"test.slot", "test.combat"}}, "test.slot", "drive"});
			session->tick();
			ConsoleModel model;
			model.refresh(session);
			const auto controls = model.gameplay_controls();
			require(std::any_of(controls.begin(), controls.end(),
			                    [](const auto &c)
			                    {
				                    return c.first == "game.sensor-contact/3" &&
				                           c.second.find("Range unknown") != std::string::npos;
			                    }),
			        "accessible bearing selection exposes no range");
			require(std::any_of(controls.begin(), controls.end(),
			                    [](const auto &c)
			                    {
				                    return c.first == "game.combat-target/3";
			                    }),
			        "anonymous bearing is a weapon target choice");
			bool weapon_button = false;
			for (const auto &button : model.observation()->gameplay.at("controls"))
				weapon_button |= button.value("combat_choice", "") == "weapon" && button.at("value") == 3;
			require(weapon_button, "bearing designation has admitted weapon assignment buttons");
			const std::string expected_bearing = "Bearing 90.0°; Range unknown";
			for (const auto &[id, label] : controls)
				if (id == "game.sensor-contact/3")
					require(label.ends_with(expected_bearing), "selection uses one-decimal bearing and degree unit");
			require(model.contacts_text().find(expected_bearing) != std::string::npos,
			        "radar summary uses the same bearing formatter");
			Json board;
			board["display"] = {
			    {"kind", "radar"},
			    {"console", "drive"},
			    {"contacts", Json::array()},
			    {"regions", Json::array()},
			    {"bearing_contacts", Json::array({{{"id", 3}, {"bearing_mdeg", 90000}, {"observed_tick", 1}}})}};
			const auto public_rows = public_board_readouts(board, 1);
			require(std::any_of(public_rows.begin(), public_rows.end(),
			                    [&](const auto &r)
			                    {
				                    return r.first == "viewscreen.readout/contact/3" &&
				                           r.second.ends_with(expected_bearing);
			                    }),
			        "shared display uses the same bearing formatter");
			require(model.act("game.sensor-contact/3"), "semantic bearing selection");
			const auto readouts = model.gameplay_readouts();
			require(std::any_of(readouts.begin(), readouts.end(),
			                    [&](const auto &r)
			                    {
				                    return r.first == "game.scan-bearing/3" && r.second.ends_with(expected_bearing);
			                    }),
			        "inspection uses the same bearing formatter");
			bool scanning = false;
			for (const auto &c : model.gameplay_controls())
				if (c.second.ends_with("Scan target"))
					scanning = model.act(c.first);
			require(scanning, "accessible scan action accepts bearing selection");
			for (int i = 0; i < 40; ++i)
				session->tick();
			model.refresh(session);
			const auto &view = *model.observation();
			require(std::any_of(view.contacts.begin(), view.contacts.end(),
			                    [](const auto &b)
			                    {
				                    return b.id == 3;
			                    }),
			        "semantic scan produces positional contact through ordinary session");
		}
		surface_contact_range();
		independent_shield_scan();
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << w.save().at("sensors").dump() << '\n';
		else
			std::cout << "PASS focused sensors\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
