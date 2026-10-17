#include <emscripten.h>
#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.blobs;
import phoenix.wire;

using namespace phoenix;
std::unique_ptr<BlobStore> store;
bool require_existing = false;
void report(const std::string &text)
{
	std::cout << text << std::endl;
	EM_ASM({ document.getElementById('result').textContent = UTF8ToString($0);},text.c_str());
}
void frame()
{
	try
	{
		if (auto result = store->poll())
		{
			if (!result->error.empty())
				throw std::runtime_error(result->error);
			if (result->opened)
				store->read(1, "synthetic-pack");
			else if (result->request == 2)
				store->read(3, "synthetic-pack");
			else if (result->bytes)
			{
				auto pack = read_pack(*result->bytes);
				auto catalog = resolve_packs({pack});
				if (!catalog.entities.contains("test.ship"))
					throw std::runtime_error("Cached production catalogue did not resolve");
				report(result->request == 1
				           ? "PASS recovered and verified the original cached pack from IndexedDB."
				           : "PASS persisted and verified an exact pack archive. Reload to require cache recovery.");
				emscripten_cancel_main_loop();
				store.reset();
			}
			else
			{
				if (require_existing)
					throw std::runtime_error("Previously cached pack did not survive reload");
				PackDraft pack;
				pack.manifest = {{"schema", 1}, {"id", "test-core"}};
				pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
				pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
				auto archive = write_pack(pack);
				resolve_packs({read_pack(archive)});
				store->write(2, "synthetic-pack", archive);
			}
		}
	}
	catch (const std::exception &error)
	{
		report(std::string("FAIL ") + error.what());
		emscripten_cancel_main_loop();
		store.reset();
	}
}
int main(int argc, char **argv)
{
	require_existing = argc > 2 && std::string(argv[2]) == "recover";
	store = open_blob_store(argc > 1 ? argv[1] : "manual-blob-probe");
	report("Opening persistent pack cache...");
	emscripten_set_main_loop(frame, 20, true);
}
