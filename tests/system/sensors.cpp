#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
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
		auto legacy = w.save();
		legacy.erase("sensors");
		legacy.erase("sensor_discoveries");
		World old;
		old.restore(catalog, legacy);
		require(samples(old).empty(), "older saves start without scans");
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
			conditioned.load(load(source), "test.map");
			require(conditioned.claim(101, 1, "drive"), "claim conditioned scan");
			command(conditioned, "target", 2);
			ticks(conditioned, 3);
			require(!samples(conditioned).contains("finding:engineering"),
			        "unavailable authored finding does not leak or accumulate");
			ticks(conditioned, 20);
			require(!samples(conditioned).at("finding:engineering").at("data").is_null(), "available finding acquires");
			require(conditioned.save().at("sensor_discoveries").size() == 1,
			        "discovery emitted once per ship and finding");
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
			require(saved.at("bodies")[0].at("combat").at("systems").at("gun").at("target") == 0,
			        "scan targeting leaves weapon target untouched");
		}
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
