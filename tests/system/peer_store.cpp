#include "../support/assertions.hpp"
#include "../support/polling.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
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

import phoenix.data;
import phoenix.persistence;

using namespace phoenix;
using phoenix::test::require;
PeerStoreResult wait(PeerStore &store)
{
	return phoenix::test::wait(store, "Peer store completion timed out");
}
int main()
{
	auto path =
	    std::filesystem::temp_directory_path() /
	    ("phoenix-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".journal");
	auto bad = path;
	bad += ".bad";
	try
	{
		auto store = open_peer_store(path.string());
		auto opened = wait(*store);
		require(opened.opened && opened.error.empty() && opened.restored.is_null(),
		        "New durable store must open asynchronously without inventing peer state");
		auto conflict = open_peer_store(path.string());
		require(!wait(*conflict).error.empty(), "Two writers must not share one peer identity journal");
		conflict.reset();
		PeerConsensus node({"storage-test", "a", {"a"}});
		auto flush = [&]
		{
			auto write = node.pending_write();
			require(write.has_value() && store->write(*write), "Peer store must accept its next ordered transaction");
			require(!store->write(*write), "Peer store must apply backpressure while a durable write is outstanding");
			auto result = wait(*store);
			require(!result.opened && result.error.empty() && result.generation == write->generation,
			        "Successful completion must identify the flushed transaction");
			node.persisted(result.generation, result.error.empty());
		};
		flush();
		for (int i = 0; i < 3 && !node.pending_write(); ++i)
			node.elapse(1000);
		flush();
		node.take_committed();
		require(node.propose({{"kind", "storage-probe"}, {"value", 42}}),
		        "Durable elected singleton must accept a synthetic protocol record");
		flush();
		require(node.take_committed().size() == 1, "Only a flushed proposed record may become executable");
		const auto previous_size = std::filesystem::file_size(path);
		require(node.checkpoint({{"value", 42}}), "Applied singleton prefix can be checkpointed");
		flush();
		require(std::filesystem::file_size(path) < previous_size,
		        "Native checkpoint atomically replaces the journal rather than appending forever");
		conflict = open_peer_store(path.string());
		require(!wait(*conflict).error.empty(), "Writer ownership survives atomic replacement");
		conflict.reset();
		store.reset();
		{
			std::ofstream tail(path, std::ios::binary | std::ios::app);
			tail << "incomplete-unacknowledged-record";
		}
		store = open_peer_store(path.string());
		auto recovered = wait(*store);
		require(recovered.error.empty() && recovered.restored.at("commit") == 2,
		        "Reopening must retain the durable prefix and discard an incomplete unacknowledged tail");
		PeerConsensus restarted({"storage-test", "a", {"a"}}, recovered.restored);
		auto replay = restarted.take_committed();
		require(replay.size() == 1 && replay.back().value.at("kind") == "consensus-snapshot" &&
		            replay.back().value.at("state").at("value") == 42,
		        "Restart must replay exactly flushed committed work");
		store.reset();
		auto bytes = read_file(path.string());
		bytes[0] = bytes[0] == '0' ? '1' : '0';
		write_file(bad.string(), bytes);
		auto broken = open_peer_store(bad.string());
		require(!wait(*broken).error.empty(),
		        "Corrupted durable records must fail closed rather than lose a vote or committed frame");
		broken.reset();
		std::filesystem::remove(path);
		std::filesystem::remove(bad);
		std::cout << "PASS async durable peer journal, exclusive writer, backpressure, restart, partial tail and "
		             "integrity refusal\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
		std::filesystem::remove(bad, ignored);
		return 1;
	}
}
