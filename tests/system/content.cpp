#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

import phoenix.content;

using namespace phoenix;
void require(bool value, const char *contract)
{
	if (!value)
		throw std::runtime_error(contract);
}
template <class Function> void rejected(Function function, const std::string &diagnostic)
{
	try
	{
		function();
	}
	catch (const std::exception &error)
	{
		if (std::string(error.what()).find(diagnostic) != std::string::npos)
			return;
		throw;
	}
	throw std::runtime_error("Expected refusal: " + diagnostic);
}
int main()
{
	try
	{
		PackDraft base;
		base.manifest = {{"schema", 1}, {"id", "test-core"}};
		base.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		base.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		auto bytes = write_pack(base);
		auto loaded = read_pack(bytes);
		require(loaded.hash() == sha256(bytes), "pack identity must be the SHA256 of transferred archive bytes");
		auto catalog = resolve_packs({loaded});
		WorkshopDocument console_document;
		console_document.load_pack(bytes);
		console_document.definitions()["consoles"].push_back(
		    {{"id", "test.child-console"}, {"extends", "test.console"}});
		auto &station_ship = console_document.definitions()["entities"][1];
		station_ship["stations"] =
		    Json::array({{{"id", "watch"},
		                  {"consoles", Json::array({{{"id", "controls"},
		                                             {"definition", "test.console"},
		                                             {"systems", {"engine", "steering", "radar"}},
		                                             {"feeds", Json::array()}}})}}});
		station_ship.erase("console");
		console_document.rename_console("test.console", "test.bridge");
		const auto &renamed_consoles = console_document.definitions();
		require(renamed_consoles["entities"][0]["console"] == "test.bridge" &&
		            renamed_consoles["entities"][1]["stations"][0]["consoles"][0]["definition"] == "test.bridge" &&
		            renamed_consoles["entities"][1]["stations"][0]["consoles"][0]["id"] == "controls" &&
		            renamed_consoles["consoles"][1]["extends"] == "test.bridge",
		        "Console rename repairs legacy, explicit Station and inheritance references without renaming runtime "
		        "seats");
		const auto before_console_refusal = console_document.definitions();
		rejected(
		    [&]
		    {
			    console_document.rename_console("test.bridge", "test.ship");
		    },
		    "already exists");
		require(console_document.definitions() == before_console_refusal,
		        "conflicting Console rename preserves the document");
		console_document.new_map("test.added-map");
		const auto console_round_trip = resolve_packs({read_pack(console_document.export_pack())});
		require(console_round_trip.consoles.contains("test.bridge") &&
		            console_round_trip.maps.contains("test.added-map"),
		        "saving a whole pack retains added maps and renamed Consoles together");
		// Navigation aids use the real content resolver and authoring round trip.
		auto aids = loaded.draft();
		auto aid_defs = parse_json(aids.files.at("definitions.json"), "synthetic navigation aids");
		aid_defs["entities"][0]["render"]["reference_grid"] = {{"minor_spacing", 7}, {"major_spacing", 35}};
		aid_defs["maps"][0]["presentation"]["dust"] = {{"count", 31},
		                                               {"native", {{"width", .2}}},
		                                               {"web", {{"count", 13}}},
		                                               {"textures", {"test-core:assets/pixel.png", "", ""}}};
		aids.files["definitions.json"] = json_bytes(aid_defs);
		const auto aid_archive = write_pack(aids);
		const auto aid_catalog = resolve_packs({read_pack(aid_archive)});
		const auto &dust = aid_catalog.maps.at("test.map").at("presentation").at("dust");
		const auto native_dust = compile_dust(dust, aid_catalog, false, "dust"),
		           web_dust = compile_dust(dust, aid_catalog, true, "dust");
		require(native_dust.count == 31 && web_dust.count == 13 && native_dust.width > web_dust.width &&
		            native_dust.textures[0] == "test-core:assets/pixel.png" && web_dust.enabled,
		        "Platform overrides preserve shared dust settings and default missing fields");
		const auto grid =
		    compile_reference_grid(aid_catalog.entities.at("test.variant").at("render").at("reference_grid"), "grid");
		require(grid.minor_spacing == 7 && grid.major_spacing == 35 && grid.patch_radius > 0,
		        "Grid inheritance and default resolution use the production content path");
		WorkshopDocument aid_document;
		aid_document.load_pack(aid_archive);
		const auto reopened_aids = resolve_packs({read_pack(aid_document.export_pack())});
		require(reopened_aids.maps.at("test.map").at("presentation").at("dust") == dust,
		        "Workshop export and reimport preserve shared and platform tuning");
		for (const auto &patch : {Json{{"count", 10001}}, Json{{"count", 2.5}}, Json{{"opacity", -1}},
		                          Json{{"centre_fade_outer", .01}}, Json{{"web", {{"enabled", "yes"}}}},
		                          Json{{"textures", {"missing:assets/a.png", "", ""}}}, Json{{"depth", 0}}})
		{
			auto invalid = aid_defs;
			invalid["maps"][0]["presentation"]["dust"].update(patch);
			aids.files["definitions.json"] = json_bytes(invalid);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(aids))});
			    },
			    "presentation.dust");
		}
		for (const auto &patch :
		     {Json{{"major_spacing", 12}}, Json{{"fade_band", 500}}, Json{{"minor_line_width_px", 0}},
		      Json{{"minor_colour", {1, 2, 0, .5}}}, Json{{"first_person_plane_y", "down"}}})
		{
			auto invalid = aid_defs;
			invalid["entities"][0]["render"]["reference_grid"].update(patch);
			aids.files["definitions.json"] = json_bytes(invalid);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(aids))});
			    },
			    "reference_grid");
		}
		const auto menu = launcher_catalog(catalog);
		require(menu.at("schema") == 1 && menu.at("scenarios").size() == 1 &&
		            menu.at("scenarios")[0].at("id") == "test.map" &&
		            menu.at("scenarios")[0].at("slots")[0].at("ships")[0].at("id") == "test.ship",
		        "HTML launch hints come from the production-resolved synthetic catalogue");
		// A synthetic material travels through the same archive, inheritance,
		// presentation sanitiser and Workshop paths as authored game content.
		auto sprite_draft = loaded.draft();
		auto sprite_defs = parse_json(sprite_draft.files.at("definitions.json"), "synthetic sprite");
		sprite_defs["schema"] = 3;
		Json sprite = {
		    {"radar_icon", "ship"},
		    {"albedo", "test-core:assets/pixel.png"},
		    {"depth", "test-core:assets/pixel.png"},
		    {"frames",
		     Json::array({{{"name", "front"},
		                   {"rect", {0, 0, 1, 1}},
		                   {"pivot", {.5, .5}},
		                   {"azimuth", 0},
		                   {"depth_scale", .7},
		                   {"depth_offset", -.1},
		                   {"attachments",
		                    {{"engine_port", {{"x", .5}, {"y", .5}, {"layer", -1}, {"depth_offset", .05}}}}}}})}};
		sprite_defs["entities"][0]["render"]["sprite"] = sprite;
		sprite_defs["maps"][0]["lighting"] = {{"sun_direction", {1, .5, -.5}}, {"hatch_thickness", 2}};
		sprite_draft.files["definitions.json"] = json_bytes(sprite_defs);
		auto sprite_archive = write_pack(sprite_draft);
		auto sprite_catalog = resolve_packs({read_pack(sprite_archive)});
		const auto compiled = compile_sprite(sprite, sprite_catalog, "test material");
		// A complete sidecar configures a hull transactionally, including its
		// public PNG icon and render size, through the real authoring/loader path.
		auto complete_sidecar = Json{{"schema_version", 1},
		                             {"id", "synthetic"},
		                             {"coordinate_system", "frame_pixels_top_left_y_down"},
		                             {"image_size", {1, 1}},
		                             {"pivot", {.5, .5}},
		                             {"markers", Json::object()},
		                             {"render_scale", 7},
		                             {"billboard_mode", "camera"},
		                             {"files", {{"sprite", "pixel.png"}, {"radar_icon", "icon.png"}}}};
		Json imported_render = {{"mode", "eight"}, {"scale", 1}, {"images", Json::array()}};
		const std::map<std::string, std::string> mapped_images = {{"sprite", "test-core:assets/pixel.png"},
		                                                          {"radar_icon", "test-core:assets/pixel.png"}};
		apply_sprite_sidecar(imported_render, complete_sidecar, mapped_images, sprite_catalog);
		require(imported_render.at("mode") == "camera" && imported_render.at("scale") == 7 &&
		            imported_render.at("sprite").at("radar_sprite") == "test-core:assets/pixel.png" &&
		            !imported_render.contains("images"),
		        "sidecar applies mode, scale and an imported radar sprite together");
		auto eight_sidecar = complete_sidecar;
		eight_sidecar["billboard_mode"] = "eight";
		eight_sidecar["sprite_sheet"] = {{"frames", Json::array()}};
		for (int i = 0; i < 8; ++i)
			eight_sidecar["sprite_sheet"]["frames"].push_back({{"name", std::to_string(i)},
			                                                   {"rect", {0, 0, 1, 1}},
			                                                   {"pivot", {.5, .5}},
			                                                   {"camera_azimuth_degrees", i * 45},
			                                                   {"markers", Json::object()}});
		apply_sprite_sidecar(imported_render, eight_sidecar, mapped_images, sprite_catalog);
		require(imported_render.at("mode") == "eight" && imported_render.at("sprite").at("frames").size() == 8,
		        "eight-bearing sidecar sets the render mode without separate image selection");
		eight_sidecar["billboard_mode"] = "camera";
		const auto before_refusal = imported_render;
		rejected(
		    [&]
		    {
			    apply_sprite_sidecar(imported_render, eight_sidecar, mapped_images, sprite_catalog);
		    },
		    "billboard_mode");
		require(imported_render == before_refusal, "incompatible sidecar leaves the current sprites intact");
		sprite_defs["entities"][0]["render"] = imported_render;
		sprite_draft.files["definitions.json"] = json_bytes(sprite_defs);
		const auto radar_catalog = resolve_packs({read_pack(write_pack(sprite_draft))});
		const auto radar_bundle = resolve_packs({console_assets(radar_catalog, "test.ship")});
		const auto icon_id = render_radar_sprite(imported_render, radar_catalog);
		WorkshopDocument new_mod;
		new_mod.load_dependency(write_pack(sprite_draft));
		new_mod.create_entity();
		new_mod.verify();
		require(!icon_id.empty() && radar_bundle.assets.contains(icon_id) && radar_catalog.assets.contains(icon_id),
		        "public radar sprites have the same content address locally and in the restricted crew bundle");
		auto missing_icon = imported_render;
		missing_icon["sprite"]["radar_sprite"] = "test-core:assets/missing.png";
		rejected(
		    [&]
		    {
			    compile_sprite(missing_icon.at("sprite"), sprite_catalog, "radar_sprite");
		    },
		    "radar_sprite");
		const Json sidecar = {{"schema_version", 1},     {"radar_icon", "asteroid"},
		                      {"id", "synthetic"},       {"coordinate_system", "frame_pixels_top_left_y_down"},
		                      {"image_size", {1, 1}},    {"pivot", {.5, .5}},
		                      {"sprite_sheet", nullptr}, {"markers", {{"engine_port", {.5, .5, -1}}}}};
		const auto imported_sprite = import_sprite_sidecar(
		    sidecar, {{"sprite", "test-core:assets/pixel.png"}, {"depth", "test-core:assets/pixel.png"}});
		const auto imported_material = compile_sprite(imported_sprite, sprite_catalog, "imported material");
		require(imported_material.radar_icon == RadarIcon::asteroid && compiled.radar_icon == RadarIcon::ship,
		        "sidecar symbols must survive import and canonical material verification");
		rejected(
		    [&]
		    {
			    compile_radar_icon("arbitrary-unknown-symbol", "test.radar_icon");
		    },
		    "radar_icon");
		require(imported_material.frames.at(0).attachments.at("engine_port").layer == -1,
		        "source-sidecar import must convert signed markers into the canonical verified vocabulary");
		require(compiled.frames.at(0).attachments.at("engine_port").layer == -1 &&
		            compiled.frames.at(0).depth_scale > .69f,
		        "signed layers and calibrated relief must survive production loading");
		auto presentation_sprite = resolve_packs({console_assets(sprite_catalog, "test.ship")});
		require(presentation_sprite.entities.at("test.ship")["render"]["sprite"]["frames"] == sprite["frames"],
		        "permitted sprite calibration and attachments must survive Console asset transfer");
		require(presentation_sprite.maps.empty(), "lighting maps must not leak into the Console bundle");
		require(render_radar_icon(presentation_sprite.entities.at("test.ship").at("render")) == RadarIcon::ship,
		        "own-ship symbols survive the permitted bundle without exporting other entity definitions");
		WorkshopDocument sprite_document;
		sprite_document.load_pack(sprite_archive);
		require(
		    resolve_packs({read_pack(sprite_document.export_pack())}).entities.at("test.ship")["render"]["sprite"] ==
		        sprite,
		    "Workshop round trip must preserve sprite material");
		for (const auto &bad_rect : {Json({0, 0, 2, 1}), Json({0, 0, 1.5, 1})})
		{
			auto invalid_sprite = sprite;
			invalid_sprite["frames"][0]["rect"] = bad_rect;
			rejected(
			    [&]
			    {
				    compile_sprite(invalid_sprite, sprite_catalog, "test material");
			    },
			    "rect");
		}
		auto invalid_sprite = sprite;
		invalid_sprite.erase("depth");
		rejected(
		    [&]
		    {
			    compile_sprite(invalid_sprite, sprite_catalog, "test material");
		    },
		    "requires depth");
		rejected(
		    [&]
		    {
			    verify_lighting({{"sun_direction", {0, 0, 0}}}, "lighting");
		    },
		    "zero");
		auto shared = loaded;
		require(shared.archive_owner() == loaded.archive_owner(),
		        "loaded pack copies must share immutable archive storage");
		auto changed_after_load = loaded.draft();
		changed_after_load.manifest["id"] = "different";
		require(loaded.manifest().at("id") == "test-core" && loaded.archive() == bytes,
		        "editing a draft must not change the loaded archive or decoded content");
		auto named = loaded.draft();
		const std::string longest_path = "notes/" + std::string(505, 'a');
		named.files[longest_path] = {1};
		require(read_pack(write_pack(named)).files().contains(longest_path),
		        "the longest supported draft path must survive archive loading");
		named.files[longest_path + "a"] = {1};
		rejected(
		    [&]
		    {
			    resolve_draft({}, named);
		    },
		    "path exceeds supported length");
		rejected(
		    [&]
		    {
			    write_pack(named);
		    },
		    "path exceeds supported length");
		named = loaded.draft();
		named.files["notes/directory/"] = {1};
		rejected(
		    [&]
		    {
			    resolve_draft({}, named);
		    },
		    "unsafe path");
		require(catalog.entities.at("test.variant")["systems"].contains("engine"),
		        "inheritance must resolve through the production loader");
		require(catalog.entities.at("test.variant")["render"]["scale"] == 2,
		        "child patch must preserve inherited render fields");
		auto defs = parse_json(base.files.at("definitions.json"), "fixture");
		defs["entities"][0]["systems"]["engine"]["max_speed_mm_s"] = 31000;
		base.files["definitions.json"] = json_bytes(defs);
		resolve_packs({read_pack(write_pack(base))});
		PackDraft mod;
		mod.manifest = {
		    {"schema", 1}, {"id", "test-mod"}, {"dependencies", {"test-core"}}, {"overrides", {"test.ship"}}};
		Json replacement = defs["entities"][0];
		replacement["render"]["scale"] = 3;
		mod.files["definitions.json"] = json_bytes({{"schema", 1}, {"entities", Json::array({replacement})}});
		auto mod_loaded = read_pack(write_pack(mod));
		auto modded = resolve_packs({loaded, mod_loaded});
		require(modded.entities.at("test.variant")["render"]["scale"] == 2,
		        "child's explicit scale must survive parent override");
		require(modded.entities.at("test.ship")["render"]["scale"] == 3, "explicit later override must win");
		rejected(
		    [&]
		    {
			    resolve_packs({mod_loaded, loaded});
		    },
		    "earlier");
		mod.manifest.erase("overrides");
		rejected(
		    [&]
		    {
			    resolve_packs({loaded, read_pack(write_pack(mod))});
		    },
		    "explicit override");
		auto broken = catalog;
		broken.maps.at("test.map")["ship_slots"][0]["allowed_ships"] = {"missing"};
		rejected(
		    [&]
		    {
			    verify_catalog(broken);
		    },
		    "allowed ship");
		broken = catalog;
		broken.consoles.at("test.console")["widgets"][0]["binding"] = "world.hidden_truth";
		rejected(
		    [&]
		    {
			    verify_catalog(broken);
		    },
		    "unsupported widget/binding");
		broken = catalog;
		broken.assets.at("test-core:assets/pixel.png").resize(24);
		rejected(
		    [&]
		    {
			    verify_catalog(broken);
		    },
		    "PNG");
		defs["entities"][0]["extends"] = "test.variant";
		base.files["definitions.json"] = json_bytes(defs);
		rejected(
		    [&]
		    {
			    resolve_packs({read_pack(write_pack(base))});
		    },
		    "inheritance cycle");
		base.files["../escape"] = {1};
		rejected(
		    [&]
		    {
			    write_pack(base);
		    },
		    "unsafe path");
		require(sha256(Bytes{'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		        "content hash must match the SHA256 format contract");
		WorkshopDocument workshop;
		workshop.import_image("pixel.png", read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png"));
		auto definition = workshop.create_entity();
		auto instance = workshop.place(definition, 1000, 2000);
		workshop.duplicate(instance);
		require(workshop.map()["entities"].size() == 2, "Workshop duplicate must create a separate stable instance");
		workshop.map()["ship_slots"].push_back({{"id", "crew"},
		                                        {"x_mm", 0},
		                                        {"y_mm", 0},
		                                        {"heading_mdeg", 0},
		                                        {"allowed_ships", Json::array({definition})}});
		auto saved_map = workshop.save_map();
		workshop.erase(instance);
		workshop.load_map(saved_map);
		workshop.new_map("workshop.second");
		workshop.place(definition, 0, 0);
		workshop.definitions()["entities"].push_back({{"id", "workshop.child"}, {"extends", definition}});
		workshop.rename_entity(definition, "workshop.renamed");
		require(workshop.definitions()["maps"][0]["entities"][0]["definition"] == "workshop.renamed" &&
		            workshop.definitions()["maps"][0]["ship_slots"][0]["allowed_ships"][0] == "workshop.renamed" &&
		            workshop.map()["entities"][0]["definition"] == "workshop.renamed" &&
		            workshop.definitions()["entities"][1]["extends"] == "workshop.renamed",
		        "template rename repairs all maps, ship slots and inherited templates");
		const auto renamed = workshop.definitions();
		rejected(
		    [&]
		    {
			    workshop.rename_entity("workshop.renamed", "workshop.child");
		    },
		    "already exists");
		require(workshop.definitions() == renamed, "conflicting template rename preserves the draft");
		definition = "workshop.renamed";
		workshop.select_map(0);
		require(workshop.map()["entities"].size() == 2, "map load must restore authored transforms and instances");
		auto authored = workshop.export_pack();
		WorkshopDocument reopened;
		reopened.load_pack(authored);
		auto reloaded = resolve_packs({read_pack(reopened.export_pack())});
		require(reloaded.maps.at("workshop.map")["ship_slots"][0]["allowed_ships"][0] == definition,
		        "export/import must preserve allowed ships and include required assets");
		auto original = reopened.map();
		rejected(
		    [&]
		    {
			    reopened.load_map(json_bytes(
			        {{"id", "bad"},
			         {"entities",
			          Json::array(
			              {{{"id", 1}, {"definition", "missing"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}})},
			         {"ship_slots", Json::array()}}));
		    },
		    "unknown entity");
		require(reopened.map() == original, "refused map load must retain the editable document");
		WorkshopDocument authored_mod;
		authored_mod.rename_pack("authored-mod");
		authored_mod.load_dependency(authored);
		auto derived = authored_mod.create_entity(definition);
		authored_mod.place(derived, 0, 0);
		auto mod_archive = authored_mod.export_pack();
		auto combined = resolve_packs({read_pack(authored), read_pack(mod_archive)});
		require(combined.entities.at(derived)["systems"].contains("engine"),
		        "Workshop mod must inherit real dependency content without embedding a second copy");
		require(combined.maps.contains("authored-mod.map"), "renaming a pack must update namespaced map identities");
		std::cout << "PASS pack round trip, inheritance, ordered overrides and runnable-content verification\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
