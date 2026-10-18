#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../../src/presentation/menu_skin.hpp"
import phoenix.menu;
import phoenix.session.local;
using namespace phoenix;
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	try
	{
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
			bool rejected = false;
			try
			{
				MenuSkin::parse(invalid, .5f);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "Invalid slice bounds, markers and resource paths are rejected");
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
		require(library.preferences.volume == 50 && library.preferences.language == "en" &&
		            library.preferences.dust_motes && library.preferences.reference_grid,
		        "First launch has English and half volume");
		PackDraft draft;
		draft.manifest = {{"schema", 1}, {"id", "test-core"}};
		draft.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		draft.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		const auto archive = write_pack(draft);
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
		library.remember();
		MenuLibrary reopened(directory);
		reopened.load();
		require(reopened.preferences.volume == 0 && reopened.preferences.pseudo &&
		            reopened.preferences.reduced_motion && !reopened.preferences.dust_motes &&
		            !reopened.preferences.reference_grid && reopened.archives(reopened.packs).front() == archive,
		        "Preferences and exact enabled archives survive reopening");
		auto legacy_index = parse_json(read_file(directory + "/library.json"), "legacy menu index");
		legacy_index.erase("dust_motes");
		legacy_index.erase("reference_grid");
		write_file(directory + "/library.json", json_bytes(legacy_index));
		MenuLibrary legacy_library(directory);
		legacy_library.load();
		require(legacy_library.preferences.dust_motes && legacy_library.preferences.reference_grid,
		        "Older preference indexes default both navigation aids on");
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
