#include <emscripten.h>
#include <iostream>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

import phoenix.persistence;

using namespace phoenix;
std::unique_ptr<PeerStore> store;
std::unique_ptr<PeerConsensus> node;
bool writing = false, proposed = false;
bool require_existing = false, recovered_existing = false;
void report(const std::string &message)
{
	std::cout << message << '\n';
	EM_ASM({ document.getElementById('result').textContent = UTF8ToString($0); },message.c_str());
}
void frame()
{
	try
	{
		if (auto event = store->poll())
		{
			if (!event->error.empty())
				throw std::runtime_error(event->error);
			if (event->opened)
			{
				recovered_existing = !event->restored.is_null();
				if (require_existing && !recovered_existing)
					throw std::runtime_error("Previously committed peer state did not survive reload");
				node = std::make_unique<PeerConsensus>(PeerRoster{"browser-storage-test", "a", {"a"}}, event->restored);
				proposed = recovered_existing;
			}
			else
			{
				node->persisted(event->generation, true);
				writing = false;
			}
		}
		if (!node)
			return;
		for (const auto &entry : node->take_committed())
			if (entry.value.value("kind", std::string{}) == "storage-probe")
			{
				if (entry.value.at("value") != 42)
					throw std::runtime_error("Stored protocol record changed");
				report(recovered_existing
				           ? "PASS recovered the previously committed peer record from IndexedDB."
				           : "PASS strict IndexedDB durable commit. Reload to require recovery of this record.");
				emscripten_cancel_main_loop();
				return;
			}
		node->elapse(50);
		if (!proposed && node->has_quorum())
			proposed = node->propose({{"kind", "storage-probe"}, {"value", 42}});
		if (!writing)
			if (auto write = node->pending_write())
				writing = store->write(*write);
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
	const std::string key = argc > 1 ? argv[1] : "manual-storage-probe";
	require_existing = argc > 2 && std::string(argv[2]) == "recover";
	store = open_peer_store(key);
	report("Opening browser peer journal...");
	emscripten_set_main_loop(frame, 20, true);
}
