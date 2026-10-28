#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include "../support/polling.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

import phoenix.blobs;
import phoenix.data;
import phoenix.wire;

using namespace phoenix;
using phoenix::test::require;
void atomic_error_context(const std::filesystem::path &root)
{
	const Bytes bytes{1, 2, 3};
	for (const auto &destination : {root / "missing" / "payload.bin", root})
	{
		const auto path = destination.string();
		bool failed = false;
		try
		{
			StagedFileReplacement replacement(path, bytes, FileWriteMode::Buffered, "synthetic atomic write");
			replacement.commit();
		}
		catch (const std::system_error &error)
		{
			const std::string message = error.what();
			failed = error.code().value() != 0 && message.find("synthetic atomic write") != std::string::npos &&
			         message.find(path) != std::string::npos &&
			         message.find(error.code().message()) != std::string::npos;
		}
		require(failed, "Atomic create and replace failures preserve OS cause, destination and operation context");
	}
}
BlobResult wait(BlobStore &store)
{
	return phoenix::test::wait(store, "Blob store completion timed out");
}
int main()
{
	auto root = std::filesystem::temp_directory_path() / ("phoenix-blobs-" + random_token());
	const auto clean = [&]
	{
		if (std::filesystem::exists(root))
		{
			for (const auto &entry : std::filesystem::directory_iterator(root))
				if (entry.is_regular_file())
					std::filesystem::remove(entry.path());
			std::filesystem::remove(root);
		}
	};
	try
	{
		auto store = open_blob_store(root.string());
		require(wait(*store).error.empty(), "Persistent store must open asynchronously");
		atomic_error_context(root);
		require(store->read(1, "missing"), "Missing cache lookup must be accepted");
		auto missing = wait(*store);
		require(!missing.bytes && missing.error.empty(), "A cache miss must differ from storage corruption");
		auto pack =
		    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");
		auto archive = write_pack(pack);
		auto key = sha256(archive);
		PackSource source({archive});
		require(store->write(2, key, archive) && !store->read(3, key),
		        "Persistent writes must apply backpressure until their completion is consumed");
		require(wait(*store).error.empty(), "Verified pack must reach durable cache storage");
		const auto profile =
		    json_bytes(Json{{"schema", 1}, {"id", "synthetic-peer"}, {"credential", std::string(64, 'a')}});
		require(store->write(4, "profile.synthetic", profile) && wait(*store).error.empty(),
		        "Synthetic session profile must persist through the same platform store");
		// Exercise the public payload bound through the real record encoder and
		// native file reader: the on-disk integrity header adds bytes to this size.
		auto maximum = std::make_shared<const Bytes>(content_archive_limit, 0xa5);
		require(store->write(8, "maximum.synthetic", maximum) && wait(*store).error.empty(),
		        "A maximum-size blob payload must be accepted and durably stored");
		store.reset();
		store = open_blob_store(root.string());
		require(wait(*store).error.empty(), "Persistent cache must reopen");
		require(store->read(9, "maximum.synthetic"), "Maximum-size blob lookup must be accepted");
		{
			auto largest = wait(*store);
			require(largest.error.empty() && largest.bytes && *largest.bytes == *maximum,
			        "Maximum-size payload plus its integrity header must survive reopening");
		}
		maximum.reset();
		require(store->read(5, key), "Reopened cache lookup must be accepted");
		auto loaded = wait(*store);
		if (!loaded.error.empty())
			std::cerr << "Cache read: " << loaded.error << '\n';
		require(loaded.error.empty() && loaded.bytes && *loaded.bytes == archive,
		        "Reopened storage must preserve the exact original pack archive");
		PackTransfer cached(source.offer(), {{key, *loaded.bytes}});
		require(cached.ready() && cached.requests(0).empty(),
		        "Reopened cached packs must pass production hash/loading/resolution before bypassing transfer");
		require(store->read(6, "profile.synthetic"), "Reopened profile lookup must be accepted");
		auto identity = wait(*store);
		require(identity.bytes && *identity.bytes == profile, "Profile state must survive store reconstruction");
		store.reset();
		auto file = root / (sha256(Bytes(key.begin(), key.end())) + ".blob");
		auto encoded = read_file(file.string());
		encoded.back() ^= 1;
		write_file(file.string(), encoded);
		store = open_blob_store(root.string());
		wait(*store);
		store->read(7, key);
		require(!wait(*store).error.empty(), "A corrupt stored pack must be refused rather than made ready");
		store.reset();
		clean();
		std::cout << "PASS persistent exact pack cache, maximum payload, profile round trip, backpressure and "
		             "corrupt-record refusal\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		try
		{
			clean();
		}
		catch (...)
		{
		}
		return 1;
	}
}
