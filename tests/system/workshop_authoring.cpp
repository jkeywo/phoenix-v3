#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

import phoenix.workshop.authoring_core;

using namespace phoenix;
using phoenix::test::require;
namespace
{
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<WorkshopAuthoring &>().document())>>);
Json run(WorkshopAuthoring &session, const std::string &op, const Json &args = Json::object())
{
	auto response = session.request({{"version", 1}, {"id", "test"}, {"operation", op}, {"arguments", args}});
	if (!response.at("ok").get<bool>())
		throw std::runtime_error(response.dump());
	return response.at("result");
}
Json t(const std::string &kind, const std::string &id, const std::string &path = {})
{
	return {{"kind", kind}, {"id", id}, {"path", path}};
}
void transactions()
{
	WorkshopAuthoring session;
	run(session, "create", {{"kind", "entities"}, {"id", "test.ship"}});
	const auto before = session.document().definitions();
	const auto missing = session.request({{"operation", "get"}, {"arguments", {{"target", t("effects", "missing")}}}});
	require(!missing.at("ok").get<bool>() && session.document().definitions() == before,
	        "failed read does not insert absent collections into the published document");
	auto failed = session.request(
	    {{"operation", "batch"},
	     {"arguments",
	      {{"requests",
	        Json::array({Json{{"operation", "set"},
	                          {"arguments",
	                           {{"target", t("entities", "test.ship")}, {"field", "name"}, {"value", "temporary"}}}},
	                     Json{{"operation", "instance.translate"},
	                          {"arguments", {{"ids", {999}}, {"dx", 1}, {"dy", 0}}}}})}}}});
	require(!failed.at("ok").get<bool>() && session.document().definitions() == before,
	        "failed batch preserves document");
	run(session, "batch",
	    {{"requests",
	      Json::array(
	          {Json{{"operation", "set"},
	                {"arguments", {{"target", t("entities", "test.ship")}, {"field", "name"}, {"value", "雪"}}}},
	           Json{{"operation", "set"},
	                {"arguments",
	                 {{"target", t("entities", "test.ship")}, {"field", "description"}, {"value", "draft"}}}}})}});
	run(session, "undo");
	require(session.document().definitions() == before, "one undo restores entire batch");
	run(session, "redo");
	require(run(session, "get", {{"target", t("entities", "test.ship")}}).at("name") == "雪", "Unicode survives redo");
	auto stale = session.request({{"operation", "delete"},
	                              {"expected_revision", 0},
	                              {"arguments", {{"kind", "entities"}, {"id", "test.ship"}}}});
	require(!stale.at("ok").get<bool>() && stale.at("error").at("code") == "stale_revision", "stale edits refused");
	const auto fields = workshop_fields();
	require(fields.contains("gameplay-editors.equipment.charge_per_second"),
	        "shared field registry is available headlessly");
	const auto &spec = fields.at("gameplay-editors.equipment.charge_per_second");
	Json value = Json::object();
	workshop_set_field(value, spec, 27);
	require(value.at("charge_per_second") == 27, "shared field setter");
	for (int i = 0; i < 2; ++i)
		run(session, "append", {{"target", {{"path", "/entities"}}}, {"prototype", "gameplay-editors.combat.decks"}});
	const auto &rows = session.document().definitions().at("entities");
	require(rows[1].at("id") != rows[2].at("id") && !rows[1].at("id").get<std::string>().empty(),
	        "record prototypes share GUI fresh identities");
}
void source_and_packages(const std::filesystem::path &root)
{
	const auto bytes = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
	auto pack = phoenix::test::fixture_pack(bytes, "test-core");
	WorkshopDocument d;
	d.load_pack(write_pack(pack));
	const auto entity = d.definitions().at("entities")[0].at("id").get<std::string>();
	WorkshopAuthoring session;
	session.reset(d);
	auto bulk_session = session;
	const auto common = run(bulk_session, "instance.fields", {{"ids", {1}}});
	require(!common.empty(), "bulk inspector describes fields through production instance resolution");
	run(bulk_session, "instance.field", {{"ids", {1}}, {"path", "/render/scale"}, {"value", 2.5}});
	require(bulk_session.document().map().at("entities")[0].at("overrides").at("render").at("scale") == 2.5,
	        "bulk inspector shares authored patch placement and numeric scale types");
	require(
	    !bulk_session
	         .request({{"operation", "instance.field"}, {"arguments", {{"ids", {1}}, {"path", "/id"}, {"value", 7}}}})
	         .at("ok")
	         .get<bool>(),
	    "bulk inspector refuses fields excluded by the shared schema");
	const auto palette = run(session, "layout.palette", {{"target", t("consoles", "test.console")}});
	require(palette.at("targets").size() > 0, "layout palette is discoverable without rendering");

	auto graph_session = session;
	const auto graph = run(graph_session, "ship.inspect", {{"entity", "test.variant"}});
	bool found_engine = false;
	for (const auto &node : graph.at("nodes"))
		if (node.at("path") == "/systems/engine")
		{
			found_engine = true;
			require(node.at("inherited"), "graph marks inherited systems");
		}
	require(found_engine, "graph discovers named systems without serialized item IDs");
	auto graph_gui = d;
	const auto gui_path = workshop_ship_graph(graph_gui, "test.variant", "override", {{"node", "/systems/engine"}});
	require(run(graph_session, "ship.override", {{"entity", "test.variant"}, {"node", "/systems/engine"}}) ==
	                gui_path &&
	            graph_gui.definitions() == graph_session.document().definitions(),
	        "graph GUI and CLI share inherited override behavior");
	const auto graph_before = graph_session.document().definitions();
	const auto bad_connection = graph_session.request({{"operation", "ship.connect"},
	                                                   {"arguments",
	                                                    {{"entity", "test.variant"},
	                                                     {"node", "/systems/engine"},
	                                                     {"field", "/power_store"},
	                                                     {"destination", "/systems/radar"}}}});
	require(!bad_connection.at("ok").get<bool>() && graph_session.document().definitions() == graph_before,
	        "graph incompatible connection refuses without mutation");

	run(graph_session, "ship.add",
	    {{"target", t("entities", "test.variant")}, {"id", "room"}, {"kind", "Compartment"}});
	for (const char *system : {"steering", "radar"})
		run(graph_session, "set",
		    {{"target", t("entities", "test.variant", std::string("/systems/") + system)},
		     {"field", "compartment"},
		     {"value", "room"}});
	run(graph_session, "ship.connect",
	    {{"entity", "test.variant"},
	     {"node", "/systems/engine"},
	     {"field", "/compartment"},
	     {"destination", "/combat/decks/0/compartments/0"}});
	require(
	    run(graph_session, "get", {{"target", t("entities", "test.variant", "/systems/engine")}}).at("compartment") ==
	        "room",
	    "compatible graph connections publish through production verification");
	const auto connected = graph_session.document().definitions();
	const auto cannot_disconnect = graph_session.request(
	    {{"operation", "ship.disconnect"},
	     {"arguments", {{"entity", "test.variant"}, {"node", "/systems/engine"}, {"field", "/compartment"}}}});
	require(!cannot_disconnect.at("ok").get<bool>() && graph_session.document().definitions() == connected,
	        "graph refuses disconnection of required physical bindings without partial edits");

	auto assembly_document = d;
	const Json radar{{"kind", "radar"}, {"range_mm", 100000}};
	assembly_document.definitions()["assemblies"] =
	    Json::array({Json{{"id", "test.sensor"}, {"systems", {{"radar", radar}}}}});
	assembly_document.definitions()["entities"][0]["assemblies"] =
	    Json::array({Json{{"id", "sensor"}, {"assembly", "test.sensor"}}});
	const auto assembly_graph = workshop_ship_graph(assembly_document, entity, "inspect", Json::object());
	bool assembly_found = false;
	for (const auto &node : assembly_graph.at("nodes"))
		if (node.at("value").contains("_assembly_origin") && node.at("category") == "systems")
		{
			assembly_found = true;
			(void)workshop_ship_graph(assembly_document, entity, "override", {{"node", node.at("path")}});
			require(assembly_document.definitions()
			                .at("entities")[0]
			                .at("assemblies")[0]
			                .at("overrides")
			                .at("systems")
			                .at("radar") == radar,
			        "assembly graph overrides retain local source identities without expanded metadata");
		}
	require(assembly_found, "synthetic assembly graph uses production expansion");
	// A GUI presentation buffer and CLI requests publish through the same boundary.
	auto gui = d;
	gui.definitions()["entities"][0]["name"] = "Shared authoring";
	WorkshopAuthoring gui_session;
	gui_session.reset(d);
	gui_session.commit(gui);
	run(session, "set",
	    {{"target", t("entities", d.definitions()["entities"][0].at("id"))},
	     {"field", "name"},
	     {"value", "Shared authoring"}});
	require(gui_session.document().definitions() == session.document().definitions(),
	        "GUI buffer and CLI edits publish equivalent content");
	const auto valid = session.document().definitions();
	const auto child_verified = session.request(
	    {{"operation", "batch"},
	     {"arguments",
	      {{"requests",
	        Json::array({Json{{"operation", "set"},
	                          {"arguments",
	                           {{"target", t("entities", entity, "/collision")},
	                            {"field", "mass_grams"},
	                            {"value", "unfinished"},
	                            {"verify", true}}}},
	                     Json{{"operation", "set"},
	                          {"arguments",
	                           {{"target", t("entities", entity, "/collision")},
	                            {"field", "mass_grams"},
	                            {"value", valid.at("entities")[0].at("collision").at("mass_grams")}}}}})}}}});
	require(!child_verified.at("ok").get<bool>() && session.document().definitions() == valid,
	        "batch respects verification on each child before later repairs");

	auto verified_edit = session.request({{"operation", "set"},
	                                      {"arguments",
	                                       {{"target", t("entities", entity, "/collision")},
	                                        {"field", "mass_grams"},
	                                        {"value", "unfinished"},
	                                        {"verify", true}}}});
	require(!verified_edit.at("ok").get<bool>() && session.document().definitions() == valid,
	        "explicit verified edits refuse invalid bindings before publication");
	run(session, "import.bytes", {{"type", "asset"}, {"filename", "assets/test.bin"}, {"bytes", Bytes{1, 2, 3}}});
	run(session, "undo");
	require(!session.document().pack().files.contains("assets/test.bin"), "undo includes imported assets");
	run(session, "redo");
	require(session.document().pack().files.at("assets/test.bin") == Bytes({1, 2, 3}),
	        "redo restores imported resources");
	run(session, "rename", {{"kind", "entities"}, {"id", entity}, {"to", "test.renamed"}});
	run(session, "verify");
	workshop_save_source(session.document(), (root / "source").string());
	WorkshopDocument reopened;
	reopened.load_draft(workshop_read_source((root / "source").string()));
	require(reopened.definitions() == session.document().definitions(), "authored source roundtrip");
	workshop_export_map(reopened, (root / "map.json").string());
	require(Json::parse(read_file((root / "map.json").string())) == reopened.map(),
	        "standalone map export shares verified GUI serialization");
	workshop_export(reopened, (root / "fixture.zip").string());
	workshop_export(reopened, (root / "fixture.dat").string());
	WorkshopDocument zip, dat;
	zip.load_pack(read_file((root / "fixture.zip").string()));
	dat.load_pack(read_file((root / "fixture.dat").string()));
	require(zip.verify().entities == dat.verify().entities, "ZIP and DAT resolve the same synthetic entities");
	workshop_save_source(dat, (root / "dat-source").string());
	auto restored_source = workshop_read_source((root / "dat-source").string());
	require(!compiled_image(restored_source.files.at("assets/pixel.png")),
	        "DAT source saving restores editable PNG assets");
	WorkshopDocument restored;
	restored.load_draft(std::move(restored_source));
	(void)restored.verify();
	auto invalid = reopened;
	invalid.definitions()["entities"][0]["collision"]["mass_grams"] = "unfinished";
	workshop_save_source(invalid, (root / "unfinished").string());
	WorkshopDocument draft;
	draft.load_draft(workshop_read_source((root / "unfinished").string()));
	require(draft.definitions() == invalid.definitions(), "unfinished draft survives save/reopen");
	const auto previous = read_file((root / "fixture.zip").string());
	bool refused = false;
	try
	{
		workshop_export(invalid, (root / "fixture.zip").string());
	}
	catch (...)
	{
		refused = true;
	}
	require(refused && previous == read_file((root / "fixture.zip").string()), "invalid export preserves output");
	refused = false;
	try
	{
		workshop_write_files(root.string(), {{"../escape", Bytes{1}}});
	}
	catch (...)
	{
		refused = true;
	}
	require(refused, "output traversal refused");
	// Library definitions are readable but cannot be silently edited.
	WorkshopDocument mod;
	mod.load_dependency(write_pack(pack));
	WorkshopAuthoring mod_session;
	mod_session.reset(mod);
	auto denied = mod_session.request(
	    {{"operation", "set"},
	     {"arguments", {{"target", t("entities", entity)}, {"field", "name"}, {"value", "silent"}}}});
	require(!denied.at("ok").get<bool>(), "dependency writes require explicit override");
	require(run(mod_session, "get", {{"target", t("entities", entity)}}).contains("systems"),
	        "dependency definitions are readable without an override");
	run(mod_session, "override", {{"kind", "entities"}, {"id", entity}});
	run(mod_session, "set", {{"target", t("entities", entity)}, {"field", "name"}, {"value", "explicit"}});
	require(run(mod_session, "get", {{"target", t("entities", entity)}}).at("name") == "explicit",
	        "explicit overrides editable");
	mod.load_draft(d.source_draft());
	require(mod.libraries().ordered_packs.empty(), "opening a standalone source draft drops unrelated libraries");
	(void)mod.verify();
	// The CLI and the canvas use this exact move/wrap implementation.
	Json layout{{"id", "root"},
	            {"kind", "column"},
	            {"children", Json::array({Json{{"id", "a"}, {"kind", "component"}, {"component", "gameplay"}}})}};
	auto direct = layout;
	const Json drop{{"destination", "root"}, {"kind", "row"}, {"index", 0}};
	(void)workshop_layout_drop(direct, drop, d.verify());
	run(session, "create", {{"kind", "layouts"}, {"id", "test.layout"}, {"value", {{"schema", 1}, {"root", layout}}}});
	auto request = drop;
	request["target"] = t("layouts", "test.layout", "/root");
	run(session, "layout.drop", request);
	require(run(session, "get", {{"target", t("layouts", "test.layout", "/root")}}) == direct,
	        "layout GUI and CLI share wrapping behavior");
	request["source"] = "root.panel1";
	run(session, "layout.drop", request);
	require(run(session, "get", {{"target", t("layouts", "test.layout", "/root")}}) == direct,
	        "layout moves accept stable source identities");
}
void semantic_parity(const std::filesystem::path &root)
{
	Json participant{{"actor", {{"participant", "boarder"}}}};
	require(workshop_control("actor.source", participant, {{"field", "actor"}}).at("value") == "participant",
	        "Shared actor controls preserve an authored task participant");
	(void)workshop_set_control(participant, "actor.source", "slot", {{"field", "actor"}});
	require(participant.at("actor") == Json{{"slot", ""}}, "Changing actor source replaces the selector atomically");
	WorkshopAuthoring session;
	run(session, "create", {{"kind", "trees"}, {"id", "test.base"}, {"context", "scenario"}});
	const auto base = run(session, "get", {{"target", t("trees", "test.base")}});
	require(base.at("initial") == "operating" && base.at("states")[0] == workshop_tree_state("operating"),
	        "tree GUI and CLI factories agree");
	run(session, "create", {{"kind", "trees"}, {"id", "test.child"}, {"parent", "test.base"}});
	require(!run(session, "get", {{"target", t("trees", "test.child")}}).contains("context"),
	        "derived tree does not shadow parent context");
	run(session, "tree.state.add", {{"target", t("trees", "test.base")}, {"id", "waiting"}});
	run(session, "tree.task.add",
	    {{"target", t("trees", "test.base")}, {"state", "waiting"}, {"phase", "entry"}, {"kind", "action"}});
	const auto vocabulary = run(session, "vocabulary", {{"target", t("trees", "test.base")}});
	require(!vocabulary.at("actions").empty(), "production action signatures available");
	run(session, "set",
	    {{"target", t("trees", "test.base", "/states/1/entry/0")},
	     {"field", "action"},
	     {"value", vocabulary.at("actions")[0].at("id")}});
	require(run(session, "tree.task.signature",
	            {{"target", t("trees", "test.base", "/states/1/entry/0")}, {"tree", t("trees", "test.base")}}) ==
	            workshop_action_arguments(vocabulary.at("actions")[0]),
	        "signature arguments use shared production defaults");
	const auto before = session.document().definitions();
	const auto denied = session.request({{"operation", "tree.state.delete"},
	                                     {"arguments", {{"target", t("trees", "test.base")}, {"id", "operating"}}}});
	require(!denied.at("ok").get<bool>() && before == session.document().definitions(),
	        "referenced state removal rolls back");
	run(session, "create", {{"kind", "entities"}, {"id", "test.owner"}});
	run(session, "tree.localize",
	    {{"target", t("entities", "test.owner")}, {"field", "tree"}, {"context", "scenario"}});
	const auto local = run(session, "get", {{"target", t("entities", "test.owner", "/tree")}});
	run(session, "tree.promote", {{"target", t("entities", "test.owner", "/tree")}, {"id", "test.promoted"}});
	run(session, "set", {{"target", t("entities", "test.owner")}, {"field", "name"}, {"value", "test.promoted"}});
	run(session, "rename", {{"kind", "trees"}, {"id", "test.promoted"}, {"to", "test.renamed"}});
	const auto owner = run(session, "get", {{"target", t("entities", "test.owner")}});
	require(owner.at("tree") == "test.renamed" && owner.at("name") == "test.promoted",
	        "tree rename repairs bindings without rewriting prose");
	run(session, "tree.localize", {{"target", t("entities", "test.owner")}, {"field", "tree"}, {"copy", true}});
	require(run(session, "get", {{"target", t("entities", "test.owner", "/tree")}}) == local,
	        "copy of reusable tree stays authored and local");
	run(session, "tree.localize", {{"target", t("entities", "test.owner")}, {"field", "policy"}});
	run(session, "tree.promote", {{"target", t("entities", "test.owner", "/policy")}, {"id", "test.policy"}});
	require(run(session, "get", {{"target", t("entities", "test.owner", "/policy")}}) ==
	            Json{{"extends", "test.policy"}},
	        "policy promotion preserves binding encoding");
	// Clearing an optional choice erases the source field; returning the result must not throw.
	for (const auto &[id, spec] : workshop_fields().items())
		if (spec.value("optional", false) && spec.contains("choices") && !spec.at("choices").empty())
		{
			run(session, "set",
			    {{"target", t("entities", "test.owner")},
			     {"field", spec.at("key")},
			     {"descriptor", id},
			     {"value", spec.at("choices")[0]}});
			require(run(session, "set",
			            {{"target", t("entities", "test.owner")},
			             {"field", spec.at("key")},
			             {"descriptor", id},
			             {"value", ""}})
			            .is_null(),
			        "optional choice clears successfully");
			break;
		}
	run(session, "create",
	    {{"kind", "messages"}, {"id", "test.message"}, {"value", {{"description", "test.message"}}}});
	run(session, "create", {{"kind", "effects"}, {"id", "test.message"}, {"value", Json::object()}});
	run(session, "rename", {{"kind", "messages"}, {"id", "test.message"}, {"to", "test.other"}});
	require(run(session, "get", {{"target", t("messages", "test.other")}}).at("description") == "test.message" &&
	            run(session, "get", {{"target", t("effects", "test.message")}}).at("id") == "test.message",
	        "generic identity edits preserve prose and other namespaces");
	run(session, "create", {{"kind", "consoles"}, {"id", "test.console"}});
	const auto console_target = t("consoles", "test.console");
	const auto widget =
	    run(session, "widget.add", {{"target", console_target}, {"kind", "radar"}, {"layout", "/layout/root"}});
	auto direct = session.document();
	auto &gui_console = direct.definitions()["consoles"][0];
	const Json args{{"kind", "row"}};
	const auto added = workshop_layout_action(direct, gui_console["layout"]["root"], "node.add", args);
	run(session, "layout.node.add", {{"target", t("consoles", "test.console", "/layout/root")}, {"kind", "row"}});
	require(gui_console == run(session, "get", {{"target", console_target}}),
	        "layout GUI adapter and CLI produce identical content");
	run(session, "layout.node.duplicate",
	    {{"target", t("consoles", "test.console", "/layout/root")},
	     {"node", gui_console.at("layout").at("root").at("id").get<std::string>() + "." + widget.get<std::string>()},
	     {"console", "test.console"}});
	require(run(session, "get", {{"target", console_target}}).at("widgets").size() == 2,
	        "panel duplication clones widget binding");
	run(session, "layout.save-template", {{"target", t("consoles", "test.console", "/layout")}, {"id", "test.layout"}});
	run(session, "layout.reference", {{"target", console_target}, {"id", "test.layout"}});
	run(session, "layout.override", {{"target", console_target}});
	run(session, "set",
	    {{"target", t("consoles", "test.console", "/layout_overrides/root")},
	     {"field", "label"},
	     {"value", "Local label"}});
	run(session, "layout.detach", {{"target", console_target}});
	require(run(session, "get", {{"target", console_target}}).at("layout").at("root").at("label") == "Local label",
	        "detach retains local layout overrides");
	run(session, "layout.compact", {{"target", t("consoles", "test.console", "/layout")}, {"enabled", true}});
	require(run(session, "get", {{"target", console_target}}).at("layout").contains("compact"),
	        "compact layout authored");
	const auto seeds = run(session, "describe", {{"seeds", true}});
	run(session, "seed", {{"target", t("entities", "test.owner")}, {"seed", "gameplay-editors.equipment.processor"}});
	require(run(session, "get", {{"target", t("entities", "test.owner", "/processor")}}) ==
	            seeds.at("gameplay-editors.equipment.processor").at("value"),
	        "GUI optional block defaults are discoverable and applied headlessly");
	run(session, "create", {{"kind", "maps"}, {"id", "test.task-map"}});
	run(session, "seed", {{"target", t("maps", "test.task-map")}, {"seed", "gameplay-editors.scenarios.scenario"}});
	run(session, "seed",
	    {{"target", t("maps", "test.task-map", "/scenario")}, {"seed", "gameplay-editors.scenarios.task_plan"}});
	const auto task_plan = run(session, "get", {{"target", t("maps", "test.task-map", "/scenario/task_plan")}});
	require(task_plan == seeds.at("gameplay-editors.scenarios.task_plan").at("value"),
	        "task-plan GUI and CLI seed share the canonical defaults");
	run(session, "undo");
	require(!run(session, "get", {{"target", t("maps", "test.task-map", "/scenario")}}).contains("task_plan"),
	        "task-plan insertion is one reversible authoring step");
	run(session, "redo");
	workshop_export_source(session.document(), (root / "unfinished.zip").string());
	WorkshopDocument reopened;
	reopened.load_draft(workshop_read_source((root / "unfinished.zip").string()));
	require(reopened.definitions() == session.document().definitions(), "unfinished source ZIP roundtrip");
	require(!session
	             .request({{"operation", "import.bytes"},
	                       {"arguments", {{"type", "asset"}, {"filename", "assets/bad.bin"}, {"bytes", {256}}}}})
	             .at("ok")
	             .get<bool>(),
	        "binary byte overflow refused");
	require(!session.request({{"operation", "list"}, {"arguments", {{"typo", true}}}}).at("ok").get<bool>(),
	        "registry rejects unknown arguments");
}
void shared_controls()
{
	WorkshopAuthoring session;
	run(session, "create", {{"kind", "entities"}, {"id", "test.controls"}});
	auto gui = session.document();
	auto &gui_render = gui.definitions()["entities"][0]["render"];
	(void)workshop_set_control(gui_render, "render.billboard", true);
	run(session, "control.set",
	    {{"target", t("entities", "test.controls", "/render")}, {"control", "render.billboard"}, {"value", true}});
	require(gui.definitions() == session.document().definitions(),
	        "GUI control helper and CLI share coupled billboard changes");
	require(run(session, "get", {{"target", t("entities", "test.controls", "/render")}}).at("images").size() == 8,
	        "billboard control changes image cardinality");
	run(session, "control.set",
	    {{"target", t("entities", "test.controls", "/collision")}, {"control", "collision.shape"}, {"value", "box"}});
	require(!run(session, "get", {{"target", t("entities", "test.controls", "/collision")}}).contains("radius_mm"),
	        "shape control removes incompatible source fields");
	Json frame{{"rect", {0, 0, 120, 80}}, {"pivot", {60, 40}}, {"attachments", {{"engine", {{"x", 0}, {"y", 0}}}}}};
	run(session, "set", {{"target", t("entities", "test.controls")}, {"field", "test_frame"}, {"value", frame}});
	const auto target = t("entities", "test.controls", "/test_frame");
	const auto described =
	    run(session, "controls",
	        {{"target", target}, {"control", "frame.marker.x"}, {"context", {{"attachment", "engine"}}}});
	require(described.at("maximum") == 120 && described.at("path") == "/attachments/engine/x",
	        "dynamic bounds and paths are backend-derived");
	auto *attachment = &frame["attachments"]["engine"];
	(void)workshop_set_control(frame, "frame.marker.x", 999, {{"attachment", "engine"}});
	require(&frame["attachments"]["engine"] == attachment, "backend publication preserves GUI child references");
	run(session, "control.set",
	    {{"target", target}, {"control", "frame.marker.x"}, {"value", 999}, {"context", {{"attachment", "engine"}}}});
	require(run(session, "get", {{"target", target}}) == frame,
	        "CLI vector/marker controls use the same dynamic limits");
	const auto before = session.document().definitions();
	auto refused =
	    session.request({{"operation", "control.set"},
	                     {"arguments", {{"target", target}, {"control", "frame.pivot"}, {"value", {1, 2, 3}}}}});
	require(!refused.at("ok").get<bool>() && session.document().definitions() == before,
	        "wrong-width vector control refuses atomically");
	Json invalid_context{{"target", target},
	                     {"control", "frame.marker.x"},
	                     {"context", Json{{"attachment", "engine"}, {"maximum", 10000}}}};
	refused = session.request({{"operation", "controls"}, {"arguments", invalid_context}});
	require(!refused.at("ok").get<bool>(), "callers cannot substitute arbitrary dynamic control bounds");
	run(session, "authoring.apply", {{"target", target}, {"action", "sprite.attachment.add"}});
	require(run(session, "get", {{"target", target}}).at("attachments").at("engine_1").at("x") == 60,
	        "attachment action uses authored pivot");
	run(session, "set", {{"target", t("entities", "test.controls")}, {"field", "dust"}, {"value", {{"opacity", .25}}}});
	const auto dust = t("entities", "test.controls", "/dust");
	const auto opacity = run(session, "controls",
	                         {{"target", dust}, {"control", "dust.opacity"}, {"context", {{"platform", "native"}}}});
	require(opacity.at("value") == .25 && !run(session, "get", {{"target", dust}}).contains("native"),
	        "platform control discovery inherits shared data without authoring overrides");
	run(session, "control.set",
	    {{"target", dust}, {"control", "dust.opacity"}, {"value", 2.0}, {"context", {{"platform", "native"}}}});
	require(run(session, "get", {{"target", dust}}).at("native").at("opacity") == 1.0,
	        "platform control applies shared constraints");
	run(session, "set", {{"target", dust}, {"field", "textures"}, {"value", {"one", "two", "three"}}});
	run(session, "control.set",
	    {{"target", dust},
	     {"control", "dust.texture"},
	     {"value", "changed"},
	     {"context", {{"platform", "native"}, {"index", 1}}}});
	require(run(session, "get", {{"target", dust}}).at("native").at("textures") ==
	            Json::array({"one", "changed", "three"}),
	        "platform texture edits preserve inherited companion layers");
	run(session, "set",
	    {{"target", t("entities", "test.controls")},
	     {"field", "operation"},
	     {"value", {{"kind", "fill"}, {"store", "old"}, {"target", "protected"}}}});
	run(session, "set",
	    {{"target", t("entities", "test.controls", "/operation")},
	     {"field", "kind"},
	     {"value", "remove"},
	     {"descriptor", "gameplay-editors.effects.kind"}});
	const auto operation = run(session, "get", {{"target", t("entities", "test.controls", "/operation")}});
	require(operation.at("target") == "protected" && !operation.contains("store"),
	        "ordinary descriptor setters cannot bypass shared change handlers");
	run(session, "set",
	    {{"target", t("entities", "test.controls")},
	     {"field", "panel"},
	     {"value", {{"rect", {{"x", 100}, {"y", 100}, {"width", 400}, {"height", 300}}}}}});
	run(session, "authoring.apply",
	    {{"target", t("entities", "test.controls", "/panel")},
	     {"action", "layout.rect.gesture"},
	     {"arguments",
	      {{"rect", {{"x", 100}, {"y", 100}, {"width", 400}, {"height", 300}}},
	       {"dx", 17},
	       {"dy", 999},
	       {"resize", false},
	       {"snap", true}}}});
	const auto rect = run(session, "get", {{"target", t("entities", "test.controls", "/panel/rect")}});
	require(rect.at("x") == 120 && rect.at("y") == 700, "instrument gesture shares snapping and containment rules");
	const auto cost =
	    run(session, "record.defaults", {{"prototype", "effect.cost"}, {"context", {{"quantity", "per_second"}}}});
	require(cost.contains("per_second") && !cost.contains("amount"),
	        "dynamic record factories expose the GUI quantity default");
	const auto report =
	    run(session, "record.defaults", {{"prototype", "scenario.report"}, {"context", {{"slots", {"test.slot"}}}}});
	require(report.at("ship").at("slot") == "test.slot",
	        "scenario report factory derives its actor from explicit synthetic context");
	run(session, "set",
	    {{"target", t("entities", "test.controls")}, {"field", "property_values"}, {"value", Json::object()}});
	run(session, "control.set",
	    {{"target", t("entities", "test.controls", "/property_values")},
	     {"control", "property.value"},
	     {"value", true},
	     {"context", {{"field", "flag"}, {"definition", {{"kind", "flag"}}}}}});
	require(run(session, "get", {{"target", t("entities", "test.controls", "/property_values")}}).at("flag") == true,
	        "typed property control is usable without GUI constructors");
	run(session, "control.set",
	    {{"target", t("entities", "test.controls", "/property_values")},
	     {"control", "binding.source"},
	     {"value", "reference"},
	     {"context", {{"field", "binding"}}}});
	require(run(session, "get", {{"target", t("entities", "test.controls", "/property_values/binding")}}).at("type") ==
	            "entity",
	        "tree binding control owns source encoding");
	require(workshop_colour_text(workshop_colour_channels("#204080")) == "#204080ff",
	        "theme colour conversion is shared with CLI tooling");
	std::set<std::string> actions;
	for (const auto &row : run(session, "authoring.actions"))
	{
		require(actions.insert(row.at("id").get<std::string>()).second, "registered action identities are unique");
		require(row.at("arguments").contains("properties"), "action schemas are generated alongside handlers");
	}
}
void identity_targets()
{
	WorkshopAuthoring session;
	run(session, "create",
	    {{"kind", "trees"},
	     {"id", "test.tree"},
	     {"value",
	      {{"states", Json::array({Json{{"id", "root"}}, Json{{"id", "child"}, {"parent", "root"}}})},
	       {"initial", "root"}}}});
	auto stable = run(session, "target", {{"target", t("trees", "test.tree", "/states/1")}});
	run(session, "patch",
	    {{"target", t("trees", "test.tree", "/states")},
	     {"patch", Json::array({Json{{"op", "add"}, {"path", "/0"}, {"value", {{"id", "inserted"}}}}})}});
	run(session, "set", {{"target", stable}, {"field", "label"}, {"value", "correct child"}});
	require(run(session, "get", {{"target", t("trees", "test.tree", "/states/2")}}).at("label") == "correct child",
	        "target follows ID across insertion");
	const auto before = session.document().definitions();
	auto failed =
	    session.request({{"operation", "tree.reparent"},
	                     {"arguments", {{"target", t("trees", "test.tree")}, {"state", "root"}, {"parent", "child"}}}});
	require(!failed.at("ok").get<bool>() && session.document().definitions() == before,
	        "cycle refuses without mutation");
	run(session, "tree.rename", {{"target", t("trees", "test.tree")}, {"from", "child"}, {"to", "renamed"}});
	require(run(session, "get", {{"target", t("trees", "test.tree", "/states/2")}}).at("id") == "renamed",
	        "state identity rename is shared");
}
} // namespace
int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() / ("phoenix-authoring-" + random_token());
		std::filesystem::create_directories(root);
		transactions();
		identity_targets();
		semantic_parity(root);
		shared_controls();
		source_and_packages(root);
		std::filesystem::remove_all(root);
		std::cout << "Workshop authoring transactions, discovery, identities, drafts and packages passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
