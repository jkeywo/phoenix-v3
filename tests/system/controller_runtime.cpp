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
		auto command_read_source = observations_source;
		command_read_source["trees"][1].erase("system_bindings");
		command_read_source["trees"][1]["command_reads"] = Json::array(
		    {{{"name", "port_requested"}, {"controller", "port_drive"}, {"channel", "speed"}, {"type", "integer"}}});
		World command_reads;
		command_reads.load(load(command_read_source), "test.map");
		require(command_reads.claim(101, 1, "drive"), "Console can observe shared Controller intent");
		command_reads.tick();
		require(controller(command_reads, 101, "port_drive").at("commands").at("speed").at("value") == 5000 &&
		            controller(command_reads, 101, "starboard_drive").at("commands").at("speed").at("value") == 0,
		        "one Controller cannot read another Controller's same-tick command write");
		command_reads.tick();
		require(controller(command_reads, 101, "starboard_drive").at("commands").at("speed").at("value") == 5000,
		        "same-Console command read receives prior committed intent");
		auto bad_command_read = command_read_source;
		bad_command_read["trees"][1]["command_reads"][0]["channel"] = "unknown";
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "Controller read must reference an existing command channel");
		bad_command_read = command_read_source;
		bad_command_read["trees"][1]["command_reads"][0]["type"] = "boolean";
		bad_command_read["trees"][1]["states"][0]["running"][0]["arguments"] = {5000};
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "Controller read cannot reinterpret the channel type");
		bad_command_read = command_read_source;
		bad_command_read["entities"][0]["control_layout"]["controllers"][1]["console"] = "observer";
		for (auto &profile : bad_command_read["entities"][0]["control_layout"]["profiles"])
		{
			const auto key = profile["id"] == "direct" ? "human" : "ai";
			profile["consoles"]["drive"][key] = {"port_drive"};
			profile["consoles"]["observer"][key] = {"starboard_drive"};
		}
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "co-hosted Consoles cannot read each other's private command sections");
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

		auto messages_source = controller_fixture(true);
		messages_source["schema"] = 4;
		messages_source["knowledge"] = Json::parse(R"json({"roles":["crew"],"schemas":[
		 {"id":"request","producers":["crew"],"readers":["crew"],"fields":[{"id":"amount","type":"integer"}]}]})json");
		auto &message_entity = messages_source["entities"][0];
		for (auto &console : message_entity["stations"][0]["consoles"])
			console["knowledge_roles"] = {"crew"};
		auto &message_layout = message_entity["control_layout"];
		message_layout["controllers"][0]["id"] = "port";
		message_layout["controllers"][1]["id"] = "port";
		message_layout["controllers"][1]["console"] = "observer";
		message_layout["profiles"] = Json::parse(R"json([
		 {"id":"direct","consoles":{"drive":{"human":["port"]},"observer":{"human":["port"]}}},
		 {"id":"automatic","consoles":{"drive":{"ai":["port"]},"observer":{"ai":["port"]}}}])json");
		message_entity["message_templates"] = {"test.request"};
		messages_source["messages"] = Json::parse(R"json([
		 {"id":"test.request","schema":"request","title_key":"test.request.title","body_key":"test.request.body",
		 "destination":{"kind":"controller","console":"drive","id":"port"},"delay_ticks":3,"expires_after_ticks":30,
		 "fields":{"amount":7},"parameters":{},"automatic":false}])json");
		const auto message_content = load(messages_source);
		World messaging;
		messaging.load(message_content, "test.map");
		require(messaging.claim(101, 1, "drive") && messaging.claim(102, 1, "observer"), "message Consoles claimed");
		require(messaging.submit(101, {1, "preset", 1, {}}), "recipient delegates before send");
		messaging.tick();
		OperatingCommand send{1, "message.test.request", 7, {}};
		send.controller = "port";
		send.generation = controller(messaging, 102, "port").at("generation").get<std::uint64_t>();
		require(messaging.submit(102, send), "human Controller sends permitted intent");
		messaging.tick();
		require(messaging.observe(101)->gameplay.at("messages").empty(), "Controller delivery respects authored delay");
		World message_resume;
		message_resume.restore(message_content, messaging.save());
		for (auto *run : {&messaging, &message_resume})
		{
			require(run->submit(101, {1, "preset", 0, {}}), "recipient becomes human before delivery");
			bool notified = false;
			for (int tick = 0; tick < 4; ++tick)
			{
				run->tick();
				const auto view = run->observe(101)->gameplay;
				for (const auto &notice : view.at("notifications"))
					if (!view.at("messages").empty() && notice.at("sequence") == view.at("messages")[0].at("sequence"))
						notified = true;
			}
			require(notified, "delivery notifies the current human Controller operator");
		}
		const auto delivered = messaging.observe(101)->gameplay.at("messages");
		require(delivered.size() == 1 && delivered[0].at("payload").at("fields").at("amount") == 7,
		        "Controller-addressed message reaches its current human Console");
		require(messaging.observe(102)->gameplay.at("messages").empty(),
		        "same Controller name in another Console cannot intercept the message");
		require(message_resume.observe(101)->gameplay.at("messages") == delivered &&
		            message_resume.save() == messaging.save(),
		        "pending Controller message identity and routing survive restoration and handover");
		const auto bindings = messaging.save().at("knowledge").at("ships")[0].at("controllers");
		require(bindings.at("drive/port") != bindings.at("observer/port"),
		        "Controller actor identity includes Console");
		World stale_sender;
		stale_sender.load(message_content, "test.map");
		require(stale_sender.claim(101, 1, "drive") && stale_sender.claim(102, 1, "observer"), "stale sender claims");
		send.generation = controller(stale_sender, 102, "port").at("generation").get<std::uint64_t>();
		require(stale_sender.submit(102, {1, "preset", 1, {}}) && stale_sender.submit(102, send),
		        "message admitted behind an uncommitted profile transition");
		for (int tick = 0; tick < 5; ++tick)
			stale_sender.tick();
		require(stale_sender.observe(101)->gameplay.at("messages").empty(),
		        "execution rechecks message sender generation after profile transition");
		auto ai_messages_source = messages_source;
		ai_messages_source["messages"][0]["fields"]["amount"] = {{"argument", "amount"}};
		ai_messages_source["trees"] = Json::parse(R"json([
		 {"id":"test.sender","context":"controller","initial":"send","message_templates":["test.request"],
		  "states":[{"id":"send","entry":[{"action":"send.test.request","arguments":[6000,false,"intent"]}]}]},
		 {"id":"test.receiver","context":"controller","initial":"wait","command_bindings":[{"channel":"speed"}],
		  "message_bindings":[{"name":"amount","schema":"request","field":"amount"}],
		  "states":[{"id":"wait","transitions":[{"target":"received","conditions":[
		    {"left":{"observation":"message.available"},"op":"eq","right":true}]}]},
		   {"id":"received","entry":[{"action":"write.speed","arguments":[{"observation":"amount"}]},
		    {"action":"acknowledge_message","arguments":[{"observation":"message.id"}]}]}]}
		])json");
		ai_messages_source["entities"][0]["control_layout"]["controllers"][0]["tree"] = "test.receiver";
		ai_messages_source["entities"][0]["control_layout"]["controllers"][1]["tree"] = "test.sender";
		const auto ai_message_content = load(ai_messages_source);
		World ai_messages;
		ai_messages.load(ai_message_content, "test.map");
		require(ai_messages.claim(101, 1, "drive") && ai_messages.claim(102, 1, "observer"), "AI message claims");
		require(ai_messages.submit(102, {1, "preset", 1, {}}), "sender delegates to AI");
		for (int tick = 0; tick < 6; ++tick)
			ai_messages.tick();
		require(ai_messages.observe(101)->gameplay.at("messages").size() == 1 && requested(ai_messages, "port") == 0,
		        "AI sends once while human recipient retains command control");
		require(ai_messages.observe(102)->gameplay.at("messages").empty(), "same-named sender has no recipient inbox");
		World ai_message_resume;
		ai_message_resume.restore(ai_message_content, ai_messages.save());
		for (auto *run : {&ai_messages, &ai_message_resume})
		{
			require(run->submit(101, {1, "preset", 1, {}}), "human yields with delivered message history intact");
			for (int tick = 0; tick < 4; ++tick)
				run->tick();
			require(requested(*run, "port") == 6000 && run->observe(101)->gameplay.at("messages").size() == 1,
			        "recipient AI reads typed payload and acknowledges without duplicating message effects");
		}
		require(ai_messages.save() == ai_message_resume.save(),
		        "AI send/receive continuation survives handover restore");
		const auto consumed = ai_messages.save();
		const auto receiver_id = consumed.at("knowledge").at("ships")[0].at("controllers").at("drive/port");
		bool acknowledged = false;
		for (const auto &memory : consumed.at("knowledge").at("runtime").at("memory"))
			if (memory.at("id") == receiver_id)
				acknowledged =
				    memory.at("cursor") == ai_messages.observe(101)->gameplay.at("messages")[0].at("sequence");
		require(acknowledged, "acknowledge action advances recipient Controller continuation");
		World consumed_resume;
		consumed_resume.restore(ai_message_content, consumed);
		for (int tick = 0; tick < 4; ++tick)
		{
			ai_messages.tick();
			consumed_resume.tick();
		}
		require(ai_messages.save() == consumed_resume.save(),
		        "acknowledged inbox and effect journal survive restoration");
		auto forbidden_message = ai_messages_source;
		forbidden_message["entities"][0]["message_templates"] = Json::array();
		rejects(
		    [&]
		    {
			    load(forbidden_message);
		    },
		    "Controller cannot send template absent from entity authority");
		forbidden_message = ai_messages_source;
		forbidden_message["entities"][0]["stations"][0]["consoles"][0]["knowledge_roles"] = Json::array();
		rejects(
		    [&]
		    {
			    load(forbidden_message);
		    },
		    "Controller cannot bind inbox payload forbidden to owning Console");
		auto public_source = ai_messages_source;
		public_source["knowledge"]["schemas"][0]["ship_publishable"] = true;
		public_source["knowledge"]["schemas"][0]["fields"][0]["publishable"] = true;
		public_source["entities"][0]["stations"][0]["consoles"].push_back({{"id", "spectator"},
		                                                                   {"definition", "test.console"},
		                                                                   {"systems", Json::array()},
		                                                                   {"knowledge_roles", {"crew"}}});
		public_source["messages"][0]["urgency"] = 2;
		public_source["messages"][0]["automatic"] = true;
		const auto public_content = load(public_source);
		auto claim_public = [&](World &run)
		{
			require(run.claim(101, 1, "drive") && run.claim(102, 1, "observer") && run.claim(103, 1, "spectator"),
			        "public messaging claims");
		};
		World public_messages;
		public_messages.load(public_content, "test.map");
		claim_public(public_messages);
		require(public_messages.submit(101, {1, "preset", 1, {}}) && public_messages.submit(102, {1, "preset", 1, {}}),
		        "important message sender and recipient use AI");
		public_messages.tick();
		World public_resume;
		public_resume.restore(public_content, public_messages.save());
		for (auto *run : {&public_messages, &public_resume})
			for (int tick = 0; tick < 6; ++tick)
				run->tick();
		const auto public_view = public_messages.observe(103)->gameplay;
		const auto target_view = public_messages.observe(101)->gameplay;
		require(public_view.at("messages").size() == 1 && public_view.at("shared").size() == 1,
		        "important AI-to-AI message reaches spectator history and shared entity board");
		require(public_view.at("messages")[0].at("id") == target_view.at("messages")[0].at("id") &&
		            public_view.at("messages")[0].at("public") == true &&
		            public_view.at("messages")[0].at("offer_only") == true &&
		            public_view.at("messages")[0].at("actionable") == false &&
		            target_view.at("messages")[0].at("actionable") == true,
		        "public copy preserves identity without broadening Controller actionability");
		require(public_messages.save() == public_resume.save(), "pending public delivery restores exactly");
		World published_resume;
		published_resume.restore(public_content, public_messages.save());
		require(published_resume.observe(103)->gameplay == public_view,
		        "delivered public flags and board survive restore");
		auto forged_public_save = public_messages.save();
		const auto spectator_id = forged_public_save.at("knowledge").at("ships")[0].at("consoles").at("spectator");
		for (auto &delivery : forged_public_save["knowledge"]["runtime"]["deliveries"])
			if (delivery.at("console") == spectator_id)
				delivery["offer_only"] = false;
		rejects(
		    [&]
		    {
			    published_resume.restore(public_content, forged_public_save);
		    },
		    "restore cannot upgrade public spectator copy to addressed actionability");
		World human_target;
		human_target.load(public_content, "test.map");
		claim_public(human_target);
		require(human_target.submit(101, {1, "preset", 1, {}}) && human_target.submit(102, {1, "preset", 1, {}}),
		        "important message initially targets AI");
		human_target.tick();
		require(human_target.submit(101, {1, "preset", 0, {}}), "recipient becomes human before delayed delivery");
		for (int tick = 0; tick < 6; ++tick)
			human_target.tick();
		require(human_target.observe(101)->gameplay.at("messages").size() == 1 &&
		            human_target.observe(103)->gameplay.at("messages").empty() &&
		            human_target.observe(103)->gameplay.at("shared").empty(),
		        "current human recipient suppresses automatic AI-to-AI public copy");
		auto ordinary_source = public_source;
		ordinary_source["messages"][0]["urgency"] = 1;
		World ordinary_messages;
		ordinary_messages.load(load(ordinary_source), "test.map");
		claim_public(ordinary_messages);
		require(ordinary_messages.submit(101, {1, "preset", 1, {}}) &&
		            ordinary_messages.submit(102, {1, "preset", 1, {}}),
		        "ordinary AI message operators");
		for (int tick = 0; tick < 6; ++tick)
			ordinary_messages.tick();
		require(ordinary_messages.observe(103)->gameplay.at("messages").empty(),
		        "ordinary AI coordination stays targeted");
		auto broadcast_source = public_source;
		broadcast_source["messages"][0]["destination"] = {{"kind", "ship"}};
		broadcast_source["messages"][0]["urgency"] = 0;
		World broadcast;
		broadcast.load(load(broadcast_source), "test.map");
		claim_public(broadcast);
		send.generation = controller(broadcast, 102, "port").at("generation").get<std::uint64_t>();
		require(broadcast.submit(102, send), "human Controller sends entity-wide message");
		for (int tick = 0; tick < 6; ++tick)
			broadcast.tick();
		require(broadcast.observe(103)->gameplay.at("messages").size() == 1 &&
		            broadcast.observe(103)->gameplay.at("shared").size() == 1,
		        "human entity-wide message publishes regardless urgency");
		auto private_publication = public_source;
		private_publication["knowledge"]["schemas"][0]["ship_publishable"] = false;
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "important message cannot publish private schema");
		private_publication = public_source;
		private_publication["knowledge"]["schemas"][0]["fields"][0]["publishable"] = false;
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "important message cannot publish private field");
		private_publication = public_source;
		private_publication["messages"][0]["parameters"]["secret"] = {{"argument", "detail"}};
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "public presentation cannot expose argument absent from public payload");
		auto reporter_source = ai_messages_source;
		auto &reporter_entity = reporter_source["entities"][0];
		reporter_entity["control_layout"]["controllers"][0].erase("tree");
		reporter_entity["control_layout"]["controllers"][1]["tree"] = "test.receiver";
		reporter_source["trees"].erase(0);
		reporter_source["messages"][0]["destination"]["console"] = "observer";
		reporter_source["messages"][0]["delay_ticks"] = 1;
		auto recovery_template = reporter_source["messages"][0];
		recovery_template["id"] = "test.recovered";
		reporter_source["messages"].push_back(recovery_template);
		reporter_entity["message_templates"].push_back("test.recovered");
		reporter_entity["systems"]["port"]["reporters"] = Json::parse(R"json([
		 {"id":"high","field":"requested","above":4000,"message":"test.request","recovery_message":"test.recovered",
		  "debounce_ticks":2,"recovery_ticks":2,"minimum_gap_ticks":5}])json");
		const auto reporter_content = load(reporter_source);
		auto initially_hot_source = reporter_source;
		initially_hot_source["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["initial"] = 6000;
		World initially_hot;
		initially_hot.load(load(initially_hot_source), "test.map");
		require(initially_hot.claim(101, 1, "drive") && initially_hot.claim(102, 1, "observer"), "initial hot claims");
		initially_hot.tick();
		initially_hot.tick();
		require(initially_hot.observe(102)->gameplay.at("messages").empty(), "first hot sample also obeys debounce");
		for (int tick = 0; tick < 3; ++tick)
			initially_hot.tick();
		require(initially_hot.observe(102)->gameplay.at("messages").size() == 1,
		        "initial sustained warning eventually reports");
		World reporting;
		reporting.load(reporter_content, "test.map");
		require(reporting.claim(101, 1, "drive") && reporting.claim(102, 1, "observer"), "reporter claims");
		require(reporting.submit(102, {1, "preset", 1, {}}), "report recipient delegates to AI");
		reporting.tick();
		require(reporting.observe(102)->gameplay.at("messages").empty(), "initial healthy telemetry is not recovery");
		require(reporting.submit(101, speed(reporting, 101, "port", 6000)), "human controls reporting physical System");
		reporting.tick();
		reporting.tick();
		require(reporting.observe(102)->gameplay.at("messages").empty(), "warning waits for configured debounce");
		for (int tick = 0; tick < 4; ++tick)
			reporting.tick();
		const auto warning_history = reporting.observe(102)->gameplay.at("messages");
		require(warning_history.size() == 1 && requested(reporting, "starboard") == 6000,
		        "always-active physical report reaches AI while producer remains human controlled");
		require(warning_history[0].at("payload").at("fields").at("amount") == 6000,
		        "report carries measured physical telemetry");
		require(warning_history[0].at("payload").at("kind") == static_cast<unsigned>(EvidenceKind::Fact) &&
		            warning_history[0].at("origin") == static_cast<unsigned>(ControlOrigin::Reporter),
		        "physical report carries fact provenance distinct from Controller intent");
		World reporting_resume;
		reporting_resume.restore(reporter_content, reporting.save());
		for (auto *run : {&reporting, &reporting_resume})
		{
			require(run->submit(101, {1, "preset", 1, {}}), "reporter producer hands over");
			for (int tick = 0; tick < 3; ++tick)
				run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 1,
			        "unchanged warning is not repeated after restore/handover");
			require(run->submit(101, {1, "preset", 0, {}}), "producer returns human");
			run->tick();
			require(run->submit(101, speed(*run, 101, "port", 0)), "physical source recovers");
			run->tick();
			run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 1, "recovery waits for hysteresis");
			for (int tick = 0; tick < 3; ++tick)
				run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 2, "stable recovery emits once");
		}
		require(reporting.save() == reporting_resume.save(), "reporter latch continuation survives restore");
		auto altered_reporter_save = reporting.save();
		altered_reporter_save["knowledge"].erase("reporter_contract");
		rejects(
		    [&]
		    {
			    reporting_resume.restore(reporter_content, altered_reporter_save);
		    },
		    "restore refuses missing reporter contract");
		require(reporting.save() == reporting_resume.save(), "failed reporter restore preserves live state");
		auto gap_source = reporter_source;
		gap_source["entities"][0]["systems"]["port"]["reporters"][0]["minimum_gap_ticks"] = 20;
		World gap_reports;
		gap_reports.load(load(gap_source), "test.map");
		require(gap_reports.claim(101, 1, "drive") && gap_reports.claim(102, 1, "observer"), "minimum-gap claims");
		gap_reports.tick();
		require(gap_reports.submit(101, speed(gap_reports, 101, "port", 6000)), "minimum-gap warning input");
		for (int tick = 0; tick < 5; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 1,
		        "first warning is not delayed by minimum gap");
		require(gap_reports.submit(101, speed(gap_reports, 101, "port", 0)), "minimum-gap recovery input");
		for (int tick = 0; tick < 5; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 1,
		        "minimum gap delays otherwise stable recovery");
		for (int tick = 0; tick < 16; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 2,
		        "recovery delivers when minimum gap expires");
		auto invalid_reporter = reporter_source;
		invalid_reporter["entities"][0]["systems"]["port"]["reporters"][0]["field"] = "heat";
		rejects(
		    [&]
		    {
			    World unavailable;
			    unavailable.load(load(invalid_reporter), "test.map");
		    },
		    "World load refuses reporter telemetry absent from fitted physical System");
		invalid_reporter = reporter_source;
		invalid_reporter["entities"][0]["systems"]["port"]["reporters"][0]["kind"] = "claim";
		rejects(
		    [&]
		    {
			    load(invalid_reporter);
		    },
		    "physical reporter cannot originate editorial claims");
		messages_source["messages"][0]["destination"]["id"] = "starboard";
		rejects(
		    [&]
		    {
			    load(messages_source);
		    },
		    "physical System cannot masquerade as a Controller recipient");

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
