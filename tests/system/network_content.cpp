#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.wire;

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
		const std::string key(64, 'a');
		Json large_state{{"tick", 75}, {"inspection", std::string(180000, 'x')}};
		StateSource state_source(large_state, 1);
		StateTransfer state_transfer(state_source.offer());
		auto state_requests = state_transfer.requests(0);
		require(state_requests.size() == 4 && state_transfer.requests(1).empty(),
		        "Projection chunks must obey the bounded outstanding window");
		auto retry = state_transfer.requests(751);
		require(retry == state_requests, "Missing projection chunks must be retried");
		std::reverse(retry.begin(), retry.end());
		for (const auto &request : retry)
		{
			auto chunk = state_source.chunk(request);
			auto packet = encode_packet({"synthetic", "a", "b", "state-chunk", chunk}, key);
			auto admitted = decode_packet(packet, "a", "b", "synthetic", key);
			require(admitted && state_transfer.accept(admitted->body), "Projection chunks must authenticate");
			require(!state_transfer.accept(admitted->body), "Projection chunk duplicates must be idempotent");
		}
		require(!state_transfer.ready(), "Partial projections must never publish world state");
		for (unsigned round = 0; !state_transfer.ready() && round < 100; ++round)
			for (const auto &request : state_transfer.requests(1502 + round * 751))
				state_transfer.accept(state_source.chunk(request));
		require(state_transfer.ready() && state_transfer.state() == large_state,
		        "A projection larger than a packet must reconstruct the exact permitted snapshot");
		auto invalid_state_offer = state_source.offer();
		invalid_state_offer["size"] = 4 * 1024 * 1024 + 1;
		bool invalid_state_refused = false;
		try
		{
			StateTransfer rejected(invalid_state_offer);
		}
		catch (const std::exception &)
		{
			invalid_state_refused = true;
		}
		require(invalid_state_refused, "Oversized projections must fail before buffer allocation");
		auto encoded = encode_packet({"synthetic", "a", "b", "command", {{"value", 42}}}, key);
		// Independent Node crypto HMAC-SHA256 result for this canonical synthetic envelope.
		require(Json::parse(encoded).at("mac") == "27940d400d6fdc0c8172869a71baf7bbcd988b63fa696c9a9ac4105b7ec3f548",
		        "Wire authentication must match an independent HMAC implementation");
		require(decode_packet(encoded, "a", "b", "synthetic", key).has_value(),
		        "Correct session/route/key must admit the shared C++ packet");
		auto altered = Json::parse(encoded);
		auto old = altered;
		old["schema"] = 1;
		old["mac"] = "3408c0a866d65aa27b4a3b4032463fcaa6a83760e494e19deb5643ccd1e973ba";
		require(!decode_packet(old.dump(), "a", "b", "synthetic", key),
		        "Old packet schema refuses before instance projection interpretation");
		altered["body"]["value"] = 99;
		require(!decode_packet(altered.dump(), "a", "b", "synthetic", key) &&
		            !decode_packet(encoded, "outsider", "b", "synthetic", key) &&
		            !decode_packet(encoded, "a", "b", "other-session", key),
		        "Forged payload, sender or session must be refused");
		auto unsigned_command = encode_packet({"synthetic", "a", "b", "command", {{"value", 42}}}, "");
		require(!decode_packet(unsigned_command, "a", "b", "synthetic", ""),
		        "A URL/discovery hint must not become an unsigned operating command");
		auto hello = encode_packet({"synthetic", "a", "b", "hello", {{"role", "client"}}}, "");
		require(decode_packet(hello, "a", "b", "synthetic", "").has_value(),
		        "Bounded unsigned hello may request admission without granting it");
		auto token = random_token();
		require(token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos &&
		            token != random_token(),
		        "Platform must supply independent cryptographic session tokens");
		PackDraft base;
		base.manifest = {{"schema", 1}, {"id", "test-core"}};
		base.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		base.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		// Incompressible synthetic data forces several bounded transfer chunks.
		Bytes bulk(65537);
		std::uint32_t random = 1234567;
		for (auto &byte : bulk)
		{
			random ^= random << 13;
			random ^= random >> 17;
			random ^= random << 5;
			byte = static_cast<std::uint8_t>(random);
		}
		base.files["notes/synthetic.bin"] = bulk;
		PackDraft mod;
		mod.manifest = {{"schema", 1}, {"id", "test-addon"}, {"dependencies", Json::array({"test-core"})}};
		mod.files["definitions.json"] = json_bytes(
		    Json{{"schema", 1},
		         {"entities",
		          Json::array({{{"id", "test.additional"}, {"extends", "test.ship"}, {"render", {{"scale", 3}}}}})}});
		const std::vector<Bytes> originals{write_pack(base), write_pack(mod)};
		PackSource source(originals);
		PackTransfer receiver(source.offer());
		require(!receiver.ready() && receiver.received_bytes() == 0,
		        "Content must not be ready before transfer and full ordered resolution");
		auto first = receiver.requests(0, 4);
		require(first.size() == 4 && receiver.requests(1, 4).empty(),
		        "Sliding transfer window must bound outstanding chunk requests");
		require(receiver.requests(2001, 4).size() == 4,
		        "Timed-out requests must retry without abandoning content identity");
		std::reverse(first.begin(), first.end());
		for (const auto &request : first)
		{
			auto chunk = source.chunk(request);
			auto packet = encode_packet({"synthetic", "a", "b", "pack-chunk", chunk}, key);
			auto admitted = decode_packet(packet, "a", "b", "synthetic", key);
			require(admitted && receiver.accept(admitted->body),
			        "Authenticated out-of-order chunks must pass through the real archive transfer path");
			require(!receiver.accept(admitted->body), "Identical duplicate chunks must be idempotent");
		}
		for (unsigned round = 0; !receiver.ready() && round < 100; ++round)
			for (const auto &request : receiver.requests(2100 + round * 2001, 4))
				receiver.accept(source.chunk(request));
		require(receiver.ready() && receiver.received_bytes() == receiver.total_bytes() &&
		            receiver.packs().size() == originals.size() && receiver.packs()[0].archive() == originals[0] &&
		            receiver.packs()[1].archive() == originals[1],
		        "Transfer must preserve exact original archives and resolve before readiness");
		require(receiver.catalog().entities.contains("test.additional"),
		        "Transferred dependent definitions must resolve through production loaders");
		std::map<std::string, Bytes> cache;
		for (const auto &bytes : originals)
			cache[sha256(bytes)] = bytes;
		PackTransfer cached(source.offer(), cache);
		require(cached.ready() && cached.requests(0).empty(), "Verified cached archives must avoid redundant transfer");
		require(cached.uncached_packs().empty(), "Verified persistent cache hits must not be rewritten");
		require(receiver.uncached_packs().size() == originals.size(),
		        "Downloaded archives must be available for persistent caching");
		auto owner = receiver.uncached_packs()[0].archive_owner();
		require(owner.get() == &receiver.packs()[0].archive() && *owner == originals[0],
		        "Queued cache writes must retain immutable verified bytes without another archive copy");
		auto broken_cache = cache;
		broken_cache.begin()->second[0] ^= 1;
		PackTransfer reload(source.offer(), broken_cache);
		require(!reload.ready() && !reload.requests(0).empty(),
		        "A corrupt cache hit must be re-fetched instead of trusted by filename");
		for (unsigned round = 0; !reload.ready() && round < 100; ++round)
			for (const auto &request : reload.requests(round * 2001, 4))
				reload.accept(source.chunk(request));
		require(reload.ready() && reload.uncached_packs().size() == 1,
		        "Only repaired cache content must be scheduled for persistence");
		auto bad_offer = source.offer();
		bad_offer["packs"][0]["size"] = 100000000;
		bool refused = false;
		try
		{
			PackTransfer::validate_offer(bad_offer);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Oversized offers must fail before allocating the advertised archive");
		PackTransfer conflict(source.offer());
		auto chunk = source.chunk({{"pack", 0}, {"offset", 0}});
		conflict.accept(chunk);
		chunk["data"] = std::string(chunk.at("data").get<std::string>().size(), 'A');
		refused = false;
		try
		{
			conflict.accept(chunk);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Conflicting repeats must not overwrite previously received archive bytes");
		std::cout << "PASS authenticated C++ packets, bounded ordered pack transfer, retries, duplicate handling and "
		             "verified cache use\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
