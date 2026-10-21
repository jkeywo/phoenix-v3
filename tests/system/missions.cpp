#include "../support/assertions.hpp"
#include "../support/ftl_fixture.hpp"
#include "../support/away_fixture.hpp"
#include <iostream>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
Json evidence = Json::array();
void checkpoint(const World &w)
{
	auto saved = w.save();
	Json e;
	for (const auto *key : {"mission_ledger", "away_missions", "scenario"})
		if (saved.contains(key))
			e[key] = saved.at(key);
	for (const auto &b : saved.at("bodies"))
		if (b.contains("mission_report"))
			e["reports"].push_back(b.at("mission_report"));
	evidence.push_back(e);
}
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
Json leaf(std::string name, Json value)
{
	return {{"fact", name}, {"value", value}};
}
Json fixture()
{
	auto s = ftl_fixture();
	auto &m = s["maps"][0];
	m["scenario"] = {
	    {"tree", "test.mission"},
	    {"bindings", {{"actor", {{"entity", 1}}}}},
	    {"events", {"observed-departure"}},
	    {"event_bindings",
	     Json::array({{{"kind", "entity.departed"}, {"subject", {{"entity", 1}}}, {"event", "observed-departure"}}})},
	    {"fact_bindings",
	     Json::array(
	         {{{"id", "life"}, {"kind", "entity"}, {"subject", {{"entity", 2}}}, {"field", "lifecycle"}},
	          {{"id", "missing"}, {"kind", "entity"}, {"subject", {{"handle", "unspawned"}}}, {"field", "lifecycle"}},
	          {{"id", "departure-event"},
	           {"kind", "event"},
	           {"event", "observed-departure"},
	           {"subject", {{"entity", 1}}}}})},
	    {"objectives", Json::array()},
	    {"reports",
	     Json::array(
	         {{{"id", "report"},
	           {"ship", {{"entity", 1}}},
	           {"title_key", "Authoritative report"},
	           {"objectives", {"secret", "unknown", "failure"}},
	           {"facts", Json::array({{{"fact", "life"}, {"label_key", "Hidden target state"}},
	                                  {{"fact", "departure-event"}, {"label_key", "Departure committed"}}})},
	           {"rules", Json::array({{{"result", "success"},
	                                   {"title_key", "Success"},
	                                   {"explanation_key", "Target is active"},
	                                   {"when", leaf("life", "active")}}})},
	           {"fallback",
	            {{"result", "failure"}, {"title_key", "Failure"}, {"explanation_key", "Target unavailable"}}}}})}};
	auto &objectives = m["scenario"]["objectives"];
	for (const auto *id : {"secret", "unknown", "failure"})
		objectives.push_back(
		    {{"id", id},
		     {"type", "condition"},
		     {"title_key", std::string("Secret ") + id},
		     {"actor", {{"entity", 1}}},
		     {"recipients", {{"ship", {{"entity", 1}}}, {"consoles", {"observer"}}}},
		     {"complete_when",
		      std::string(id) == "unknown" ? Json{{"not", leaf("missing", "destroyed")}} : leaf("life", "active")}});
	objectives[2]["fail_when"] = leaf("life", "active");
	Json entry = Json::array();
	for (const auto *id : {"secret", "unknown", "failure"})
		entry.push_back({{"action", "add_objective"}, {"arguments", {id, {{"observation", "self.entity"}}, id}}});
	s["trees"].push_back({{"id", "test.mission"},
	                      {"context", "scenario"},
	                      {"initial", "start"},
	                      {"states", Json::array({{{"id", "start"}, {"entry", entry}}})}});
	return s;
}
Observation nav(const World &w)
{
	auto o = *w.observe(101);
	for (const auto &a : o.additional_consoles)
		if (a.console == "navigator")
			return a;
	throw std::runtime_error("Missing navigator");
}
OperatingCommand cmd(const World &w, std::string action, int value = 0)
{
	auto o = nav(w);
	OperatingCommand c{
	    1, action, value, action == "ftl.depart" ? "jump" : "", "jump", action == "ftl.depart" ? "" : "enabled"};
	c.console = "navigator";
	for (const auto &x : o.gameplay.at("controllers"))
		if (x.at("id") == "jump")
			c.generation = x.at("generation");
	return c;
}
Json status(const World &w, std::string name)
{
	const auto saved = w.save();
	for (const auto &o : saved.at("scenario").at("runtime").at("objectives"))
		if (o.at("definition") == name)
			return o.at("status");
	throw std::runtime_error("Objective not instantiated");
}
OperatingCommand order(const World &w, const std::string &action, const std::string &text = {},
                       std::uint64_t claim = 101)
{
	auto o = *w.observe(claim);
	OperatingCommand c{o.own.id, "away." + action, o.gameplay.at("away_missions")[0].at("revision"), "job"};
	c.text = text;
	c.controller = claim == 102 ? "team" : action == "transport" ? "transport" : "dispatch";
	for (const auto &x : o.gameplay.at("controllers"))
		if (x.at("id") == c.controller)
			c.generation = x.at("generation");
	return c;
}
Json lost_save(const World &w)
{
	auto saved = w.save();
	for (auto &b : saved["bodies"])
		if (b.at("body").at("id") == 1)
		{
			b["destructible"]["durability"] = 0;
			b["destructible"]["destroyed_tick"] = w.ticks();
			for (auto &c : b["combat"]["compartments"])
			{
				c["hp"] = 0;
				c["disabled"] = true;
			}
		}
	return saved;
}
void loss_cases()
{
	auto s = away_fixture();
	s["trees"].push_back({{"id", "test.loss"},
	                      {"context", "scenario"},
	                      {"initial", "idle"},
	                      {"states", Json::array({{{"id", "idle"}}})}});
	s["maps"][0]["scenario"] = {
	    {"tree", "test.loss"},
	    {"events", {"party-lost"}},
	    {"fact_bindings", Json::array({{{"id", "recovered"},
	                                    {"kind", "away"},
	                                    {"subject", {{"slot", "crew"}}},
	                                    {"operation", "job"},
	                                    {"field", "recovered"}}})},
	    {"event_bindings",
	     Json::array({{{"kind", "personnel.lost"}, {"subject", {{"entity", 1}}}, {"event", "party-lost"}}})}};
	const auto catalog = load(s);
	for (int phase = 0; phase < 3; ++phase)
	{
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		auto ship = *w.ship_for_slot("crew");
		require(w.claim(101, ship, "drive") && w.claim(102, ship, "observer"), "claim loss fixture");
		ticks(w, 4);
		require(w.submit(101, order(w, "open")), "open loss fixture");
		ticks(w, 1);
		require(w.submit(102, order(w, "assign", "repair/team:team", 102)), "assign loss fixture");
		ticks(w, 1);
		require(w.submit(101, order(w, "transport", "transport")), "bind transport");
		ticks(w, 12);
		require(w.submit(101, order(w, "send")), "send party");
		ticks(w, phase == 0 ? 1 : 4);
		if (phase == 2)
		{
			require(w.submit(101, order(w, "recall")), "queue return");
			ticks(w, 1);
		}
		w.restore(catalog, lost_save(w));
		ticks(w, 1);
		const auto saved = w.save();
		const auto &mission = saved.at("away_missions")[0];
		require(mission.at("phase") == (phase == 0 ? "open" : "lost"), "loss follows actual atomic transfer location");
		require(saved.at("mission_ledger").at("lost").empty() == (phase == 0), "only delivered personnel become lost");
		World r;
		r.restore(catalog, saved);
		ticks(w, 5);
		ticks(r, 5);
		require(w.save() == r.save(), "lost disposition and transition receipts continue identically");
		checkpoint(w);
		if (phase)
		{
			require(mission.at("assignments").empty(), "lost disposition survives cleared manifest");
			int events = 0;
			for (const auto &e : saved.at("scenario").at("runtime").at("events"))
				events += e.at("name") == "party-lost";
			require(events == 1, "loss emits once");
		}
	}
}
void physical_boundary()
{
	auto source = away_fixture();
	auto target = source["entities"][0];
	target["id"] = "test.disabled-target";
	for (auto &deck : target["combat"]["decks"])
		for (auto &c : deck["compartments"])
			c["initial_hp"] = 0;
	source["entities"].push_back(target);
	source["maps"][0]["entities"][0]["definition"] = "test.disabled-target";
	source["trees"].push_back(Json::parse(
	    R"json({"id":"test.boundary","context":"scenario","initial":"start","states":[{"id":"start","entry":[{"action":"add_objective","arguments":["boundary",{"observation":"self.entity"},"boundary"]}]}]})json"));
	source["maps"][0]["scenario"] = {
	    {"tree", "test.boundary"},
	    {"bindings", {{"actor", {{"slot", "crew"}}}}},
	    {"fact_bindings",
	     Json::array({{{"id", "life"}, {"kind", "entity"}, {"subject", {{"entity", 1}}}, {"field", "lifecycle"}}})},
	    {"objectives", Json::array({{{"id", "boundary"},
	                                 {"type", "condition"},
	                                 {"title_key", "Boundary"},
	                                 {"actor", {{"slot", "crew"}}},
	                                 {"recipients", {{"ship", {{"slot", "crew"}}}}},
	                                 {"complete_when", leaf("life", "active")},
	                                 {"fail_when", leaf("life", "destroyed")}}})}};
	World w;
	w.load(load(source), "test.map", {{"crew", "test.away"}});
	for (const auto &b : w.save().at("bodies"))
		if (b.at("body").at("id") == 1)
			require(b.at("destructible").at("destroyed_tick") == -1, "destruction is committed by physical simulation");
	w.tick();
	require(status(w, "boundary") == 3,
	        "new objectives evaluate after physical changes, without an empty departure boundary");
	checkpoint(w);
}
void departure_batch()
{
	auto source = fixture();
	auto &scenario = source["maps"][0]["scenario"];
	scenario["fact_bindings"].push_back(
	    {{"id", "own-life"}, {"kind", "entity"}, {"subject", {{"entity", 1}}}, {"field", "lifecycle"}});
	auto &report = scenario["reports"][0];
	report["rules"][0]["when"] = {{"all", Json::array({leaf("life", "departed"), leaf("own-life", "departed")})}};
	auto other = report;
	other["id"] = "other-report";
	other["ship"] = {{"entity", 2}};
	scenario["reports"].push_back(other);
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map");
	require(w.claim(101, 1, "drive") && w.claim(102, 2, "drive"), "batch claims");
	ticks(w, 2);
	auto a = cmd(w, "system_enabled", 1), b = a;
	b.target = 2;
	require(w.submit(101, a) && w.submit(102, b), "charge both ships");
	ticks(w, 1000);
	a = cmd(w, "ftl.depart");
	b = a;
	b.target = 2;
	require(w.submit(102, b) && w.submit(101, a), "reverse order batch requests");
	w.tick();
	for (auto claimant : {101, 102})
		require(w.observe(claimant)->gameplay.at("mission_report").at("result") == "success",
		        "all reports see simultaneous committed departures");
	require(status(w, "secret") == 2, "completed objective stays latched after its condition changes");
	World restored;
	restored.restore(catalog, w.save());
	ticks(w, 3);
	ticks(restored, 3);
	require(w.save() == restored.save(), "simultaneous report continuation");
	checkpoint(w);
}
void ai_departure()
{
	auto source = ftl_fixture();
	auto &e = source["entities"][0];
	for (auto &c : e["control_layout"]["controllers"])
		if (c.at("id") == "jump")
		{
			c.erase("fallback");
			c["tree"] = "test.jump";
		}
	source["trees"].push_back(Json::parse(
	    R"json({"id":"test.jump","context":"controller","ftl_system":"jump","command_bindings":[{"channel":"enabled","type":"boolean"}],"initial":"charging","states":[{"id":"charging","running":[{"action":"write.enabled","arguments":[true]}],"transitions":[{"target":"leave","conditions":[{"left":{"observation":"ftl.ready"},"op":"eq","right":true},{"left":{"observation":"local.visitors"},"op":"eq","right":0}]}]},{"id":"leave","running":[{"action":"write.enabled","arguments":[true]},{"action":"ftl.depart","arguments":["jump"]}]}]})json"));
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map");
	ticks(w, 150);
	checkpoint(w);
	World r;
	r.restore(catalog, w.save());
	ticks(w, 850);
	ticks(r, 850);
	require(w.save() == r.save(), "AI charging and departure continuation");
	checkpoint(w);
	for (const auto &body : w.save().at("bodies"))
		require(body.contains("mission_report"), "unclaimed AI uses ordinary FTL departure");
	auto visitors = away_fixture();
	fit_ftl(visitors, "helm", "drive");
	visitors["trees"].push_back(source["trees"].back());
	for (auto &c : visitors["entities"][0]["control_layout"]["controllers"])
		if (c.at("id") == "jump")
		{
			c.erase("fallback");
			c["tree"] = "test.jump";
		}
	const auto visitor_catalog = load(visitors);
	World visiting;
	visiting.load(visitor_catalog, "test.map", {{"crew", "test.away"}});
	auto own = *visiting.ship_for_slot("crew");
	require(visiting.claim(101, own, "drive") && visiting.claim(102, own, "observer"), "visitor crew claims");
	ticks(visiting, 4);
	require(visiting.submit(101, order(visiting, "open")), "open visiting operation");
	visiting.tick();
	require(visiting.submit(102, order(visiting, "assign", "repair/team:team", 102)), "assign visiting party");
	visiting.tick();
	require(visiting.submit(101, order(visiting, "transport", "transport")), "visiting transport");
	ticks(visiting, 12);
	require(visiting.submit(101, order(visiting, "send")), "deliver visiting party");
	ticks(visiting, 650);
	for (const auto &b : visiting.save().at("bodies"))
		if (b.at("body").at("id") == 1)
			require(!b.contains("mission_report"), "AI waits with charged drive while visitors remain");
	require(visiting.submit(101, order(visiting, "recall")), "recover visitors");
	ticks(visiting, 10);
	for (const auto &b : visiting.save().at("bodies"))
		if (b.at("body").at("id") == 1)
			require(b.contains("mission_report"), "AI departs after actual recovery");
	require(visiting.save().at("away_missions")[0].at("phase") == "recovered",
	        "departed target never causes destruction loss");
	checkpoint(visiting);
	auto blocked = source;
	blocked["entities"][0]["resources"]["stores"][0]["initial"] = 0;
	// An empty supply never grants charge; policies cannot grant readiness.

	const auto denied = load(blocked);
	World no;
	no.load(denied, "test.map");
	ticks(no, 1000);
	for (const auto &body : no.save().at("bodies"))
		require(!body.contains("mission_report"), "AI cannot bypass power refusal");
}
int main(int argc, char **argv)
{
	try
	{
		auto source = fixture();
		source["maps"][0]["scenario"]["fact_bindings"].push_back({{"id", "filtered-event"},
		                                                          {"kind", "event"},
		                                                          {"event", "observed-departure"},
		                                                          {"subject", {{"entity", 2}}}});
		source["maps"][0]["scenario"]["reports"][0]["facts"].push_back(
		    {{"fact", "filtered-event"}, {"label_key", "Other subject"}});
		source["maps"][0]["scenario"]["objectives"][0]["complete_when"] = {
		    {"all",
		     Json::array({leaf("life", "active"), Json{{"any", Json::array({Json{{"not", leaf("missing", "destroyed")}},
		                                                                    leaf("life", "active")})}}})}};
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map");
		require(w.claim(101, 1, "drive") && w.claim(102, 2, "drive"), "claims");
		ticks(w, 2);
		require(status(w, "secret") == 2 && status(w, "failure") == 3 && status(w, "unknown") == 1,
		        "conditions, failure precedence and Missing negation");
		require(w.observe(101)->gameplay.dump().find("Secret secret") == std::string::npos,
		        "private objective absent during play");
		require(w.submit(101, cmd(w, "system_enabled", 1)), "enable FTL");
		ticks(w, 100);
		World r;
		r.restore(catalog, w.save());
		ticks(w, 450);
		ticks(r, 450);
		require(w.save() == r.save(), "fact and partial FTL continuation");
		for (int i = 0; i < 2000 && !nav(w).gameplay.at("ftl")[0].at("ready").get<bool>(); ++i)
		{
			w.tick();
			r.tick();
		}
		require(w.submit(101, cmd(w, "ftl.depart")), "depart");
		ticks(w, 1);
		auto report = w.observe(101)->gameplay.at("mission_report");
		require(report.at("result") == "success" && report.at("objectives").size() == 3 &&
		            report.at("facts")[1].at("value") == true && report.at("facts")[2].at("value") == false,
		        "authoritative report contains undiscovered truth and committed departure");
		require(w.observe(102)->gameplay.at("mission_report").is_null(), "report isolated per crew");
		require(network::read_observation(network::observation_json(*w.observe(101)), catalog)
		                .gameplay.at("mission_report") == report,
		        "bounded report projection");
		r.restore(catalog, w.save());
		ticks(w, 3);
		ticks(r, 3);
		require(w.save() == r.save() && w.observe(101)->gameplay.at("mission_report") == report,
		        "frozen report continues without duplicate events");
		auto legacy = w.save();
		legacy["scenario"]["runtime"]["version"] = 1;
		World old;
		old.restore(catalog, legacy);
		auto prior = legacy;
		for (auto &schema : prior["knowledge"]["contract"]["schemas"])
			if (schema.at("name") == "phoenix.objective")
				for (auto &field : schema["fields"])
					if (field.at("name") == "region" || field.at("name") == "radius" ||
					    field.at("name") == "destination_x" || field.at("name") == "destination_y")
						field["required"] = true;
		old.restore(catalog, prior);

		auto malformed = source;
		malformed["maps"][0]["scenario"]["objectives"][0]["complete_when"]["all"][0]["value"] = 1;
		rejects(
		    [&]
		    {
			    load(malformed);
		    },
		    "typed conditions reject mismatched literal");
		physical_boundary();
		loss_cases();
		departure_batch();
		ai_departure();
		if (argc > 1 && std::string(argv[1]) == "--trace")
			std::cout << w.save().at("mission_ledger").dump() << "\n"
			          << report.dump() << "\n"
			          << evidence.dump() << "\n";
		else
			std::cout << "PASS generic mission facts, objectives, losses and authoritative reports\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
