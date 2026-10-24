#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include "../support/model_fixture.hpp"
#include "../support/controller_fixture.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <array>
#include <filesystem>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

import phoenix.content;

using namespace phoenix;
using phoenix::test::rejected;
using phoenix::test::require;
int main()
{
	try
	{
		rejected(
		    []
		    {
			    const std::string duplicate = R"({"contributions":{"fore":{},"fore":{}}})";
			    parse_json(Bytes(duplicate.begin(), duplicate.end()), "duplicate-shield.json");
		    },
		    "duplicate object key");
		auto base =
		    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");
		auto bytes = write_pack(base);
		auto loaded = read_pack(bytes);
		require(loaded.hash() == sha256(bytes), "pack identity must be the SHA256 of transferred archive bytes");
		auto catalog = resolve_packs({loaded});
		{
			auto source = base;
			auto definitions = parse_json(source.files.at("definitions.json"), "UI fixture");
			auto &console = definitions["consoles"][0];
			console["layout"] = convert_console_layout(console);
			definitions["layouts"] = Json::array(
			    {{{"id", "test.layout"},
			      {"schema", 1},
			      {"root",
			       {{"id", "root"},
			        {"kind", "row"},
			        {"children",
			         Json::array(
			             {{{"id", "first"}, {"kind", "component"}, {"component", "gameplay"}, {"min_width", 300}},
			              {{"id", "second"},
			               {"kind", "component"},
			               {"component", "messages"},
			               {"min_width", 300}}})}}}}});
			console["layout"] = "test.layout";
			definitions["themes"] =
			    Json::array({{{"id", "test.theme"}, {"schema", 1}, {"tokens", {{"accent", "#123456"}}}}});
			console["theme"] = "test.theme";
			source.files["definitions.json"] = json_bytes(definitions);
			const auto ui_catalog = resolve_packs({read_pack(write_pack(source))});
			const auto program =
			    compile_ui_layout(ui_catalog, ui_catalog.consoles.at(console.at("id").get<std::string>()));
			const auto wide = resolve_ui_layout(program, {0, 0, 1200, 700}, 1);
			const auto phone = resolve_ui_layout(program, {0, 0, 360, 700}, 1);
			require(!wide.compact && wide.placements.size() == 2 && wide.pages.empty(),
			        "wide containers retain both components");
			require(phone.compact && phone.pages.size() == 2 && phone.placements[0].visible &&
			            !phone.placements[1].visible,
			        "compact fallback creates named pages without changing component identity");
			const auto second = resolve_ui_layout(program, {0, 0, 360, 700}, 1, {{"root", "root.second"}});
			require(!second.placements[0].visible && second.placements[1].visible,
			        "page selection resolves permitted geometry");
			UiTabs tabs;
			tabs.update({{"a", "First"}, {"b", "Second"}, {"c", "Third"}});
			require(tabs.select("b") && !tabs.select("unknown"), "tab selection accepts only available identities");
			tabs.update({{"a", "First"}, {"c", "Third"}});
			require(tabs.selected() == "c", "revoked tab selects next available authored neighbour");
			require(resolved_ui_theme(ui_catalog, "test.theme").at("tokens").at("accent") == "#123456",
			        "theme merges built-in defaults");
			const auto ship = ui_catalog.entities.begin()->first;
			const auto client = resolve_packs({console_assets(ui_catalog, ship)});
			require(client.layouts.contains("test.layout") && client.themes.contains("test.theme"),
			        "client bundle retains reachable presentation definitions");
			definitions["layouts"][0]["compact"] = {
			    {"id", "compact"}, {"kind", "component"}, {"component", "messages"}};
			source.files["definitions.json"] = json_bytes(definitions);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(source))});
			    },
			    "preserve all component targets");
		}

		{
			auto source = base;
			auto definitions = parse_json(source.files.at("definitions.json"), "resource fixture");
			definitions["resources"] = Json::array(
			    {{{"id", "test.ore"}, {"label", "Ore"}, {"tags", Json::array({"cargo"})}, {"sizes", {{"cargo", 3}}}},
			     {{"id", "test.ingot"}, {"extends", "test.ore"}, {"label", "Ingot"}, {"sizes", {{"cargo", 2}}}}});
			definitions["recipes"] = Json::array({{{"id", "test.smelting"},
			                                       {"label", "Smelting"},
			                                       {"tags", Json::array({"industry"})},
			                                       {"work", 20},
			                                       {"inputs", {{"test.ore", 2}}},
			                                       {"outputs", {{"test.ingot", 1}}}}});
			definitions["recipes"].push_back({{"id", "test.continuous"},
			                                  {"label", "Exact mixed outputs"},
			                                  {"work", 1},
			                                  {"inputs", {{"test.ore", 2}}},
			                                  {"outputs", {{"test.ingot", 4}, {"test.ore", 2}}}});
			source.files["definitions.json"] = json_bytes(definitions);
			const auto resolved = resolve_packs({read_pack(write_pack(source))});
			require(compile_resource_catalog(resolved).at("test.ingot").sizes.at("cargo") == 2,
			        "resource inheritance resolves capacity sizes");
			require(compile_recipe(resolved, "test.smelting").inputs.at("test.ore") == 2,
			        "recipes retain exact resource requirements");
			Json processor = {{"recipe", "test.smelting"},
			                  {"mode", "continuous"},
			                  {"inputs", {{"test.ore", "ore"}}},
			                  {"outputs", {{"test.ingot", "ingots"}, {"test.ore", "recovered"}}}};
			rejected(
			    [&]
			    {
				    (void)compile_processor(resolved, processor, "smelter");
			    },
			    "continuous recipe period 20000 exceeds");
			processor["mode"] = "batch";
			require(!compile_processor(resolved, processor, "smelter").continuous,
			        "batch processing retains recipes outside continuous ratio bounds");
			processor["mode"] = "continuous";
			processor["recipe"] = "test.continuous";
			const auto continuous = compile_processor(resolved, processor, "smelter");
			require(continuous.continuous && continuous.recipes.front().outputs.size() == 2 &&
			            continuous.recipes.front().inputs.at("test.ore") == 2 &&
			            continuous.recipes.front().outputs.at("test.ingot") == 4,
			        "supported exact continuous ratios retain all inputs and multiple outputs");
			Json stock = {{"capacity_tag", "cargo"},
			              {"capacity", 10},
			              {"filter", {{"tags", Json::array({"cargo"})}}},
			              {"initial", {{"test.ore", 2}, {"test.ingot", 2}}}};
			require(compile_stock_store(resolved, stock, "hold").initial.size() == 2,
			        "mixed stores share one capacity measured by tag size");
			stock["initial"]["test.ingot"] = 3;
			rejected(
			    [&]
			    {
				    (void)compile_stock_store(resolved, stock, "hold");
			    },
			    "initial stock exceeds capacity");
			auto bad = source;
			auto unknown = definitions;
			unknown["recipes"][0]["inputs"]["missing.resource"] = 1;
			bad.files["definitions.json"] = json_bytes(unknown);
			rejected(
			    [&]
			    {
				    (void)resolve_packs({read_pack(write_pack(bad))});
			    },
			    "unknown exact resource");
			PackDraft patch;
			patch.manifest = {
			    {"schema", 1}, {"id", "resource-mod"}, {"dependencies", {"test-core"}}, {"overrides", {"test.ore"}}};
			auto ore = definitions["resources"][0];
			ore["sizes"]["cargo"] = 4;
			patch.files["definitions.json"] = json_bytes({{"schema", 4}, {"resources", Json::array({ore})}});
			const auto modified = resolve_packs({read_pack(write_pack(source)), read_pack(write_pack(patch))});
			require(compile_resource_catalog(modified).at("test.ore").sizes.at("cargo") == 4,
			        "declared resource override replaces catalogue metadata");
		}
		{
			auto definitions = phoenix::test::combat_fixture();
			definitions["resources"] = Json::array({{{"id", "test.filtered-round"},
			                                         {"label", "Filtered round"},
			                                         {"tags", Json::array({"ammo", "cargo"})},
			                                         {"sizes", {{"ammo", 1}, {"cargo", 3}}}}});
			auto &magazine = definitions["entities"][0]["systems"]["mag"];
			magazine.erase("capacity");
			magazine.erase("reload");
			magazine["stock"] = {
			    {"capacity_tag", "ammo"},
			    {"capacity", 3},
			    {"single_resource", true},
			    {"selection", "test.filtered-round"},
			    {"initial", {{"test.filtered-round", 3}}},
			    {"filter", {{"tags", Json::array({"ammo"})}, {"resources", Json::array({"test.filtered-round"})}}}};
			auto &filter = magazine["stock"]["filter"];
			for (bool conjunction : {false, true})
			{
				filter["mode"] = conjunction ? "and" : "or";
				filter["require_all"] = conjunction;
				const auto accepted = phoenix::test::load(definitions);
				const auto combat = compile_combat(accepted, "test.combat");
				require(
				    combat->systems[combat->system_ids.at("mag")].get<MagazineDefinition>().stock.filter.require_all ==
				        conjunction,
				    "matching filter composition options preserve their authored semantics through the loader");
				filter["require_all"] = !conjunction;
				rejected(
				    [&]
				    {
					    (void)phoenix::test::load(definitions);
				    },
				    "conflicting mode and require_all");
			}
		}
		{
			auto definitions = phoenix::test::controller_fixture();
			definitions["resources"] = Json::array({{{"id", "test.capacity-energy"},
			                                         {"label", "Test energy"},
			                                         {"tags", Json::array({"energy"})},
			                                         {"sizes", {{"energy", 1}}}}});
			definitions["recipes"] = Json::array({{{"id", "test.partial-cycle"},
			                                       {"label", "Partial cycle"},
			                                       {"work", 1},
			                                       {"inputs", Json::object()},
			                                       {"outputs", {{"test.capacity-energy", 4}}}}});
			auto &entity = definitions["entities"][0];
			entity.erase("motion_model");
			entity["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                                {"resource", "test.capacity-energy"},
			                                                {"capacity", 2},
			                                                {"initial", 0},
			                                                {"charge_rate", 100},
			                                                {"discharge_rate", 100}}})}};
			entity["processors"] = Json::array({{{"id", "flow"},
			                                     {"equipment", "port"},
			                                     {"recipe", "test.partial-cycle"},
			                                     {"mode", "continuous"},
			                                     {"outputs", {{"test.capacity-energy", "energy:cell"}}}}});
			const auto accepted = phoenix::test::load(definitions);
			require(compile_processors(accepted, "test.controllable").size() == 1,
			        "continuous authoring accepts nominal cycle output larger than the bound store");
			entity["processors"][0]["mode"] = "batch";
			rejected(
			    [&]
			    {
				    (void)phoenix::test::load(definitions);
			    },
			    "recipe batch exceeds bound store capacity");
		}
		{
			auto source = base;
			auto definitions = parse_json(source.files.at("definitions.json"), "effect fixture");
			definitions["effects"] =
			    Json::array({{{"id", "test.shield-effect"},
			                  {"kind", "shield"},
			                  {"contributions", {{"fore", {{"capacity", 100}, {"recharge_per_second", 5}}}}}},
			                 {{"id", "test.shield-child"},
			                  {"extends", "test.shield-effect"},
			                  {"contributions", {{"fore", {{"initial", 25}}}}}}});
			source.files["definitions.json"] = json_bytes(definitions);
			auto source_pack = read_pack(write_pack(source));
			auto effects = resolve_packs({source_pack});
			require(compile_shield_effect(effects, "test.shield-child").contributions.front().shield.reserve.initial ==
			            25,
			        "effect inheritance preserves explicit contribution specialisation");
			PackDraft patch;
			patch.manifest = {{"schema", 1},
			                  {"id", "effect-mod"},
			                  {"dependencies", {"test-core"}},
			                  {"overrides", {"test.shield-effect"}}};
			auto replacement = definitions["effects"][0];
			replacement["contributions"]["fore"]["capacity"] = 200;
			patch.files["definitions.json"] = json_bytes({{"schema", 4}, {"effects", Json::array({replacement})}});
			effects = resolve_packs({source_pack, read_pack(write_pack(patch))});
			require(compile_shield_effect(effects, "test.shield-child").contributions.front().shield.reserve.capacity ==
			            200,
			        "shared effect overrides reach inherited consumers before compilation");
			WorkshopDocument effect_document;
			effect_document.load_pack(write_pack(source));
			require(!effect_document.definition_choices(WorkshopDefinitionKind::Effect).empty(),
			        "Workshop exposes shared effects");
			auto invalid_effect = source;
			definitions["effects"][0]["contributions"]["fore"]["unsupported_hook"] = 1;
			invalid_effect.files["definitions.json"] = json_bytes(definitions);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(invalid_effect))});
			    },
			    "unsupported_hook");
			rejected(
			    [&]
			    {
				    compile_shield_effect(effects, "missing.effect");
			    },
			    "unknown effect");
			{
				auto invalid = effects;
				auto &arc = invalid.effects.at("test.shield-effect")["contributions"]["fore"];
				arc["reactions"] =
				    Json::array({{{"operations", Json::array({{{"kind", "remove"}, {"grant", "x"}, {"damage", 1}}})}}});
				rejected(
				    [&]
				    {
					    compile_shield_effect(invalid, "test.shield-effect");
				    },
				    "unsupported remove field damage");
				arc["reactions"] = Json::array({{{"operations", Json::array({{{"kind", "damage"}, {"damage", 1}},
				                                                             {{"kind", "remove"}, {"grant", "x"}}})}}});
				rejected(
				    [&]
				    {
					    compile_shield_effect(invalid, "test.shield-effect");
				    },
				    "prepared operations must precede damage");
				arc.erase("reactions");
				arc["pre_damage_permille"] = 1001;
				rejected(
				    [&]
				    {
					    compile_shield_effect(invalid, "test.shield-effect");
				    },
				    "outside supported bounds");
			}
		}

		{
			auto definitions = phoenix::test::controller_fixture();
			definitions["schema"] = 4;
			definitions["effects"] = Json::parse(R"json([
			 {"id":"test.drive-effect","kind":"modifier","contributions":{
			   "thrust":{"property":"propulsion","role":"drive","delta_permille":250},
			   "speed":{"property":"speed","role":"drive","delta_permille":100}}},
			 {"id":"test.drive-child","extends":"test.drive-effect","contributions":{
			   "thrust":{"delta_permille":500}}}])json");
			definitions["entities"][0]["personnel"] = Json::parse(R"json({
			 "departments":[{"id":"sailing","label":"Sailing","station":"operations"}],
			 "compartments":["bridge"],"connections":[],
			 "traits":[{"id":"expert","label":"Expert","effects":["test.drive-child"]}],
			 "duties":[{"id":"drive","label":"Drive","controller":"propulsion","department":"sailing",
			 "compartment":"bridge","bindings":{"port":["drive"],"starboard":["drive"]}}],
			 "officers":[{"id":"a","name":"A","department":"sailing","compartment":"bridge",
			 "duty":"drive","traits":["expert"]}]})json");
			const auto original = phoenix::test::archive(definitions);
			auto effects = resolve_packs({read_pack(original)});
			auto people = compile_personnel(effects, "test.controllable");
			require(people.traits.at("expert").effects.at(0).contributions.size() == 2 &&
			            people.duties.at(0).effects.at("port") == std::vector<std::string>{"drive"},
			        "compiled trait references bind a multi-property role once per equipment");
			PackDraft patch;
			patch.manifest = {{"schema", 1},
			                  {"id", "property-mod"},
			                  {"dependencies", {"test-core"}},
			                  {"overrides", {"test.drive-effect"}}};
			auto replacement = definitions["effects"][0];
			replacement["contributions"]["speed"]["delta_permille"] = -250;
			patch.files["definitions.json"] = json_bytes({{"schema", 4}, {"effects", Json::array({replacement})}});
			effects = resolve_packs({read_pack(original), read_pack(write_pack(patch))});
			require(compile_modifier_effect(effects, "test.drive-child").contributions.front().delta_permille == -250,
			        "shared modifier overrides reach inherited trait effects");
			WorkshopDocument document;
			document.load_dependency(original);
			document.load_pack(write_pack(patch));
			WorkshopDocument reopened;
			reopened.load_dependency(original);
			reopened.load_pack(document.export_pack());
			require(
			    compile_modifier_effect(reopened.verify(), "test.drive-child").contributions.front().delta_permille ==
			        -250,
			    "modifier references and overrides survive Workshop export and reload");
			for (const auto &property : {std::string("capacity"), std::string("unknown")})
			{
				auto invalid = effects;
				invalid.effects["test.drive-effect"]["contributions"]["speed"]["property"] = property;
				rejected(
				    [&]
				    {
					    compile_modifier_effect(invalid, "test.drive-effect");
				    },
				    "unknown property");
			}
			{
				auto invalid = effects;
				invalid.effects["test.drive-child"]["contributions"]["speed"]["role"] = "other";
				rejected(
				    [&]
				    {
					    compile_personnel(invalid, "test.controllable");
				    },
				    "partially bound");
			}
			{
				auto invalid = effects;
				invalid.effects["test.drive-child"]["contributions"]["speed"]["property"] = "recharge";
				rejected(
				    [&]
				    {
					    compile_personnel(invalid, "test.controllable");
				    },
				    "incompatible property target");
			}
			{
				auto invalid = effects;
				invalid.entities["test.controllable"]["personnel"]["duties"][0]["bindings"]["port"] = {"drive",
				                                                                                       "drive"};
				rejected(
				    [&]
				    {
					    compile_personnel(invalid, "test.controllable");
				    },
				    "duplicate target role");
				invalid = effects;
				invalid.entities["test.controllable"]["personnel"]["traits"][0]["bonuses"] = {{"propulsion", 100}};
				rejected(
				    [&]
				    {
					    compile_personnel(invalid, "test.controllable");
				    },
				    "replaced by effect references");
				invalid = effects;
				invalid.effects["test.drive-effect"]["contributions"]["speed"]["delta_permille"] = 100001;
				rejected(
				    [&]
				    {
					    compile_modifier_effect(invalid, "test.drive-effect");
				    },
				    "-100000..100000");
				invalid.effects["test.drive-effect"]["contributions"]["speed"]["delta_permille"] = 1.5;
				rejected(
				    [&]
				    {
					    compile_modifier_effect(invalid, "test.drive-effect");
				    },
				    "-100000..100000");
			}
		}

		{
			for (auto encoding : {PackEncoding::Zip, PackEncoding::Dat})
			{
				const auto source = read_pack(write_pack(base, encoding));
				const auto companions = pack_companions({source}, "fixture.dat");
				const auto menu = parse_json(companions.at("fixture.dat.menu.json"), "menu");
				require(menu.at("hash") == source.hash() &&
				            menu.at("sources") == Json(resolve_packs({source}).ordered_packs),
				        "sidecars pin exact archives");
				require(!menu.contains("entities") && !menu.at("clients").empty(),
				        "menu contains display hints and client descriptors");
				for (const auto &descriptor : menu.at("clients"))
				{
					const auto client = read_pack(companions.at(descriptor.at("file").get<std::string>()));
					validate_ship_client(client, descriptor);
					const auto visible = resolve_packs({client});
					validate_ship_client(client, visible, descriptor);
					for (const auto *field : {"sources", "ship", "hash"})
					{
						auto stale = descriptor;
						if (std::string(field) == "sources")
							stale["sources"][0][1] = std::string(64, '0');
						else
							stale[field] = "wrong";
						for (const bool prepared : {false, true})
							rejected(
							    [&]
							    {
								    if (prepared)
									    validate_ship_client(client, visible, stale);
								    else
									    validate_ship_client(client, stale);
							    },
							    "does not match");
					}
					require(client.hash() == descriptor.at("hash").get<std::string>() && client.archive()[0] == 'P',
					        "client companion is a hash-verified DAT");
					require(visible.maps.empty() && visible.trees.empty() && visible.entities.size() == 1,
					        "ship companions exclude mission and private behavior");
					require(resolve_consoles(visible, descriptor.at("ship")).size() ==
					            resolve_consoles(catalog, descriptor.at("ship")).size(),
					        "ship companion contains all Console choices");
				}
			}
		}

		{
			auto isolated = resolve_packs({loaded});
			isolated.entities.clear();
			isolated.assets.clear();
			const auto repeated = resolve_packs({read_pack(bytes)});
			require(repeated.entities == catalog.entities && repeated.assets == catalog.assets,
			        "Repeated verified resolution returns independent catalogue values");
		}
		{
			// Independent 2x1 panorama, never derived from production artwork.
			const AssetImage pixels{2, 1, {80, 20, 10, 255, 10, 20, 80, 255}};
			PackDraft sky;
			sky.manifest = {{"schema", 1}, {"id", "test-sky"}};
			sky.files["definitions.json"] = json_bytes(Json{{"schema", 3}});
			sky.files["assets/sky.png"] = source_asset_image(pixels);
			const auto sky_pack = read_pack(write_pack(sky));
			auto source = parse_json(base.files.at("definitions.json"), "skybox fixture");
			const auto map = source["maps"][0]["id"].get<std::string>();
			auto draft = base;
			draft.manifest["dependencies"] = Json::array({"test-sky"});
			auto load_sky = [&](const Json &selection)
			{
				source["maps"][0]["presentation"]["skybox"] = selection;
				draft.files["definitions.json"] = json_bytes(source);
				return resolve_packs({sky_pack, read_pack(write_pack(draft))});
			};
			require(!catalog.maps.at(map).value("presentation", Json::object()).contains("skybox"),
			        "legacy maps have no implicit skybox");
			load_sky("");
			const std::string reference = "test-sky:assets/sky.png";
			const auto selected = load_sky(reference);
			const auto prepared = scenario_assets(selected, map);
			require(std::find(prepared.begin(), prepared.end(), reference) != prepared.end(),
			        "selected dependency skybox is prepared before play");
			for (const auto &invalid :
			     {Json(7), Json(nullptr), Json("missing:assets/sky.png"), Json("test-core:assets/pixel.png")})
				rejected(
				    [&]
				    {
					    load_sky(invalid);
				    },
				    "presentation.skybox");
			load_sky(reference);
			for (const auto encoding : {PackEncoding::Zip, PackEncoding::Dat})
			{
				WorkshopDocument document;
				document.load_dependency(write_pack(sky, encoding));
				document.load_pack(write_pack(draft, encoding));
				require(document.map()["presentation"]["skybox"] == reference,
				        "Workshop reload retains selected skybox");
				const auto saved = read_pack(document.export_pack(encoding));
				const auto scenario = read_pack(document.export_pack(encoding, true));
				const auto roundtrip = resolve_packs({sky_pack, saved, scenario});
				require(roundtrip.maps.at(map)["presentation"]["skybox"] == reference &&
				            decode_asset_image(roundtrip.assets.at(reference)).rgba == pixels.rgba,
				        "ZIP/DAT scenario export retains dependency panorama and pixels");
				document.map()["presentation"].erase("skybox");
				require(!resolve_packs({sky_pack, read_pack(document.export_pack(encoding))})
				             .maps.at(map)
				             .at("presentation")
				             .contains("skybox"),
				        "clearing skybox persists without a fallback");
				document.new_map("test.blank-sky");
				require(!document.map().value("presentation", Json::object()).contains("skybox"),
				        "new maps default to no skybox");
			}
			AssetImage cube{1, 6, {}};
			for (int face = 0; face < 6; ++face)
				cube.rgba.insert(cube.rgba.end(), {static_cast<unsigned char>(face * 40), 20, 80, 255});
			sky.files["assets/sky.png"] = source_asset_image(cube);
			auto cube_catalog = resolve_packs({read_pack(write_pack(sky)), read_pack(write_pack(draft))});
			require(scenario_assets(cube_catalog, map) == prepared,
			        "cube strip uses ordinary map preparation and export references");
			sky.files["assets/sky.png"] = {1, 2, 3};
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(sky)), read_pack(write_pack(draft))});
			    },
			    "presentation.skybox");
		}
		{
			const auto geometry = phoenix::test::model_fixture();
			const auto model = compile_model(geometry);
			require(model.parts.size() == 1 &&
			            model.parts[0].positions == std::vector<float>({1, 2, 3, 3, 2, 3, 1, 3, 3}),
			        "Default-scene hierarchy applies node transforms without instancing inactive scene nodes");
			require(model.parts[0].normals == std::vector<float>({0, 0, 1, 0, 0, 1, 0, 0, 1}),
			        "Missing GLB normals generate face normals for directional and thrust lighting");
			const auto luminous = compile_model(phoenix::test::model_fixture(
			    {{"extensionsUsed", {"KHR_materials_emissive_strength"}},
			     {"extensionsRequired", {"KHR_materials_emissive_strength"}},
			     {"materials",
			      Json::array({{{"emissiveFactor", {.25, .5, 1}},
			                    {"extensions", {{"KHR_materials_emissive_strength", {{"emissiveStrength", 2}}}}}}})},
			     {"meshes",
			      Json::array(
			          {{{"primitives",
			             Json::array({{{"attributes", {{"POSITION", 0}}}, {"material", 0}, {"mode", 4}}})}}})}}));
			require(luminous.parts[0].emission == std::array<float, 3>{.5, 1, 2},
			        "Supported required emissive-strength materials retain their authored emission");
			for (const auto &patch : {Json{{"extensionsRequired", {"unsupported.feature"}}},
			                          Json{{"buffers", Json::array({{{"byteLength", 36}, {"uri", "outside.bin"}}})}},
			                          Json{{"animations", Json::array({Json::object()})}}})
				rejected(
				    [&]
				    {
					    compile_model(phoenix::test::model_fixture(patch));
				    },
				    "model:");
			auto source = base;
			source.manifest["base"] = {{"render_mode", "models"}};
			auto definitions = parse_json(source.files.at("definitions.json"), "model fixture");
			for (auto &entity : definitions["entities"])
				entity["render"] = {
				    {"scale", 2},
				    {"model", {{"asset", "test-core:assets/triangle.glb"}}},
				    {"radar_icon", "ship"},
				    {"attachments", {{"engine_test", {{"position", {0, 0, 0}}, {"direction", {0, 0, 1}}}}}}};
			source.files["definitions.json"] = json_bytes(definitions);
			source.files["assets/triangle.glb"] = geometry;
			for (const auto encoding : {PackEncoding::Zip, PackEncoding::Dat})
			{
				const auto archive = write_pack(source, encoding);
				const auto resolved = resolve_packs({read_pack(archive)});
				require(resolved.base.render_mode == "models" &&
				            resolved.assets.at("test-core:assets/triangle.glb") == geometry,
				        "Model pack round trips retain exact verified geometry and family");
				WorkshopDocument workshop;
				workshop.load_pack(archive);
				require(resolve_packs({read_pack(workshop.export_pack(encoding))}).entities == resolved.entities,
				        "Workshop model export preserves representation and attachment definitions");
			}
		}
		{
			// Independent colour/data texture: all alpha levels and nonzero RGB under alpha=0.
			AssetImage raw{256, 1, {}};
			for (unsigned i = 0; i < 256; ++i)
			{
				raw.rgba.push_back(static_cast<std::uint8_t>(i));
				raw.rgba.push_back(static_cast<std::uint8_t>(255 - i));
				raw.rgba.push_back(static_cast<std::uint8_t>(i ^ 85));
				raw.rgba.push_back(static_cast<std::uint8_t>(i % 4 ? i : 0));
			}
			auto draft = base;
			const auto png = source_asset_image(raw);
			draft.files["assets/lossless.png"] = png;
			const auto encoded = write_pack(draft, PackEncoding::Dat);
			const auto packed = read_pack(encoded);
			resolve_packs({packed});
			const auto &image = packed.files().at("assets/lossless.png");
			require(std::string(image.begin(), image.begin() + 8) == "PHXWEBP1", "DAT uses versioned lossless WebP");
			std::cout << "LOSSLESS-FIXTURE " << sha256(image) << "\n";
			require(decode_asset_image(image).rgba == raw.rgba,
			        "all RGBA bytes including hidden RGB survive DAT packaging");
			require(draft.files.at("assets/lossless.png") == png, "packaging leaves PNG sources unchanged");
			const auto editable = read_pack(write_pack(packed.draft()));
			require(decode_asset_image(editable.files().at("assets/lossless.png")).rgba == raw.rgba,
			        "Workshop-compatible PNG re-export preserves all decoded pixels");
			auto truncated = image;
			truncated.resize(25);
			rejected(
			    [&]
			    {
				    decode_asset_image(truncated);
			    },
			    "WebP");
			auto obsolete = image;
			std::copy_n("PHXRGBA1", 8, obsolete.begin());
			rejected(
			    [&]
			    {
				    decode_asset_image(obsolete);
			    },
			    "unsupported compiled image version");
			draft.files["assets/lossless.png"] = obsolete;
			rejected(
			    [&]
			    {
				    write_pack(draft, PackEncoding::Dat);
			    },
			    "unsupported compiled image version");
		}
		const auto dat = write_pack(base, PackEncoding::Dat);
		const auto compiled_pack = read_pack(dat);
		const auto compiled_catalog = resolve_packs({compiled_pack});
		require(compiled_pack.hash() == sha256(dat) && compiled_catalog.entities == catalog.entities &&
		            compiled_catalog.maps == catalog.maps,
		        "ZIP/DAT share the production catalogue and exact byte identity");
		require(compiled_image(compiled_pack.files().at("assets/pixel.png")), "DAT stores versioned lossless pixels");
		const auto repacked =
		    read_pack(write_pack({compiled_pack.manifest(), compiled_pack.files()}, PackEncoding::Dat));
		require(repacked.files().at("assets/pixel.png") == compiled_pack.files().at("assets/pixel.png"),
		        "DAT repack preserves compiled image records without re-encoding");
		require(decode_asset_image(compiled_pack.files().at("assets/pixel.png")).rgba ==
		            decode_asset_image(base.files.at("assets/pixel.png")).rgba,
		        "compiled pixels preserve source image");
		require(!compiled_image(compiled_pack.draft().files.at("assets/pixel.png")) &&
		            parse_json(compiled_pack.draft().files.at("definitions.json"), "editable") ==
		                parse_json(base.files.at("definitions.json"), "source"),
		        "compiled imports restore editable sources");
		resolve_packs({read_pack(write_pack(compiled_pack.draft()))});
		auto corrupt_dat = dat;
		corrupt_dat[6] = '9';
		rejected(
		    [&]
		    {
			    read_pack(corrupt_dat);
		    },
		    "version");
		corrupt_dat = dat;
		corrupt_dat.resize(20);
		rejected(
		    [&]
		    {
			    read_pack(corrupt_dat);
		    },
		    "ZIP");
		auto invalid_pixels = compiled_pack.files().at("assets/pixel.png");
		invalid_pixels[8] = 0;
		invalid_pixels[9] = 0;
		invalid_pixels[10] = 0;
		invalid_pixels[11] = 0;
		rejected(
		    [&]
		    {
			    decode_asset_image(invalid_pixels);
		    },
		    "dimensions");
		rejected(
		    [&]
		    {
			    PackDraft invalid{compiled_pack.manifest(), compiled_pack.files()};
			    invalid.files["assets/pixel.png"] = invalid_pixels;
			    write_pack(invalid, PackEncoding::Dat);
		    },
		    "dimensions");
		{
			const auto directory = std::filesystem::temp_directory_path() / "phoenix-content-source-loading-fixture";
			std::filesystem::create_directories(directory / "assets");
			write_file((directory / "manifest.json").generic_string(), json_bytes(base.manifest));
			for (const auto &[path, payload] : base.files)
				write_file((directory / path).generic_string(), payload);
			require(resolve_packs({read_pack_path(directory.generic_string())}).entities == catalog.entities,
			        "source directories load through the production pack reader");
		}
		const auto map_id = catalog.maps.begin()->first;
		const auto required_images = scenario_assets(catalog, map_id);
		require(!required_images.empty(), "scenario closure includes initial and allowed-ship sprites");
		WorkshopDocument packaging;
		packaging.load_pack(bytes);
		packaging.map()["loading_background"] = "test-core:assets/pixel.png";
		const auto extra_image = packaging.import_image("later.png", base.files.at("assets/pixel.png"));
		auto later = packaging.definitions().at("entities").front();
		later["id"] = "test.later";
		later["render"]["images"] = Json::array({extra_image});
		packaging.definitions()["entities"].push_back(later);
		const auto unused = scenario_assets(packaging.verify(), map_id);
		require(std::find(unused.begin(), unused.end(), extra_image) == unused.end(),
		        "unrelated content is not preloaded");
		packaging.map()["preload"] = Json::array({"test.later"});
		const auto future = scenario_assets(packaging.verify(), map_id);
		require(std::find(future.begin(), future.end(), extra_image) != future.end(),
		        "future dynamic entities preload their complete material dependencies");
		later["id"] = "test.unused";
		packaging.definitions()["entities"].push_back(later);
		const auto bounded_catalog = packaging.verify();
		ScenarioRuntime preloaded_scenario(bounded_catalog.scenarios.at(map_id));
		preloaded_scenario.begin_tick(0, {}, {});
		const auto preload_instance = preloaded_scenario.create_instance("preload-contract");
		preloaded_scenario.spawn_entity({preload_instance, 1, 0}, "test.later", {}, 0, "declared");
		require(
		    preloaded_scenario.pending_spawns().size() == 1 &&
		        preloaded_scenario.spawn_entity({preload_instance, 1, 1}, "test.unused", {}, 0, "undeclared").status ==
		            ScenarioActionStatus::Failed,
		    "dynamic spawns admit prepared definitions and refuse undeclared definitions without first-use loading");
		const auto scenario_dat = packaging.export_pack(PackEncoding::Dat, true);
		const auto scenario_catalog = resolve_packs({read_pack(packaging.export_pack()), read_pack(scenario_dat)});
		require(scenario_catalog.maps.size() == 1 &&
		            scenario_catalog.maps.contains(packaging.map().at("id").get<std::string>()),
		        "Workshop packages the selected scenario through the production loader");
		const auto seat = resolve_consoles(catalog, "test.ship").front().id;
		const auto client_pack = console_assets(catalog, "test.ship", seat);
		const auto client_catalog = resolve_packs({client_pack});
		require(client_catalog.maps.empty() && client_catalog.trees.empty() &&
		            resolve_consoles(client_catalog, "test.ship").size() == 1,
		        "assigned Console package excludes scenarios, private trees and unassigned Consoles");
		require(resolve_packs({console_assets(catalog, "test.ship", "__selection__")}).entities.empty(),
		        "selection bootstrap needs no ship or Console definitions");

		// Signed coordinates must be checked before conversion from unsigned JSON storage.
		const auto numeric_source = parse_json(base.files.at("definitions.json"), "numeric fixture");
		const auto load_coordinate = [&](const Json &coordinate)
		{
			auto source = numeric_source;
			source["maps"][0]["entities"][0]["x_mm"] = coordinate;
			auto draft = base;
			draft.files["definitions.json"] = json_bytes(source);
			return resolve_packs({read_pack(write_pack(draft))});
		};
		for (const Json coordinate : {Json(-1000000000LL), Json(1000000000ULL), Json(0)})
			load_coordinate(coordinate);
		for (const Json coordinate : {Json(std::numeric_limits<std::uint64_t>::max()), Json(1000000001ULL),
		                              Json(-1000000001LL), Json(true), Json(1.5), Json("12")})
			rejected(
			    [&]
			    {
				    load_coordinate(coordinate);
			    },
			    "x_mm");
		require(checked_integer(Json(std::numeric_limits<std::int64_t>::min()),
		                        std::numeric_limits<std::int64_t>::min(), 0,
		                        "minimum") == std::numeric_limits<std::int64_t>::min() &&
		            checked_unsigned(Json(std::numeric_limits<std::uint64_t>::max()), 0,
		                             std::numeric_limits<std::uint64_t>::max(),
		                             "maximum") == std::numeric_limits<std::uint64_t>::max(),
		        "full-width integers remain representable");
		rejected(
		    [&]
		    {
			    checked_unsigned(Json(-1), 0, 100, "unsigned");
		    },
		    "unsigned");
		require(json_integer(Json::object(), "optional", 7, 0, 10, "fixture") == 7,
		        "missing optional integer retains its default");
		{
			auto source = numeric_source;
			source["maps"][0]["entities"][0].erase("x_mm");
			auto draft = base;
			draft.files["definitions.json"] = json_bytes(source);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(draft))});
			    },
			    "x_mm");
		}

		// Migration is a compiler boundary: originals remain untouched and outputs use explicit contracts.
		auto conversion_source = catalog;
		conversion_source.entities.at("test.ship")["systems"]["engine"]["enabled"] = false;
		const auto original_conversion = conversion_source.entities;
		const auto converted = convert_controller_content(conversion_source);
		require(conversion_source.entities == original_conversion &&
		            !conversion_source.entities.at("test.ship").contains("control_layout"),
		        "migration leaves source Catalog authored definitions unchanged");
		require(converted.entities.at("test.ship").at("motion_model") == "vector" &&
		            !converted.entities.at("test.ship").contains("controller"),
		        "basic conversion selects vector mechanism and removes old owner");
		const auto converted_layout = compile_controllable_layout(converted, "test.ship");
		bool disabled_default = false, pose_heading = false;
		for (const auto &owner : converted_layout.controllers)
			for (const auto &command : owner.commands)
			{
				if (command.action == OperatingAction::EngineEnabled)
					disabled_default = command.initial == 0;
				if (command.action == OperatingAction::Heading)
					pose_heading = command.initial_heading;
			}
		require(disabled_default && pose_heading,
		        "converted physical defaults preserve disabled equipment and spawn heading");
		{
			auto retired = numeric_source;
			retired["trees"] = Json::parse(
			    R"json([{"id":"old","initial":"run","states":{"run":{"ticks":3,"next":"run","speed_mm_s":5000}}}])json");
			auto draft = base;
			draft.files["definitions.json"] = json_bytes(retired);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(draft))});
			    },
			    "timed tree format retired");
		}
		auto system_conversion = conversion_source;
		system_conversion.entities.at("test.ship").erase("controller");
		system_conversion.entities.at("test.ship")["controllers"] = {{"engine", "test.system"}};
		system_conversion.trees["test.system"] = {{"id", "test.system"}};
		system_conversion.tree_programs["test.system"] = {};
		rejected(
		    [&]
		    {
			    convert_controller_content(system_conversion);
		    },
		    "legacy System decision trees require");
		auto summary_conversion = conversion_source;
		summary_conversion.entities.at("test.ship")["presets"] = Json::parse(
		    R"json([{"id":"old","consoles":{"main":{"summary_intents":[{"system":"engine","channel":"level"}]}}}])json");
		rejected(
		    [&]
		    {
			    convert_controller_content(summary_conversion);
		    },
		    "legacy summary intents require explicit Controller preference channels");
		// Controller compilation is exercised through production pack loading, including unused variants.
		auto control_source = parse_json(base.files.at("definitions.json"), "controller fixture");
		control_source["consoles"][0]["widgets"] = Json::array();
		auto &controlled = control_source["entities"][0];
		controlled["systems"]["reserve"] = {
		    {"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 2000}};
		controlled.erase("console");
		controlled["stations"] = Json::array(
		    {{{"id", "watch"},
		      {"consoles",
		       Json::array({{{"id", "helm"}, {"definition", "test.console"}, {"systems", Json::array()}},
		                    {{"id", "support"}, {"definition", "test.console"}, {"systems", Json::array()}}})}}});
		controlled["control_layout"] = {
		    {"schema", 1},
		    {"controllers",
		     Json::array(
		         {{{"id", "propulsion"},
		           {"console", "helm"},
		           {"fallback", "hold"},
		           {"commands", Json::array({{{"id", "speed"},
		                                      {"action", "speed_mm_s"},
		                                      {"maximum", 17000},
		                                      {"consumers", {"engine", "reserve"}}}})}},
		          {{"id", "power"},
		           {"console", "support"},
		           {"fallback", "hold"},
		           {"commands",
		            Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"consumers", {"engine"}}}})}}})},
		    {"profiles",
		     Json::array({{{"id", "normal"},
		                   {"stars", 2},
		                   {"consoles", {{"helm", {{"human", {"propulsion"}}}}, {"support", {{"ai", {"power"}}}}}}}})},
		    {"default_profile", "normal"}};
		auto load_controls = [&](const Json &source)
		{
			auto draft = base;
			draft.files["definitions.json"] = json_bytes(source);
			return resolve_packs({read_pack(write_pack(draft))});
		};
		auto control_catalog = load_controls(control_source);
		const auto whole_ship = resolve_packs({ship_client_assets(control_catalog, "test.ship")});
		require(resolve_consoles(whole_ship, "test.ship").size() == 2,
		        "Whole-ship client content includes auxiliary Consoles independently of the claimed profile");
		const auto one_console = resolve_packs({console_assets(control_catalog, "test.ship", "helm")});
		const auto console_set =
		    resolve_packs({console_assets(control_catalog, "test.ship", std::vector<std::string>{"helm", "support"})});
		require(resolve_consoles(one_console, "test.ship").size() == 1 &&
		            resolve_consoles(console_set, "test.ship").size() == 2,
		        "restricted bundles include exactly the assigned Console set, including additional profile Consoles");
		{
			auto multi_station = control_source;
			auto &stations = multi_station["entities"][0]["stations"];
			const auto support = stations[0]["consoles"][1];
			stations[0]["consoles"].erase(1);
			stations.push_back({{"id", "engineering"}, {"consoles", Json::array({support})}});
			stations[0]["consoles"][0]["human_preferences"] = {"engineering", "watch"};
			const auto full = load_controls(multi_station);
			const auto restricted = resolve_packs({console_assets(full, "test.ship", "helm")});
			const auto seats = resolve_consoles(restricted, "test.ship");
			require(
			    seats.size() == 1 && seats[0].human_preferences == std::vector<std::string>{"watch"} &&
			        resolve_consoles(full, "test.ship")[0].human_preferences ==
			            std::vector<std::string>{"engineering", "watch"},
			    "restricted remote Console bundles omit unavailable Station preferences without changing the source");
		}
		auto layout = compile_controllable_layout(control_catalog, "test.variant");
		require(!layout.converted && layout.controllers.size() == 2 &&
		            layout.controllers[0].commands[0].consumers.size() == 2,
		        "inherited layout shares one Controller command across two engine entities");
		require(layout.profiles[0].human[0] && !layout.profiles[0].human[1], "profiles allocate Console Controllers");
		require(layout.profiles[0].stars == 2, "Authored star rating survives profile compilation");
		auto bad_controls = control_source;
		bad_controls["entities"][0]["control_layout"]["profiles"][0]["stars"] = 4;
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "stars must be between zero and three");
		bad_controls = control_source;
		bad_controls["entities"][0]["control_layout"]["controllers"][1]["commands"][0]["action"] = "speed_mm_s";
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "multiple command sources");
		bad_controls = control_source;
		bad_controls["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "enable"}, {"action", "engine_enabled"}, {"consumers", {"engine"}}});
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "multiple command sources");
		bad_controls = control_source;
		bad_controls["entities"][0]["control_layout"]["profiles"][0]["consoles"]["helm"]["ai"] = {"power"};
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "foreign or unknown profile Controller");
		bad_controls = control_source;
		bad_controls["entities"][0]["systems"]["engine"]["impulse"] = Json::object();
		bad_controls["entities"][0]["systems"]["reserve"]["impulse"] = Json::object();
		bad_controls["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["action"] = "impulse";
		bad_controls["entities"][0]["control_layout"]["controllers"][0]["commands"][0].erase("maximum");
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "single executor");
		bad_controls = control_source;
		bad_controls["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["consumers"] = {"radar"};
		rejected(
		    [&]
		    {
			    load_controls(bad_controls);
		    },
		    "incompatible physical input");
		require(compile_controllable_layout(catalog, "test.ship").converted,
		        "old ownership converts at the compiler boundary");

		WorkshopDocument history_document;
		history_document.load_pack(bytes);
		WorkshopHistory history;
		history.reset(history_document);
		const auto history_original = history_document.definitions();
		history_document.definitions()["entities"][1]["name"] = "History ship";
		history_document.definitions()["entities"][1]["systems"]["engine"]["max_speed_mm_s"] = 12345;
		history.checkpoint(history_document);
		const auto fitted = history_document.definitions();
		history_document.new_map("test.history-map");
		history_document.import_image("history.png", read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png"));
		history_document.manifest()["description"] = "History pack";
		history.checkpoint(history_document);
		require(history.undo(history_document) && history_document.definitions() == fitted &&
		            !history_document.pack().files.contains("assets/history.png") &&
		            !history_document.manifest().contains("description"),
		        "Workshop undo restores map, asset and manifest edits together without reverting earlier fitting");
		require(history.undo(history_document) && history_document.definitions() == history_original,
		        "Workshop undo crosses inspector domains");
		require(history.redo(history_document) && history_document.definitions() == fitted,
		        "Workshop redo restores both appearance and gameplay edits");
		require(history.redo(history_document) && history_document.map().at("id") == "test.history-map",
		        "Workshop redo restores selected map alongside authored data");
		WorkshopDocument history_reopened;
		history_reopened.load_pack(history_document.export_pack());
		require(history_reopened.verify().maps.contains("test.history-map"),
		        "Restored Workshop drafts export and reload through the production loader");
		history.undo(history_document);
		history_document.definitions()["consoles"][0]["name"] = "New branch";
		history.checkpoint(history_document);
		require(!history.can_redo(), "A new Console edit discards stale gameplay/map redo");
		history.reset(history_document);
		require(!history.can_undo() && !history.can_redo(), "Opening a document starts a fresh history");
		// Large resource payloads must not crowd every ordinary edit out of history.
		auto large_draft = base;
		large_draft.files["history-resource-a.dat"] = Bytes(9 * 1024 * 1024, 0);
		large_draft.files["history-resource-b.dat"] = Bytes(9 * 1024 * 1024, 0);
		WorkshopDocument large_history_document;
		large_history_document.load_pack(write_pack(large_draft));
		history.reset(large_history_document);
		for (int edit = 0; edit < 3; ++edit)
		{
			large_history_document.definitions()["entities"][1]["name"] = std::to_string(edit);
			history.checkpoint(large_history_document);
		}
		require(history.undo(large_history_document) && history.undo(large_history_document) &&
		            history.undo(large_history_document),
		        "Large unchanged pack resources are shared across multiple undo steps");
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
		{
			auto sockets = sprite;
			auto &points = sockets["frames"][0]["attachments"];
			points = Json::object();
			for (int i = 0; i < 64; ++i)
				points["socket-" + std::to_string(i)] = {{"x", .5}, {"y", .5}, {"layer", 0}};
			auto draft = sprite_draft;
			auto definitions = sprite_defs;
			definitions["entities"][0]["render"]["sprite"] = sockets;
			draft.files["definitions.json"] = json_bytes(definitions);
			const auto loaded_sockets = resolve_packs({read_pack(write_pack(draft))});
			require(compile_sprite(sockets, loaded_sockets, "sockets").frames[0].attachments.size() == 64,
			        "large modular sprites retain all bounded attachment sockets through the production loader");
			points["overflow"] = {{"x", .5}, {"y", .5}, {"layer", 0}};
			definitions["entities"][0]["render"]["sprite"] = sockets;
			draft.files["definitions.json"] = json_bytes(definitions);
			rejected(
			    [&]
			    {
				    resolve_packs({read_pack(write_pack(draft))});
			    },
			    "64 named attachments");
		}
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
		{
			// Five physical regions supply eight explicit bearings through the
			// real importer, pack loader, Workshop and permitted crew transfer.
			auto draft = sprite_draft;
			draft.files["assets/mirror-atlas.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/mirror-atlas.png");
			auto initial = resolve_packs({read_pack(write_pack(draft))});
			auto source = eight_sidecar;
			source["billboard_mode"] = "eight";
			for (int i = 0; i < 8; ++i)
			{
				auto &f = source["sprite_sheet"]["frames"][i];
				f["rect"] = {4 * (i <= 4 ? i : 8 - i), 0, 4, 4};
				f["pivot"] = {.75, 2};
				f["markers"] = {{"engine_port", {3, 1, -1}}};
				if (i > 4)
					f["mirror_x"] = true;
			}
			const std::map<std::string, std::string> images = {{"sprite", "test-core:assets/mirror-atlas.png"},
			                                                   {"normal", "test-core:assets/mirror-atlas.png"},
			                                                   {"depth", "test-core:assets/mirror-atlas.png"},
			                                                   {"radar_icon", "test-core:assets/pixel.png"}};
			Json render;
			apply_sprite_sidecar(render, source, images, initial);
			auto defs = sprite_defs;
			defs["entities"][0]["render"] = render;
			draft.files["definitions.json"] = json_bytes(defs);
			WorkshopDocument document;
			document.load_pack(write_pack(draft));
			const auto resolved = resolve_packs({read_pack(document.export_pack(PackEncoding::Dat))});
			const auto crew = resolve_packs({console_assets(resolved, "test.ship")});
			const auto &saved = crew.entities.at("test.ship").at("render").at("sprite");
			require(resolved.entities.at("test.ship").at("render").at("sprite") == render.at("sprite") &&
			            saved.at("frames") == render.at("sprite").at("frames") && saved.contains("normal") &&
			            saved.contains("depth"),
			        "Mirroring and maps survive compiled export and crew transfer");
			const auto compiled = compile_sprite(saved, crew, "mirrored");
			for (int i = 0; i < 8; ++i)
			{
				const auto &f = sprite_frame(compiled, 0, i * 45.f);
				require(f.name == std::to_string(i) && f.mirror_x == (i > 4) && f.rect[0] == 4 * (i <= 4 ? i : 8 - i) &&
				            f.pivot[0] == .75f && f.attachments.at("engine_port").x == 3,
				        "Explicit bearings reuse regions without changing source pivots or attachments");
			}
			auto named = source;
			named["sprite_sheet"] = {
			    {"entries", Json::array({{{"name", "hull"}, {"frames", source["sprite_sheet"]["frames"]}}})}};
			require(
			    compile_sprite(import_sprite_sidecar(named, images), initial, "named").entries[0].frames[7].mirror_x,
			    "Named sidecar entries preserve mirroring");
			auto single = complete_sidecar;
			single["mirror_x"] = true;
			require(
			    compile_sprite(import_sprite_sidecar(single, mapped_images), initial, "single").frames[0].mirror_x &&
			        !compiled.frames[0].mirror_x && !compile_sprite(sprite, initial, "legacy").frames[0].mirror_x,
			    "Single-image mirroring is optional and legacy frames remain unmirrored");
			for (const auto &invalid : {Json("true"), Json(1), Json(nullptr)})
			{
				auto bad_source = source;
				bad_source["sprite_sheet"]["frames"][7]["mirror_x"] = invalid;
				const auto before = render;
				rejected(
				    [&]
				    {
					    apply_sprite_sidecar(render, bad_source, images, initial);
				    },
				    "mirror_x");
				require(render == before, "Invalid mirror flags preserve the current render configuration");
				auto bad_defs = defs;
				bad_defs["entities"][0]["render"]["sprite"]["frames"][7]["mirror_x"] = invalid;
				draft.files["definitions.json"] = json_bytes(bad_defs);
				rejected(
				    [&]
				    {
					    resolve_packs({read_pack(write_pack(draft))});
				    },
				    "mirror_x");
			}
		}

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
		{
			WorkshopDocument entries;
			entries.load_pack(sprite_archive);
			WorkshopHistory history;
			history.reset(entries);
			auto &hull = entries.definitions()["entities"][0];
			auto &material = hull["render"]["sprite"];
			material["entries"] = Json::array({{{"name", "original"}, {"frames", material.at("frames")}},
			                                   {{"name", "variant"}, {"frames", material.at("frames")}}});
			material.erase("frames");
			hull["render"]["sprite_entry"] = "variant";
			hull["tags"] = {"ship", "friendly"};
			hull["systems"]["radar"]["shows"] = {"ship", "cloud"};
			entries.map()["regions"] =
			    Json::array({{{"id", "cloud"},
			                  {"shape", "annulus"},
			                  {"x_mm", 0},
			                  {"y_mm", 0},
			                  {"radius_mm", 20000},
			                  {"inner_radius_mm", 5000},
			                  {"display",
			                   {{"name", "Charted cloud"},
			                    {"navigation", true},
			                    {"radar", true},
			                    {"tags", {"cloud"}},
			                    {"colour", {.3, .4, .8}}}},
			                  {"nebula", {{"density", .01}, {"sensor_factor", 2}, {"half_height_mm", 50000}}}}});
			history.checkpoint(entries);
			const auto exported = entries.export_pack();
			WorkshopDocument reopened;
			reopened.load_pack(exported);
			require(reopened.definitions() == entries.definitions(),
			        "Environment and entry authoring changed during verified export/reload");
			const auto entry_catalog = resolve_packs({read_pack(exported)});
			const auto &entry_sprite = entry_catalog.entities.at("test.ship").at("render").at("sprite");
			SpriteCompiler shared_atlas(entry_catalog);
			(void)shared_atlas.compile(entry_sprite, "shared atlas");
			auto bad_entry = entry_sprite;
			bad_entry["entries"][1]["frames"][0]["rect"] = {0, 0, 2, 1};
			rejected(
			    [&]
			    {
				    shared_atlas.compile(bad_entry, "shared atlas");
			    },
			    "rect");
			auto edited_images = entry_catalog;
			// Keep the same path and byte length: validation receipts must compare
			// content, so an in-place corruption cannot reuse the earlier result.
			edited_images.assets.at(entry_sprite.at("albedo").get<std::string>())[0] ^= 0xff;
			rejected(
			    [&]
			    {
				    compile_sprite(entry_sprite, edited_images, "edited atlas");
			    },
			    "edited atlas");
			const auto projected = resolve_packs({console_assets(entry_catalog, "test.ship")});
			require(projected.entities.at("test.ship")["render"]["sprite"]["entries"].size() == 2 &&
			            projected.entities.at("test.ship")["systems"]["radar"]["shows"].size() == 2,
			        "Sprite entries or radar filters lost in Console packaging");
			require(history.undo(entries) && history.redo(entries) && entries.definitions() == reopened.definitions(),
			        "Environment authoring failed undo/redo");
			Json directional = Json::array();
			for (int i = 0; i < 8; ++i)
			{
				auto f = sprite.at("frames")[0];
				f["name"] = "view" + std::to_string(i);
				f["azimuth"] = i * 45;
				directional.push_back(f);
			}
			auto &authored = entries.definitions();
			authored["entities"][0]["render"]["sprite"]["entries"][1]["frames"] = directional;
			entries.map()["entities"][0]["sprite_entry"] = "variant";
			authored["entities"][1] = {{"id", "test.variant"}, {"extends", "test.ship"}, {"render", {{"scale", 2}}}};
			const auto mixed = entries.verify();
			const auto mixed_sprite =
			    compile_sprite(mixed.entities.at("test.variant").at("render").at("sprite"), mixed, "inherited entries");
			require(sprite_frame(mixed_sprite, sprite_entry_index(mixed_sprite, "variant"), 270).azimuth == 270 &&
			            sprite_frame(mixed_sprite, 0, 270).name == sprite["frames"][0]["name"].get<std::string>(),
			        "Inherited mixed entries did not select direction independently");
			auto rock = authored["entities"][0];
			rock["id"] = "test.field-rock";
			rock["systems"] = Json::object();
			rock.erase("console");
			authored["entities"].push_back(rock);
			auto &field = entries.map()["regions"][0]["asteroids"];
			field = {{"types", Json::array({{{"definition", "test.field-rock"}, {"sprite_entry", "variant"}}})},
			         {"decorative_types", Json::array({{{"definition", "test.field-rock"}}})}};
			entries.map()["scenario"]["regions"] = entries.map()["regions"];
			history.checkpoint(entries);
			entries.rename_entity("test.field-rock", "test.renamed-rock");
			history.checkpoint(entries);
			auto renamed = entries.export_pack();
			WorkshopDocument renamed_copy;
			renamed_copy.load_pack(renamed);
			for (const auto *key : {"types", "decorative_types"})
				require(renamed_copy.map()["regions"][0]["asteroids"][key][0]["definition"] == "test.renamed-rock" &&
				            renamed_copy.map()["scenario"]["regions"][0]["asteroids"][key][0]["definition"] ==
				                "test.renamed-rock",
				        "Renaming left stale field references");
			require(history.undo(entries) && history.redo(entries) &&
			            entries.verify().entities == renamed_copy.verify().entities,
			        "Field reference rename did not survive undo/redo");
		}
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
		const auto overridden_files = pack_companions({loaded, mod_loaded}, "child.zip");
		const auto overridden_menu = parse_json(overridden_files.at("child.zip.menu.json"), "child menu");
		require(overridden_menu.at("sources") == Json(modded.ordered_packs),
		        "Child companions retain ordered dependency and override identities");
		for (const auto &descriptor : overridden_menu.at("clients"))
		{
			const auto client = read_pack(overridden_files.at(descriptor.at("file").get<std::string>()));
			validate_ship_client(client, descriptor);
			require(client.hash() == ship_client_assets(modded, descriptor.at("ship")).hash(),
			        "Published and host-generated whole-ship packages agree after overrides");
		}
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
		const auto old_definition = definition;
		const auto spawn =
		    Json{{"action", "spawn_entity"}, {"arguments", Json::array({definition, "", 0, 0, 0, definition})}};
		workshop.definitions()["trees"] = Json::array(
		    {{{"id", "workshop.spawner"},
		      {"context", "scenario"},
		      {"description", definition},
		      {"initial", "spawn"},
		      {"memory", Json::array({{{"name", "note"}, {"type", "text"}, {"initial", definition}}})},
		      {"states", Json::array({{{"id", "spawn"},
		                               {"entry", Json::array({spawn, {{"memory", "note"}, {"value", definition}}})},
		                               {"running", Json::array({spawn})},
		                               {"exit", Json::array({spawn})}}})}},
		     {{"id", "workshop.derived-spawner"},
		      {"extends", "workshop.spawner"},
		      {"overrides", Json::array({{{"state", "spawn"}, {"running", Json::array({spawn})}}})}}});
		const auto preview_tree = workshop.preview_tree(workshop.definitions()["trees"][1]);
		require(!preview_tree.contains("extends") && preview_tree.at("states")[0].at("running")[0] == spawn,
		        "Workshop resolves inherited structural overrides through the production resolver");
		auto bad_override = workshop.definitions()["trees"][1];
		bad_override["overrides"][0]["state"] = "missing";
		rejected(
		    [&]
		    {
			    workshop.preview_tree(bad_override);
		    },
		    "unresolved structural override 'missing'");
		rejected(
		    [&]
		    {
			    state_tree::compile(bad_override, gameplay_tree_registry(state_tree::Context::Scenario),
			                        workshop.preview_equipment().trees);
		    },
		    "unresolved structural override 'missing'");
		workshop.map()["scenario"] = {
		    {"tree", "workshop.derived-spawner"},
		    {"spawn_groups",
		     Json::array(
		         {{{"id", "traffic"},
		           {"entities",
		            Json::array(
		                {{{"definition", definition}, {"x_mm", 20000}, {"y_mm", 0}, {"heading_mdeg", 0}}})}}})}};
		workshop.rename_entity(definition, "workshop.renamed");
		require(workshop.definitions()["maps"][0]["entities"][0]["definition"] == "workshop.renamed" &&
		            workshop.definitions()["maps"][0]["ship_slots"][0]["allowed_ships"][0] == "workshop.renamed" &&
		            workshop.map()["entities"][0]["definition"] == "workshop.renamed" &&
		            workshop.definitions()["entities"][1]["extends"] == "workshop.renamed",
		        "template rename repairs all maps, ship slots and inherited templates");
		require(workshop.map()["scenario"]["spawn_groups"][0]["entities"][0]["definition"] == "workshop.renamed",
		        "template rename repairs scenario spawn-group definition references");
		const auto &spawn_tree = workshop.definitions()["trees"][0];
		for (const char *phase : {"entry", "running", "exit"})
			require(spawn_tree["states"][0][phase][0]["arguments"][0] == "workshop.renamed",
			        "template rename repairs literal spawn_entity references in every task phase");
		require(
		    workshop.definitions()["trees"][1]["overrides"][0]["running"][0]["arguments"][0] == "workshop.renamed" &&
		        spawn_tree["description"] == old_definition && spawn_tree["memory"][0]["initial"] == old_definition &&
		        spawn_tree["states"][0]["entry"][0]["arguments"][5] == old_definition &&
		        spawn_tree["states"][0]["entry"][1]["value"] == old_definition,
		    "template rename repairs inherited task patches while preserving prose, handles and ordinary text");
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
		require(reloaded.maps.at("workshop.second")["scenario"]["spawn_groups"][0]["entities"][0]["definition"] ==
		                definition &&
		            reloaded.trees.at("workshop.spawner")["states"][0]["entry"][0]["arguments"][0] == definition,
		        "renamed scenario spawns survive production Workshop export and reload verification");
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
		{
			auto source = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"),
			                         "draft resolution fixture");
			source["schema"] = 4;
			source["knowledge"] = Json::parse(R"json({"history_limit":256,"roles":["crew"],"schemas":[
			 {"id":"sample","producers":["crew"],"readers":["crew"],"fields":[{"id":"amount","type":"integer"}]}]})json");
			const auto parent = Json::parse(R"json({"id":"test.draft-base","context":"controller","initial":"run",
			 "record_bindings":[{"name":"reading","schema":"sample","field":"amount"}],"states":[{"id":"run"}]})json");
			source["trees"].push_back(parent);
			const auto dependency = phoenix::test::archive(source);
			WorkshopDocument draft;
			draft.load_dependency(dependency);
			draft.definitions()["schema"] = 4;
			draft.definitions()["trees"] =
			    Json::array({{{"id", "test.draft-child"}, {"extends", "test.draft-base"}}, parent});
			draft.definitions()["knowledge"] = source.at("knowledge");
			draft.definitions()["knowledge"]["history_limit"] = 96;
			draft.definitions()["knowledge"]["schemas"][0]["fields"][0]["type"] = "boolean";
			const auto choices = draft.definition_choices(WorkshopDefinitionKind::Tree);
			require(choices.size() >= 2 && choices[0] == "test.draft-child" && choices[1] == "test.draft-base" &&
			            std::count(choices.begin(), choices.end(), "test.draft-base") == 1,
			        "draft choices preserve authored order and deduplicate shadowed dependency definitions");
			auto reading_type = [&]
			{
				const auto registry =
				    draft.preview_controller_registry(draft.preview_tree(std::string("test.draft-child")));
				for (const auto &field : registry.observations)
					if (field.name == "reading")
						return field.type;
				throw std::runtime_error("draft Controller reading absent");
			};
			const auto before = draft.definitions();
			require(draft.preview_knowledge().at("history_limit") == 96 && reading_type() == state_tree::Type::Boolean,
			        "draft knowledge retention and Controller vocabulary use current local overrides");
			require(draft.definitions() == before, "draft resolution queries do not mutate authored content");
			rejected(
			    [&]
			    {
				    draft.export_pack();
			    },
			    "explicit override");
			draft.manifest()["overrides"] = {"test.draft-base", "knowledge.sample"};
			const auto verified = draft.verify();
			require(verified.knowledge_definition.history_limit == 96,
			        "preview retention agrees with strict verification");
			WorkshopDocument reloaded;
			reloaded.load_dependency(dependency);
			reloaded.load_pack(draft.export_pack());
			require(reloaded.preview_knowledge() == draft.preview_knowledge(),
			        "draft knowledge survives export and reload");
			WorkshopHistory edits;
			edits.reset(draft);
			draft.definitions()["knowledge"]["schemas"][0]["fields"][0]["type"] = "integer";
			edits.checkpoint(draft);
			require(reading_type() == state_tree::Type::Integer && edits.undo(draft) &&
			            reading_type() == state_tree::Type::Boolean && edits.redo(draft) &&
			            reading_type() == state_tree::Type::Integer,
			        "draft queries immediately reflect edits, undo and redo without stale compiled data");
			draft.definitions()["trees"].push_back({{"id", "test.broken"}, {"extends", "missing.parent"}});
			draft.definitions()["equipment"] = Json::parse(R"json([
			 {"id":"draft.engine","kind":"engine"},
			 {"id":"draft.child","extends":"draft.engine"},
			 {"id":"draft.broken","extends":"missing.equipment"}])json");
			require(draft.preview_definition(WorkshopDefinitionKind::Equipment, "draft.child").at("kind") == "engine",
			        "equipment preview resolves only the selected inheritance closure");
			require(draft.preview_definition(WorkshopDefinitionKind::Map, "test.map").at("ship_slots").size() == 1,
			        "test-map query resolves dependency definitions without a GUI-owned catalog");
			draft.definitions()["knowledge"]["schemas"].push_back({{"id", "unrelated.broken"}, {"fields", 7}});
			require(reading_type() == state_tree::Type::Integer &&
			            !draft.definition_choices(WorkshopDefinitionKind::Tree).empty(),
			        "unrelated incomplete trees and schemas do not disable valid draft queries");
			rejected(
			    [&]
			    {
				    draft.preview_tree(std::string("test.broken"));
			    },
			    "missing.parent");
			draft.definitions()["knowledge"]["schemas"][0]["fields"][0]["type"] = "unsupported";
			rejected(
			    [&]
			    {
				    reading_type();
			    },
			    "test.draft-child");
			draft.definitions()["trees"].push_back(parent);
			rejected(
			    [&]
			    {
				    draft.preview_tree(std::string("test.draft-child"));
			    },
			    "duplicate draft definition");
		}
		std::cout << "PASS pack round trip, inheritance, ordered overrides and runnable-content verification\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
