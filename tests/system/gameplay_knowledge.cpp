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

import phoenix.simulation;

using namespace phoenix;
namespace
{
void require(bool condition, const char *contract)
{
	if (!condition)
		throw std::runtime_error(contract);
}
Catalog load(const Json &definitions)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	pack.files["definitions.json"] = json_bytes(definitions);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	return resolve_packs({read_pack(write_pack(pack))});
}
void refused(const Json &definitions, const char *contract)
{
	bool rejected = false;
	try
	{
		(void)load(definitions);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected, contract);
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
	                                {"context", "system"},
	                                {"initial", "ready"},
	                                {"states", Json::array({{{"id", "ready"}}})}});
	auto &ship = definitions["entities"][0];
	ship.erase("console");
	ship["controllers"] = {{"engine", "test.idle"}};
	ship["systems"]["engine"]["kind"] = "engine";
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
	ship["default_preset"] = "direct";
	ship["presets"] = Json::parse(R"json([
		{"id":"assisted","consoles":{"helm":{"direct":["steering"],"delegated":["engine"]}}},
		{"id":"direct","consoles":{"helm":{"direct":["engine","steering"],"delegated":[]}}}
	])json");
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
Json with_notifier()
{
	auto definitions = fixture();
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.notify","context":"system","initial":"wait",
		 "record_bindings":[{"name":"known.requested","scope":"console","schema":"drive-status",
		                     "field":"requested","subject":"self"}],
		 "states":[{"id":"wait","transitions":[{"target":"report","conditions":[
			{"left":{"observation":"known.requested"},"op":"ge","right":0}]}]},
		 {"id":"report","entry":[
			{"action":"send_console_message","arguments":[
				{"observation":"self.system"},{"observation":"self.console"},"test.notice",
				{"observation":"self.entity"},11,false,"report","",3,30,1]}
		 ]}]}
	])json");
	definitions["entities"][0]["controllers"] = {{"engine", "test.notify"}};
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
		{"id":"test.cleanup","context":"system","initial":"active",
		 "memory":[{"name":"publication","type":"integer","initial":0}],
		 "states":[
			{"id":"active","initial":"opening","entry":[
				{"action":"publish_record","arguments":[{"observation":"self.console"},"drive-request",
				 {"observation":"self.entity"},true,""]}],
			 "exit":[
				{"action":"stop_publication","arguments":[{"observation":"self.console"},{"memory":"publication"}]},
				{"action":"withdraw_request","arguments":[{"observation":"self.system"},"test.cleanup.fast"]},
				{"action":"withdraw_request","arguments":[{"observation":"self.system"},"test.cleanup.slow"]}]},
			{"id":"opening","parent":"active","running":[
				{"memory":"publication","value":{"observation":"result.publication"}}],
			 "transitions":[{"target":"serving","conditions":[
				{"left":{"observation":"result.publication"},"op":"gt","right":0}]}]},
			{"id":"serving","parent":"active","entry":[
				{"action":"send_console_message","arguments":[{"observation":"self.system"},
				 {"observation":"self.console"},"test.cleanup-fast",{"observation":"self.entity"},7,false,
				 "request","test.cleanup.fast",1,100,1]},
				{"action":"send_console_message","arguments":[{"observation":"self.system"},
				 {"observation":"self.console"},"test.cleanup-slow",{"observation":"self.entity"},7,false,
				 "request","test.cleanup.slow",20,100,1]}]}
		 ]}
	])json");
	definitions["entities"][0]["controllers"] = {{"engine", "test.cleanup"}};
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
	message["fields"] = Json::parse(R"json({"amount":{"memory":"report_amount"},"flag":{"argument":"flag"},
		"detail":{"parameter":"report_detail"},"observed":{"observation":"system.requested"},
		"intended":{"intent":"speed_mm_s"},"elapsed":{"elapsed":true}})json");
	message["parameters"] = Json::parse(R"json({"named_memory":{"memory":"report_amount"},
		"named_argument":{"argument":"detail"},"named_parameter":{"parameter":"report_detail"},
		"named_intent":{"intent":"speed_mm_s"},"named_observation":{"observation":"system.requested"},
		"named_elapsed":{"elapsed":true}})json");
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.child","context":"system","initial":"report",
		 "parameters":{"report_detail":{"type":"text","default":"child-default"}},
		 "memory":[{"name":"report_amount","type":"integer","initial":23}],
		 "states":[{"id":"report","entry":[
			{"action":"set_speed","arguments":[{"observation":"self.system"},1234]},
			{"action":"send_console_message","arguments":[{"observation":"self.system"},
			 {"observation":"self.console"},"test.notice",{"observation":"self.entity"},999,true,
			 "argument-detail","",1,30,1]},
			{"memory":"report_amount","value":99},
			{"action":"set_speed","arguments":[{"observation":"self.system"},4321]}]}]},
		{"id":"test.parent","context":"system","initial":"run",
		 "parameters":{"report_detail":{"type":"text","default":"parent-value"}},
		 "memory":[{"name":"report_amount","type":"integer","initial":88}],
		 "states":[{"id":"run","entry":[{"call":"test.child","parameters":{"report_detail":"child-value"}}]}]}
	])json");
	definitions["entities"][0]["controllers"] = {{"engine", "test.parent"}};
	return definitions;
}
Json with_latched_manual()
{
	auto definitions = fixture();
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.manual","context":"system","initial":"run","states":[{"id":"run","entry":[
			{"action":"set_flight_mode","arguments":[{"observation":"self.system"},1]},
			{"action":"set_longitudinal_thrust","arguments":[{"observation":"self.system"},1000]},
			{"action":"send_console_message","arguments":[{"observation":"self.system"},
			 {"observation":"self.console"},"test.notice",{"observation":"self.entity"},11,false,"manual","",1,30,1]}
		 ]}]}
	])json");
	definitions["entities"][0]["controllers"] = {{"engine", "test.manual"}};
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
	require(!world.submit(102, {1, "publish.drive-request", 0, {}}) &&
	            !world.submit(101, {2, "publish.drive-request", 0, {}}),
	        "publication admission enforces source Console and ship ownership");
	require(world.submit(101, {1, "publish.drive-request", 0, {}}) &&
	            world.submit(101, {1, "publish.drive-enabled", 0, {}}),
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
	require(world.submit(101, {1, "speed_mm_s", 500, "engine"}) &&
	            world.submit(101, {1, "engine_enabled", 0, "engine"}),
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
	require(!world.submit(102, {1, "message.test.request", 0, "engine"}),
	        "another Console cannot impersonate the message sender");
	require(world.submit(101, {1, "message.test.request", 0, "engine"}), "human request uses normal sender admission");
	require(world.submit(101, {1, "away", 1, {}}), "explicit Away can follow an admitted request");
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
	require(world.submit(101, {1, "delegate", 1, "engine"}), "owned System delegates to its ordinary controller");
	world.tick();
	require(!world.can_submit(101, {1, "speed_mm_s", 200, "engine"}),
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
	require(world.submit(101, {1, "preset", 1, {}}), "authored direct station preset is selectable");
	world.tick();
	require(world.can_submit(101, {1, "speed_mm_s", 200, "engine"}),
	        "direct preset returns effective control to the human");
	require(world.submit(101, {1, "away", 1, {}}), "explicit Away is admitted");
	world.tick();
	require(!world.can_submit(101, {1, "speed_mm_s", 200, "engine"}), "Away yields actuator authority to backfill");
	require(world.submit(101, {1, "away", 0, {}}), "player can return from explicit Away");
	world.tick();
	require(world.can_submit(101, {1, "speed_mm_s", 200, "engine"}),
	        "return restores current direct authority without rewriting machinery settings");
	World quiet;
	quiet.load(catalog, "test.map");
	claim_all(quiet);
	quiet.tick();
	require(quiet.submit(101, {1, "delegate", 1, "engine"}), "second report sender delegates");
	quiet.tick();
	require(quiet.submit(101, {1, "away", 1, {}}), "recipient goes Away while AI report is in flight");
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
	require(world.submit(101, {1, "delegate", 1, "engine"}), "cleanup controller receives delegated authority");
	world.tick();
	world.tick();
	const auto active = projection(world, 101);
	require(active.at("messages").size() == 1 && active.at("messages")[0].at("actionable") == true,
	        "controller enters live operation and delivers its fast standing request");
	require(!records(projection(world, 102), "shared", "drive-status").empty(),
	        "controller entry activates its declared live publication");
	require(world.submit(101, {1, "delegate", 0, "engine"}),
	        "human can take control from the running cleanup controller");
	world.tick();
	require(world.can_submit(101, {1, "speed_mm_s", 600, "engine"}),
	        "exit cleanup completes before human actuator authority is granted");
	const auto stopped = projection(world, 101);
	require(stopped.at("messages").size() == 1 && stopped.at("messages")[0].at("actionable") == false,
	        "exit withdrawal invalidates an unconsumed delivered request");
	const auto frozen = projection(world, 102).at("shared");
	require(world.submit(101, {1, "speed_mm_s", 600, "engine"}), "human changes intent after controller interruption");
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
	const auto held = projection(world, 101);
	require(held.at("messages").size() == 1 && held.at("messages")[0].at("actionable") == false,
	        "ordinary claim executes exit withdrawal before marking the System human-held");
	const auto frozen = projection(world, 102).at("shared");
	require(world.submit(101, {1, "speed_mm_s", 700, "engine"}), "ordinary takeover grants actuator authority");
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
		world.tick();
		require(world.submit(101, {1, "delegate", 1, "engine"}), "entry-only manual AI is delegated");
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
	require(!world.submit(101, {1, "message.test.notice", 999, "engine"}),
	        "human template command cannot invent unavailable AI memory or parameter bindings");
	world.tick();
	require(world.submit(101, {1, "delegate", 1, "engine"}), "typed sender receives delegated authority");
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
	small["trees"][1]["budget"] = 96;
	small["trees"][0]["memory"].push_back({{"name", "retained_note"}, {"type", "text"}, {"initial", "short"}});
	const auto small_catalog = load(small);
	const auto &program = small_catalog.tree_programs.at("test.parent");
	std::vector<state_tree::Value> observations(program.registry.observations.size());
	observations[static_cast<std::size_t>(GameplayTreeObservation::SelfEntity)] =
	    state_tree::Value::reference(state_tree::Type::Entity, 1);
	observations[static_cast<std::size_t>(GameplayTreeObservation::SelfSystem)] =
	    state_tree::Value::reference(state_tree::Type::System, 2);
	observations[static_cast<std::size_t>(GameplayTreeObservation::SelfConsole)] =
	    state_tree::Value::reference(state_tree::Type::Console, 3);
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
	require(world.submit(101, {1, "message.test.request", 0, "engine"}),
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
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.seek","context":"system","initial":"send","states":[{"id":"send","entry":[
			{"action":"send_system_message","arguments":[{"observation":"self.system"},
			 {"observation":"self.system"},"test.notice",{"observation":"self.entity"},11,false,"offer","",3,30,1]}
		 ]}]},
		{"id":"test.receiver","context":"system","initial":"wait","states":[
			{"id":"wait","transitions":[{"target":"received","conditions":[
			 {"left":{"observation":"message.available"},"op":"eq","right":true}]}]},
			{"id":"received","entry":[{"action":"set_enabled","arguments":[{"observation":"self.system"},false]}]}
		 ]}
	])json");
	ship["controllers"] = {{"engine", "test.seek"}, {"radar", "test.receiver"}};
	const auto catalog = load(definitions);
	World world;
	world.load(catalog, "test.map");
	claim_all(world);
	require(world.claim(105, 1, "away-reader"), "eligible reader can explicitly go Away");
	world.tick();
	require(world.submit(101, {1, "delegate", 1, "engine"}), "human-seeking sender remains delegated AI");
	world.tick();
	require(projection(world, 102).at("messages").empty(), "human-seeking routing respects authored delay");
	World resumed;
	resumed.restore(catalog, world.save());
	require(resumed.claim(104, 1, "assistant"), "human arriving during delivery delay may receive an offer");
	require(resumed.submit(105, {1, "away", 1, {}}), "Away removes a human from offer eligibility before delivery");
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
	require(records(offer, "records", "request").empty() && projection(resumed, 103).at("messages").empty() &&
	            projection(resumed, 105).at("messages").empty() && projection(resumed, 201).at("messages").empty(),
	        "extra offers do not ingest AI board data or reach forbidden, Away or other-ship readers");
	require(!resumed.can_submit(102, {1, "speed_mm_s", 700, "engine"}) &&
	            !resumed.can_submit(101, {1, "speed_mm_s", 700, "engine"}),
	        "offering work transfers no Console claim or delegated actuator authority");
	World recovered;
	recovered.restore(catalog, resumed.save());
	require(projection(recovered, 102).at("messages").size() == 1 &&
	            projection(recovered, 102).at("notifications").empty(),
	        "offer-only history persists without replaying a historical popup");
	require(recovered.submit(102, {1, "away", 1, {}}), "offered Console can return to AI after delivery");
	recovered.tick();
	recovered.tick();
	require(latest_field(projection(recovered, 102), "records", "support-private", "enabled") == true,
	        "human observer offers never become later AI inbox work after handover");
}
void reject_saved(const Catalog &catalog, const Json &state, const char *contract)
{
	World target;
	target.load(catalog, "test.map");
	const auto before = target.save();
	bool rejected = false;
	try
	{
		target.restore(catalog, state);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected, contract);
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
	auto omitted = state;
	remove_knowledge_ship(omitted, 2);
	reject_saved(catalog, omitted,
	             "knowledge continuation cannot omit a physical ship even with internally coherent bindings");
	auto definition_mismatch = state;
	definition_mismatch["knowledge"]["ships"][0]["definition"] = "test.variant";
	reject_saved(catalog, definition_mismatch,
	             "knowledge definition must match the restored physical entity definition");
	auto missing_ship = state;
	missing_ship["delegated"] = Json(std::map<EntityId, std::set<std::string>>{{99, {"engine"}}});
	reject_saved(catalog, missing_ship, "delegation cannot refer to an absent ship");
	auto missing_system = state;
	missing_system["delegated"] = Json(std::map<EntityId, std::set<std::string>>{{1, {"not-fitted"}}});
	reject_saved(catalog, missing_system, "delegation cannot refer to an unfitted System");
	auto bad_preset = state;
	bad_preset["preset"] = Json::array({{{"address", {{"ship", 1}, {"console", "helm"}}}, {"value", "not-authored"}}});
	reject_saved(catalog, bad_preset, "preset continuation must name an authored choice on that Console");
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
