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
		// A synthetic material travels through the same archive, inheritance,
		// presentation sanitiser and Workshop paths as authored game content.
		auto sprite_draft = loaded.draft();
		auto sprite_defs = parse_json(sprite_draft.files.at("definitions.json"), "synthetic sprite");
		sprite_defs["schema"] = 3;
		Json sprite = {
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
		require(compiled.frames.at(0).attachments.at("engine_port").layer == -1 &&
		            compiled.frames.at(0).depth_scale > .69f,
		        "signed layers and calibrated relief must survive production loading");
		auto presentation_sprite = resolve_packs({console_assets(sprite_catalog, "test.ship")});
		require(presentation_sprite.entities.at("test.ship")["render"]["sprite"]["frames"] == sprite["frames"],
		        "permitted sprite calibration and attachments must survive Console asset transfer");
		require(presentation_sprite.maps.empty(), "lighting maps must not leak into the Console bundle");
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
