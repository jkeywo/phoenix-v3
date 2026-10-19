#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

import phoenix.simulation;
import phoenix.console;
import phoenix.session.execution;

using namespace phoenix;
using namespace phoenix::test;

namespace
{
Json controller(const World &world, std::uint64_t claimant, const std::string &id)
{
	const auto observation = world.observe(claimant);
	require(observation.has_value(), "claimed Console has a permitted projection");
	for (const auto &value : observation->gameplay.at("controllers"))
		if (value.at("id") == id)
			return value;
	throw std::runtime_error("Controller absent from owning Console projection: " + id);
}
OperatingCommand speed(const World &world, std::uint64_t claimant, const std::string &id, std::int64_t value)
{
	OperatingCommand command{1, {}, value, {}};
	command.controller = id;
	command.channel = "speed";
	command.generation = controller(world, claimant, id).at("generation").get<std::uint64_t>();
	return command;
}
std::int64_t requested(const World &world, const std::string &id)
{
	const auto observation = world.observe(101);
	require(observation.has_value(), "physical observation has an owning Console");
	for (const auto &system : observation->systems)
		if (system.id == id)
			return system.requested;
	throw std::runtime_error("Bound physical observation absent: " + id);
}
} // namespace

int main()
{
	try
	{
		const auto content = load(controller_fixture());
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "Consoles claim independently");
		auto command = speed(world, 101, "propulsion", 5000);
		require(!world.submit(102, command), "another Console cannot write a Controller section");
		auto foreign = command;
		foreign.target = 999;
		require(!world.submit(101, foreign), "Controller identity never authorises a foreign entity");
		require(world.submit(101, command), "own Controller accepts a valid human proposal");
		world.tick();
		require(requested(world, "port") == 5000 && requested(world, "starboard") == 5000,
		        "one committed setting reaches both physical engines");
		require(controller(world, 101, "propulsion").at("commands").at("speed").at("value") == 5000,
		        "accepted intent is exposed separately from physical observations");

		const auto old_command = speed(world, 101, "propulsion", 9000);
		require(world.submit(101, old_command) && world.submit(101, {1, "preset", 1, {}}),
		        "queued command and profile transition are admitted before commit");
		world.tick();
		require(!controller(world, 101, "propulsion").at("human").get<bool>(), "profile delegates the Controller");
		require(requested(world, "port") == 5000 && !world.submit(101, old_command),
		        "handover invalidates obsolete writes without erasing accepted settings");
		require(world.submit(101, {1, "preset", 0, {}}), "Console returns to selected direct profile");
		world.tick();
		require(!world.submit(101, old_command), "previous human generation remains invalid after return");
		require(world.submit(101, speed(world, 101, "propulsion", 7000)), "fresh operator generation can write");
		world.tick();

		const auto saved = world.save();
		require(saved.at("schema") == 5, "Controller continuation has an explicit new save schema");
		World resumed;
		resumed.restore(content, saved);
		for (int tick = 0; tick < 12; ++tick)
		{
			world.tick();
			resumed.tick();
		}
		require(world.save() == resumed.save(), "shared settings and physical continuation survive restore");
		auto forged = saved;
		forged["bodies"][0]["console_controllers"]["controllers"][0]["human"] = false;
		const auto before_refusal = resumed.save();
		rejects(
		    [&]
		    {
			    resumed.restore(content, forged);
		    },
		    "saved operator must agree with claims and profile");
		require(resumed.save() == before_refusal, "malformed Controller restore is transactional");

		World disconnected;
		disconnected.restore(content, saved);
		auto obsolete = speed(disconnected, 101, "propulsion", 11000);
		require(disconnected.submit(101, obsolete), "human command queues before recovery clears claims");
		disconnected.clear_claims();
		disconnected.tick();
		require(disconnected.claim(101, 1, "drive"), "Console can acquire a fresh claim after recovery");
		require(requested(disconnected, "port") == 7000 && !disconnected.submit(101, obsolete),
		        "clearing claims cancels queued writes and resets the effective operator generation");
		require(disconnected.submit(101, {1, "away", 1, {}}), "Away is a Console availability transition");
		disconnected.tick();
		require(!controller(disconnected, 101, "propulsion").at("human").get<bool>(), "Away uses ordinary AI");
		require(disconnected.submit(101, {1, "away", 0, {}}), "human can return from Away");
		disconnected.tick();
		require(controller(disconnected, 101, "propulsion").at("human").get<bool>() &&
		            requested(disconnected, "port") == 7000,
		        "return restores profile allocation and accepted setting");

		World split;
		split.load(load(controller_fixture(true)), "test.map");
		require(split.claim(101, 1, "drive"), "split throttle Console claims through ordinary route");
		require(split.submit(101, speed(split, 101, "port_drive", 3000)) &&
		            split.submit(101, speed(split, 101, "starboard_drive", 8000)),
		        "separate sections accept separate intent");
		split.tick();
		require(requested(split, "port") == 3000 && requested(split, "starboard") == 8000,
		        "independent Controllers cannot overwrite each other's throttle");

		auto request_source = controller_fixture();
		request_source["entities"][0]["systems"]["port"]["impulse"] = {{"charge_ticks", 20}};
		request_source["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "impulse"}, {"action", "impulse"}, {"consumers", {"port"}}});
		const auto request_content = load(request_source);
		World requests;
		requests.load(request_content, "test.map");
		require(requests.claim(101, 1, "drive"), "request fixture claims the ordinary Console");
		auto request = speed(requests, 101, "propulsion", 1);
		request.channel = "impulse";
		request.request_id = 1;
		require(requests.submit(101, request) && requests.submit(101, request),
		        "duplicate request identity is idempotent");
		auto concurrent = request;
		concurrent.request_id = 2;
		require(!requests.submit(101, concurrent), "a second outstanding request cannot silently replace the first");
		requests.tick();
		const auto receipt = controller(requests, 101, "propulsion").at("commands").at("impulse");
		require(receipt.at("accepted").get<bool>(), "request reaches its single physical executor");
		require(requests.submit(101, request), "completed request retry returns its prior outcome");
		requests.tick();
		require(controller(requests, 101, "propulsion").at("commands").at("impulse").at("revision") ==
		            receipt.at("revision"),
		        "retry does not create a second physical request revision");
		World request_resumed;
		request_resumed.restore(request_content, requests.save());
		require(request_resumed.submit(101, request), "request identity survives restore");
		request_resumed.tick();
		requests.tick();
		require(request_resumed.save() == requests.save(), "restored retry cannot restart physical work");

		// An autonomous generic entity uses a real decision program and the same command board as a human.
		World automatic, human;
		auto ai_source = controller_fixture();
		ai_source["trees"] =
		    Json::array({{{"id", "test.propulsion"},
		                  {"initial", "run"},
		                  {"states", {{"run", {{"speed_mm_s", 5000}, {"ticks", 10}, {"next", "run"}}}}}}});
		auto &ai_controller = ai_source["entities"][0]["control_layout"]["controllers"][0];
		ai_controller.erase("fallback");
		ai_controller["tree"] = "test.propulsion";
		const auto ai_content = load(ai_source);
		automatic.load(ai_content, "test.map");
		human.load(content, "test.map");
		require(human.claim(101, 1, "drive") && human.submit(101, speed(human, 101, "propulsion", 5000)),
		        "equivalent human command admitted");
		for (int tick = 0; tick < 20; ++tick)
		{
			automatic.tick();
			human.tick();
			const auto a = automatic.inspect().at(0), b = human.inspect().at(0);
			require(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy,
			        "autonomous and human-operated entities use identical bound physical execution");
		}
		World ai_resumed;
		ai_resumed.restore(ai_content, automatic.save());
		for (int tick = 0; tick < 11; ++tick)
		{
			automatic.tick();
			ai_resumed.tick();
		}
		if (automatic.save() != ai_resumed.save())
			std::cerr << Json::diff(automatic.save(), ai_resumed.save()).dump(2) << '\n';
		require(automatic.save() == ai_resumed.save(),
		        "Controller decision continuation resumes independently of equipment");

		auto contextual_source = controller_fixture();
		contextual_source["trees"] = Json::parse(R"json([
		    {"id":"test.contextual","context":"controller","initial":"root",
		     "command_bindings":[{"channel":"speed","type":"integer","semantics":"setting"}],
		     "memory":[{"name":"remembered","type":"integer","initial":5000}],
		     "states":[
		       {"id":"root","initial":"operate"},
		       {"id":"operate","parent":"root","running":[
		         {"action":"write.speed","arguments":[{"memory":"remembered"}]},
		         {"memory":"remembered","value":{"intent":"speed"}}
		       ]}
		     ]}
		])json");
		auto &contextual_definition = contextual_source["entities"][0]["control_layout"]["controllers"][0];
		contextual_definition.erase("fallback");
		contextual_definition["tree"] = "test.contextual";
		const auto contextual_content = load(contextual_source);
		World contextual, direct;
		contextual.load(contextual_content, "test.map");
		direct.load(content, "test.map");
		require(direct.claim(101, 1, "drive") && direct.submit(101, speed(direct, 101, "propulsion", 5000)),
		        "human supplies same setting as contextual Controller");
		for (int tick = 0; tick < 5; ++tick)
		{
			contextual.tick();
			direct.tick();
			const auto a = contextual.inspect().at(0), b = direct.inspect().at(0);
			require(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy,
			        "hierarchical Controller AI and human use equivalent admitted commands");
		}
		const auto memory = contextual.save()
		                        .at("bodies")[0]
		                        .at("console_controllers")
		                        .at("controllers")[0]
		                        .at("brain")
		                        .at("instance")
		                        .at("memory");
		require(contextual.claim(101, 1, "drive"), "human takes over contextual Controller");
		require(!controller(contextual, 101, "propulsion").contains("memory"),
		        "private AI memory stays out of Console projection");
		require(contextual.submit(101, speed(contextual, 101, "propulsion", 7000)),
		        "human writes after tree interruption");
		contextual.tick();
		require(requested(contextual, "port") == 7000, "interrupted AI cannot overwrite human control");
		require(contextual.save()
		                .at("bodies")[0]
		                .at("console_controllers")
		                .at("controllers")[0]
		                .at("brain")
		                .at("instance")
		                .at("memory") == memory,
		        "human operation preserves the Controller's private memory");
		World contextual_resumed;
		contextual_resumed.restore(contextual_content, contextual.save());
		require(contextual.release(101) && contextual_resumed.release(101), "ordinary AI resumes on release");
		for (int tick = 0; tick < 5; ++tick)
		{
			contextual.tick();
			contextual_resumed.tick();
		}
		require(contextual.save() == contextual_resumed.save(),
		        "interrupted hierarchical continuation survives restore");

		auto observations_source = controller_fixture(true);
		observations_source["trees"] = Json::parse(R"json([
		    {"id":"test.writer","context":"controller","initial":"run",
		     "command_bindings":[{"channel":"speed"}],
		     "states":[{"id":"run","running":[{"action":"write.speed","arguments":[5000]}]}]},
		    {"id":"test.reader","context":"controller","initial":"run",
		     "command_bindings":[{"channel":"speed"}],
		     "system_bindings":[{"name":"port_requested","system":"port","field":"requested","type":"integer"}],
		     "states":[{"id":"run","running":[{"action":"write.speed","arguments":[{"observation":"port_requested"}]}]}]}
		])json");
		auto &observation_layout = observations_source["entities"][0]["control_layout"];
		observation_layout["default_profile"] = "automatic";
		observation_layout["controllers"][0].erase("fallback");
		observation_layout["controllers"][0]["tree"] = "test.writer";
		observation_layout["controllers"][1].erase("fallback");
		observation_layout["controllers"][1]["tree"] = "test.reader";
		World observations;
		observations.load(load(observations_source), "test.map");
		require(observations.claim(101, 1, "drive") && observations.claim(102, 1, "observer"),
		        "human can observe a fully AI Console without changing its profile");
		const auto before_tick = requested(observations, "port");
		observations.tick();
		require(requested(observations, "port") == 5000 && requested(observations, "starboard") == before_tick,
		        "later Controller sees frozen pre-execution observations, not earlier Controller's physical effects");
		const auto human_port = requested(observations, "port");
		observations.tick();
		require(requested(observations, "starboard") == human_port,
		        "Controller reads the same permitted System observation presented to the human");
		require(observations.observe(102)->systems.empty() && observations.observe(102)->contacts.empty(),
		        "another Console does not inherit physical observations from a co-hosted Console");
		auto foreign_feed = observations_source;
		foreign_feed["entities"][0]["stations"][0]["consoles"][0]["feeds"] = {"starboard"};
		rejects(
		    [&]
		    {
			    load(foreign_feed);
		    },
		    "Controller System observation must belong to the Console's feed roster");
		auto wrong_type = observations_source;
		wrong_type["trees"][1]["system_bindings"][0]["type"] = "boolean";
		wrong_type["trees"][1]["states"][0]["running"][0]["arguments"] = {5000};
		rejects(
		    [&]
		    {
			    load(wrong_type);
		    },
		    "System observation type cannot reinterpret requested speed as a boolean");
		auto forbidden = contextual_source;
		forbidden["trees"][0]["command_bindings"].push_back({{"channel", "foreign"}, {"type", "integer"}});
		rejected(
		    [&]
		    {
			    load(forbidden);
		    },
		    "unowned command channel");
		forbidden = contextual_source;
		forbidden["trees"][0]["states"][1]["running"][0]["arguments"] = Json::array({{{"observation", "self.x"}}});
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "Controller trees cannot acquire direct physical observations");
		forbidden = contextual_source;
		forbidden["trees"][0]["context"] = "scenario";
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "scenario context cannot operate a Console Controller");
		forbidden = contextual_source;
		forbidden["knowledge"] = Json::parse(R"json({
		    "roles":["drive","private"],
		    "schemas":[{"id":"test.secret","producers":["private"],"readers":["private"],
		      "fields":[{"id":"amount","type":"integer"}]}]
		})json");
		forbidden["entities"][0]["stations"][0]["consoles"][0]["knowledge_roles"] = {"drive"};
		forbidden["trees"][0]["record_bindings"] =
		    Json::array({{{"name", "secret"}, {"scope", "console"}, {"schema", "test.secret"}, {"field", "amount"}}});
		rejected(
		    [&]
		    {
			    load(forbidden);
		    },
		    "record binding forbidden to owning Console");
		forbidden["trees"][0]["record_bindings"][0]["scope"] = "ship";
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "entity-wide read cannot bypass private-field publication policy");

		const auto session_archive = archive(controller_session_fixture());
		const GameSetup setup{"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"};
		auto local = make_local_session();
		local->add_pack(session_archive);
		local->start(setup);
		ConsoleModel model;
		model.refresh(local);
		std::string increase;
		for (const auto &[id, label] : model.gameplay_controls())
			if (label == "propulsion speed 850")
				increase = id;
		require(!increase.empty() && model.act(increase),
		        "ConsoleModel activates a Controller descriptor through SessionPort");
		local->tick();
		require(local->observation()->gameplay.at("controllers")[0].at("commands").at("speed").at("value") == 850,
		        "presentation command retains Controller/channel/generation through local execution");
		auto recovered = make_local_session();
		recovered->restore(local->save());
		require(recovered->recovery_pending() && !recovered->observation(),
		        "portable Controller recovery drops live authority");
		recovered->resume_local();
		require(recovered->observation()->gameplay.at("controllers")[0].at("commands").at("speed").at("value") == 850,
		        "portable recovery retains committed Controller settings");
		model.refresh(recovered);
		bool profile_changed = false;
		for (const auto &[id, label] : model.gameplay_controls())
			if (label == "automatic")
				profile_changed = model.act(id);
		require(profile_changed, "Console settings expose profile selection through the presentation model");
		recovered->tick();
		require(!recovered->observation()->gameplay.at("controllers")[0].at("human").get<bool>(),
		        "Console profile action changes effective Controller operator");

		auto host = make_host_world();
		host->add_pack(session_archive);
		host->start_server(setup);
		GameExecution execution(host, {{"server", "test.slot"}});
		const auto ship_id = *host->authoritative_world().ship_for_slot("test.slot");
		const std::string lease(64, 'a');
		require(execution.apply("server", {{"kind", "station-claim"},
		                                   {"owner", "crew"},
		                                   {"route", 1},
		                                   {"ship", ship_id},
		                                   {"seat", "drive"},
		                                   {"lease", lease}}),
		        "committed execution grants an authenticated Console lease");
		const auto projected = execution.observe("server", "crew");
		OperatingCommand framed{ship_id, {}, 4200, {}};
		framed.controller = "propulsion";
		framed.channel = "speed";
		framed.generation = projected->gameplay.at("controllers")[0].at("generation").get<std::uint64_t>();
		auto frame = encode_operating_command(framed);
		frame.update({{"kind", "command"}, {"owner", "crew"}, {"route", 1}, {"lease", lease}, {"sequence", 1}});
		require(execution.apply("server", frame) && !execution.apply("server", frame),
		        "committed command frames preserve Controller metadata and reject sequence replay");
		require(execution.apply("server", {{"kind", "tick"}, {"tick", 1}}), "committed tick executes routed command");
		require(
		    execution.observe("server", "crew")->gameplay.at("controllers")[0].at("commands").at("speed").at("value") ==
		        4200,
		    "frame execution reaches the shared command section");
		execution.absent("server");
		frame["sequence"] = 2;
		require(!execution.apply("server", frame), "server absence cannot revive a stale Controller lease");
		std::cout << "PASS Controller sections, physical subscriptions, operator generations and continuation\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
