#include "../support/assertions.hpp"
#include <emscripten.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

import phoenix.blobs;

using namespace phoenix;
using phoenix::test::require;
namespace
{
std::unique_ptr<BlobStore> store;
std::shared_ptr<const Bytes> payload;
constexpr auto write_request = std::uint64_t{9007199254740993ULL};
constexpr auto read_request = std::numeric_limits<std::uint64_t>::max();
int phase = 0;
void frame()
{
	try
	{
		const auto result = store->poll();
		if (!result)
			return;
		require(result->error.empty(), result->error.c_str());
		if (phase == 0)
		{
			require(result->opened && result->request == 0, "Opening has no operation request identifier");
			payload = std::make_shared<const Bytes>(content_archive_limit, 0xa5);
			require(store->write(write_request, "maximum.synthetic", payload), "Maximum payload is admitted");
			require(!store->read(17, "missing"), "Rejected request does not replace the pending identifier");
			phase = 1;
		}
		else if (phase == 1)
		{
			require(!result->opened && result->request == write_request && result->key == "maximum.synthetic",
			        "Write completion preserves a request above JavaScript's exact integer range");
			require(store->read(read_request, "maximum.synthetic"), "Maximum record read is admitted");
			phase = 2;
		}
		else
		{
			require(result->request == read_request && result->key == "maximum.synthetic",
			        "Read completion preserves the full uint64 request range");
			require(result->bytes && *result->bytes == *payload,
			        "Maximum payload and its integrity header survive the browser adapter round trip");
			std::cout << "PASS browser blob maximum payload, exact request identifiers and backpressure\n";
			emscripten_cancel_main_loop();
			store.reset();
			payload.reset();
		}
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		emscripten_force_exit(1);
	}
}
} // namespace
int main()
{
	store = open_blob_store("synthetic-browser-blob-contracts");
	emscripten_set_main_loop(frame, 100, true);
}
