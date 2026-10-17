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
#include <vector>

import phoenix.blobs;
import phoenix.wire;

using namespace phoenix;
void require(bool result, const char *message)
{
	if (!result)
		throw std::runtime_error(message);
}
BlobResult wait(BlobStore &store)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	do
	{
		if (auto result = store.poll())
			return *result;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (std::chrono::steady_clock::now() < end);
	throw std::runtime_error("Blob store completion timed out");
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
		require(store->read(1, "missing"), "Missing cache lookup must be accepted");
		auto missing = wait(*store);
		require(!missing.bytes && missing.error.empty(), "A cache miss must differ from storage corruption");
		Pack pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
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
		store.reset();
		store = open_blob_store(root.string());
		require(wait(*store).error.empty(), "Persistent cache must reopen");
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
		std::cout << "PASS persistent exact pack cache, profile round trip, backpressure and corrupt-record refusal\n";
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
