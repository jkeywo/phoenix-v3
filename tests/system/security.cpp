#include "../support/assertions.hpp"
#include "../support/boarding_fixture.hpp"
#include <iostream>
#include <algorithm>
import phoenix.simulation;
import phoenix.session.execution;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json &body(Json &save, EntityId id)
{
	for (auto &row : save["bodies"])
		if (row["body"]["id"] == id)
			return row;
	throw std::runtime_error("missing boarding body");
}
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
OperatingCommand order(const World &w, const std::string &team, const std::string &text)
{
	const auto view = w.observe(102);
	for (const auto &row : view->gameplay.at("teams"))
		if (row["id"] == team)
			for (const auto &c : view->gameplay.at("controllers"))
				if (c["id"] == team)
				{
					OperatingCommand command{2, "team.order", row.at("revision"), team};
					command.controller = team;
					command.generation = c.at("generation");
					command.text = text;
					return command;
				}
	throw std::runtime_error("missing boarding team");
}
OperatingCommand mission_command(const World &w, const std::string &action, const std::string &text = {},
                                 std::uint64_t route = 101, const std::string &controller = "dispatch")
{
	const auto view = w.observe(route);
	for (const auto &row : view->gameplay.at("away_missions"))
		if (row["id"].get<std::string>().starts_with("board."))
		{
			OperatingCommand cmd{2, "away." + action, row.at("revision"), row.at("id")};
			cmd.controller = controller;
			cmd.text = text;
			for (const auto &c : view->gameplay.at("controllers"))
				if (c["id"] == controller)
					cmd.generation = c.at("generation");
			return cmd;
		}
	throw std::runtime_error("No discovered boarding mission");
}
void craft_regression(Json &trace)
{
	for (const auto &mode : {std::string("shuttle"), std::string("boarding_torpedo")})
	{
		auto fixture = boarding_craft_fixture(mode);
		fixture["entities"][1]["boarding_missions"][0]["objectives"]["bridge"]["prize_crew"] = false;
		// Stationary, deliberately damaged target. Its repair team cannot erase the experiment.
		for (auto &c : fixture["entities"][1]["control_layout"]["controllers"])
			if (c["id"] == "team")
				c["commands"][0]["initial"] = 0;
		const auto catalog = load(fixture);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "claim craft fixture");
		require(w.inspect().size() == 2, "stored craft are absent from physical/sensor bodies");
		ticks(w, 5);
		auto initial = w.save();
		require(body(initial, 2)["body"]["position"] == Json{{"x", 0}, {"y", 0}},
		        "stored craft cannot collide with or displace the carrier");
		require(w.submit(101, mission_command(w, "open")), "open physical boarding");
		ticks(w, 1);
		require(w.submit(102, mission_command(w, "assign", "security/team:security", 102, "security")),
		        "assign craft passengers");
		ticks(w, 1);
		require(w.submit(101, mission_command(w, "transport", "transport", 101, "transport")), "bind actual craft bay");
		ticks(w, 7);
		auto saved = w.save();
		if (mode == "shuttle")
		{
			body(saved, 1)["combat"]["compartments"][0]["hp"] = 100;
			body(saved, 1)["combat"]["compartments"][1]["hp"] = 0;
			body(saved, 1)["combat"]["compartments"][1]["disabled"] = true;
			w.restore(catalog, saved);
			require(w.observe(101)->gameplay["away_missions"][0]["blocker"].is_string(),
			        "boarding readiness is projected");
			// Exactly 50% blocks; damage to 49.5% admits physical delivery.
			auto view = w.observe(101);
			bool blocked = false;
			for (const auto &m : view->gameplay["away_missions"])
				if (m["id"].get<std::string>().starts_with("board."))
					blocked = m["blocker"] == "Shuttle boarding requires hull below 50%";
			require(blocked, "shuttle threshold is strictly below half total HP");
			body(saved, 1)["combat"]["compartments"][0]["hp"] = 99;
			w.restore(catalog, saved);
		}
		require(w.submit(101, mission_command(w, "send")), "send finite physical craft");
		ticks(w, 2);
		saved = w.save();
		bool flying = false;
		for (const auto &c : saved["carried_craft"])
			flying |= c["phase"] == "outbound";
		require(flying, "launch is an actual flying entity, not immediate transfer");
		auto malformed_craft = saved;
		malformed_craft["carried_craft"][0]["phase"] = "teleport";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed_craft);
		    },
		    "unknown craft phases rejected");
		if (mode == "boarding_torpedo")
			require(!w.submit(101, mission_command(w, "cancel")),
			        "launched boarding torpedo cannot return or be reused");
		else
		{
			World aborted;
			aborted.restore(catalog, saved);
			require(aborted.submit(101, mission_command(aborted, "cancel")), "airborne abort requests physical return");
			ticks(aborted, 1);
			World abort_resume;
			abort_resume.restore(catalog, aborted.save());
			ticks(aborted, 500);
			ticks(abort_resume, 500);
			require(aborted.save() == abort_resume.save(), "aborted insertion resumes its physical return");
			require(aborted.save()["carried_craft"][0]["phase"] == "stored" && aborted.save()["captured_by"].empty(),
			        "abort returns passengers without completing target mission");
		}
		World resumed;
		resumed.restore(catalog, saved);
		ticks(w, 500);
		ticks(resumed, 500);
		require(w.save() == resumed.save(), "physical flight and encounter continue identically");
		saved = w.save();
		bool delivered = false;
		for (const auto &m : saved["away_missions"])
			if (m["id"].get<std::string>().starts_with("board."))
				delivered = m["boarding"]["captured"];
		require(delivered, "ordinary flight guidance physically delivers boarding party");
		World delivered_resume;
		delivered_resume.restore(catalog, saved);
		ticks(w, 1);
		ticks(delivered_resume, 1);
		require(w.save() == delivered_resume.save(), "docked and consumed craft restore identically");
		const auto phase = saved["carried_craft"][0]["phase"];
		require(phase == (mode == "shuttle" ? "docked" : "consumed"),
		        "finite shuttle retained or disposable torpedo consumed");
		if (mode == "boarding_torpedo")
		{
			require(!w.submit(101, mission_command(w, "recall")), "spent torpedo cannot extract personnel");
			require(w.submit(101, mission_command(w, "transport", "rescue", 101, "rescue")),
			        "bind different extraction method after torpedo delivery");
			ticks(w, 1);
			require(w.submit(101, mission_command(w, "recall")),
			        "qualified displacement extracts torpedo-delivered party");
			ticks(w, 8);
		}
		else
		{
			require(w.submit(101, mission_command(w, "recall")), "recall retained shuttle");
			ticks(w, 500);
			require(w.save()["carried_craft"][0]["phase"] == "stored", "physical shuttle returns to finite inventory");
		}
		trace.push_back(w.save());
	}
}
void encounter_outcomes(Json &trace)
{
	for (const auto &experiment : {std::string("reactor"), std::string("repelled"), std::string("lost")})
	{
		auto fixture = boarding_mission_fixture();
		if (experiment == "reactor")
			fixture["trees"][fixture["trees"].size() - 2]["prompts"]["objective"]["default_choice"] = "reactor";
		else
		{
			auto &e = fixture["entities"][1]["boarding_missions"][0]["encounters"][0];
			e["success_permille"] = 0;
			e["team_bonus_permille"] = 0;
			e["defences"] = {{"reactor", 1000}};
			if (experiment == "lost")
			{
				e["injury_permille"] = 0;
				e["loss_permille"] = 1000;
			}
		}
		const auto catalog = load(fixture);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "claim encounter alternatives");
		ticks(w, 5);
		require(w.submit(101, mission_command(w, "open")), "open alternative");
		ticks(w, 1);
		require(w.submit(102, mission_command(w, "assign", "security/team:security", 102, "security")),
		        "assign alternative");
		ticks(w, 1);
		require(w.submit(101, mission_command(w, "transport", "transport", 101, "transport")), "bind alternative");
		ticks(w, 7);
		require(w.submit(101, mission_command(w, "send")), "send alternative");
		ticks(w, 40);
		auto saved = w.save();
		const Json *mission = nullptr;
		for (const auto &m : saved["away_missions"])
			if (m["id"].get<std::string>().starts_with("board."))
				mission = &m;
		require(mission && !mission->at("boarding").at("captured").get<bool>(),
		        "reactor or failed clash never grants bridge authority");
		if (experiment == "reactor")
		{
			require(mission->at("outcome") == "complete:reactor_stopped",
			        "authored Comms default selects real reactor objective");
			require(body(saved, 1)["resources"]["network"]["generators"][0]["actual_rate"] == 0,
			        "held generator cannot produce power");
			require(w.submit(101, mission_command(w, "recall")), "withdraw reactor occupation");
			ticks(w, 8);
			saved = w.save();
			require(body(saved, 1)["resources"]["network"]["generators"][0]["actual_rate"].get<int>() > 0,
			        "generator resumes after physical withdrawal");
		}
		else
		{
			require(mission->at("outcome").get<std::string>().starts_with("fail:"),
			        "authored failed clash ends unsuccessfully");
			if (experiment == "lost")
				require(body(saved, 2)["combat"]["systems"]["security"]["condition"] == "lost",
				        "lethal clash permanently loses whole team");
		}
		World resumed;
		resumed.restore(catalog, saved);
		ticks(w, 3);
		ticks(resumed, 3);
		require(w.save() == resumed.save(), "alternative encounter state resumes identically");
		trace.push_back(w.save());
	}
}
void boarding_mission_regression(Json &trace)
{
	for (bool medical : {false, true})
	{
		const auto catalog = load(boarding_mission_fixture());
		auto host = make_host_world();
		host->add_pack(archive(boarding_mission_fixture()));
		host->start_server({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
		auto &w = host->authoritative_world();
		GameExecution execution(host, {{"crew-server", "crew"}});
		require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "claim boarding operation");
		require(w.claim(103, 1, "drive"), "old target crew can initially claim Console");
		ticks(w, 5);
		require(w.submit(101, mission_command(w, "open")), "open target-authored boarding offer");
		ticks(w, 1);
		require(w.submit(102, mission_command(w, "assign", "security/team:security", 102, "security")),
		        "commit actual security team");
		ticks(w, 1);
		if (medical)
		{
			require(w.submit(102, mission_command(w, "assign", "medical/team:medical", 102, "medical")),
			        "commit optional boarding Medical");
			ticks(w, 1);
		}
		require(w.submit(101, mission_command(w, "transport", "transport", 101, "transport")),
		        "bind boarding displacement");
		ticks(w, 7);
		require(w.submit(101, mission_command(w, "send")), "send staged boarding party");
		ticks(w, 5);
		auto pending = w.save();
		World resumed;
		resumed.restore(catalog, pending);
		ticks(w, 24);
		ticks(resumed, 24);
		require(w.save() == resumed.save(),
		        "boarding route, unanswered Comms decision and encounter resume without rerolls");
		auto saved = w.save();
		bool captured = false;
		for (const auto &m : saved["away_missions"])
			if (m["id"].get<std::string>().starts_with("board."))
				captured = m["boarding"]["captured"];
		require(captured && saved["captured_by"] == Json(std::map<EntityId, EntityId>{{1, 2}}),
		        "authored bridge objective captures the actual target");
		require(!w.observe(103) && w.operational_fleet(2) == std::vector<EntityId>({1, 2}),
		        "capture revokes old claims and exposes whole prize");
		require(execution.apply("crew-server", {{"kind", "tick"}, {"tick", w.ticks() + 1}}),
		        "committed tick refreshes capture leases");
		require(execution.can_claim("crew-server", "prize-watch", 3, {1, "drive"}, false),
		        "captor server can lease prize Console");
		require(!execution.can_claim("other-server", "prize-watch", 3, {1, "drive"}, false),
		        "other servers cannot lease prize");
		const auto leases = execution.snapshot();
		auto reordered = leases;
		std::reverse(reordered["seats"].begin(), reordered["seats"].end());
		execution.restore(reordered);
		require(execution.snapshot() == leases, "legacy lease ordering restores by stable Console identity");
		saved = w.save();
		require(body(saved, 2)["combat"]["systems"]["security"]["condition"] == (medical ? "healthy" : "injured"),
		        "optional Medical reduces nonfatal clash injuries");
		require(body(saved, 1)["combat"]["systems"]["security"]["condition"] == "injured",
		        "defending security team suffers real clash injury");
		require(!w.submit(101, mission_command(w, "recall")), "prize crew remains physically committed after capture");
		World captured_resume;
		captured_resume.restore(catalog, saved);
		ticks(w, 3);
		ticks(captured_resume, 3);
		require(w.save() == captured_resume.save(), "captured authority and completed encounter survive restore");
		auto malformed = saved;
		malformed["captured_by"] = Json::array();
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, malformed);
		    },
		    "capture requires valid authority receipts");
		trace.push_back(w.save());
	}
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array();
		boarding_mission_regression(trace);
		craft_regression(trace);
		encounter_outcomes(trace);
		const auto catalog = load(boarding_fixture());
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		require(world.claim(101, 2, "drive") && world.claim(102, 2, "observer"), "claim security fixture");
		auto saved = world.save();
		auto &security = body(saved, 2)["combat"]["systems"]["security"];
		security["condition"] = "injured";
		world.restore(catalog, saved);
		require(world.submit(102, order(world, "medical", "treat/workshop/team:security")),
		        "Medical accepts an injured team");
		ticks(world, 2);
		saved = world.save();
		require(body(saved, 2)["combat"]["systems"]["security"]["treatment"] == 2000,
		        "team treatment is real per-tick work");
		World resumed;
		resumed.restore(catalog, saved);
		ticks(world, 10);
		ticks(resumed, 10);
		require(world.save() == resumed.save(), "team injury and treatment resume identically");
		saved = world.save();
		require(body(saved, 2)["combat"]["systems"]["security"]["condition"] == "healthy",
		        "team treatment heals injury");
		body(saved, 2)["combat"]["systems"]["security"]["condition"] = "lost";
		world.restore(catalog, saved);
		require(!world.submit(102, order(world, "medical", "treat/workshop/team:security")),
		        "Medical cannot resurrect a lost team");
		auto invalid = saved;
		body(invalid, 2)["combat"]["systems"]["security"]["condition"] = "unknown";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, invalid);
		    },
		    "unknown team condition rejected");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			trace.push_back(world.save());
			for (auto &state : trace)
				state.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "PASS security boarding, clinical care, finite craft, capture authority and continuation\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
