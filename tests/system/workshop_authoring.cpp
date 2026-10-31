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
