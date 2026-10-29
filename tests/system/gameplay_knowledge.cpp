#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

import phoenix.console;
import phoenix.simulation;

using namespace phoenix;
namespace
{
using phoenix::test::load;
using phoenix::test::require;
void refused(const Json &definitions, const char *contract)
{
	phoenix::test::rejects(
	    [&]
	    {
		    (void)load(definitions);
	    },
	    contract);
}
Json fixture()
{
	auto definitions =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic knowledge core");
	definitions["schema"] = 4;
	definitions["consoles"][0]["widgets"] = Json::array();
	definitions["knowledge"] = Json::parse(R"json({
		"history_limit":32,
		"roles":["pilot","support"],
		"schemas":[
			{"id":"drive-status","producers":["pilot"],"readers":["pilot","support"],
			 "ship_publishable":true,"stale_after_ticks":2,
			 "fields":[{"id":"requested","type":"integer","publishable":true},
			           {"id":"enabled","type":"boolean","required":false,"publishable":true},
			           {"id":"private","type":"integer","required":false}]},
			{"id":"request","producers":["pilot"],"readers":["pilot","support"],
			 "fields":[{"id":"amount","type":"integer"}]},
			{"id":"support-private","producers":["support"],"readers":["support"],
			 "fields":[{"id":"secret","type":"integer"}]}
		]
	})json");
	definitions["trees"].push_back({{"id", "test.idle"},
	                                {"context", "controller"},
	                                {"initial", "ready"},
	                                {"states", Json::array({{{"id", "ready"}}})}});
	auto &ship = definitions["entities"][0];
	ship.erase("console");

	ship["systems"]["engine"]["kind"] = "engine";
	ship["systems"]["engine"]["observation_console"] = "helm";
	ship["systems"]["radar"]["observation_console"] = "support";
	ship["systems"]["engine"]["records"] = Json::parse(R"json([
		{"schema":"drive-status","producer":"pilot",
		 "fields":{"requested":"requested","enabled":"enabled","private":"requested"}}
	])json");
	ship["systems"]["radar"]["records"] = Json::parse(R"json([
		{"schema":"support-private","producer":"support","fields":{"secret":"requested"}}
	])json");
	ship["stations"] = Json::parse(R"json([
		{"id":"bridge","consoles":[
			{"id":"helm","definition":"test.console","systems":["engine","steering"],"knowledge_roles":["pilot"]},
			{"id":"support","definition":"test.console","systems":["radar"],"knowledge_roles":["support"]},
			{"id":"recorder","definition":"test.console","systems":[],"knowledge_roles":["support"]}
		]}
	])json");
	ship["feeds"] = Json::parse(R"json([
		{"source_console":"helm","target_console":"recorder","schema":"drive-status",
		 "subject":0,"fields":["enabled"]}
	])json");
	ship["publications"] = Json::parse(R"json([
		{"id":"drive-request","source_console":"helm","schema":"drive-status","subject":0,
		 "fields":["requested"],"live":true},
		{"id":"drive-enabled","source_console":"helm","schema":"drive-status","subject":0,
		 "fields":["enabled"],"live":false}
	])json");
	ship["message_templates"] = {"test.request", "test.notice"};
	ship["motion_model"] = "inertial";
	for (auto &console : ship["stations"][0]["consoles"])
	{
		console["feeds"] = console["systems"];
		console["systems"] = Json::array();
	}
	ship["control_layout"] = Json::parse(R"json({"schema":1,"default_profile":"direct","controllers":[
 {"id":"engine","console":"helm","tree":"test.idle","commands":[
 {"id":"speed_mm_s","action":"speed_mm_s","consumers":["engine"]},
 {"id":"engine_enabled","action":"engine_enabled","initial":1,"consumers":["engine"]},
 {"id":"assisted","action":"assisted","initial":1,"consumers":["engine"]},
 {"id":"longitudinal_thrust","action":"longitudinal_thrust","consumers":["engine"]}]},
 {"id":"steering","console":"helm","fallback":"hold","commands":[{"id":"heading_mdeg","action":"heading_mdeg","consumers":["steering"]}]},
 {"id":"radar","console":"support","fallback":"hold","commands":[{"id":"radar_enabled","action":"radar_enabled","initial":1,"consumers":["radar"]}]}],
 "profiles":[{"id":"assisted","consoles":{"helm":{"ai":["engine"],"human":["steering"]},"support":{"ai":["radar"]}}},
 {"id":"direct","consoles":{"helm":{"human":["engine","steering"]},"support":{"human":["radar"]}}}]
 })json");
	definitions["messages"] = Json::parse(R"json([
		{"id":"test.request","schema":"request","title_key":"test.request.title","body_key":"test.request.body",
		 "destination":{"kind":"console","id":"support"},"delay_ticks":3,"expires_after_ticks":30,
		 "request_key":"test.power","fields":{"amount":7},"parameters":{}},
		{"id":"test.notice","schema":"request","title_key":"test.notice.title","body_key":"test.notice.body",
		 "destination":{"kind":"console","id":"helm"},"delay_ticks":3,"expires_after_ticks":30,
		 "fields":{"amount":11},"parameters":{},"urgency":1}
	])json");
	definitions["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.ship"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 2}, {"definition", "test.ship"}, {"x_mm", 1000000}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	return definitions;
}
void use_tree(Json &definitions, const char *controller, const char *tree)
{
	for (auto &owner : definitions["entities"][0]["control_layout"]["controllers"])
		if (owner.at("id") == controller)
		{
			owner["tree"] = tree;
			owner.erase("fallback");
		}
	for (auto &program : definitions["trees"])
		if (program.value("context", std::string{}) == "controller" &&
		    (program.at("id") == tree || (std::string(tree) == "test.parent" && program.at("id") == "test.child")))
		{
			program["message_templates"] =
			    program.at("id") == "test.parent" ? Json::array({"test.notice"}) : Json::array();
			for (const auto &state : program.at("states"))
				for (const auto *phase : {"entry", "running", "exit"})
					for (const auto &action : state.value(phase, Json::array()))
					{
						const auto name = action.value("action", std::string{});
						if (name.starts_with("send."))
							program["message_templates"].push_back(name.substr(5));
					}
			if (!program.contains("command_bindings"))
				program["command_bindings"] =
				    std::string(controller) == "radar"
				        ? Json::parse(R"json([{"channel":"radar_enabled","type":"boolean"}])json")
				        : Json::parse(
				              R"json([{"channel":"speed_mm_s"},{"channel":"engine_enabled","type":"boolean"},{"channel":"assisted","type":"boolean"},{"channel":"longitudinal_thrust"}])json");
			if (!program.contains("system_bindings") && std::string(controller) == "engine")
				program["system_bindings"] =
				    Json::array({{{"name", "system.requested"}, {"system", "engine"}, {"field", "requested"}}});
		}
}
OperatingCommand addressed(const World &world, std::uint64_t claimant, OperatingCommand command)
{
	if (command.system.empty())
		return command;
	const auto view = world.observe(claimant);
	command.controller = command.system;
	command.system.clear();
	if (!command.action.starts_with("message."))
		command.channel = command.action;
	if (view)
		for (const auto &controller : view->gameplay.at("controllers"))
			if (controller.at("id") == command.controller)
				command.generation = controller.at("generation").get<std::uint64_t>();
	return command;
}
bool submit(World &world, std::uint64_t claimant, OperatingCommand command)
{
	return world.submit(claimant, addressed(world, claimant, std::move(command)));
}
bool can_submit(const World &world, std::uint64_t claimant, OperatingCommand command)
{
	return world.can_submit(claimant, addressed(world, claimant, std::move(command)));
}
Json with_notifier()
{
	auto definitions = fixture();
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.notify","context":"controller","initial":"wait",
		 "record_bindings":[{"name":"known.requested","scope":"console","schema":"drive-status",
		                     "field":"requested","subject":"self"}],
		 "states":[{"id":"wait","transitions":[{"target":"report","conditions":[
			{"left":{"observation":"known.requested"},"op":"ge","right":0}]}]},
		 {"id":"report","entry":[
			{"action":"send.test.notice","arguments":[11,false,"report"]}
		 ]}]}
	])json");
	use_tree(definitions, "engine", "test.notify");
	return definitions;
}
Json with_exit_cleanup()
{
	auto definitions = fixture();
	auto fast = definitions["messages"][0];
	fast["id"] = "test.cleanup-fast";
	fast["request_key"] = "test.cleanup.fast";
	fast["destination"] = {{"kind", "console"}, {"id", "helm"}};
	fast["delay_ticks"] = 1;
	fast["expires_after_ticks"] = 100;
	auto slow = fast;
	slow["id"] = "test.cleanup-slow";
	slow["request_key"] = "test.cleanup.slow";
	slow["delay_ticks"] = 20;
	definitions["messages"].push_back(fast);
	definitions["messages"].push_back(slow);
	definitions["entities"][0]["message_templates"].push_back("test.cleanup-fast");
	definitions["entities"][0]["message_templates"].push_back("test.cleanup-slow");
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.cleanup","context":"controller","initial":"active",
 "publication_templates":["drive-request"],
		 "memory":[{"name":"publication","type":"integer","initial":0}],
		 "states":[
            {"id":"stopped"},
			{"id":"active","initial":"opening","transitions":[{"target":"stopped","conditions":[{"left":{"observation":"command.engine_enabled"},"op":"eq","right":false}]}],"entry":[
				{"action":"publish.drive-request","arguments":[]}],
			 "exit":[
				{"action":"stop_publication","arguments":[{"memory":"publication"}]},
				{"action":"withdraw_request","arguments":["test.cleanup.fast"]},
				{"action":"withdraw_request","arguments":["test.cleanup.slow"]}]},
			{"id":"opening","parent":"active","running":[
				{"memory":"publication","value":{"observation":"result.publication"}}],
			 "transitions":[{"target":"serving","conditions":[
				{"left":{"observation":"result.success"},"op":"eq","right":true}]}]},
			{"id":"serving","parent":"active","entry":[
				{"action":"send.test.cleanup-fast","arguments":[7,false,
				 "request"]},
				{"action":"send.test.cleanup-slow","arguments":[7,false,
				 "request"]}]}
		 ]}
	])json");
	use_tree(definitions, "engine", "test.cleanup");
	return definitions;
}
Json with_typed_messages()
{
	auto definitions = fixture();
	definitions["knowledge"]["schemas"][1]["fields"] = Json::parse(R"json([
		{"id":"amount","type":"integer"},{"id":"flag","type":"boolean","required":false},
		{"id":"detail","type":"text","required":false},{"id":"observed","type":"integer","required":false},
		{"id":"intended","type":"integer","required":false},{"id":"elapsed","type":"integer","required":false}
	])json");
	auto &message = definitions["messages"][1];
	message["delay_ticks"] = 1;
	message["fields"] = Json::parse(R"json({"amount":{"memory":"report_amount"},"flag":{"argument":"flag"},
		"detail":{"parameter":"report_detail"},"observed":{"observation":"system.requested"},
		"intended":{"intent":"speed_mm_s"},"elapsed":{"elapsed":true}})json");
	message["parameters"] = Json::parse(R"json({"named_memory":{"memory":"report_amount"},
		"named_argument":{"argument":"detail"},"named_parameter":{"parameter":"report_detail"},
		"named_intent":{"intent":"speed_mm_s"},"named_observation":{"observation":"system.requested"},
		"named_elapsed":{"elapsed":true}})json");
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.child","context":"controller","initial":"report",
		 "parameters":{"report_detail":{"type":"text","default":"child-default"}},
		 "memory":[{"name":"report_amount","type":"integer","initial":23}],
		 "states":[{"id":"report","entry":[
			{"action":"write.speed_mm_s","arguments":[1234]},
			{"action":"send.test.notice","arguments":[999,true,
			 "argument-detail"]},
			{"memory":"report_amount","value":99},
			{"action":"write.speed_mm_s","arguments":[4321]}]}]},
		{"id":"test.parent","context":"controller","initial":"run",
		 "parameters":{"report_detail":{"type":"text","default":"parent-value"}},
		 "memory":[{"name":"report_amount","type":"integer","initial":88}],
		 "states":[{"id":"run","entry":[{"call":"test.child","parameters":{"report_detail":"child-value"}}]}]}
	])json");
	use_tree(definitions, "engine", "test.parent");
	return definitions;
}
Json with_latched_manual()
{
	auto definitions = fixture();
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.manual","context":"controller","initial":"run","states":[{"id":"run","entry":[
			{"action":"write.assisted","arguments":[false]},
			{"action":"write.longitudinal_thrust","arguments":[1000]},
			{"action":"send.test.notice","arguments":[11,false,"manual"]}
		 ]}]}
	])json");
	use_tree(definitions, "engine", "test.manual");
	definitions["messages"][1]["delay_ticks"] = 1;
	return definitions;
}
void authoring_refusals()
{
	auto unknown_role = fixture();
	unknown_role["consoles"].push_back(
	    {{"id", "test.unused-console"}, {"widgets", Json::array()}, {"knowledge_roles", {"not-declared"}}});
	refused(unknown_role, "unused Console role bindings are verified without creating a World");
	auto unknown_source = fixture();
	unknown_source["entities"][0]["systems"]["engine"]["records"][0]["fields"]["requested"] = "hidden-scene-state";
	refused(unknown_source, "telemetry binding names cannot inspect arbitrary or unsupported source data");
	auto bad_feed = fixture();
	bad_feed["entities"][0]["feeds"][0]["target_console"] = "not-fitted";
	refused(bad_feed, "scoped feed destinations resolve before World creation");
	auto private_publication = fixture();
	private_publication["entities"][0]["publications"][0]["fields"] = {"private"};
	refused(private_publication, "authored publication cannot select a private schema field");
	auto unknown_message = fixture();
	unknown_message["messages"].push_back({{"id", "test.unused-message"},
	                                       {"schema", "not-declared"},
	                                       {"title_key", "title"},
	                                       {"body_key", "body"},
	                                       {"destination", {{"kind", "ship"}, {"id", "self"}}},
	                                       {"fields", Json::object()},
	                                       {"parameters", Json::object()}});
	refused(unknown_message, "unused message schemas are verified through the production content loader");
	auto missing_field = fixture();
	missing_field["messages"][0]["fields"] = Json::object();
	refused(missing_field, "message templates must bind required typed fields");
	auto mismatched_argument = fixture();
	mismatched_argument["messages"][0]["fields"]["amount"] = {{"argument", "flag"}};
	refused(mismatched_argument, "dynamic message argument types must match the declared payload field");
	auto private_binding = with_notifier();
	private_binding["trees"][0]["record_bindings"][0]["scope"] = "ship";
	private_binding["trees"][0]["record_bindings"][0]["field"] = "private";
	refused(private_binding, "state-tree ship observations cannot select unpublishable private fields");
	auto wrong_binding_type = with_notifier();
	wrong_binding_type["trees"][0]["record_bindings"][0]["type"] = "text";
	refused(wrong_binding_type, "state-tree record type must agree with the schema field");
	auto shadowed_builtin = with_notifier();
	shadowed_builtin["trees"][0]["record_bindings"][0]["name"] = "tick";
	refused(shadowed_builtin, "record bindings cannot shadow built-in typed observations");
	auto private_controller = with_notifier();
	private_controller["trees"][0]["record_bindings"][0]["schema"] = "support-private";
	private_controller["trees"][0]["record_bindings"][0]["field"] = "secret";
	refused(private_controller, "fitted controller cannot bind a schema outside its owning Console reader roles");
	auto missing_memory = with_typed_messages();
	missing_memory["messages"][1]["fields"]["amount"] = {{"memory", "not_declared"}};
	refused(missing_memory, "message memory bindings resolve in the emitting referenced tree");
	auto wrong_parameter = with_typed_messages();
	wrong_parameter["messages"][1]["fields"]["amount"] = {{"parameter", "report_detail"}};
	refused(wrong_parameter, "typed message parameter binding must match the destination field");
	auto wrong_publication = with_exit_cleanup();
	wrong_publication["trees"][0]["publication_templates"] = {"not-authored"};
	refused(wrong_publication, "Controller publication vocabulary cannot name unowned templates");
	auto foreign_publication = with_exit_cleanup();
	foreign_publication["entities"][0]["publications"][0]["source_console"] = "support";
	refused(foreign_publication, "Controller publication must remain scoped to its Console");
	auto physical_destination = fixture();
	physical_destination["messages"][0]["destination"] = {{"kind", "system"}, {"id", "engine"}};
	phoenix::test::rejected(
	    [&]
	    {
		    (void)load(physical_destination);
	    },
	    "System message destination has no Controller inbox");
	auto wrong_sender = with_notifier();
	wrong_sender["knowledge"]["schemas"][1]["producers"] = {"support"};
	refused(wrong_sender, "statically named controller message requires its actual owner's producer role");
}
Json projection(const World &world, std::uint64_t claim)
{
	const auto observed = world.observe(claim);
	require(observed.has_value(), "claimed Console has a permitted projection");
	return observed->gameplay;
}
std::vector<Json> records(const Json &projection, const char *board, const char *schema)
{
	std::vector<Json> found;
	for (const auto &record : projection.at(board))
		if (record.at("schema") == schema)
			found.push_back(record);
	return found;
}
Json latest_field(const Json &projection, const char *board, const char *schema, const char *field)
{
	Json result;
	std::pair<std::uint64_t, std::uint64_t> latest{};
	bool found = false;
	for (const auto &record : records(projection, board, schema))
		if (record.at("fields").contains(field))
		{
			const auto version =
			    std::pair{record.at("observed_tick").get<std::uint64_t>(), record.at("revision").get<std::uint64_t>()};
			if (!found || latest <= version)
			{
				result = record.at("fields").at(field);
				latest = version;
				found = true;
			}
		}
	require(found, "requested permitted field exists");
	return result;
}
void claim_all(World &world)
{
	require(world.claim(101, 1, "helm") && world.claim(102, 1, "support") && world.claim(103, 1, "recorder") &&
	            world.claim(201, 2, "helm"),
	        "synthetic Consoles claim independently on both ships");
}
void telemetry_publication_and_feeds(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	world.tick();
	const auto own = projection(world, 101), support = projection(world, 102), recorder = projection(world, 103);
	require(!records(own, "records", "drive-status").empty(), "owning Console receives real fitted telemetry");
	require(records(support, "records", "drive-status").empty() && records(support, "shared", "drive-status").empty(),
	        "schema permission alone does not pool another Console's telemetry");
	require(!records(recorder, "records", "drive-status").empty(),
	        "authored scoped feed reaches its explicit recipient");
	for (const auto &record : records(recorder, "records", "drive-status"))
		require(record.at("fields").contains("enabled") && !record.at("fields").contains("requested") &&
		            !record.at("fields").contains("private"),
		        "scoped feed retains only declared fields");
	require(!records(support, "records", "support-private").empty() &&
	            records(own, "records", "support-private").empty(),
	        "different producer/reader roles retain Console privacy");
	require(!submit(world, 102, {1, "publish.drive-request", 0, {}}) &&
	            !submit(world, 101, {2, "publish.drive-request", 0, {}}),
	        "publication admission enforces source Console and ship ownership");
	require(submit(world, 101, {1, "publish.drive-request", 0, {}}) &&
	            submit(world, 101, {1, "publish.drive-enabled", 0, {}}),
	        "explicit source publications are admitted");
	world.tick();
	const auto shared = projection(world, 102);
	require(latest_field(shared, "shared", "drive-status", "requested").is_number_integer() &&
	            latest_field(shared, "shared", "drive-status", "enabled") == true,
	        "concurrent publication subsets retain independent evidence");
	for (const auto &record : records(shared, "shared", "drive-status"))
		require(!record.at("fields").contains("private"), "deliberate publication never leaks private schema fields");
	require(records(projection(world, 201), "shared", "drive-status").empty(),
	        "shipwide publication stays on its source ship");
	require(submit(world, 101, {1, "speed_mm_s", 500, "engine"}) &&
	            submit(world, 101, {1, "engine_enabled", 0, "engine"}),
	        "ordinary actuator settings change through their admitted owner");
	world.tick();
	require(latest_field(projection(world, 102), "shared", "drive-status", "requested") == 500,
	        "live publication follows actual permitted requested-setting observations");
	require(latest_field(projection(world, 102), "shared", "drive-status", "enabled") == true &&
	            latest_field(projection(world, 103), "records", "drive-status", "enabled") == false,
	        "snapshot publication retains dated evidence while the scoped live feed refreshes");
	auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	resumed.clear_claims();
	claim_all(resumed);
	world.tick();
	resumed.tick();
	require(projection(world, 102).at("shared") == projection(resumed, 102).at("shared") &&
	            projection(world, 103).at("records") == projection(resumed, 103).at("records"),
	        "resumed scoped feeds/publications continue identically without duplicate streams");
}
void human_message_origin_and_routing(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	world.tick();
	const auto before_projection = world.save();
	for (const auto route : {101u, 102u, 201u})
		for (int repeat = 0; repeat < 3; ++repeat)
			require(world.observe(route).has_value(), "claimed Console can project available controls");
	require(world.save() == before_projection,
	        "control availability probes must not publish, send, or consume identities");
	require(!submit(world, 102, {1, "message.test.request", 0, "engine"}),
	        "another Console cannot impersonate the message sender");
	require(submit(world, 101, {1, "message.test.request", 0, "engine"}), "human request uses normal sender admission");
	require(submit(world, 101, {1, "away", 1, {}}), "explicit Away can follow an admitted request");
	world.tick();
	require(projection(world, 102).at("messages").empty(), "Coordination does not deliver in its sending tick");
	world.tick();
	world.tick();
	const auto support = projection(world, 102);
	require(support.at("messages").size() == 1 &&
	            support.at("messages")[0].at("origin") == static_cast<unsigned>(ControlOrigin::Human),
	        "delayed request preserves captured human origin after sender hands over to AI");
	require(support.at("messages")[0].at("payload").at("fields").at("amount") == 7 &&
	            support.at("notifications").empty(),
	        "human-to-human automatic delivery retains typed history without popups");
	require(projection(world, 201).at("messages").empty(), "targeted Coordination never reaches another ship");
}
void ai_delivery_handover_and_continuation(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	world.tick();
	require(submit(world, 101, {1, "preset", 0, {}}), "Console complexity selects AI for the engine Controller");
	world.tick();
	require(!can_submit(world, 101, {1, "speed_mm_s", 200, "engine"}),
	        "delegation leaves a single effective actuator writer");
	require(projection(world, 101).at("messages").empty(), "delegated AI's authored report respects delivery delay");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	resumed.clear_claims();
	claim_all(resumed);
	world.tick();
	resumed.tick();
	require(projection(world, 101).at("notifications").empty(),
	        "AI report remains queued until its authored delivery tick");
	world.tick();
	resumed.tick();
	const auto observed = projection(world, 101), recovered = projection(resumed, 101);
	require(observed.at("messages").size() == 1 && observed.at("notifications").size() == 1 &&
	            observed.at("messages")[0].at("origin") == static_cast<unsigned>(ControlOrigin::AI),
	        "AI-to-human Console report notifies the current human while the sender remains delegated");
	require(observed.at("messages") == recovered.at("messages") &&
	            observed.at("notifications") == recovered.at("notifications"),
	        "save/resume preserves pending delivery and controller continuation exactly once");
	for (int i = 0; i < 4; ++i)
	{
		world.tick();
		resumed.tick();
	}
	require(projection(resumed, 101).at("messages").size() == 1,
	        "resuming an entered tree does not send its entry report twice");
	require(submit(world, 101, {1, "preset", 1, {}}), "authored direct station preset is selectable");
	world.tick();
	require(can_submit(world, 101, {1, "speed_mm_s", 200, "engine"}),
	        "direct preset returns effective control to the human");
	require(submit(world, 101, {1, "away", 1, {}}), "explicit Away is admitted");
	world.tick();
	require(!can_submit(world, 101, {1, "speed_mm_s", 200, "engine"}), "Away yields actuator authority to backfill");
	require(submit(world, 101, {1, "away", 0, {}}), "player can return from explicit Away");
	world.tick();
	require(can_submit(world, 101, {1, "speed_mm_s", 200, "engine"}),
	        "return restores current direct authority without rewriting machinery settings");
	World quiet;
	quiet.load(catalog, "test.map");
	claim_all(quiet);
	quiet.tick();
	require(submit(quiet, 101, {1, "preset", 0, {}}), "second report sender delegates");
	quiet.tick();
	require(submit(quiet, 101, {1, "away", 1, {}}), "recipient goes Away while AI report is in flight");
	quiet.tick();
	quiet.tick();
	require(projection(quiet, 101).at("messages").size() == 1 && projection(quiet, 101).at("notifications").empty(),
	        "recipient control is resolved at delivery: Away AI receives silently");
	const auto delivered = quiet.save();
	World reconnect;
	reconnect.restore(catalog, delivered);
	reconnect.clear_claims();
	claim_all(reconnect);
	require(projection(reconnect, 101).at("messages").size() == 1 &&
	            projection(reconnect, 101).at("notifications").empty(),
	        "recovery retains history without replaying historical notifications");
}
void controller_exit_cleanup(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	world.tick();
	require(submit(world, 101, {1, "preset", 0, {}}), "cleanup Controller receives AI ownership");
	world.tick();
	world.tick();
	const auto active = projection(world, 101);
	require(active.at("messages").size() == 1 && active.at("messages")[0].at("actionable") == true,
	        "controller enters live operation and delivers its fast standing request");
	require(!records(projection(world, 102), "shared", "drive-status").empty(),
	        "controller entry activates its declared live publication");
	require(submit(world, 101, {1, "preset", 1, {}}), "human can take control from the running cleanup controller");
	world.tick();
	require(submit(world, 101, {1, "engine_enabled", 0, "engine"}), "Human stop intent drives authored policy cleanup");
	world.tick();
	require(can_submit(world, 101, {1, "speed_mm_s", 600, "engine"}),
	        "Human actuator authority and authored policy cleanup coexist");
	const auto stopped = projection(world, 101);
	require(stopped.at("messages").size() == 1 && stopped.at("messages")[0].at("actionable") == false,
	        "exit withdrawal invalidates an unconsumed delivered request");
	const auto frozen = projection(world, 102).at("shared");
	require(submit(world, 101, {1, "speed_mm_s", 600, "engine"}), "human changes intent after controller interruption");
	world.tick();
	require(latest_field(projection(world, 101), "records", "drive-status", "requested") == 600 &&
	            projection(world, 102).at("shared") == frozen,
	        "stopped publication retains dated evidence and cannot follow later human settings");
	for (int i = 0; i < 24; ++i)
		world.tick();
	require(projection(world, 101).at("messages").size() == 1,
	        "exit withdrawal cancels the slow pending request before its delivery tick");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	resumed.clear_claims();
	claim_all(resumed);
	for (int i = 0; i < 3; ++i)
		resumed.tick();
	require(projection(resumed, 101).at("messages").size() == 1 &&
	            projection(resumed, 101).at("messages")[0].at("actionable") == false &&
	            projection(resumed, 102).at("shared") == frozen,
	        "recovery of a stopped human-held controller does not replay entry or cleanup effects");
}
void ordinary_claim_exit_cleanup(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	require(world.claim(102, 1, "support") && world.claim(103, 1, "recorder") && world.claim(201, 2, "helm"),
	        "observe autonomous Helm from its permitted recipients");
	world.tick();
	world.tick();
	require(!records(projection(world, 102), "shared", "drive-status").empty(),
	        "unclaimed Console AI activates its live publication");
	require(world.claim(101, 1, "helm"), "ordinary claim takes over an active AI Console");
	require(submit(world, 101, {1, "engine_enabled", 0, "engine"}),
	        "Claimed operator stops the live policy through its command");
	world.tick();
	const auto held = projection(world, 101);
	require(held.at("messages").size() == 1 && held.at("messages")[0].at("actionable") == false,
	        "Authored stop transition executes exit withdrawal under human control");
	const auto frozen = projection(world, 102).at("shared");
	require(submit(world, 101, {1, "speed_mm_s", 700, "engine"}), "ordinary takeover grants actuator authority");
	for (int i = 0; i < 24; ++i)
		world.tick();
	require(projection(world, 102).at("shared") == frozen && projection(world, 101).at("messages").size() == 1,
	        "ordinary takeover stops live updates and cancels pending AI requests");
}
void delegated_manual_survives_route_release(const Catalog &catalog)
{
	for (const bool clear_all : {false, true})
	{
		World world;
		world.load(catalog, "test.map");
		claim_all(world);
		require(submit(world, 101, {1, "preset", 0, {}}), "entry-only manual AI is delegated");
		world.tick();
		world.tick();
		const auto speed = world.inspect()[0].vx;
		if (clear_all)
			world.clear_claims();
		else
			world.release(101);
		world.tick();
		require(world.inspect()[0].vx > speed, "route release preserves manual thrust already owned by delegated AI");
		world.tick();
		require(world.claim(101, 1, "helm"), "closed Console can reconnect while engine remains delegated");
		require(projection(world, 101).at("messages").size() == 1,
		        "release and clear_claims preserve the existing AI activation without replaying entry effects");
	}
}
void typed_message_emission(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	require(!submit(world, 101, {1, "message.test.notice", 999, "engine"}),
	        "human template command cannot invent unavailable AI memory or parameter bindings");
	require(submit(world, 101, {1, "preset", 0, {}}), "typed sender receives delegated authority");
	world.tick();
	world.tick();
	const auto delivered = projection(world, 101);
	require(delivered.at("messages").size() == 1, "referenced controller emits one structured message");
	const auto &message = delivered.at("messages")[0];
	const auto &fields = message.at("payload").at("fields");
	require(fields.at("amount") == 23 && fields.at("flag") == true && fields.at("detail") == "child-value" &&
	            fields.at("observed") == 0 && fields.at("intended") == 1234 && fields.at("elapsed") == 0,
	        "typed bindings use emission-time child memory, parameters, intents and permitted observations");
	const auto &parameters = message.at("parameters");
	require(parameters.at("named_memory") == "23" && parameters.at("named_argument") == "argument-detail" &&
	            parameters.at("named_parameter") == "child-value" && parameters.at("named_intent") == "1234" &&
	            parameters.at("named_observation") == "0" && parameters.at("named_elapsed") == "0",
	        "presentation parameter keys never substitute for their declared typed source bindings");
}
void message_capture_budget()
{
	auto small = with_typed_messages();
	small["trees"][1]["budget"] = 80;
	small["trees"][0]["memory"].push_back({{"name", "retained_note"}, {"type", "text"}, {"initial", "short"}});
	const auto small_catalog = load(small);
	const auto &program = small_catalog.tree_programs.at("test.parent");
	std::vector<state_tree::Value> observations(program.registry.observations.size());
	for (std::size_t i = 0; i < program.registry.observations.size(); ++i)
	{
		const auto &name = program.registry.observations[i].name;
		if (name == "self_entity")
			observations[i] = state_tree::Value::reference(state_tree::Type::Entity, 1);
		if (name == "self_console")
			observations[i] = state_tree::Value::reference(state_tree::Type::Console, 3);
		if (name == "system.requested")
			observations[i] = state_tree::Value::integer(0);
	}
	state_tree::Instance accepted;
	require(!state_tree::step(program, accepted, observations).outputs.empty(),
	        "ordinary typed message capture fits the authored evaluation budget");
	auto large = small;
	large["trees"][0]["memory"][1]["initial"] = std::string(4096, 'x');
	const auto large_catalog = load(large);
	const auto &bounded = large_catalog.tree_programs.at("test.parent");
	state_tree::Instance rejected;
	const auto before = state_tree::save(bounded, rejected);
	bool budget_refused = false;
	try
	{
		(void)state_tree::step(bounded, rejected, observations);
	}
	catch (const std::exception &error)
	{
		budget_refused = std::string(error.what()).find("evaluation budget exhausted") != std::string::npos;
	}
	require(budget_refused && state_tree::save(bounded, rejected) == before,
	        "captured text bytes consume the shared work budget and refusal rolls back child memory and effects");
}
void station_recipient_set()
{
	auto definitions = fixture();
	definitions["messages"][0]["destination"] = {{"kind", "station"}, {"id", "bridge"}};
	const auto catalog = load(definitions);
	World world;
	world.load(catalog, "test.map");
	require(world.claim(101, 1, "helm") && world.claim(201, 2, "helm"), "claim sending and other-ship Consoles");
	require(submit(world, 101, {1, "message.test.request", 0, "engine"}),
	        "multi-Console Station is a valid exact target");
	world.tick();
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	resumed.tick();
	resumed.tick();
	require(projection(resumed, 101).at("messages").size() == 1 &&
	            projection(resumed, 101).at("notifications").empty() && projection(resumed, 201).at("messages").empty(),
	        "Station delivery reaches its human member once and excludes another ship's Station");
	require(resumed.claim(102, 1, "support") && resumed.claim(103, 1, "recorder"), "AI recipients can later reconnect");
	require(projection(resumed, 102).at("messages").size() == 1 &&
	            projection(resumed, 103).at("messages").size() == 1 &&
	            projection(resumed, 102).at("notifications").empty() &&
	            projection(resumed, 103).at("notifications").empty(),
	        "same-Station AI members retain independent history without historical popup replay");
}
void delayed_human_seeking()
{
	auto definitions = fixture();
	definitions["knowledge"]["roles"].push_back("observer");
	definitions["knowledge"]["schemas"][2]["fields"].push_back(
	    {{"id", "enabled"}, {"type", "boolean"}, {"required", false}});
	auto &ship = definitions["entities"][0];
	ship["feeds"] = Json::array();
	ship["systems"]["radar"]["records"][0]["fields"]["enabled"] = "enabled";
	auto &consoles = ship["stations"][0]["consoles"];
	consoles[2]["knowledge_roles"] = {"observer"};
	for (const auto *id : {"assistant", "away-reader"})
		consoles.push_back(
		    {{"id", id}, {"definition", "test.console"}, {"systems", Json::array()}, {"knowledge_roles", {"support"}}});
	definitions["messages"][1]["human_seeking"] = true;
	definitions["messages"][1]["destination"] = {{"kind", "controller"}, {"console", "helm"}, {"id", "engine"}};
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.seek","context":"controller","initial":"send","states":[{"id":"send","entry":[
			{"action":"send.test.notice","arguments":[11,false,"offer"]}
		 ]}]},
		{"id":"test.receiver","context":"controller","initial":"wait","states":[
			{"id":"wait","transitions":[{"target":"received","conditions":[
			 {"left":{"observation":"message.available"},"op":"eq","right":true}]}]},
			{"id":"received","entry":[{"action":"write.radar_enabled","arguments":[false]}]}
		 ]}
	])json");
	use_tree(definitions, "engine", "test.seek");
	use_tree(definitions, "radar", "test.receiver");
	const auto catalog = load(definitions);
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	require(world.claim(105, 1, "away-reader"), "eligible reader can explicitly go Away");
	require(submit(world, 101, {1, "preset", 0, {}}), "human-seeking sender remains delegated AI");
	world.tick();
	require(projection(world, 102).at("messages").empty(), "human-seeking routing respects authored delay");
	World resumed;
	resumed.restore(catalog, world.save());
	require(resumed.claim(104, 1, "assistant"), "human arriving during delivery delay may receive an offer");
	require(submit(resumed, 105, {1, "away", 1, {}}), "Away removes a human from offer eligibility before delivery");
	resumed.tick();
	require(projection(resumed, 104).at("messages").empty(), "late human does not bypass message delay");
	resumed.tick();
	const auto normal = projection(resumed, 101), offer = projection(resumed, 102), late = projection(resumed, 104);
	require(normal.at("messages").size() == 1 && normal.at("messages")[0].at("offer_only") == false &&
	            normal.at("notifications").size() == 1,
	        "AI System delivery keeps its addressed route while notifying its present human once");

	require(offer.at("messages").size() == 1 && offer.at("messages")[0].at("offer_only") == true &&
	            offer.at("notifications").size() == 1 && late.at("messages").size() == 1 &&
	            late.at("notifications").size() == 1,
	        "human-seeking offers reach eligible current humans independently of the exact AI target");
	const auto other_ship = projection(resumed, 201).at("messages");
	const bool leaked = std::any_of(other_ship.begin(), other_ship.end(),
	                                [](const auto &message)
	                                {
		                                return message.at("payload").at("subject") == 1;
	                                });
	require(records(offer, "records", "request").empty() && projection(resumed, 103).at("messages").empty() &&
	            projection(resumed, 105).at("messages").empty() && !leaked,
	        "extra offers do not ingest AI board data or reach forbidden, Away or other-ship readers");
	require(!can_submit(resumed, 102, {1, "speed_mm_s", 700, "engine"}) &&
	            !can_submit(resumed, 101, {1, "speed_mm_s", 700, "engine"}),
	        "offering work transfers no Console claim or delegated actuator authority");
	World recovered;
	recovered.restore(catalog, resumed.save());
	require(projection(recovered, 102).at("messages").size() == 1 &&
	            projection(recovered, 102).at("notifications").empty(),
	        "offer-only history persists without replaying a historical popup");
	require(submit(recovered, 102, {1, "away", 1, {}}), "offered Console can return to AI after delivery");
	recovered.tick();
	recovered.tick();
	require(latest_field(projection(recovered, 102), "records", "support-private", "enabled") == true,
	        "human observer offers never become later AI inbox work after handover");
}
void comic_message_presentation()
{
	auto definitions = fixture();
	definitions["consoles"][0]["name"] = "Helm";
	definitions["messages"][0]["title_key"] = "Power request";
	definitions["messages"][0]["body_key"] = "Please supply more power.";
	const auto catalog = load(definitions);
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	OperatingCommand spoken{1, "message.test.request", 0, "engine"};
	spoken.speaker_name = "Morgan";
	spoken.speaker_avatar = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	require(submit(world, 101, decode_operating_command(encode_operating_command(spoken))),
	        "profile snapshot uses ordinary command admission");
	world.tick();
	World restored;
	restored.restore(catalog, world.save());
	restored.release(101);
	require(restored.claim(104, 1, "helm"), "another operator takes the sending Console before delivery");
	for (int i = 0; i < 3; ++i)
		restored.tick();
	const auto board = projection(restored, 102);
	const auto views = console_messages(board);
	require(views.size() == 1 && views[0].sender == "Morgan" && views[0].avatar == spoken.speaker_avatar &&
	            views[0].text == "Power request: Please supply more power." &&
	            views[0].accessible_text() == "Morgan: Power request: Please supply more power.",
	        "delayed human message retains original speaker after restore and handover, with prose-only semantics");
	require(console_messages(restored.public_board(1), true).empty() && console_messages(board, true).empty(),
	        "private messages cannot appear on the shared Viewscreen");
	auto public_copy = board;
	public_copy["messages"][0]["public"] = true;
	public_copy["messages"][0]["speaker_name"] = "";
	auto ai = console_messages(public_copy, true);
	require(ai.size() == 1 && ai[0].sender == "Helm" && ai[0].portrait == "messages/console-1.png",
	        "Console identity resolves to a stable portrait and readable authored name");
	public_copy["messages"][0]["title_key"] = "missing.translation.key";
	public_copy["messages"][0]["body_key"] = "Please supply {missing}.";
	require(console_messages(public_copy)[0].text == "Message unavailable",
	        "missing translation and unresolved interpolation never expose internal keys");
	public_copy["messages"][0]["speaker_name"] = "Morgan";
	public_copy["messages"][0]["speaker_avatar"] = {1, 2, 3};
	require(console_messages(public_copy)[0].avatar.empty(),
	        "invalid portraits have a readable sender and bundled fallback");
	for (int i = 1; i < 12; ++i)
	{
		auto message = public_copy["messages"][0];
		message["sequence"] = i + 1;
		public_copy["messages"].push_back(message);
	}
	require(console_messages(public_copy, true).size() == 12,
	        "message history has no extra eight-row presentation cut-off");
}
void authorized_message_producers()
{
	auto human_definitions = fixture();
	human_definitions["knowledge"]["schemas"][1]["producers"] = {"support", "pilot"};
	const auto human_catalog = load(human_definitions);
	World human;
	human.load(human_catalog, "test.map");
	claim_all(human);
	require(submit(human, 101, {1, "message.test.request", 0, "engine"}),
	        "human sender selects a permitted later schema producer instead of impersonating the first");
	human.tick();
	const auto human_pending = human.save().at("knowledge").at("runtime").value("pending", Json::array());
	require(human_pending.size() == 1, "authorized human send enters delayed transport");
	require(human_pending[0].at("payload").at("producer") == human_catalog.knowledge_definition.role("pilot"),
	        "human message persists the actual authorized producer role");
	human.tick();
	human.tick();
	require(projection(human, 102).at("messages").size() == 1, "authorized human message reaches its exact recipient");
	auto ai_definitions = with_notifier();
	ai_definitions["knowledge"]["schemas"][1]["producers"] = {"support", "pilot"};
	const auto ai_catalog = load(ai_definitions);
	World ai;
	ai.load(ai_catalog, "test.map");
	claim_all(ai);
	ai.tick();
	require(submit(ai, 101, {1, "preset", 0, {}}), "AI sender receives effective control");
	ai.tick();
	const auto ai_pending = ai.save().at("knowledge").at("runtime").value("pending", Json::array());
	require(ai_pending.size() == 2, "Each synthetic ship shared policy reports through its own delayed transport");
	require(ai_pending[0].at("payload").at("producer") == ai_catalog.knowledge_definition.role("pilot"),
	        "AI message also selects its actual authorized later producer role");
	ai.tick();
	ai.tick();
	require(projection(ai, 101).at("messages").size() == 1, "authorized AI message reaches its exact recipient");
	auto forbidden = fixture();
	forbidden["knowledge"]["schemas"][1]["producers"] = {"support"};
	const auto forbidden_catalog = load(forbidden);
	World denied_human;
	denied_human.load(forbidden_catalog, "test.map");
	claim_all(denied_human);
	require(!submit(denied_human, 101, {1, "message.test.request", 0, "engine"}),
	        "human with no producer intersection cannot originate that schema");
	auto unauthorized_ai = with_notifier();
	unauthorized_ai["knowledge"]["schemas"][1]["producers"] = {"support"};
	refused(unauthorized_ai, "Controller send vocabulary must reject unauthorized templates at authoring boundary");
}
void current_record_streams()
{
	auto definitions = fixture();
	definitions["knowledge"]["schemas"][1]["fields"].push_back(
	    {{"id", "detail"}, {"type", "boolean"}, {"required", false}});
	definitions["knowledge"]["schemas"][2]["fields"].push_back(
	    {{"id", "enabled"}, {"type", "boolean"}, {"required", false}});
	auto &ship = definitions["entities"][0];
	ship["systems"]["radar"]["records"][0]["fields"]["enabled"] = "enabled";

	definitions["trees"].push_back(Json::parse(R"json(
		{"id":"test.detail-reader","context":"controller","initial":"read",
		 "record_bindings":[{"name":"known.detail","scope":"console","schema":"request","field":"detail","subject":"self"}],
		 "states":[{"id":"read","running":[
			{"action":"write.radar_enabled","arguments":[false]},
			{"action":"write.radar_enabled","arguments":[{"observation":"known.detail"}]}
		 ]}]}
	)json"));
	use_tree(definitions, "radar", "test.detail-reader");
	auto detail = definitions["messages"][0];
	detail["id"] = "test.detail";
	detail.erase("request_key");
	detail["delay_ticks"] = 1;
	detail["expires_after_ticks"] = 100;
	detail["fields"] = {{"amount", 1}, {"detail", true}};
	auto replace = detail;
	replace["id"] = "test.replace";
	replace["fields"].erase("detail");
	definitions["messages"].push_back(detail);
	definitions["messages"].push_back(replace);
	ship["message_templates"].push_back("test.detail");
	ship["message_templates"].push_back("test.replace");
	const auto catalog = load(definitions);
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	require(submit(world, 102, {1, "preset", 0, {}}), "record reader runs through its Console Controller");
	world.tick();
	const auto send = [&](const char *message, const char *sender)
	{
		require(submit(world, 101, {1, std::string("message.") + message, 0, sender}),
		        "record versions originate through the production Coordination command path");
		world.tick();
		world.tick();
	};
	send("test.detail", "engine");
	require(latest_field(projection(world, 102), "records", "support-private", "enabled") == true,
	        "custom System record binding reads the current optional detail");
	send("test.replace", "engine");
	require(latest_field(projection(world, 102), "records", "support-private", "enabled") == false,
	        "newer same-stream omission suppresses an older retained optional field");
	send("test.detail", "steering");
	send("test.replace", "engine");
	require(latest_field(projection(world, 102), "records", "support-private", "enabled") == true,
	        "an omitted field in one source does not suppress current evidence from an independent source");
	// A valid saved tombstone enters through the same production restore boundary
	// as dated history. Keep an older version to detect resurrection on withdrawal.
	send("test.detail", "steering");
	auto withdrawn = world.save();
	const auto request = catalog.knowledge_definition.schema("request");
	auto &rows = withdrawn["knowledge"]["runtime"]["records"];
	for (std::size_t i = rows.size(); i > 0; --i)
	{
		auto &row = rows[i - 1];
		if (row.at("value").at("schema") == request && row.at("value").at("fields").size() == 2)
		{
			row["value"]["withdrawn"] = true;
			row["value"]["fields"] = Json::array();
			break;
		}
	}
	World resumed;
	resumed.restore(catalog, withdrawn);
	resumed.tick();
	require(latest_field(projection(resumed, 102), "records", "support-private", "enabled") == false,
	        "restored withdrawal suppresses older detail from that source without discarding retained history");
}
void reject_saved(const Catalog &catalog, const Json &state, const char *contract)
{
	World target;
	target.load(catalog, "test.map");
	const auto before = target.save();
	phoenix::test::rejects(
	    [&]
	    {
		    target.restore(catalog, state);
	    },
	    contract);
	require(target.save() == before, "failed continuation restore leaves the live World unchanged");
}
void remove_knowledge_ship(Json &state, EntityId ship)
{
	auto &knowledge = state["knowledge"];
	std::set<KnowledgeId> consoles, publications;
	for (const auto &binding : knowledge.at("ships"))
		if (binding.at("ship") == ship)
			for (auto value = binding.at("consoles").begin(); value != binding.at("consoles").end(); ++value)
				consoles.insert(value.value().get<KnowledgeId>());
	auto erase = [](Json &rows, auto predicate)
	{
		for (std::size_t i = rows.size(); i > 0; --i)
			if (predicate(rows[i - 1]))
				rows.erase(rows.begin() + static_cast<Json::difference_type>(i - 1));
	};
	erase(knowledge["ships"],
	      [&](const auto &row)
	      {
		      return row.at("ship") == ship;
	      });
	auto &runtime = knowledge["runtime"];
	if (runtime.contains("publications"))
		for (const auto &row : runtime.at("publications"))
			if (consoles.contains(row.at("console").template get<KnowledgeId>()))
				publications.insert(row.at("id").template get<KnowledgeId>());
	if (runtime.contains("consoles"))
		erase(runtime["consoles"],
		      [&](const auto &row)
		      {
			      return consoles.contains(row.at("id").template get<KnowledgeId>());
		      });
	for (const auto *field : {"systems", "memory", "publications"})
		if (runtime.contains(field))
			erase(runtime[field],
			      [&](const auto &row)
			      {
				      return consoles.contains(row.at("console").template get<KnowledgeId>());
			      });
	if (runtime.contains("records"))
		erase(runtime["records"],
		      [&](const auto &row)
		      {
			      const auto scope = row.at("scope").template get<unsigned>();
			      return scope == static_cast<unsigned>(KnowledgeScope::Ship)
			                 ? row.at("owner") == ship
			                 : consoles.contains(row.at("owner").template get<KnowledgeId>());
		      });
	erase(knowledge["publications"],
	      [&](const auto &row)
	      {
		      return publications.contains(row.at("id").template get<KnowledgeId>());
	      });
}
void continuation_roster_validation(const Catalog &catalog)
{
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	world.tick();
	const auto state = world.save();
	std::set<KnowledgeId> expected_ai;
	for (const auto &ship : state.at("knowledge").at("ships"))
		for (const auto &[name, id] : ship.at("controllers").items())
			expected_ai.insert(id.get<KnowledgeId>());
	std::set<KnowledgeId> actual_ai;
	for (const auto &memory : state.at("knowledge").at("runtime").value("memory", Json::array()))
		actual_ai.insert(memory.at("id").get<KnowledgeId>());
	require(actual_ai == expected_ai,
	        "Only Console Controllers have gameplay AI memory and inbox cursors; physical reporters remain sources");
	auto physical_ai = state;
	const auto &binding = state.at("knowledge").at("ships")[0];
	physical_ai["knowledge"]["runtime"]["memory"].push_back({{"id", binding.at("systems").at("engine")},
	                                                         {"console", binding.at("consoles").at("helm")},
	                                                         {"cursor", 0},
	                                                         {"values", Json::array()}});
	reject_saved(catalog, physical_ai,
	             "Restore must reject fabricated physical-System AI memory even with a valid source identity");
	auto omitted = state;
	remove_knowledge_ship(omitted, 2);
	reject_saved(catalog, omitted,
	             "knowledge continuation cannot omit a physical ship even with internally coherent bindings");
	auto definition_mismatch = state;
	definition_mismatch["knowledge"]["ships"][0]["definition"] = "test.variant";
	reject_saved(catalog, definition_mismatch,
	             "knowledge definition must match the restored physical entity definition");
	auto missing_controller = state;
	missing_controller["bodies"][0]["console_controllers"]["controllers"][0]["id"] = "not-authored";
	reject_saved(catalog, missing_controller, "Controller continuation cannot invent an owner");
	auto missing_command = state;
	missing_command["bodies"][0]["console_controllers"]["controllers"][0]["commands"].erase(0);
	reject_saved(catalog, missing_command, "Controller continuation cannot omit owned command sections");
	auto bad_profile = state;
	bad_profile["bodies"][0]["console_controllers"]["profiles"]["helm"] = 99;
	reject_saved(catalog, bad_profile, "Continuation profile must name an authored choice");
	auto bad_away = state;
	bad_away["away"] = Json::array({{{"address", {{"ship", 1}, {"console", "not-fitted"}}}, {"value", true}}});
	reject_saved(catalog, bad_away, "Away continuation cannot create an unknown Console");
	auto bad_allocator = state;
	bad_allocator["next_entity"] = 1;
	reject_saved(catalog, bad_allocator,
	             "spawn identity allocator cannot overlap the physical roster even before a scenario is active");
}
} // namespace
int main()
{
	try
	{
		const auto catalog = load(fixture());
		authoring_refusals();
		telemetry_publication_and_feeds(catalog);
		human_message_origin_and_routing(catalog);
		ai_delivery_handover_and_continuation(load(with_notifier()));
		controller_exit_cleanup(load(with_exit_cleanup()));
		ordinary_claim_exit_cleanup(load(with_exit_cleanup()));
		delegated_manual_survives_route_release(load(with_latched_manual()));
		typed_message_emission(load(with_typed_messages()));
		message_capture_budget();
		station_recipient_set();
		delayed_human_seeking();
		comic_message_presentation();
		authorized_message_producers();
		current_record_streams();
		continuation_roster_validation(catalog);
		std::cout << "Gameplay knowledge integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
