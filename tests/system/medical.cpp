#include "../support/assertions.hpp"
#include "../support/away_fixture.hpp"
#include "../support/combat_fixture.hpp"
#include <algorithm>
#include <iostream>
import phoenix.simulation;
import phoenix.console_controls;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
Json order_tree(const std::string &id, const std::string &action)
{
	return {{"id", id},
	        {"context", "team"},
	        {"initial", "work"},
	        {"states",
	         Json::array(
	             {{{"id", "work"},
	               {"running", Json::array({{{"action", action}}})},
	               {"transitions", Json::array({{{"target", "done"},
	                                             {"conditions", Json::array({{{"left", {{"observation", "work_done"}}},
	                                                                          {"op", "eq"},
	                                                                          {"right", true}}})}}})}},
	              {{"id", "done"}, {"entry", Json::array({{{"action", "complete"}}})}}})}};
}
Json medical_fixture()
{
	auto s = away_fixture();
	auto target = s["entities"][0];
	target["id"] = "test.patient";
	target["personnel"]["officers"][0]["condition"] = "minor";
	auto &ship = s["entities"][0];
	s["trees"].push_back(order_tree("test.treat", "treat"));
	s["trees"].push_back(order_tree("test.recover", "recover"));
	s["trees"].push_back(order_tree("test.custom", "move"));
	ship["systems"]["medical"] = {{"kind", "team"},
	                              {"type", "clinical"},
	                              {"capabilities", {"medical"}},
	                              {"compartment", "workshop"},
	                              {"treatment_ticks", 8},
	                              {"stabilise_ticks", 4},
	                              {"orders", Json::array({{{"id", "treat"},
	                                                       {"label", "Treat patients"},
	                                                       {"target", "patients"},
	                                                       {"capability", "medical"},
	                                                       {"tree", "test.treat"}},
	                                                      {{"id", "recover"},
	                                                       {"label", "Recover patients"},
	                                                       {"target", "patients"},
	                                                       {"capability", "medical"},
	                                                       {"tree", "test.recover"}},
	                                                      {{"id", "survey"},
	                                                       {"label", "Survey compartment"},
	                                                       {"target", "compartments"},
	                                                       {"tree", "test.custom"}}})}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "medical"},
	     {"console", "observer"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"medical"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back("medical");
	ship["stations"][1]["consoles"][0]["feeds"].push_back("medical");
	auto &p = ship["personnel"];
	p["duties"].push_back({{"id", "medical-controller"},
	                       {"label", "Medical coordination"},
	                       {"controller", "medical"},
	                       {"console", "observer"},
	                       {"department", "engineering"},
	                       {"compartment", "workshop"},
	                       {"effects", Json::object()}});
	p["duties"].push_back({{"id", "medical-team"},
	                       {"label", "Clinical team"},
	                       {"team", "medical"},
	                       {"department", "engineering"},
	                       {"compartment", "workshop"},
	                       {"effects", {{"medical", {"medical"}}}}});
	p["traits"].push_back({{"id", "medical"}, {"label", "Medical specialist"}, {"bonuses", {{"medical", 1000}}}});
	p["facilities"] =
	    Json::array({{{"id", "bay"}, {"compartment", "workshop"}, {"capacity", 1}, {"recovery_ticks", 6}}});
	p["officers"][0]["condition"] = "minor";
	p["officers"][1]["condition"] = "severe";
	s["entities"].push_back(target);
	s["maps"][0]["entities"][0]["definition"] = "test.patient";
	s["maps"][0]["away_missions"][0]["slots"][0]["capability"] = "repair";
	s["maps"][0]["away_missions"][0]["slots"].push_back(
	    {{"id", "medical"}, {"kind", "team"}, {"capability", "medical"}, {"required", false}});
	return s;
}
OperatingCommand away_order(const World &w, const std::string &action, const std::string &text = {},
                            std::uint64_t claim = 101, const std::string &controller = "dispatch")
{
	auto o = w.observe(claim);
	const auto &m = o->gameplay.at("away_missions")[0];
	OperatingCommand c{o->own.id, "away." + action, m.at("revision"), "job"};
	c.console = o->console;
	c.controller = controller;
	c.text = text;
	for (const auto &ctrl : o->gameplay.at("controllers"))
		if (ctrl["id"] == controller)
			c.generation = ctrl.at("generation");
	return c;
}
Json &saved_body(Json &save, EntityId id)
{
	for (auto &b : save["bodies"])
		if (b["body"]["id"] == id)
			return b;
	throw std::runtime_error("missing body");
}
Json officer(const World &w, EntityId id, int index)
{
	auto s = w.save();
	return saved_body(s, id)["personnel"]["officers"][index];
}
OperatingCommand command(const World &w, const std::string &action, const std::string &text)
{
	auto view = w.observe(102);
	for (const auto &t : view->gameplay.at("teams"))
		if (t["id"] == "medical")
			for (const auto &c : view->gameplay.at("controllers"))
				if (c["id"] == "medical")
				{
					OperatingCommand o{view->own.id, "team." + action, t.at("revision"), "medical"};
					o.text = text;
					o.controller = "medical";
					o.generation = c.at("generation");
					return o;
				}
	throw std::runtime_error("missing medical team");
}
void claim(World &w)
{
	require((w.observe(101).has_value() || w.claim(101, 2, "drive")) &&
	            (w.observe(102).has_value() || w.claim(102, 2, "observer")),
	        "claim clinical fixture");
}
Json casualty_fixture(const std::string &condition, bool lethal = false)
{
	auto s = combat_fixture();
	auto &ship = s["entities"][0];
	ship["personnel"] = {{"departments", Json::array({{{"id", "crew"}, {"label", "Crew"}, {"station", "operations"}}})},
	                     {"traits", Json::array()},
	                     {"duties", Json::array()},
	                     {"officers", Json::array()}};
	auto &p = ship["personnel"];
	for (const auto &c : ship["control_layout"]["controllers"])
		p["duties"].push_back({{"id", c["id"]},
		                       {"label", c["id"]},
		                       {"controller", c["id"]},
		                       {"console", c["console"]},
		                       {"department", "crew"},
		                       {"compartment", "fore"},
		                       {"effects", Json::object()}});
	for (const auto *team : {"team-a", "team-b"})
		p["duties"].push_back({{"id", std::string("duty-") + team},
		                       {"label", team},
		                       {"team", team},
		                       {"department", "crew"},
		                       {"compartment", "fore"},
		                       {"effects", Json::object()}});
	p["officers"].push_back({{"id", "watch"},
	                         {"name", "Watch officer"},
	                         {"department", "crew"},
	                         {"compartment", "fore"},
	                         {"duty", "radar"},
	                         {"traits", Json::array()}});
	p["casualties"] = {{"significant_damage", 1},
	                   {"lethal_damage", 1},
	                   {"lethal", lethal},
	                   {"heavy_below_permille", 900},
	                   {"stun_permille", condition == "stunned" ? 1000 : 0},
	                   {"minor_permille", condition == "minor" ? 1000 : 0},
	                   {"severe_permille", condition == "severe" ? 1000 : 0},
	                   {"killed_permille", condition == "dead" ? 1000 : 0}};
	return s;
}
void shoot(World &w)
{
	auto view = w.observe(201);
	for (const auto &c : view->gameplay.at("controllers"))
		if (c["id"] == "gun")
		{
			OperatingCommand o{1, "", 2, "", "gun", "target", c.at("generation")};
			require(w.submit(201, o), "fire at casualty fixture");
			return;
		}
	throw std::runtime_error("missing gun");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array();
		auto source = medical_fixture();
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		claim(w);
		const auto projected = w.observe(102)->gameplay.at("teams");
		require(team_projection::board(projected) == projected,
		        "bounded team projection preserves orders and patients");
		require(w.observe(101)->gameplay.at("teams").empty(), "foreign Controller receives no clinical team authority");
		auto survey = command(w, "order", "survey/bridge");
		require(eligible_console_command(*w.observe(102), Json::object(), Json::object(), survey).has_value(),
		        "remote client admits a permitted generic team order");
		require(!eligible_console_command(*w.observe(101), Json::object(), Json::object(), survey),
		        "remote client refuses foreign team authority");
		require(w.submit(102, survey), "custom authored movement order admitted");
		require(!w.submit(101, survey), "foreign Station cannot order medical team");
		ticks(w, 2);
		require(!w.submit(102, survey), "stale order revision refused");
		auto snapshot = w.save();
		World resumed;
		resumed.restore(catalog, snapshot);
		claim(resumed);
		ticks(w, 4);
		ticks(resumed, 4);
		require(w.save() == resumed.save(), "custom order traversal and tree resume identically");
		require(w.submit(102, command(w, "order", "treat/bridge")), "compartment triage order admitted");
		ticks(w, 4);
		auto mid = w.save();
		World midway;
		midway.restore(catalog, mid);
		claim(midway);
		ticks(w, 10);
		ticks(midway, 10);
		require(w.save() == midway.save(), "partial treatment continuation matches");
		require(officer(w, 2, 0)["condition"] == "healthy", "minor injury heals with a leaderless medical team");
		require(w.submit(102, command(w, "order", "treat/workshop")), "severe casualty triage admitted");
		ticks(w, 10);
		require(officer(w, 2, 1)["condition"] == "severe" && officer(w, 2, 1)["stabilised"] == true,
		        "field care stabilises severe injury without silently healing");
		auto moved = w.save();
		saved_body(moved, 2)["personnel"]["officers"][1]["location"] = 0;
		w.restore(catalog, moved);
		claim(w);
		require(w.submit(102, command(w, "order", "recover/bridge")), "facility recovery admitted");
		ticks(w, 7);
		snapshot = w.save();
		World facility;
		facility.restore(catalog, snapshot);
		claim(facility);
		ticks(w, 12);
		ticks(facility, 12);
		require(w.save() == facility.save(), "facility occupancy and recovery resume identically");
		require(officer(w, 2, 1)["condition"] == "healthy", "facility restores severe casualty");
		auto legacy = w.save();
		for (auto &body : legacy["bodies"])
		{
			for (auto &o : body["personnel"]["officers"])
				for (const auto *key : {"condition", "facility", "escort", "stabilised", "stun_until", "treatment",
				                        "recovery", "casualty_tick"})
					o.erase(key);
			for (const auto *key : {"revision", "order", "activity", "patient", "work_done"})
				body["combat"]["systems"]["team"].erase(key);
		}
		World legacy_restore;
		legacy_restore.restore(catalog, legacy);
		require(officer(legacy_restore, 2, 0)["condition"] == "healthy", "legacy officer state defaults to healthy");
		auto normalised = legacy_restore.save();
		require(saved_body(normalised, 2)["combat"]["systems"]["team"]["location"] ==
		            saved_body(legacy, 2)["combat"]["systems"]["team"]["location"],
		        "legacy repair traversal migrates without resetting physical location");
		auto malformed = w.save();
		saved_body(malformed, 2)["personnel"]["officers"][0]["condition"] = "unknown";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "unknown saved injury rejected");
		malformed = w.save();
		saved_body(malformed, 2)["combat"]["systems"]["medical"]["order"] = "absent";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "unknown saved order rejected");
		malformed = w.save();
		for (auto &o : saved_body(malformed, 2)["personnel"]["officers"])
		{
			o["condition"] = "severe";
			o["stabilised"] = true;
			o["present"] = false;
			o["location"] = 1;
			o["edge_to"] = -1;
			o["remaining"] = 0;
			o["facility"] = "bay";
			o["recovery"] = 0;
		}
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "over-capacity facility state rejected");
		auto invalid = source;
		invalid["entities"][0]["systems"]["medical"]["capabilities"] = {"security"};
		rejects(
		    [&]
		    {
			    load(invalid);
		    },
		    "medical order cannot grant unsupported capability");
		// Qualified clinical expertise speeds real treatment while leaderless teams remain useful.
		auto expert_source = source;
		expert_source["entities"][0]["personnel"]["officers"].push_back({{"id", "medic"},
		                                                                 {"name", "Medic"},
		                                                                 {"department", "engineering"},
		                                                                 {"duty", "medical-team"},
		                                                                 {"compartment", "workshop"},
		                                                                 {"traits", {"medical"}}});
		const auto expert_catalog = load(expert_source);
		World expert;
		expert.load(expert_catalog, "test.map", {{"crew", "test.away"}});
		claim(expert);
		ticks(expert, 12);
		require(officer(expert, 2, 0)["condition"] == "healthy", "attached medical officer improves treatment rate");
		// Optional clinical deployment cannot substitute for the required repair capability.
		auto away_source = source;
		for (auto &o : away_source["entities"][0]["personnel"]["officers"])
			o.erase("condition");
		away_source["entities"][1]["personnel"]["officers"][0]["condition"] = "minor";
		away_source["trees"].push_back(order_tree("test.unused", "move"));
		const auto away_catalog = load(away_source);
		World away;
		away.load(away_catalog, "test.map", {{"crew", "test.away"}});
		claim(away);
		ticks(away, 4);
		require(eligible_console_command(*away.observe(101), Json::object(), Json::object(), away_order(away, "open"))
		            .has_value(),
		        "remote client admits Dispatch mission control");
		require(away.submit(101, away_order(away, "open")), "open medical mission");
		ticks(away, 1);
		require(!away.submit(102, away_order(away, "assign", "repair/team:medical", 102, "medical")),
		        "medical team cannot fill required repair slot");
		require(away.submit(102, away_order(away, "assign", "repair/team:team", 102, "team")),
		        "assign required engineering team");
		ticks(away, 1);
		require(away.submit(102, away_order(away, "assign", "medical/team:medical", 102, "medical")),
		        "assign optional clinical team");
		ticks(away, 1);
		require(away.submit(101, away_order(away, "transport", "transport", 101, "transport")), "bind transport");
		ticks(away, 7);
		require(!away.submit(102, command(away, "order", "treat/bridge")),
		        "away reservation refuses onboard clinical order");
		require(away.submit(101, away_order(away, "send")), "send real medical party");
		ticks(away, 5);
		auto away_saved = away.save();
		World away_resume;
		away_resume.restore(away_catalog, away_saved);
		claim(away_resume);
		ticks(away, 12);
		ticks(away_resume, 12);
		require(away.save() == away_resume.save(), "away clinical traversal and work resume identically");
		require(officer(away, 1, 0)["condition"] == "healthy", "deployed medical team heals target ship officer");
		require(away.submit(101, away_order(away, "recall")), "recall clinical party");
		ticks(away, 5);
		require(away.observe(101)->gameplay.at("away_missions")[0]["phase"] == "recovered",
		        "medical party needs and completes physical recall");
		trace.push_back(away.save());
		// Actual projectile damage exercises casualty hooks, rather than calling an injury helper.
		for (const auto *condition : {"stunned", "minor", "severe", "dead"})
		{
			auto combat_source = casualty_fixture(condition, true);
			const auto combat_catalog = load(combat_source);
			World battle;
			battle.load(combat_catalog, "test.map");
			require(battle.claim(201, 1, "drive"), "claim casualty gun");
			if (std::string(condition) == "dead")
			{
				auto damaged = battle.save();
				saved_body(damaged, 2)["combat"]["compartments"][0]["hp"] = 80;
				battle.restore(combat_catalog, damaged);
				require(battle.observe(201).has_value() || battle.claim(201, 1, "drive"),
				        "reclaim prepared lethal fixture");
			}
			shoot(battle);
			ticks(battle, 1);
			for (int i = 0; i < 70 && officer(battle, 2, 0)["condition"] == "healthy"; ++i)
				battle.tick();
			require(officer(battle, 2, 0)["condition"] == condition,
			        "significant projectile event applies authored casualty outcome");
			if (std::string(condition) == "stunned")
			{
				const auto stopped_view = battle.observe(201);
				for (const auto &c : stopped_view->gameplay.at("controllers"))
					if (c["id"] == "gun")
					{
						OperatingCommand stop{1, "", 0, "", "gun", "enabled", c.at("generation")};
						require(battle.submit(201, stop), "stop gun after casualty");
					}
				ticks(battle, 10);
				const auto end = officer(battle, 2, 0)["stun_until"].get<std::int64_t>();
				auto saved = battle.save();
				World restart;
				restart.restore(combat_catalog, saved);
				while (officer(battle, 2, 0)["condition"] == "stunned" &&
				       static_cast<std::int64_t>(battle.ticks()) <= end + 1000)
				{
					battle.tick();
					restart.tick();
				}
				require(officer(battle, 2, 0)["condition"] == "healthy" && battle.save() == restart.save(),
				        "20-second stun clears on deterministic resumed simulation time");
				require(static_cast<std::int64_t>(battle.ticks()) ==
				            officer(battle, 2, 0)["casualty_tick"].get<std::int64_t>() + 20 * World::ticks_per_second +
				                1,
				        "stun lasts 20 seconds from the final significant injury event");
			}
			if (std::string(condition) == "dead")
				require(officer(battle, 2, 0)["duty"] == "", "death permanently removes duty assignment");
		}
		for (bool lethal : {false, true})
		{
			auto protected_source = casualty_fixture("dead", lethal);
			const auto protected_catalog = load(protected_source);
			World protected_world;
			protected_world.load(protected_catalog, "test.map");
			require(protected_world.claim(201, 1, "drive"), "claim protected casualty fixture");
			shoot(protected_world);
			// Inspect the first damaging event: a healthy compartment cannot immediately kill its officer.
			for (int n = 0; n < 30; ++n)
			{
				protected_world.tick();
				auto save = protected_world.save();
				if (saved_body(save, 2)["combat"]["compartments"][0]["hp"] < 100)
				{
					require(officer(protected_world, 2, 0)["condition"] != "dead",
					        "death requires damage that existed before the event");
					break;
				}
			}
			if (!lethal)
			{
				ticks(protected_world, 60);
				require(officer(protected_world, 2, 0)["condition"] != "dead",
				        "nonlethal events never kill despite authored death probability");
			}
		}
		trace.push_back(w.save());
		// Synthetic ZIP timestamps are not simulation state. Restores above still
		// validate the actual content identity within each run.
		for (auto &state : trace)
			state.erase("packs");
		if (argc > 1 && std::string(argv[1]) == "--trace-state")
			std::cout << trace.dump() << "\n";
		else if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << sha256(json_bytes(trace)) << "\n";
		else
			std::cout
			    << "PASS generic orders, clinical triage, facility recovery, projectile casualties and continuation\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
