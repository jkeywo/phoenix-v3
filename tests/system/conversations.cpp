#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
import phoenix.simulation;
import phoenix.conversations;
import phoenix.console_controls;
#include "../../src/core/conversation_projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
bool projected_command(const Observation &view, const OperatingCommand &command)
{
	return eligible_console_command(view, Json{{"widgets", Json::array()}}, Json::object(), command).has_value();
}
Json fixture()
{
	auto s = controller_fixture();
	auto &ship = s["entities"][0];
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"capacity", 100000},
	                                              {"initial", 100000},
	                                              {"charge_rate", 100000},
	                                              {"discharge_rate", 100000}}})}};
	ship["systems"]["radio"] = {
	    {"kind", "comms"}, {"range_mm", 100000}, {"energy_per_second", 20}, {"power_store", "cell"}};
	ship["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 200000}, {"shows", {"ship"}}};
	ship["tags"] = {"ship"};
	ship["stations"][0]["consoles"][0]["feeds"].push_back("radio");
	ship["stations"][0]["consoles"][0]["feeds"].push_back("radar");
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "comms"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"radio"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("comms");
	ship["control_layout"]["profiles"][1]["consoles"]["drive"]["ai"].push_back("comms");
	ship["comms"] = {
	    {"default_conversation", "test.dialogue"},
	    {"characters", Json::array({{{"id", "captain"}, {"name_key", "test.captain"}, {"public", true}}})}};
	auto tree = Json::parse(
	    R"({"id":"test.dialogue","context":"conversation","title_key":"test.chat","initial":"hello","lines":{"hello":{"speaker":"target","text_key":"test.hello","delay_ticks":2,"incoming_ticks":3}},"prompts":{"question":{"speaker":"target","text_key":"test.question","choices":[{"id":"yes","text_key":"test.yes"},{"id":"no","text_key":"test.no"}],"ai_choice":"yes","ai_delay_ticks":20,"timeout_ticks":100,"default_choice":"no"}},"states":[{"id":"hello","entry":[{"action":"say","arguments":["hello"]}],"transitions":[{"target":"ask","conditions":[{"left":{"elapsed":true},"op":"ge","right":6}]}]},{"id":"ask","entry":[{"action":"offer","arguments":["question"]}]}]})");
	s["trees"].push_back(tree);
	for (const auto &key : {"test.captain", "test.chat", "test.hello", "test.question", "test.yes", "test.no"})
		s["translations"]["en"][key] = key;
	s["maps"][0]["entities"].push_back(
	    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
	return s;
}
#include "../support/received_information_cases.hpp"
void cancelled_station_traffic()
{
	auto source = fixture();
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.station-traffic","context":"scenario","initial":"start","states":[{"id":"start","entry":[{"action":"emit_scenario_event","arguments":["reminder",0,false,""]}],"transitions":[{"target":"terminal","conditions":[{"left":{"elapsed":true},"op":"ge","right":3}]}]},{"id":"terminal","entry":[{"action":"emit_scenario_event","arguments":["receipt",0,false,""]}]}]})"));
	source["maps"][0]["scenario"] = {{"tree", "test.station-traffic"}, {"events", {"reminder", "receipt"}}};
	source["maps"][0]["conversations"] = Json::parse(
	    R"([{"id":"reminder","tree":"test.dialogue","start_event":"reminder","cancel_events":["receipt"],"bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]}])");
	const auto catalog = load(source);
	World w;
	w.load(catalog, "test.map");
	require(w.claim(1, 1, "drive"), "Station traffic crew");
	w.tick();
	w.tick();
	World resumed;
	resumed.restore(catalog, w.save());
	for (int i = 0; i < 30; ++i)
	{
		w.tick();
		resumed.tick();
	}
	require(w.save() == resumed.save(), "Queued reminder cancellation survives continuation");
	require(w.save().at("conversation_starts").at("starts")[0].at("cancelled") == true,
	        "Terminal receipt latches traffic cancellation");
	const auto observed = w.observe(1);
	for (const auto &thread : observed->gameplay.at("conversations").at("threads"))
		require(thread.at("messages").empty(), "Obsolete queued reminders never deliver");
}

void contact_introductions()
{
	auto source = fixture();
	auto reception = Json::parse(R"json({"id":"test.reception","context":"conversation","initial":"ask",
 "prompts":{"contact":{"speaker":"target","text_key":"Who do you need?","timeout_ticks":100,"default_choice":"captain",
 "choices":[{"id":"captain","text_key":"Speak to the captain"}]}},
 "lines":{"reply":{"speaker":"captain","text_key":"Captain speaking"}},
 "states":[{"id":"ask","entry":[{"action":"offer","arguments":["contact"]}],"transitions":[{"target":"introduce","conditions":[{"left":{"observation":"answer"},"op":"eq","right":"captain"}]}]},
 {"id":"introduce","entry":[{"action":"add_participant","arguments":["captain"]},{"action":"say","arguments":["reply"]}]}]})json");
	source["trees"].push_back(reception);
	source["entities"][0]["comms"]["default_conversation"] = "test.reception";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(1, 1, "drive"), "Claim directory caller");
	for (int i = 0; i < 5; ++i)
		world.tick();
	auto command =
	    [&](const std::string &action, std::uint64_t thread, const std::string &text, std::uint64_t prompt = 0)
	{
		OperatingCommand c{1, "comms." + action, static_cast<std::int64_t>(thread)};
		c.console = "drive";
		c.controller = "comms";
		c.text = text;
		c.request_id = prompt;
		const auto observation = world.observe(1);
		for (const auto &controller : observation->gameplay.at("controllers"))
			if (controller.at("id") == "comms")
				c.generation = controller.at("generation");
		return c;
	};
	const auto initial = world.observe(1);
	for (const auto &p : initial->gameplay.at("conversations").at("directory"))
		require(p.at("id").get<std::string>().find('/') == std::string::npos,
		        "Sensors disclose vessels, not their personnel directory");
	require(!world.submit(1, command("hail", 0, "ship:2/captain")), "Undisclosed individual cannot be hailed directly");
	require(world.submit(1, command("hail", 0, "ship:2")), "Call the ship before asking for an individual");
	for (int i = 0; i < 3; ++i)
		world.tick();
	const auto thread = world.observe(1)->gameplay.at("conversations").at("threads")[0];
	require(world.submit(1, command("answer", thread.at("id"), "captain", thread.at("prompt"))),
	        "Ask reception to introduce the captain");
	for (int i = 0; i < 3; ++i)
		world.tick();
	require(world.can_submit(1, command("hail", 0, "ship:2/captain")),
	        "Conversation introduction permits later direct calls");
	World resumed;
	resumed.restore(catalog, world.save());
	require(resumed.observe(1)->gameplay.at("conversations") == world.observe(1)->gameplay.at("conversations"),
	        "Introduced contacts persist through save and resume");
}
void ship_conversation_history()
{
	auto source = fixture();
	source["entities"][0]["comms"]["characters"].push_back(
	    {{"id", "engineer"}, {"name_key", "Engineer"}, {"public", false}});
	auto second = source["trees"].back();
	second["id"] = "test.secondary";
	second["title_key"] = "Separate subject";
	second["lines"]["hello"]["text_key"] = "A second operation";
	source["trees"].push_back(second);
	source["maps"][0]["entities"].push_back(
	    {{"id", 3}, {"definition", "test.controllable"}, {"x_mm", 40000}, {"y_mm", 0}, {"heading_mdeg", 0}});
	source["maps"][0]["conversations"] = Json::parse(R"([
 {"id":"assistance","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"}},"members":["caller","target"]},
 {"id":"escalation","tree":"test.secondary","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"engineer"}},"members":["caller","target"]}
 ])");
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(1, 1, "drive"), "Claim ship conversation operator");
	auto command =
	    [&](const std::string &action, std::uint64_t id, const std::string &text = "", std::uint64_t prompt = 0)
	{
		OperatingCommand c{1, "comms." + action, static_cast<std::int64_t>(id)};
		c.console = "drive";
		c.controller = "comms";
		c.text = text;
		c.request_id = prompt;
		const auto current = world.observe(1);
		for (const auto &ctrl : current->gameplay.at("controllers"))
			if (ctrl["id"] == "comms")
				c.generation = ctrl["generation"];
		return c;
	};
	for (int i = 0; i < 12; ++i)
		world.tick();
	require(world.submit(1, command("hail", 0, "ship:2")), "Hail the ship after its scripted assistance calls");
	for (int i = 0; i < 12; ++i)
		world.tick();
	auto board = world.observe(1)->gameplay.at("conversations");
	require(board["threads"].size() == 1,
	        "Hail, assistance and escalation with one ship share a single visible conversation");
	auto thread = board["threads"][0];
	require(thread["sources"].size() == 3 && thread["choices"].size() == 6,
	        "Independent dialogue programs and all pending questions survive grouping");
	std::uint64_t previous = 0;
	for (const auto &message : thread["messages"])
	{
		auto id = message["id"].get<std::uint64_t>();
		require(id > previous, "Cross-operation history follows stable global message order");
		previous = id;
	}
	require(world.submit(1, command("select", thread["id"])), "Select the combined ship history");
	world.tick();
	board = world.observe(1)->gameplay.at("conversations");
	require(board["selected"] == thread["id"] && board["threads"][0]["unread"] == false,
	        "Selecting the ship marks every visible source conversation read");
	const auto remote = conversation_projection::board(board);
	require(remote["threads"][0]["choices"] == board["threads"][0]["choices"],
	        "Remote projection preserves prompt ownership and questions");
	auto forged = board;
	forged["threads"][0]["choices"][0]["conversation"] = 999999;
	bool rejected_source = false;
	try
	{
		conversation_projection::board(forged);
	}
	catch (const std::exception &)
	{
		rejected_source = true;
	}
	require(rejected_source, "A prompt cannot claim ownership outside its scoped source conversations");

	const auto choice = board["threads"][0]["choices"][2];
	auto answer = command("answer", thread["id"], choice["id"], choice["prompt"]);
	require(projected_command(*world.observe(1), answer) && world.submit(1, answer),
	        "Grouped command admission routes a non-primary question by its prompt receipt");
	world.tick();
	int answered = 0;
	const auto answered_state = world.save();
	for (const auto &raw : answered_state["conversations"]["threads"])
		if (raw["prompt"]["answer"] != "")
		{
			++answered;
			require(raw["id"] == choice["conversation"], "Only the owning operation accepts the answer");
		}
	require(answered == 1, "Duplicate choice names cannot answer another pending question");
	require(world.submit(1, command("hail", 0, "ship:3")), "Hail another ship with the same display name");
	for (int i = 0; i < 12; ++i)
		world.tick();
	board = world.observe(1)->gameplay.at("conversations");
	require(board["threads"].size() == 2, "Ship identity, rather than title or character name, controls grouping");
	World resumed;
	resumed.restore(catalog, world.save());
	require(resumed.save() == world.save(), "Grouping does not rewrite dialogue continuation or receipts on restore");
	require(resumed.observe(1).has_value(), "Restore the selected conversation station");
	require(resumed.observe(1)->gameplay.at("conversations") == board,
	        "Selected ship, merged history and all remaining questions survive resume");

	source["maps"][0]["conversations"].push_back(Json::parse(
	    R"({"id":"conference","title_key":"Conference","tree":"test.dialogue","bindings":{"caller":{"entity":1},"target":{"entity":2,"character":"captain"},"peer":{"entity":3}},"members":["caller","target","peer"]})"));
	World conference;
	conference.load(load(source), "test.map");
	require(conference.claim(1, 1, "drive") && conference.claim(3, 3, "drive"), "Claim both crews in a conference");
	for (int i = 0; i < 12; ++i)
		conference.tick();
	require(conference.observe(1)->gameplay.at("conversations")["threads"].size() == 2,
	        "A multi-ship conference remains separate from the direct ship conversation");
	require(conference.observe(3)->gameplay.at("conversations")["threads"].size() == 1,
	        "Grouping cannot disclose private ship conversations to a conference member");
	ConversationRuntime live;
	live.programs = catalog.conversation_programs;
	live.participants = {{"ship:1", {"ship:1", "Crew", "", "", 1}},
	                     {"ship:2", {"ship:2", "Target", "", "", 2}},
	                     {"ship:2/engineer", {"ship:2/engineer", "Engineer", "", "", 2, true}}};
	live.endpoints = {{1, {1, 0, 0, 100000, true, true}}, {2, {2, 1000, 0, 100000, true, true}}};
	const auto scripted = live.open("test.dialogue", {{"caller", "ship:1"}, {"target", "ship:2/engineer"}},
	                                {"ship:1", "ship:2/engineer"});
	const auto direct = live.open("", {}, {"ship:1", "ship:2"});
	require(live.send_text(1, scripted, "Crew text"),
	        "A grouped selection routes human text to its writable crew channel");
	require(live.conversations.at(scripted).messages.empty() && live.conversations.at(direct).messages.back().literal,
	        "Crew chat does not replace or advance the scripted operation");
	live.advance(1);
	live.selected[1] = direct;
	require(live.project(1)["threads"].size() == 1 && live.project(1)["selected"] == scripted,
	        "A saved noncanonical selection resolves to the same ship conversation");
	auto restored = live;
	restored.restore(live.save());
	require(restored.save() == live.save() && restored.project(1) == live.project(1),
	        "Existing independent conversation saves need no regrouping migration");
	auto unknown = live;
	unknown.known[1].erase("ship:2");
	unknown.participants.at("ship:2").name = "Undisclosed vessel identity";
	require(unknown.project(1)["threads"][0]["title"] != "Undisclosed vessel identity",
	        "Grouping does not disclose an unknown ship name through a known officer");
}
int main(int argc, char **argv)
{
	try
	{
		contact_introductions();
		ship_conversation_history();
		cancelled_station_traffic();
		received_information_cases();
		{
			auto source = fixture();
			auto &choice = source["trees"].back()["prompts"]["question"]["choices"][1];
			choice["requires_capability"] = "security";
			choice["hide_unavailable"] = true;
			ConversationRuntime runtime;
			runtime.programs = load(source).conversation_programs;
			runtime.participants = {{"ship:1", {"ship:1", "Crew", "", "", 1}},
			                        {"ship:2", {"ship:2", "Target", "", "", 2}}};
			runtime.endpoints = {{1, {1, 0, 0, 100000, true, true}}, {2, {2, 1000, 0, 100000, true, false}}};
			const auto id =
			    runtime.open("test.dialogue", {{"caller", "ship:1"}, {"target", "ship:2"}}, {"ship:1", "ship:2"});
			for (int n = 0; n < 12; ++n)
				runtime.advance(n);
			require(runtime.project(1).at("threads")[0].at("choices").size() == 1,
			        "Unavailable seizure choice is omitted");
			runtime.capabilities[id].insert("security");
			require(runtime.project(1).at("threads")[0].at("choices").size() == 2, "Deployed Security exposes seizure");
		}
		{
			auto source = fixture();
			auto &layout = source["entities"][0]["control_layout"];
			layout["station_complexity"] = true;
			layout["profiles"][0]["stars"] = 1;
			auto &operators = layout["profiles"][0]["consoles"]["drive"];
			auto &human = operators["human"];
			human.erase(std::find(human.begin(), human.end(), Json("comms")));
			operators["ai"].push_back("comms");
			World assisted;
			assisted.load(load(source), "test.map");
			require(assisted.claim(1, 1, "drive"), "Assisted radio claim");
			for (int i = 0; i < 5; ++i)
				assisted.tick();
			auto view = *assisted.observe(1);
			OperatingCommand hail{1, "comms.hail", 0};
			hail.console = "drive";
			hail.controller = "comms";
			hail.text = "ship:2";
			for (const auto &controller : view.gameplay.at("controllers"))
				if (controller.at("id") == "comms")
				{
					require(!controller.at("human").get<bool>(), "Radio readiness stays automatic");
					hail.generation = controller.at("generation");
				}
			require(projected_command(view, hail) && assisted.submit(1, hail),
			        "Station operator retains conversations with automatic radio readiness");
			OperatingCommand toggle{1, "system_enabled", 0};
			toggle.console = "drive";
			toggle.controller = "comms";
			toggle.channel = "enabled";
			toggle.generation = hail.generation;
			require(!assisted.submit(1, toggle), "Conversation authority cannot switch the radio off");
		}
		for (const auto y : {0, 1})
		{
			auto boundary = fixture();
			boundary["maps"][0]["entities"].back()["x_mm"] = 100000;
			boundary["maps"][0]["entities"].back()["y_mm"] = y;
			World radio;
			radio.load(load(boundary), "test.map");
			require(radio.claim(1, 1, "drive"), "boundary radio claim");
			for (int i = 0; i < 5; ++i)
				radio.tick();
			bool found = false;
			const auto directory = radio.observe(1)->gameplay.at("conversations").at("directory");
			for (const auto &p : directory)
				if (p.at("host") == 2)
				{
					found = true;
					require(p.at("available") == (y == 0),
					        "radio reach uses the exact range sphere, without square-root rounding");
				}
			require(found, "boundary contact remains observable");
		}
		{
			auto source = fixture();
			source["entities"].push_back(
			    {{"id", "test.rock"},
			     {"tags", {"asteroid"}},
			     {"systems", Json::object()},
			     {"render",
			      {{"mode", "camera"},
			       {"scale", 1},
			       {"sprite",
			        {{"albedo", "test-core:assets/pixel.png"},
			         {"frames", Json::array({{{"name", "front"}, {"rect", {0, 0, 1, 1}}, {"pivot", {.5, .5}}}})}}}}},
			     {"collision", {{"shape", "circle"}, {"radius_mm", 100}, {"mass_grams", 0}}}});
			source["maps"][0]["scenario"] = {{"events", {"test.signal"}}};
			source["maps"][0]["regions"] = Json::array({{{"id", "belt"},
			                                             {"shape", "circle"},
			                                             {"x_mm", 0},
			                                             {"y_mm", 0},
			                                             {"radius_mm", 40000},
			                                             {"asteroids",
			                                              {{"seed", 91},
			                                               {"spacing_mm", 10000},
			                                               {"jitter_mm", 0},
			                                               {"density", .1},
			                                               {"clustering_permille", 0},
			                                               {"visual_distance_mm", 50000},
			                                               {"types", Json::array({{{"definition", "test.rock"}}})}}}}});
			const auto field_catalog = load(source);
			World initial, restored;
			initial.load(field_catalog, "test.map");
			const auto snapshot = initial.save();
			require(!snapshot.at("asteroid_fields").empty(), "Comms recovery fixture has streamed asteroids");
			restored.restore(field_catalog, snapshot);
			require(restored.save() == snapshot, "Comms and event-only scenario restore with streamed contacts");
			initial.tick();
			restored.tick();
			require(initial.save() == restored.save(), "Comms and streamed radar continue identically after restore");
		}
		auto catalog = load(fixture());
		World world;
		world.load(catalog, "test.map");
		require(world.claim(1, 1, "drive"), "claim caller Comms");
		world.tick();
		auto cmd = [&](std::uint64_t who, const std::string &action, std::uint64_t thread, const std::string &text,
		               std::uint64_t prompt = 0)
		{
			auto o = world.observe(who);
			OperatingCommand c{o->own.id, "comms." + action, static_cast<std::int64_t>(thread)};
			c.console = "drive";
			c.controller = "comms";
			c.text = text;
			c.request_id = prompt;
			for (const auto &v : o->gameplay.at("controllers"))
				if (v.at("id") == "comms")
					c.generation = v.at("generation");
			return c;
		};
		require(projected_command(*world.observe(1), cmd(1, "hail", 0, "ship:2")), "remote hail projection admission");
		const auto before_admission = world.observe(1)->gameplay.at("conversations");
		require(world.submit(1, cmd(1, "hail", 0, "ship:2")), "hail discovered ship");
		require(world.observe(1)->gameplay.at("conversations") == before_admission,
		        "hail admission does not create or select a conversation");
		world.tick();
		auto view = [&]()
		{
			return world.observe(1)->gameplay.at("conversations");
		};
		require(view().at("threads").size() == 1, "persistent thread created");
		auto id = view().at("selected").get<std::uint64_t>();
		require(!world.submit(1, cmd(1, "text", id, "NPC must not parse this")), "NPC disables free text");
		for (int i = 0; i < 3; ++i)
			world.tick();
		require(!view().at("threads")[0].at("incoming").empty(), "authored incoming interval");
		auto saved = world.save();
		World resumed;
		resumed.restore(catalog, saved);
		for (int i = 0; i < 6; ++i)
		{
			world.tick();
			resumed.tick();
		}
		require(world.save() == resumed.save(), "exact continuation during incoming indication");
		auto thread = view().at("threads")[0];
		require(thread.at("choices").size() == 2, "delivered prompt has choices");
		require(world.submit(1, cmd(1, "answer", id, "yes", thread.at("prompt"))), "admit answer");
		world.tick();
		require(!world.submit(1, cmd(1, "answer", id, "no", thread.at("prompt"))),
		        "resolved prompt cannot answer twice");
		require(world.submit(1, cmd(1, "hail", 0, "ship:2")), "repeat hail");
		world.tick();
		require(view().at("threads").size() == 1, "repeat hail reopens thread");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			std::cout << world.save().at("conversations").dump() << '\n';
			return 0;
		}
		require(world.claim(2, 2, "drive"), "Claim second human Comms after the NPC exchange");
		// Group prompts resolve once across two human Consoles.
		auto group_source = fixture();
		group_source["maps"][0]["conversations"] =
		    Json::array({{{"id", "group"},
		                  {"tree", "test.dialogue"},
		                  {"bindings",
		                   {{"caller", {{"entity", 1}}},
		                    {"second", {{"entity", 2}}},
		                    {"target", {{"entity", 2}, {"character", "captain"}}}}},
		                  {"members", {"caller", "second", "target"}}}});
		auto group_catalog = load(group_source);
		World group;
		group.load(group_catalog, "test.map");
		require(group.claim(1, 1, "drive") && group.claim(2, 2, "drive"), "group claims");
		for (int i = 0; i < 10; ++i)
			group.tick();
		auto g = group.observe(1)->gameplay.at("conversations").at("threads")[0];
		auto answer = cmd(1, "answer", g.at("id"), "yes", g.at("prompt"));
		require(group.submit(1, answer), "first group answer admitted");
		require(group.submit(2, cmd(2, "answer", g.at("id"), "no", g.at("prompt"))),
		        "concurrent reply queued for ordered arbitration");
		group.tick();
		require(!group.submit(2, cmd(2, "answer", g.at("id"), "no", g.at("prompt"))), "second group answer rejected");
		require(group.save().at("conversations").at("threads")[0].at("prompt").at("respondent") == "ship:1",
		        "group resolution identifies respondent");
		auto stale = cmd(1, "hail", 0, "ship:2");
		++stale.generation;
		require(!world.submit(1, stale), "stale controller generation rejected");
		require(world.submit(1, cmd(1, "hail", 0, "ship:2")), "human ship hail");
		world.tick();
		auto human_id = view().at("selected").get<std::uint64_t>();
		require(decode_operating_command(encode_operating_command(cmd(1, "text", human_id, "wire text"))).text ==
		            "wire text",
		        "command codec preserves text");
		require(projected_command(*world.observe(1), cmd(1, "text", human_id, "wire text")),
		        "remote human text admitted");
		auto public_thread = conversation_projection::thread(view().at("threads")[0], true);
		require(!public_thread.contains("choices") && !public_thread.contains("prompt") &&
		            !public_thread.contains("text_allowed"),
		        "public transcript strips authority");
		require(world.submit(1, cmd(1, "text", human_id, "Human message <literal>")), "all-human free text");
		world.tick();
		world.tick();
		require(world.observe(2)->gameplay.at("conversations").at("threads").back().at("messages").back().at("text") ==
		            "Human message <literal>",
		        "human delivery retains literal text");
		// The production runtime consumes compiled synthetic pack trees and explicit endpoint observations.
		ConversationRuntime runtime;
		runtime.programs = catalog.conversation_programs;
		for (EntityId i = 1; i <= 3; ++i)
		{
			auto id = "ship:" + std::to_string(i);
			runtime.participants[id] = {id, id, "", "", i, false, true};
			runtime.endpoints[i] = {i, static_cast<std::int64_t>(i) * 10, 0, 100, true, true};
		}
		runtime.participants["captain"] = {"captain", "Captain", "", "test.dialogue", 2, true, true};
		auto rid = runtime.open("test.dialogue", {{"caller", "ship:1"}, {"target", "captain"}}, {"ship:1", "captain"});
		runtime.advance(1);
		runtime.endpoints[1].working = false;
		for (int i = 2; i <= 10; ++i)
			runtime.advance(i);
		require(runtime.project(1).at("threads")[0].at("messages").empty(), "lost link queues messages and prompt");
		auto queued = runtime.save();
		auto continued = runtime;
		continued.restore(queued);
		runtime.endpoints[1].working = true;
		continued.endpoints[1].working = true;
		runtime.advance(11);
		continued.advance(11);
		require(runtime.save() == continued.save(), "queued deliveries survive restore");
		require(runtime.project(1).at("threads")[0].at("messages").size() == 2, "reconnect delivers queued history");
		runtime.endpoints[1].human = false;
		runtime.advance(40);
		require(runtime.conversations.at(rid).prompt.answer == "yes", "AI follows authored choice");
		auto malformed = runtime.save();
		malformed["threads"][0]["messages"][0]["delivered"].push_back("unknown");
		auto before = runtime.save();
		bool rejected = false;
		try
		{
			runtime.restore(malformed);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		require(rejected && runtime.save() == before, "invalid saved recipients rejected atomically");
		auto timeout = continued;
		timeout.endpoints[1].working = false;
		timeout.advance(200);
		require(timeout.conversations.at(rid).prompt.answer == "no", "disconnected deadline follows authored default");

		auto joining_source = fixture();
		joining_source["trees"].back()["states"][1]["entry"].insert(
		    joining_source["trees"].back()["states"][1]["entry"].begin(),
		    Json{{"action", "add_participant"}, {"arguments", {"captain"}}});
		auto joining_catalog = load(joining_source);
		ConversationRuntime joining;
		joining.participants = runtime.participants;
		joining.endpoints = runtime.endpoints;
		joining.endpoints[1].human = true;
		joining.programs["test.dialogue"] =
		    compile_conversation(joining_catalog.trees.at("test.dialogue"), joining_catalog.trees);
		auto jid = joining.open("test.dialogue", {{"caller", "ship:1"}, {"target", "captain"}, {"captain", "ship:3"}},
		                        {"ship:1", "captain"});
		for (int i = 1; i <= 10; ++i)
			joining.advance(i);
		require(joining.project(3).at("threads")[0].at("messages").size() == 1,
		        "scripted join excludes earlier transcript");
		require(joining.project(1).at("threads")[0].at("messages").size() == 2,
		        "existing member retains earlier transcript");
		auto expiring = joining;
		expiring.conversations.clear();
		expiring.programs["test.dialogue"].lines.at("hello").expiry = 1;
		expiring.open("test.dialogue", {{"caller", "ship:1"}, {"target", "captain"}, {"captain", "ship:3"}},
		              {"ship:1", "captain"});
		expiring.endpoints[1].working = false;
		for (int i = 11; i <= 22; ++i)
			expiring.advance(i);
		expiring.endpoints[1].working = true;
		expiring.advance(23);
		require(expiring.project(1).at("threads")[0].at("messages").size() == 1,
		        "expired message is not delivered after reconnection");
		auto bad_source = fixture();
		bad_source["trees"].back()["lines"]["hello"]["speaker"] = "missing";
		rejected = false;
		try
		{
			load(bad_source);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		require(rejected, "unbound dialogue speaker rejected by production verifier");

		auto trigger_source = fixture();
		trigger_source["trees"].back()["states"][0]["transitions"][0]["conditions"] =
		    Json::array({{{"left", {{"observation", "event"}}}, {"op", "eq"}, {"right", "radio-trigger"}}});
		auto trigger_catalog = load(trigger_source);
		ConversationRuntime triggered;
		triggered.participants = joining.participants;
		triggered.endpoints = joining.endpoints;
		triggered.programs["test.dialogue"] =
		    compile_conversation(trigger_catalog.trees.at("test.dialogue"), trigger_catalog.trees);
		auto tid =
		    triggered.open("test.dialogue", {{"caller", "ship:1"}, {"target", "captain"}}, {"ship:1", "captain"});
		for (int i = 1; i < 20; ++i)
			triggered.advance(i);
		require(!triggered.conversations.at(tid).prompt.id, "dialogue waits for its trigger");
		triggered.event(tid, "radio-trigger");
		auto triggered_resume = triggered;
		triggered_resume.restore(triggered.save());
		triggered.advance(20);
		triggered_resume.advance(20);
		triggered.advance(21);
		triggered_resume.advance(21);
		require(triggered.save() == triggered_resume.save() && triggered.conversations.at(tid).prompt.id,
		        "queued trigger resumes and advances the state tree");
		// A late script refusal must discard earlier outputs, disclosure and continuation.
		auto failing_source = fixture();
		failing_source["trees"].back()["states"][0]["entry"] =
		    Json::array({{{"action", "say"}, {"arguments", {"hello"}}},
		                 {{"action", "reveal"}, {"arguments", {"captain"}}},
		                 {{"action", "add_participant"}, {"arguments", {"captain"}}},
		                 {{"action", "remove_participant"}, {"arguments", {"target"}}},
		                 {{"action", "offer"}, {"arguments", {"question"}}}});
		auto failing_catalog = load(failing_source);
		ConversationRuntime failing;
		failing.programs = failing_catalog.conversation_programs;
		failing.participants = runtime.participants;
		failing.endpoints = runtime.endpoints;
		failing.open("test.dialogue", {{"caller", "ship:1"}, {"target", "captain"}, {"captain", "ship:3"}},
		             {"ship:1", "captain"});
		failing.tick = 1;
		const auto before_failure = failing.save();
		rejected = false;
		try
		{
			failing.advance(1);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		require(rejected && failing.save() == before_failure, "failed script outputs leave no partial changes");

		const auto before_check = continued.save();
		const auto pending = continued.conversations.at(rid).prompt.id;
		require(continued.answer(1, rid, pending, "yes", false), "answer admission succeeds");
		require(continued.save() == before_check, "answer admission preserves prompt and transcript");
		continued.disclose(1, "ship:3");
		const auto before_hail = continued.save();
		require(continued.hail(1, "ship:3", false) != 0 && continued.save() == before_hail,
		        "direct hail admission preserves runtime state");
		const auto chat = continued.hail(1, "ship:3");
		const auto before_text = continued.save();
		require(continued.send_text(1, chat, "draft", false) && continued.save() == before_text,
		        "text admission preserves transcript and identities");

		auto projected = view();
		projected["selected"] = 0;
		const auto compact = conversation_projection::board(projected);
		for (std::size_t i = 0; i < projected.at("threads").size(); ++i)
		{
			const auto &original = projected.at("threads")[i].at("messages");
			const auto &summary = compact.at("threads")[i];
			require(summary.at("messages").size() == std::min<std::size_t>(1, original.size()) &&
			            summary.at("omitted_messages").get<std::size_t>() + summary.at("messages").size() ==
			                original.size(),
			        "unselected thread retains only its latest message with an omission count");
			if (!original.empty())
				require(summary.at("messages").back() == original.back(), "thread summary retains the latest message");
		}
		require(conversation_projection::board(compact) == compact, "bounded transcript projection is idempotent");
		auto authority = cmd(1, "hail", 0, "ship:2");
		OperatingCommand disable{1, "system_enabled", 0, "", "comms", "enabled", authority.generation};
		disable.console = "drive";
		require(world.submit(1, disable), "disable physical Comms controller");
		world.tick();
		require(!world.submit(1, cmd(1, "hail", 0, "ship:2")), "unpowered or disabled Comms cannot hail");
		std::cout << "Comms production-loader, authority, timing and continuation contracts passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
