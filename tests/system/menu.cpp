#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../../src/presentation/menu_skin.hpp"
#include "../../src/presentation/sprite_direction.hpp"
import phoenix.menu;
import phoenix.session.local;
using namespace phoenix;
using phoenix::test::require;
int main()
{
	try
	{
		require(retain_sprite_direction(26, 0, 45) && !retain_sprite_direction(28, 0, 45) &&
		            retain_sprite_direction(19, 45, 0) && !retain_sprite_direction(17, 45, 0) &&
		            retain_sprite_direction(334, 0, 315) && !retain_sprite_direction(332, 0, 315) &&
		            !retain_sprite_direction(180, 0, 180),
		        "Directional sprites resist boundary jitter by five degrees, wrap through zero and follow large turns");
		// Synthetic sidecar: asymmetric borders catch uniform-scaling regressions.
		nlohmann::json skin_source = {{"schema_version", 1},
		                              {"coordinate_system", "frame_pixels_top_left_y_down"},
		                              {"sprite_sheet", nullptr},
		                              {"files", {{"sprite", "synthetic.png"}}},
		                              {"image_size", {120, 80}},
		                              {"nine_slice", {{"top", 10}, {"right", 10}, {"bottom", 20}, {"left", 20}}},
		                              {"markers", {{"label_center", {65, 35, 1}}}}};
		auto skin = MenuSkin::parse(skin_source, .5f);
		const auto label = skin.label_position(300, 100), compressed = skin.label_position(8, 6);
		require(std::abs(label[0] - 152.5f) < .01f && std::abs(label[1] - 47.5f) < .01f &&
		            std::abs(compressed[0] - 16.f / 3.f) < .01f && std::abs(compressed[1] - 2) < .01f,
		        "Nine-slice markers follow stretched centres and compressed corners");
		skin_source["markers"]["label_center"] = {10, 70, 1};
		const auto corner = MenuSkin::parse(skin_source, .5f).label_position(300, 100);
		require(corner[0] == 5 && corner[1] == 95, "Corner markers retain the display scale");
		for (const auto &patch :
		     {nlohmann::json{{"nine_slice", {{"top", 80}, {"right", 10}, {"bottom", 20}, {"left", 20}}}},
		      nlohmann::json{{"markers", {{"label_center", {121, 35, 1}}}}},
		      nlohmann::json{{"files", {{"sprite", "../outside.png"}}}}})
		{
			auto invalid = skin_source;
			invalid.update(patch);
			phoenix::test::rejects(
			    [&]
			    {
				    MenuSkin::parse(invalid, .5f);
			    },
			    "Invalid slice bounds, markers and resource paths are rejected");
		}
		// A nested library must not exceed Windows path limits just for its temporary writes.
		const auto directory = "menu-integration-" + random_token() + "/nested-library";
		const auto launch_directory = std::filesystem::current_path();
		const auto native_data = std::filesystem::u8path(native_data_directory());
		require(native_data.is_absolute(), "Native default storage is independent of the launch directory");
		std::filesystem::create_directories(directory);
		std::filesystem::current_path(directory);
		const auto other_launch_data = std::filesystem::u8path(native_data_directory());
		std::filesystem::current_path(launch_directory);
		require(native_data == other_launch_data,
		        "Different launch directories share the same native library and saves");
		MenuLibrary library(directory);
		library.load();
		require(library.preferences.volume == 50 && library.preferences.sfx_volume == 50 &&
		            library.preferences.master_volume == 50 && library.preferences.language == "en" &&
		            library.preferences.dust_motes && library.preferences.reference_grid,
		        "First launch has English and half volume");
		auto draft =
		    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");

		// Independent fixtures exercise rooted trees and profile isolation through the real loader.
		auto rooted = draft;
		rooted.manifest["base"] = {{"render_mode", "sprites"}, {"interface_style", "current"}};
		const auto root_bytes = write_pack(rooted);
		const auto root_id = rooted.manifest.at("id").get<std::string>();
		const auto scoped_directory = directory + "/rooted";
		MenuLibrary rooted_library(scoped_directory, "default/" + root_id);
		rooted_library.load();
		rooted_library.launch_base(root_bytes);
		auto rejects = [&](auto action, const char *message)
		{
			bool rejected = false;
			try
			{
				action();
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, message);
		};
		rejects(
		    [&]
		    {
			    rooted_library.toggle_pack(rooted_library.packs, rooted_library.base().hash);
		    },
		    "Launch base cannot be toggled");
		rejects(
		    [&]
		    {
			    rooted_library.apply({});
		    },
		    "Launch base cannot be removed");
		rejects(
		    [&]
		    {
			    rooted_library.replace_pack(rooted_library.base().hash, root_bytes);
		    },
		    "Launch base cannot be replaced");
		PackDraft child;
		child.manifest = {{"schema", 1}, {"id", "child"}, {"parent", root_id}};
		child.files["definitions.json"] = json_bytes(Json{{"schema", 1}});
		auto child_bytes = write_pack(child);
		rooted_library.install(child_bytes);
		PackDraft grandchild = child;
		grandchild.manifest["id"] = "grandchild";
		grandchild.manifest["parent"] = "child";
		auto grandchild_bytes = write_pack(grandchild);
		rooted_library.install(grandchild_bytes);
		rooted_library.apply(rooted_library.toggle_pack(rooted_library.packs, read_pack(grandchild_bytes).hash()));
		require(rooted_library.packs[0].enabled && rooted_library.packs[1].enabled && rooted_library.packs[2].enabled,
		        "Enabling descendants enables ordered ancestors");
		rooted_library.apply(rooted_library.toggle_pack(rooted_library.packs, read_pack(child_bytes).hash()));
		require(rooted_library.packs[0].enabled && !rooted_library.packs[1].enabled && !rooted_library.packs[2].enabled,
		        "Disabling parent leaves base enabled and disables descendants");
		rooted_library.preferences.player_name = "First base";
		rooted_library.preferences.volume = 37;
		rooted_library.remember();
		auto second = rooted;
		second.manifest["id"] = "other-root";
		// Rewrite fixture asset addresses to the second independent root.
		auto second_text =
		    std::string(second.files.at("definitions.json").begin(), second.files.at("definitions.json").end());
		for (size_t at = 0; (at = second_text.find(root_id + ":", at)) != std::string::npos; at += 11)
			second_text.replace(at, root_id.size() + 1, "other-root:");
		second.files["definitions.json"] = json_bytes(Json::parse(second_text));
		const auto second_bytes = write_pack(second);
		MenuLibrary other(scoped_directory, "default/other-root");
		other.load();
		other.launch_base(second_bytes);
		require(other.preferences.player_name == "<username>" && other.preferences.volume == 37 &&
		            other.packs.size() == 1,
		        "Base profile isolates player/content but shares audio preferences");
		MenuLibrary separate_profile(scoped_directory, "second/" + root_id);
		separate_profile.load();
		require(separate_profile.preferences.volume == 50, "Global preferences are isolated between local profiles");
		separate_profile.launch_base(root_bytes);
		separate_profile.preferences.volume = 12;
		separate_profile.remember();
		other.preferences.player_name = "Second base";
		other.remember();
		MenuLibrary root_reopened(scoped_directory, "default/" + root_id);
		root_reopened.load();
		root_reopened.launch_base(root_bytes);
		require(root_reopened.preferences.player_name == "First base" && root_reopened.packs.size() == 3,
		        "Reopening a base restores its own profile and mod choices");
		rejects(
		    [&]
		    {
			    root_reopened.launch_base(second_bytes);
		    },
		    "A process cannot switch its launch root");
		auto root_session = make_local_session();
		root_session->replace_packs({root_bytes});
		root_session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "main"});
		const auto root_save = root_session->save();
		auto matching_session = make_local_session();
		matching_session->replace_packs({root_bytes});
		matching_session->restore(root_save);
		require(matching_session->recovery_pending(), "Matching root can restore its paused save");
		auto foreign_session = make_local_session();
		foreign_session->replace_packs({second_bytes});
		rejects(
		    [&]
		    {
			    foreign_session->restore(root_save);
		    },
		    "Cross-base saves are rejected before state restoration");
		auto updated_root = rooted;
		updated_root.manifest["name"] = "Updated fixture";
		auto revision_session = make_local_session();
		revision_session->replace_packs({write_pack(updated_root)});
		rejects(
		    [&]
		    {
			    revision_session->restore(root_save);
		    },
		    "Different root revisions cannot silently restore saves");
		auto cycle_a = child, cycle_b = child;
		cycle_a.manifest = {{"schema", 1}, {"id", "cycle-a"}, {"parent", "cycle-b"}};
		cycle_b.manifest = {{"schema", 1}, {"id", "cycle-b"}, {"parent", "cycle-a"}};
		rejects(
		    [&]
		    {
			    resolve_packs({read_pack(root_bytes), read_pack(write_pack(cycle_a)), read_pack(write_pack(cycle_b))});
		    },
		    "Cyclic parent relationships cannot resolve under the launch base");
		auto wrong = child;
		wrong.manifest["parent"] = "other-root";
		rejects(
		    [&]
		    {
			    resolve_packs({read_pack(root_bytes), read_pack(write_pack(wrong))});
		    },
		    "Cross-base children cannot resolve");
		auto presentation = rooted;
		auto visuals = parse_json(presentation.files.at("definitions.json"), "synthetic visuals");
		for (auto &e : visuals["entities"])
			e["render"] = {{"scale", 2}, {"star", Json::object()}, {"radar_icon", "sun"}};
		presentation.manifest["base"]["render_mode"] = "models";
		presentation.files["definitions.json"] = json_bytes(visuals);
		const auto model_catalog = resolve_packs({read_pack(write_pack(presentation))});
		require(model_catalog.base.render_mode == "models", "Procedural model base loads without sprite assets");
		const auto model_console = resolve_packs({console_assets(model_catalog, "test.ship")});
		require(model_console.base == model_catalog.base &&
		            model_console.entities.at("test.ship").at("render").value("contact_only", false) &&
		            !model_console.entities.at("test.ship").at("render").contains("images"),
		        "Model Console projection retains its launch identity and radar without sprite fallbacks");
		WorkshopDocument model_document;
		model_document.load_dependency(write_pack(presentation));
		const auto created = model_document.create_entity();
		require(model_document.verify().entities.at(created).at("render").contains("star"),
		        "New model-base Workshop entities use a model-family representation");

		rejects(
		    [&]
		    {
			    resolve_packs({read_pack(write_pack(PackDraft{rooted.manifest, presentation.files}))});
		    },
		    "Mixed visual family is rejected");
		rejects(
		    [&]
		    {
			    compile_model(Bytes{1, 2, 3});
		    },
		    "Malformed models rejected before GPU upload");

		const auto archive = write_pack(draft);
		PackDraft addon;
		addon.manifest = {{"schema", 1}, {"id", "test-practice"}, {"dependencies", {"test-core"}}};
		auto practice = parse_json(draft.files.at("definitions.json"), "synthetic base").at("maps").at(0);
		practice["id"] = "test.practice";
		addon.files["definitions.json"] = json_bytes(Json{{"schema", 1}, {"maps", {practice}}});
		const auto addon_archive = write_pack(addon);
		MenuLibrary layered(directory + "/layered");
		layered.install_bundled(archive, addon_archive);
		require(layered.packs.size() == 2 && layered.packs[0].enabled && !layered.packs[1].enabled,
		        "Bundled examples install as a disabled mod over the enabled default pack");
		auto active = layered.toggle_pack(layered.packs, layered.packs[1].hash);
		layered.apply(active);
		auto layered_session = make_local_session();
		layered_session->replace_packs(layered.archives(layered.packs));
		require(layered_session->catalog().maps.contains("test.practice"),
		        "Enabled mod scenarios resolve against the default pack");
		layered.apply(layered.toggle_pack(layered.packs, layered.packs[0].hash));
		layered_session->replace_packs(layered.archives(layered.packs));
		require(!layered.packs[0].enabled && !layered.packs[1].enabled && layered_session->catalog().maps.empty(),
		        "Disabling the default pack disables its dependent mod and clears available scenarios");
		MenuLibrary layered_reopen(directory + "/layered");
		layered_reopen.load();
		layered_reopen.install_bundled(archive, addon_archive);
		require(!layered_reopen.packs[0].enabled && !layered_reopen.packs[1].enabled,
		        "Startup preserves an entirely disabled selection");
		auto reversed = layered_reopen.packs;
		std::reverse(reversed.begin(), reversed.end());
		layered_reopen.apply(layered_reopen.toggle_pack(reversed, reversed[0].hash));
		require(layered_reopen.packs[0].id == "test-core" && layered_reopen.packs[0].enabled &&
		            layered_reopen.packs[1].enabled,
		        "Enabling a mod enables its dependency and restores dependency order");
		PackDraft old_base = draft;
		auto old_definitions = parse_json(old_base.files.at("definitions.json"), "old synthetic base");
		practice["name"] = "Edited practice";
		old_definitions["maps"].push_back(practice);
		auto authored_map = practice;
		authored_map["id"] = "test.user-map";
		old_definitions["maps"].push_back(authored_map);
		old_base.files["definitions.json"] = json_bytes(old_definitions);
		MenuLibrary migrated(directory + "/migrated");
		migrated.install_default(write_pack(old_base));
		migrated.install_bundled(archive, addon_archive);
		auto migrated_base = resolve_packs({read_pack(migrated.read_pack_archive(migrated.packs[0].hash))});
		auto migrated_all = resolve_packs({read_pack(migrated.read_pack_archive(migrated.packs[0].hash)),
		                                   read_pack(migrated.read_pack_archive(migrated.packs[1].hash))});
		require(migrated_base.maps.contains("test.user-map") && !migrated_base.maps.contains("test.practice") &&
		            migrated_all.maps.at("test.practice").at("name") == "Edited practice" && migrated.packs[1].enabled,
		        "Splitting an older base retains user maps and moves edited examples intact into the mod");
		// Upgrade an already-installed, Workshop-edited base through startup's real path.
		auto upgrade = draft;
		auto updated = parse_json(upgrade.files.at("definitions.json"), "updated synthetic base");
		auto extra_ship = updated["entities"][0];
		extra_ship["id"] = "test.new-ship";
		updated["entities"].push_back(extra_ship);
		auto extra_map = updated["maps"][0];
		extra_map["id"] = "test.new-mission";
		extra_map["ship_slots"][0]["allowed_ships"] = {"test.new-ship"};
		updated["maps"].push_back(extra_map);
		upgrade.files["definitions.json"] = json_bytes(updated);
		upgrade.files["assets/new.png"] = draft.files.at("assets/pixel.png");
		const auto upgraded_archive = write_pack(upgrade);
		const auto old_archive = migrated.read_pack_archive(migrated.packs[0].hash);
		migrated.install_bundled(upgraded_archive, addon_archive);
		MenuLibrary upgraded_library(directory + "/migrated");
		upgraded_library.load();
		const auto upgraded_pack = read_pack(upgraded_library.read_pack_archive(upgraded_library.packs[0].hash));
		const auto upgraded_catalog = resolve_packs({upgraded_pack});
		require(
		    upgraded_catalog.maps.contains("test.new-mission") && upgraded_catalog.entities.contains("test.new-ship") &&
		        upgraded_catalog.maps.contains("test.user-map") && upgraded_pack.files().contains("assets/new.png") &&
		        upgraded_library.packs[0].enabled && upgraded_library.packs[1].enabled,
		    "Startup adds shipped missions, support definitions and assets without losing authored maps or selections");
		const auto before_upgrade = parse_json(read_pack(old_archive).files().at("definitions.json"), "prior base");
		const auto after_upgrade = parse_json(upgraded_pack.files().at("definitions.json"), "upgraded base");
		for (const auto &existing : before_upgrade.at("maps"))
			require(std::find(after_upgrade.at("maps").begin(), after_upgrade.at("maps").end(), existing) !=
			            after_upgrade.at("maps").end(),
			        "Existing map definitions survive bundled additions unchanged");
		const auto stable_hash = upgraded_library.packs[0].hash;
		upgraded_library.install_bundled(upgraded_archive, addon_archive);
		require(upgraded_library.packs[0].hash == stable_hash, "Repeated startup does not rewrite the upgraded pack");
		MenuLibrary first_run(directory + "/first-run");
		first_run.load();
		first_run.install_default(archive);
		require(first_run.packs.size() == 1 && first_run.packs.front().enabled &&
		            first_run.archives(first_run.packs).front() == archive,
		        "a fresh library starts with a verified default pack enabled");
		WorkshopDocument base_document;
		base_document.load_pack(first_run.read_pack_archive(first_run.packs.front().hash));
		base_document.new_map("test.authored-map");
		const auto authored_archive = base_document.export_pack();
		const auto authored = first_run.replace_pack(first_run.packs.front().hash, authored_archive);
		MenuLibrary authored_library(directory + "/first-run");
		authored_library.load();
		auto authored_session = make_local_session();
		authored_session->replace_packs(authored_library.archives(authored_library.packs));
		require(authored_library.packs.size() == 1 && authored_library.packs.front().enabled &&
		            authored_library.read_pack_archive(authored.hash) == authored_archive &&
		            authored_session->catalog().maps.contains("test.authored-map"),
		        "Save replaces the installed default pack and new scenarios survive library/session reopening");
		first_run.install_default(archive);
		require(first_run.packs.front().hash == authored.hash,
		        "Startup does not replace an edited default pack with its bundled original");
		bool failed_replace = false;
		try
		{
			first_run.replace_pack(authored.hash, Bytes{1, 2, 3});
		}
		catch (const std::exception &)
		{
			failed_replace = true;
		}
		require(failed_replace && first_run.read_pack_archive(authored.hash) == authored_archive,
		        "A refused replacement preserves the installed pack");
		auto disabled_default = first_run.packs;
		disabled_default.front().enabled = false;
		first_run.apply(disabled_default);
		first_run.install_default(archive);
		first_run.replace_pack(authored.hash, authored_archive);
		require(first_run.packs.size() == 1 && !first_run.packs.front().enabled,
		        "default installation preserves deliberate disabling and does not duplicate a pack");
		library.install(archive);
		require(library.packs.size() == 1 && !library.packs.front().enabled,
		        "Imports are retained but do not silently activate");
		auto selected = library.packs;
		selected.front().enabled = true;
		library.apply(selected);
		library.preferences = {0, "en", true, true, false, false};
		library.preferences.master_volume = 65;
		library.preferences.sfx_volume = 30;
		library.preferences.player_name = "Navigator";
		library.preferences.avatar = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		library.remember();
		MenuLibrary reopened(directory);
		reopened.load();
		require(reopened.preferences.volume == 0 && reopened.preferences.master_volume == 65 &&
		            reopened.preferences.sfx_volume == 30 && reopened.preferences.pseudo &&
		            reopened.preferences.reduced_motion && !reopened.preferences.dust_motes &&
		            !reopened.preferences.reference_grid && reopened.archives(reopened.packs).front() == archive,
		        "Preferences and exact enabled archives survive reopening");
		require(reopened.preferences.player_name == "Navigator" &&
		            reopened.preferences.avatar == library.preferences.avatar,
		        "Player names and validated avatars persist with the local profile");
		auto legacy_index = parse_json(read_file(directory + "/library.json"), "legacy menu index");
		legacy_index.erase("dust_motes");
		legacy_index.erase("reference_grid");
		legacy_index.erase("master_volume");
		legacy_index.erase("sfx_volume");
		write_file(directory + "/library.json", json_bytes(legacy_index));
		MenuLibrary legacy_library(directory);
		legacy_library.load();
		require(legacy_library.preferences.dust_motes && legacy_library.preferences.reference_grid &&
		            legacy_library.preferences.master_volume == 50 && legacy_library.preferences.sfx_volume == 50 &&
		            legacy_library.preferences.volume == 0,
		        "Older indexes default new mixer channels to half without resetting the music preference");
		auto session = make_local_session();
		session->replace_packs(reopened.archives(reopened.packs));
		const auto before = session->catalog().ordered_packs;
		bool refused = false;
		try
		{
			session->replace_packs({Bytes{1, 2, 3}});
		}
		catch (...)
		{
			refused = true;
		}
		require(refused && session->catalog().ordered_packs == before,
		        "Invalid replacement preserves verified content");
		session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		session->tick();
		const auto saved = library.store_save(session->save(), "Synthetic operation", "test.map");
		MenuLibrary saved_library(directory);
		saved_library.load();
		auto recovered = make_local_session();
		recovered->restore(saved_library.read_save(saved.id));
		require(recovered->recovery_pending() && recovered->ticks() == session->ticks(),
		        "Managed saves load the same paused operation through production restore");
		refused = false;
		try
		{
			library.store_save(archive, "Wrong format", "test.map");
		}
		catch (...)
		{
			refused = true;
		}
		require(refused && library.saves.size() == 1, "A failed save import preserves the managed index");
		refused = false;
		try
		{
			library.read_save("../outside");
		}
		catch (...)
		{
			refused = true;
		}
		require(refused, "Unindexed or path-like identifiers cannot read arbitrary files");
		std::cout << "PASS native sprite sidecars, preferences, installed archives, transactional selection and "
		             "portable managed saves\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
