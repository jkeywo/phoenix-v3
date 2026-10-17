#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.files;
import phoenix.content;
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
		PackDraft pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		const auto archive = write_pack(pack);
		FileImportInbox game, workshop;
		const auto game_request = game.begin(FilePurpose::GamePack);
		const auto editor_request = workshop.begin(FilePurpose::WorkshopPack);
		complete_import(game_request, "game.zip", archive);
		require(!workshop.take(), "Workshop must not consume a completed game request while game is inactive");
		bool busy = false;
		try
		{
			game.begin(FilePurpose::GameSave);
		}
		catch (const std::exception &)
		{
			busy = true;
		}
		require(busy, "A retained completion must not be overwritten by another request");
		complete_import(editor_request, "editor.zip", archive);
		auto editor_result = workshop.take();
		WorkshopDocument document;
		document.load_pack(editor_result->bytes);
		auto result = game.take();
		require(result && result->id == game_request && result->purpose == FilePurpose::GamePack,
		        "Return must retrieve the original consumer's result and purpose");
		auto session = make_local_session();
		session->add_pack(result->bytes);
		require(session->catalog().entities == document.verify().entities,
		        "Independent consumers must use the real pack verification paths");
		complete_import(game_request, "duplicate.zip", Bytes{1});
		require(!game.take(), "Duplicate completion must have no effect");
		const auto stale = game.begin(FilePurpose::GameSave);
		game.reset();
		const auto current = game.begin(FilePurpose::GamePack);
		complete_import(stale, "old.zip", archive);
		require(!game.take(), "Reset must invalidate the old generation");
		complete_import(current, {}, {}, "synthetic picker failure");
		result = game.take();
		require(result && result->status == ImportStatus::Failed && result->purpose == FilePurpose::GamePack &&
		            !result->error.empty(),
		        "Errors must retain owner and purpose");
		const auto cancel = game.begin(FilePurpose::GameSave);
		complete_import(cancel, {}, {}, {}, true);
		require(game.take()->status == ImportStatus::Cancelled,
		        "Cancellation must complete and release the pending operation");
		const auto delayed = game.begin(FilePurpose::GamePack);
		session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		complete_import(delayed, "delayed.zip", archive);
		result = game.take();
		bool refused = false;
		try
		{
			session->add_pack(result->bytes);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && session->running(),
		        "Applying a delayed result must recheck current eligibility and preserve the live world");
		const auto too_large = workshop.begin(FilePurpose::WorkshopImage);
		complete_import(too_large, "large.png", Bytes(32 * 1024 * 1024 + 1));
		result = workshop.take();
		require(result->status == ImportStatus::Failed && result->bytes.empty(),
		        "Oversized platform results must not be retained");
		ImportId destroyed;
		{
			FileImportInbox temporary;
			destroyed = temporary.begin(FilePurpose::GamePack);
		}
		complete_import(destroyed, "late.zip", archive);
		std::cout << "PASS owned file imports, deferred application, cancellation and stale results\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
