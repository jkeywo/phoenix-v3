#pragma once
// Synthetic mission: production loader, actual deployment, interrupted work,
// real duty-officer casualties and exact continuation at every event boundary.
void away_priority_cases()
{
	for (const bool officer : {false, true})
		for (const bool fast : {false, true})
		{
			auto source = away_fixture();
			if (!officer)
				source["entities"][0]["personnel"]["officers"][1]["duty"] = "";
			source["maps"][0]["scenario"] = {{"events", {"test.done"}}};
			for (auto &tree : source["trees"])
				if (tree["id"] == "test.dialogue")
				{
					tree["states"] =
					    Json::parse(R"([{"id":"start","entry":[{"action":"say","arguments":["intro"]}]}])");
					tree["prompts"]["choice"]["default_choice"] = fast ? "fast" : "slow";
					tree["prompts"]["choice"]["timeout_ticks"] = 3;
					tree["prompts"]["choice"]["choices"] = Json::parse(R"([
 {"id":"slow","text_key":"Controlled shutdown"}, {"id":"fast","text_key":"Immediate shutdown"},
 {"id":"patients","text_key":"Rescue patients","requires_capability":"medical","unavailable_reason":"Medical absent"}])");
				}
				else if (tree["id"] == "test.away-tree")
				{
					tree = Json::parse(R"({"id":"test.away-tree","context":"away","initial":"wait","flags":["reactor"],
 "team_capabilities":["medical"],"states":[
 {"id":"wait","transitions":[{"target":"first","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},
 {"id":"first","entry":[{"action":"checkpoint","arguments":["patients"]},{"action":"timed_work","arguments":[12]}],
 "transitions":[{"target":"emergency","conditions":[{"left":{"elapsed":true},"op":"ge","right":4}]}]},
 {"id":"emergency","entry":[{"action":"pause","arguments":[""]},{"action":"log","arguments":["Interrupted"]},{"action":"offer","arguments":["choice"]}],
 "transitions":[{"target":"slow","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"slow"}]},{"target":"fast","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"fast"}]}]},
 {"id":"slow","entry":[{"action":"checkpoint","arguments":["reactor"]},{"action":"injure_duty","arguments":["repair"]},{"action":"timed_work","arguments":[8]}],"transitions":[{"target":"resume","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"fast","entry":[{"action":"checkpoint","arguments":["reactor"]},{"action":"kill_duty","arguments":["repair"]},{"action":"timed_work","arguments":[2]}],"transitions":[{"target":"resume","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"resume","entry":[{"action":"mark","arguments":["reactor"]},{"action":"checkpoint","arguments":["patients"]},{"action":"timed_work","arguments":[12]}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true}]}]},
 {"id":"done","entry":[{"action":"crew_deaths","arguments":[1]},{"action":"log","arguments":["One death"]},{"action":"emit","arguments":["test.done"]},{"action":"complete","arguments":["repaired"]}]}]})");
				}
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map", {{"crew", "test.away"}});
			const auto ship = *world.ship_for_slot("crew");
			require(world.claim(101, ship, "drive") && world.claim(102, ship, "observer"), "priority claims");
			ticks(world, 4);
			require(world.submit(101, order(world, "open")), "priority open");
			ticks(world, 1);
			require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "priority team");
			ticks(world, 1);
			require(world.submit(101, order(world, "transport", "transport")), "priority transport");
			ticks(world, 12);
			require(world.submit(101, order(world, "send")), "priority send");
			bool interrupted = false, resumed_work = false, disabled_choice = false;
			for (int tick = 0; tick < 50; ++tick)
			{
				world.tick();
				const auto saved = world.save();
				const auto &mission = saved.at("away_missions")[0];
				if (mission.at("saved_progress").contains("patients") &&
				    mission.at("saved_progress").at("patients") > 0)
				{
					interrupted = true;
					require(mission.at("saved_progress").at("patients") >= 4000, "interrupted progress retained");
					resumed_work |= mission.at("checkpoint") == "patients" && mission.at("flags").size() == 1;
				}
				const auto observed = world.observe(101);
				for (const auto &thread : observed->gameplay.at("conversations").at("threads"))
					for (const auto &choice : thread.at("choices"))
						if (choice.at("id") == "patients")
						{
							disabled_choice = true;
							require(!choice.at("enabled").get<bool>() && choice.at("reason") == "Medical absent",
							        "Medical gate explained");
						}
				World restored;
				restored.restore(catalog, saved);
				require(restored.save() == saved, "priority exact restore including casualty and paused progress");
				restored.tick();
				World next;
				next.restore(catalog, saved);
				next.tick();
				require(restored.save() == next.save(), "priority deterministic continuation");
			}
			require(interrupted && resumed_work && disabled_choice,
			        ("interrupt, resume and Medical availability exercised: " + std::to_string(interrupted) + "/" +
			         std::to_string(resumed_work) + "/" + std::to_string(disabled_choice) + " " +
			         world.save().at("away_missions").dump())
			            .c_str());
			const auto saved = world.save();
			require(saved.at("away_missions")[0].at("outcome") == "complete:repaired" &&
			            saved.at("away_missions")[0].at("crew_deaths") == 1,
			        "completed work retains crew death");
			int casualties = 0, logs = 0;
			for (const auto &entry : saved.at("mission_ledger").at("transitions"))
			{
				if (entry.at("kind") == "mission.casualty")
				{
					++casualties;
					require(entry.at("operation") == "chief" && entry.at("detail") == (fast ? "dead" : "severe"),
					        "actual repair duty officer harmed");
				}
				if (entry.at("kind") == "mission.log")
					++logs;
			}
			require(casualties == (officer ? 1 : 0) && logs == 2, "no substitute casualties or duplicate log credit");
			require(world.submit(101, order(world, "recall")), "casualty party can recall");
			ticks(world, 5);
			require(board(world)[0].at("phase") == "recovered", "injured party recovery completes");
		}
}
