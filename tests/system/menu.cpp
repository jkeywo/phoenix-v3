#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
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
		const auto directory = "menu-integration-" + random_token();
		MenuLibrary library(directory);
		library.load();
		require(library.preferences.volume == 50 && library.preferences.language == "en",
		        "First launch has English and half volume");
		PackDraft draft;
		draft.manifest = {{"schema", 1}, {"id", "test-core"}};
		draft.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		draft.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		const auto archive = write_pack(draft);
		library.install(archive);
		require(library.packs.size() == 1 && !library.packs.front().enabled,
		        "Imports are retained but do not silently activate");
		auto selected = library.packs;
		selected.front().enabled = true;
		library.apply(selected);
		library.preferences = {0, "en", true, true};
		library.remember();
		MenuLibrary reopened(directory);
		reopened.load();
		require(reopened.preferences.volume == 0 && reopened.preferences.pseudo &&
		            reopened.preferences.reduced_motion && reopened.archives(reopened.packs).front() == archive,
		        "Preferences and exact enabled archives survive reopening");
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
		std::cout
		    << "PASS native preferences, installed archives, transactional selection and portable managed saves\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
