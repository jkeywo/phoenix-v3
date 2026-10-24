#include "../support/assertions.hpp"
#include "../support/interdiction_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &w, int count)
{
	while (count--)
		w.tick();
}
const Json state(const World &w)
{
	return w.save().at("interdictions")[0];
}
Json &body(Json &save, EntityId id)
{
	for (auto &b : save["bodies"])
		if (b["body"]["id"] == id)
			return b;
	throw std::runtime_error("No fixture body");
}
OperatingCommand reply(const World &w, const std::string &answer)
{
	const auto s = state(w);
	const auto save = w.save();
	for (const auto &thread : save["conversations"]["threads"])
		if (thread["id"] == s["conversation"])
		{
			OperatingCommand c{2, "comms.answer", s["conversation"], ""};
			c.console = "drive";
			c.controller = "radio";
			c.request_id = thread["prompt"]["id"];
			c.text = answer;
			const auto view = w.observe(101);
			for (const auto &ctrl : view->gameplay.at("controllers"))
				if (ctrl["id"] == "radio")
					c.generation = ctrl["generation"];
			return c;
		}
	throw std::runtime_error("No demand");
}
OperatingCommand setting(const World &w, const std::string &controller, const std::string &channel, std::int64_t value,
                         std::uint64_t route = 101)
{
	const auto view = w.observe(route);
	OperatingCommand c{2, "", value, ""};
	c.controller = controller;
	c.channel = channel;
	c.console = view->console;
	for (const auto &control : view->gameplay["controllers"])
		if (control["id"] == controller)
			c.generation = control["generation"];
	return c;
}
OperatingCommand jump(const World &w, bool depart, std::int64_t enabled = 1)
{
	const auto main = w.observe(101);
	const Observation *view = &*main;
	for (const auto &extra : main->additional_consoles)
		if (extra.console == "navigator")
			view = &extra;
	OperatingCommand c{2, depart ? "ftl.depart" : "system_enabled", depart ? 0 : enabled, depart ? "jump" : ""};
	c.console = "navigator";
	c.controller = "jump";
	c.channel = depart ? "" : "enabled";
	for (const auto &control : view->gameplay.at("controllers"))
		if (control["id"] == "jump")
			c.generation = control["generation"];
	return c;
}
void continuation(const Catalog &catalog, World &w, int count, const char *why)
{
	World resumed;
	resumed.restore(catalog, w.save());
	for (int n = 0; n < count; ++n)
	{
		w.tick();
		resumed.tick();
	}
	require(w.save() == resumed.save(), why);
}
OperatingCommand away_order(const World &w, const std::string &action, const std::string &text = "",
                            std::uint64_t route = 101, const std::string &controller = "dispatch",
                            const std::string &mission = "job")
{
	const auto view = w.observe(route);
	for (const auto &row : view->gameplay.at("away_missions"))
		if (row["id"] == mission)
		{
			OperatingCommand c{2, "away." + action, row.at("revision"), mission};
			c.controller = controller;
			c.text = text;
			for (const auto &control : view->gameplay.at("controllers"))
				if (control["id"] == controller)
					c.generation = control["generation"];
			return c;
		}
	throw std::runtime_error("No ordinary away operation");
}
void send_party(World &w, bool security)
{
	require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "Claim departmental deployment authority");
	ticks(w, 5);
	require(w.submit(101, away_order(w, "open")), "Open rescue operation");
	ticks(w, 1);
	require(w.submit(102, away_order(w, "assign", "repair/team:team", 102, "team")),
	        "Engineering supplies repair team");
	ticks(w, 1);
	if (security)
	{
		require(w.submit(102, away_order(w, "assign", "security/team:security", 102, "security")),
		        "Authorise Security");
		ticks(w, 1);
	}
	require(w.submit(101, away_order(w, "transport", "transport", 101, "transport")), "Select displacement");
	ticks(w, 30);
	require(w.submit(101, away_order(w, "send")), "Dispatch physical insertion");
	w.tick();
	for (int n = 0; n < 180 && w.save()["away_missions"][0]["phase"] != "deployed"; ++n)
		w.tick();
	require(w.save()["away_missions"][0]["phase"] == "deployed", "Party physically arrives");
}
void selective_prize(Json &trace)
{
	auto fixture = boarding_fixture();
	fit_ftl(fixture, "helm", "drive");
	fixture["trees"].push_back(Json::parse(
	    R"({"id":"test.claim","context":"away","team_capabilities":["security"],"initial":"wait","states":[{"id":"wait","transitions":[{"target":"claim","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},{"id":"claim","entry":[{"action":"claim","arguments":["security"]},{"action":"complete","arguments":["claimed"]}]}]})"));
	fixture["maps"][0]["away_missions"][0]["tree"] = "test.claim";
	auto officer = fixture["entities"][0]["personnel"]["officers"][1];
	officer["id"] = "security-chief";
	officer["duty"] = "team-security";
	fixture["entities"][0]["personnel"]["officers"].push_back(officer);
	const auto catalog = load(fixture);
	World w;
	w.load(catalog, "test.map", {{"crew", "test.away"}});
	send_party(w, true);
	ticks(w, 2);
	auto s = w.save();
	require(s["personnel_postings"].dump().find("prize_crew") != std::string::npos,
	        "Security receives persistent prize posting");
	require(s["away_missions"][0]["assignments"].size() == 1 &&
	            s["away_missions"][0]["accompanying"] == Json::array({"chief"}),
	        "Repair and its actual officer remain recoverable");
	continuation(catalog, w, 5, "Selective prize manifest and ownership survive resume");
	require(w.submit(101, away_order(w, "recall")), "Recall the non-prize party");
	w.tick();
	for (int n = 0; n < 180 && w.save()["away_missions"][0]["phase"] != "recovered"; ++n)
		w.tick();
	require(w.save()["away_missions"][0]["phase"] == "recovered", "Physical recovery excludes intentional prize crew");
	continuation(catalog, w, 3, "Recovered repair and retained prize assignments remain distinct");
	require(w.submit(101, jump(w, false)), "Navigator enables parent FTL");
	w.tick();
	require(!w.submit(101, jump(w, true)), "Group departure cannot bypass separate charging");
	for (int n = 0; n < 700; ++n)
		w.tick();
	require(w.submit(101, jump(w, true)) && w.submit(101, jump(w, false, 0)),
	        "Queue departure and disable the leader drive in the same tick");
	w.tick();
	s = w.save();
	require(!body(s, 1).contains("mission_report") && !body(s, 2).contains("mission_report"),
	        "Authoritative group revalidation prevents partial departure");
	require(w.submit(101, jump(w, false)), "Restore the leader drive");
	ticks(w, 30);
	require(w.submit(101, jump(w, true)), "Ready player and prize admit joint departure");
	w.tick();
	s = w.save();
	require(body(s, 1).contains("mission_report") && body(s, 2).contains("mission_report") &&
	            body(s, 1)["mission_report"]["tick"] == body(s, 2)["mission_report"]["tick"],
	        "Group members cross the authoritative departure boundary together");
	require(body(s, 2)["mission_report"]["left_behind"].empty(),
	        "Intentional prize crew are not accidentally left behind");
	continuation(catalog, w, 3, "Joint departure and prize assignments remain frozen on resume");
	trace.push_back(w.save());
}
void captive_party(Json &trace)
{
	auto fixture = interdiction_fixture();
	fixture["trees"].push_back(Json::parse(
	    R"({"id":"test.retake","context":"away","boarding":true,"initial":"wait","states":[{"id":"wait","transitions":[{"target":"fight","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},{"id":"fight","entry":[{"action":"encounter","arguments":["occupation"]}],"transitions":[{"target":"claim","conditions":[{"left":{"observation":"encounter_done"},"op":"eq","right":true},{"left":{"observation":"encounter_success"},"op":"eq","right":true}]}]},{"id":"claim","entry":[{"action":"secure","arguments":["bridge"]},{"action":"capture","arguments":["bridge"]},{"action":"complete","arguments":["retaken"]}]}]})"));
	fixture["trees"].push_back({{"id", "test.retake-dialogue"},
	                            {"context", "conversation"},
	                            {"initial", "wait"},
	                            {"states", Json::array({{{"id", "wait"}}})}});
	fixture["entities"][1]["boarding_missions"] = Json::array(
	    {{{"id", "retake"},
	      {"title_key", "Retake target"},
	      {"entry", "bridge"},
	      {"tree", "test.retake"},
	      {"dialogue", "test.retake-dialogue"},
	      {"slots",
	       Json::array({{{"id", "security"}, {"kind", "team"}, {"capability", "security"}, {"required", true}},
	                    {{"id", "medical"}, {"kind", "team"}, {"capability", "medical"}, {"required", false}}})},
	      {"encounters", Json::array({{{"id", "occupation"},
	                                   {"compartment", "bridge"},
	                                   {"defenders", {"occupation"}},
	                                   {"success_permille", 1000},
	                                   {"defender_penalty_permille", 0},
	                                   {"injury_permille", 0},
	                                   {"loss_permille", 0},
	                                   {"assessment_ticks", 2}}})},
	      {"objectives",
	       {{"bridge",
	         {{"kind", "bridge"},
	          {"compartment", "bridge"},
	          {"prize_crew", true},
	          {"prize_capability", "security"}}}}}}});
	fixture["maps"][0]["ship_slots"][0]["x_mm"] = 0;
	fixture["maps"][0]["entities"][0]["x_mm"] = 10000;
	for (auto &tree : fixture["trees"])
		if (tree["id"] == "test.interdiction")
			tree["states"][0]["transitions"][0]["conditions"] =
			    Json::array({{{"left", {{"elapsed", true}}}, {"op", "ge"}, {"right", 120}}});
	const auto catalog = load(fixture);
	World w;
	w.load(catalog, "test.map", {{"crew", "test.away"}});
	send_party(w, false);
	auto initial = w.save();
	body(initial, 2)["body"]["position"] = {{"x", -200000}, {"y", 0}};
	w.restore(catalog, initial);
	while (state(w)["ships"].empty())
		w.tick();
	ticks(w, 3);
	require(w.submit(101, reply(w, "withdraw")), "Existing away party can remain during accepted truce");
	ticks(w, 1);
	for (int n = 0; n < 1800 && !state(w)["captured"].get<bool>(); ++n)
		w.tick();
	require(state(w)["captured"], "Boarder takes target with visiting repair personnel");
	auto captured = w.save();
	require(captured["away_missions"][0]["phase"] == "captured", "Visitors enter explicit custody phase");
	for (const auto &o : body(captured, 2)["personnel"]["officers"])
		require(o["condition"] != "dead", "Takeover does not kill visiting player officers");
	continuation(catalog, w, 20, "Captured visitor identities and injuries survive continuation");
	auto before_retake = w.save();
	const auto location = body(before_retake, 1)["body"]["position"];
	body(before_retake, 2)["body"]["position"] = {{"x", location["x"].get<std::int64_t>() - 10000},
	                                              {"y", location["y"]}};
	w.restore(catalog, before_retake);
	ticks(w, 1);
	const std::string retake = "board.2.1.retake";
	auto order = [&](const std::string &action, const std::string &text = "", std::uint64_t route = 101,
	                 const std::string &controller = "dispatch")
	{
		return away_order(w, action, text, route, controller, retake);
	};
	require(w.submit(101, order("open")), "Open ordinary hostile boarding to retake the captured ship");
	w.tick();
	require(w.submit(102, order("assign", "security/team:security", 102, "security")),
	        "Send reserve Security to retake the ship");
	w.tick();
	require(w.submit(102, order("assign", "medical/team:medical", 102, "medical")),
	        "Send Medical alongside required Security");
	w.tick();
	require(w.submit(101, order("transport", "transport", 101, "transport")), "Use physical displacement insertion");
	ticks(w, 20);
	require(w.submit(101, order("send")), "Retaking uses normal boarding admission");
	w.tick();
	for (int n = 0; n < 150 && w.save()["away_missions"][0]["phase"] == "captured"; ++n)
		w.tick();
	require(w.save()["away_missions"][0]["phase"] == "deployed",
	        "Successful retaking frees captives awaiting extraction");
	continuation(catalog, w, 5, "Freed visitors and new Security prize crew continue exactly");
	auto retained = w.save();
	auto retake_state = [&]() -> const Json &
	{
		for (const auto &mission : retained.at("away_missions"))
			if (mission.at("id") == retake)
				return mission;
		throw std::runtime_error("Retake operation missing");
	};
	require(retake_state().at("outcome") == "complete:retaken",
	        "Intentional Security prize assignment cannot turn successful boarding into party loss");
	require(retake_state().at("assignments") == Json{{"medical", "team:medical"}},
	        "Medical remains aboard for selective recovery after required Security is retained");
	require(w.submit(101, order("recall")), "Recall non-prize Medical after successful capture");
	w.tick();
	for (int n = 0; n < 180; ++n)
	{
		retained = w.save();
		if (retake_state().at("phase") == "recovered")
			break;
		w.tick();
	}
	retained = w.save();
	require(retake_state().at("phase") == "recovered" && retake_state().at("outcome") == "complete:retaken",
	        "Selective Medical recovery preserves the successful boarding result");
	continuation(catalog, w, 5, "Recovered Medical, Security prize crew and freed captives resume exactly");
	require(w.submit(101, away_order(w, "recall")), "Freed visitors still need physical recovery");
	w.tick();
	ticks(w, 50);
	require(w.save()["away_missions"][0]["phase"] == "recovered", "Released repair personnel physically return");
	trace.push_back(w.save());
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array();
		{
			const auto catalog = load(interdiction_fixture());
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			auto destroyed = w.save();
			for (auto &part : body(destroyed, 2)["combat"]["compartments"])
			{
				part["hp"] = 0;
				part["disabled"] = true;
			}
			body(destroyed, 2)["destructible"]["destroyed_tick"] = destroyed["tick"];
			w.restore(catalog, destroyed);
			w.tick();
			const auto frozen = w.observe(101)->gameplay.at("mission_report");
			require(frozen.at("reason") == "destroyed", "Player destruction finalises a frozen mission report");
			continuation(catalog, w, 3, "Destruction reports survive exact continuation");
			require(w.observe(101)->gameplay.at("mission_report") == frozen,
			        "Later NPC activity cannot rewrite a destruction report");
			trace.push_back(w.save());
		}
		selective_prize(trace);
		captive_party(trace);
		{
			auto fixture = interdiction_fixture(3);
			fixture["maps"][0]["ship_slots"][0]["x_mm"] = 15000;
			const auto catalog = load(fixture);
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "Claim truce deployment fixture");
			ticks(w, 4);
			require(w.submit(101, reply(w, "withdraw")), "Accept truce at close range");
			w.tick();
			require(w.submit(101, away_order(w, "open")), "Open a later away manifest");
			w.tick();
			require(w.submit(102, away_order(w, "assign", "repair/team:team", 102, "team")),
			        "Assigning a manifest alone is permitted during truce");
			w.tick();
			require(w.submit(101, away_order(w, "transport", "transport", 101, "transport")),
			        "Prepare a later insertion");
			ticks(w, 30);
			require(state(w)["posture"] == "truce" && !state(w)["reinforced"].get<bool>(),
			        "Close approach and manifest preparation preserve the truce");
			require(w.submit(101, away_order(w, "send")), "Admit the actual outbound insertion");
			w.tick();
			require(state(w)["reinforced"] && state(w)["posture"] == "hostile",
			        "The actual insertion breaks the truce");
			trace.push_back(w.save());
		}
		for (bool outage : {false, true})
		{
			const auto catalog = load(interdiction_fixture());
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			ticks(w, 4);
			if (outage)
			{
				require(w.submit(101, setting(w, "radio", "enabled", 0)), "Disable Comms during ultimatum");
				w.tick();
			}
			while (w.ticks() < 10)
				w.tick();
			if (!outage)
				require(w.submit(101, reply(w, "withdraw")), "Admit a valid reply at the silence boundary");
			w.tick();
			require(state(w)["posture"] == (outage ? "hostile" : "truce"),
			        "Admitted reply wins timeout; communications failure does not stop the clock");
			trace.push_back(w.save());
		}
		{
			const auto catalog = load(interdiction_fixture(3));
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			ticks(w, 4);
			require(w.submit(101, reply(w, "refuse")), "Explicit refusal is admitted");
			w.tick();
			require(state(w)["posture"] == "hostile", "Refusal starts hostilities immediately");
			for (int n = 0; n < 1600 && state(w)["progress"] != 29; ++n)
				w.tick();
			require(state(w)["progress"] == 29, "Takeover reaches its last unfinished tick while actually docked");
			auto before = w.save();
			const auto boarder = state(w)["boarder"].get<EntityId>();
			const auto p = body(before, boarder)["body"]["position"];
			body(before, 2)["body"]["position"] = {{"x", p["x"]}, {"y", p["y"].get<std::int64_t>() + 10000}};
			w.restore(catalog, before);
			w.tick();
			require(!state(w)["captured"].get<bool>() && state(w)["progress"] == 0 && state(w)["blocked"],
			        "A same-tick close threat prevents completion and resets all work");
			continuation(catalog, w, 6, "Interrupted docking and cancelled pleas continue exactly");
			auto interrupted = w.save();
			require(!body(interrupted, boarder).contains("dock_connection"),
			        "The threatened boarder physically undocks");
			trace.push_back(std::move(interrupted));
		}
		{
			const auto catalog = load(interdiction_fixture(3));
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			ticks(w, 3);
			const auto target = state(w)["ships"][2].get<EntityId>();
			require(w.submit(101, setting(w, "gun", "target", target)), "Admit a directed attack");
			w.tick();
			require(state(w)["attacked"] && state(w)["posture"] == "hostile",
			        "Committed attack on any pirate breaks ultimatum before an impact");
			ticks(w, 2);
			auto missed = w.save();
			for (const auto &part : body(missed, target)["combat"]["compartments"])
				require(part["hp"] == 100, "Missed attack still triggered hostilities");
			trace.push_back(std::move(missed));
		}
		{
			const auto catalog = load(interdiction_fixture(3));
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			ticks(w, 4);
			const auto first = state(w)["boarder"].get<EntityId>();
			auto saved = w.save();
			for (auto &part : body(saved, first)["combat"]["compartments"])
			{
				part["hp"] = 0;
				part["disabled"] = true;
			}
			body(saved, first)["destructible"]["destroyed_tick"] = saved["tick"];
			w.restore(catalog, saved);
			w.tick();
			require(state(w)["boarder"] == state(w)["ships"][1] && state(w)["progress"] == 0,
			        "A surviving replacement boarder is chosen deterministically");
			continuation(catalog, w, 4, "Replacement boarder continues without restoring takeover progress");
			trace.push_back(w.save());
		}
		for (int count : {1, 3})
		{
			const auto catalog = load(interdiction_fixture(count));
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			require(w.claim(101, 2, "drive"), "Claim ultimatum Comms");
			ticks(w, 4);
			require(state(w)["ships"].size() == count, "Authored one/three-ship encounter roster");
			require(state(w)["posture"] == "watch", "Pirates hold fire during unanswered demand");
			continuation(catalog, w, 9, "Ultimatum silence and spawned NPCs continue exactly");
			require(state(w)["posture"] == "hostile", "Silence starts hostilities on the authored clock");
			trace.push_back(w.save());
		}
		{
			const auto catalog = load(interdiction_fixture());
			World w;
			w.load(catalog, "test.map", {{"crew", "test.away"}});
			w.claim(101, 2, "drive");
			ticks(w, 4);
			auto before = w.save();
			for (const auto id : state(w)["ships"])
			{
				require(body(before, id.get<EntityId>())["combat"]["systems"]["gun"]["target"] == 0,
				        "Shared targeting must not override encounter hold-fire authority");
			}
			require(w.submit(101, reply(w, "withdraw")), "Accept the demand through normal Comms");
			ticks(w, 1);
			require(state(w)["posture"] == "truce", "Accepted demand ends ultimatum");
			for (int n = 0; n < 1600 && !state(w)["captured"].get<bool>(); ++n)
				w.tick();
			require(state(w)["captured"], "Physical AI docking completes a timed takeover");
			auto taken = w.save();
			require(!taken["captured_by"].empty(), "Takeover persists ownership separately from docking");
			for (const auto &officer : body(taken, 1)["personnel"]["officers"])
				require(officer["condition"] == "dead", "Only original target complement dies at takeover");
			continuation(catalog, w, 20, "Takeover, undocking and group charging continue exactly");
			trace.push_back(w.save());
		}
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			for (auto &s : trace)
				s.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "PASS interdiction ultimatum, physical docking, takeover and continuation\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
