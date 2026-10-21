#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <iostream>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

import phoenix.wire;
#include "../../src/network/projection.hpp"

using namespace phoenix;
using phoenix::test::require;
void bounded_gameplay_projection(const Catalog &catalog, const std::string &key)
{
	Observation view{{1, "test.ship", 0, 0, 0, 0, 0}, {}};
	view.observed_tick = 4;
	view.own.radar_icon = RadarIcon::ship;
	view.own.radar_sprite = "console-assets:assets/radar/" + std::string(64, 'a') + ".png";
	const auto seat = resolve_consoles(catalog, "test.ship").front();
	view.console = seat.id;
	view.controls = seat.controls;
	view.feeds = seat.feeds;
	for (const auto &spec : resolve_systems(catalog, "test.ship"))
		view.systems.push_back(
		    {spec.id, spec.kind, 0, spec.kind == OperatingSystem::Steering ? 359999 : spec.maximum, true, true});
	for (auto &sensor : view.systems)
		if (sensor.kind == OperatingSystem::Radar)
		{
			sensor.effective_range = sensor.maximum;
			sensor.reach_profile = {{500, sensor.maximum}, {1500, sensor.maximum / 4}, {360000, sensor.maximum}};
		}
	view.gameplay = {{"console", view.console},   {"ship", 1},
	                 {"controls", Json::array()}, {"notifications", Json::array()},
	                 {"records", Json::array()},  {"shared", Json::array()},
	                 {"messages", Json::array()}};
	view.gameplay["controls"].push_back({{"id", "away"}, {"action", "away"}, {"system", ""}, {"value", 1}});
	view.gameplay["notifications"].push_back({{"sequence", 120}, {"title_key", "test.latest"}});
	// An oversized individual record is omitted whole; fields are never trimmed
	// into a record that could be mistaken for a complete permitted report.
	view.gameplay["records"].push_back({{"id", 1}, {"fields", {{"large", std::string(2 * 1024 * 1024, 'x')}}}});
	view.gameplay["records"].push_back({{"id", 2}, {"fields", {{"latest", 7}}}});
	for (unsigned sequence = 1; sequence <= 120; ++sequence)
		view.gameplay["messages"].push_back({{"sequence", sequence}, {"payload", std::string(49152, 'm')}});
	require(view.gameplay.dump().size() > 4 * 1024 * 1024,
	        "Synthetic permitted history must exceed the complete transport envelope");
	const auto before = view.gameplay;
	const auto projected = phoenix::network::observation_json(view);
	const auto &gameplay = projected.at("gameplay");
	const auto restored_view = phoenix::network::read_observation(projected, catalog);
	require(restored_view.observed_tick == 4, "remote observation preserves producer sample tick");
	require(restored_view.own.radar_icon == view.own.radar_icon &&
	            restored_view.own.radar_sprite == view.own.radar_sprite &&
	            display::body_json(display::read_body(projected.at("own"))) == projected.at("own"),
	        "Console snapshots and shared displays preserve the same body appearance");
	auto legacy = projected;
	legacy["own"].erase("radar_icon");
	legacy["own"].erase("radar_sprite");
	const auto legacy_body = phoenix::network::read_observation(legacy, catalog).own;
	require(legacy_body.radar_icon == RadarIcon::contact && legacy_body.radar_sprite.empty(),
	        "legacy Console snapshots retain their optional appearance defaults");
	phoenix::test::rejects(
	    [&]
	    {
		    display::read_body(legacy.at("own"));
	    },
	    "public displays require their complete body allowlist");
	auto invalid_body = projected;
	invalid_body["own"]["radar_sprite"] = "private:assets/ship.png";
	phoenix::test::rejects(
	    [&]
	    {
		    phoenix::network::read_observation(invalid_body, catalog);
	    },
	    "Console snapshots reject a nonpublic radar sprite reference");
	phoenix::test::rejects(
	    [&]
	    {
		    display::read_body(invalid_body.at("own"));
	    },
	    "public displays apply the same radar sprite restriction");
	for (std::size_t i = 0; i < view.systems.size(); ++i)
		require(view.systems[i].reach_profile == restored_view.systems[i].reach_profile,
		        "remote projection lost adaptive reach intervals");
	const auto radar = std::find_if(projected.at("systems").begin(), projected.at("systems").end(),
	                                [](const auto &s)
	                                {
		                                return s.at("kind") == "radar";
	                                });
	if (radar != projected.at("systems").end())
		for (const auto &bad :
		     {Json::array({{2000, 0}, {1000, 0}, {360000, 0}}), Json::array({{1000, 0}}), Json::array({{360000, -1}})})
		{
			auto invalid = projected;
			invalid["systems"][static_cast<std::size_t>(radar - projected.at("systems").begin())]["reach_profile"] =
			    bad;
			phoenix::test::rejects(
			    [&]
			    {
				    (void)phoenix::network::read_observation(invalid, catalog);
			    },
			    "malformed reach profile must not cross network boundary");
		}
	auto invalid_sample_tick = projected;
	invalid_sample_tick["observed_tick"] = -1;
	phoenix::test::rejects(
	    [&]
	    {
		    phoenix::network::read_observation(invalid_sample_tick, catalog);
	    },
	    "remote observation rejects malformed negative sample tick");
	require(gameplay.dump().size() <= 1024 * 1024 && view.gameplay == before &&
	            gameplay.at("controls") == before.at("controls") &&
	            gameplay.at("notifications") == before.at("notifications"),
	        "Bounded network projection must preserve controls/notifications without changing authoritative history");
	require(gameplay.at("records").size() == 1 && gameplay.at("records")[0] == before.at("records")[1] &&
	            gameplay.at("omitted").at("records") == 1 && !gameplay.at("messages").empty() &&
	            gameplay.at("messages").back() == before.at("messages").back() &&
	            gameplay.at("messages").size() + gameplay.at("omitted").at("messages").get<std::size_t>() == 120,
	        "Projection must retain newest whole reports and explicitly count omitted records/history");
	StateSource source(Json{{"tick", 5}, {"observation", projected}}, 2);
	StateTransfer transfer(source.offer());
	for (unsigned round = 0; !transfer.ready() && round < 100; ++round)
		for (const auto &request : transfer.requests(round * 751))
		{
			const auto packet = encode_packet({"synthetic", "a", "b", "state-chunk", source.chunk(request)}, key);
			const auto admitted = decode_packet(packet, "a", "b", "synthetic", key);
			require(admitted && transfer.accept(admitted->body), "Bounded gameplay snapshot chunks must authenticate");
		}
	require(transfer.ready() &&
	            phoenix::network::read_observation(transfer.state().at("observation"), catalog).gameplay == gameplay,
	        "Oversized permitted histories must reconstruct a valid bounded observation with explicit omissions");
	Json crowded{{"controls", before.at("controls")}};
	crowded["controls"].push_back({{"id", "profile.automatic"}, {"action", "preset"}, {"system", ""}, {"value", 0}});
	for (unsigned i = 0; i < 4096; ++i)
		crowded["controls"].push_back({{"id", "actuator" + std::to_string(i)},
		                               {"action", "longitudinal_thrust"},
		                               {"system", std::string(128, 's')},
		                               {"value", 1000},
		                               {"label", std::string(256, 'l')}});
	const auto visible = phoenix::network::bounded_gameplay(crowded);
	require(visible.dump().size() <= 1024 * 1024 && visible.at("omitted").at("controls") > 0 &&
	            visible.at("controls")[0] == crowded.at("controls")[0] &&
	            visible.at("controls")[1] == crowded.at("controls")[1],
	        "A crowded control roster must retain authored Away/resume/profile priority before optional actuators");
}
int main()
{
	try
	{
		Json public_input{{"shared", Json::array({{{"schema", "public"}}})},
		                  {"messages", Json::array({{{"id", 1}, {"public", false}, {"secret", "hidden"}},
		                                            {{"id", 2}, {"public", true}, {"actionable", true}}})},
		                  {"records", Json::array({{{"secret", "hidden"}}})},
		                  {"controls", Json::array({{{"action", "danger"}}})},
		                  {"omitted", {{"shared", 3}, {"messages", 4}}}};
		const auto public_projection = network::public_board_json(public_input);
		require(!public_projection.contains("records") && !public_projection.contains("controls") &&
		            public_projection.at("messages").size() == 1 &&
		            public_projection.at("messages")[0].at("actionable") == false &&
		            public_projection.at("messages")[0].at("offer_only") == true &&
		            public_projection.at("omitted").at("shared") == 3 &&
		            public_projection.at("omitted").at("messages") == 4,
		        "wire public board excludes private data, strips authority and preserves prior omissions");
		const std::string key(64, 'a');
		Json large_state{{"tick", 75}, {"inspection", std::string(180000, 'x')}};
		StateSource state_source(large_state, 1);
		StateTransfer state_transfer(state_source.offer());
		auto state_requests = state_transfer.requests(0);
		require(state_requests.size() == 4 && state_transfer.requests(1).empty(),
		        "Projection chunks must obey the bounded outstanding window");
		auto stale = state_source.chunk(state_requests.front());
		stale["sequence"] = 2;
		require(!state_transfer.accept(stale) && !state_transfer.ready(),
		        "A stale projection sequence must not enter the current byte assembly");
		require(state_transfer.requests(749).empty(), "Projection requests must wait for their own retry deadline");
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
		StateSource small_state(Json{{"tick", 1}}, 3);
		auto corrupt_offer = small_state.offer();
		corrupt_offer["hash"] = std::string(64, '0');
		StateTransfer corrupt(corrupt_offer);
		bool bad_hash = false;
		try
		{
			corrupt.accept(small_state.chunk(corrupt.requests(0).front()));
		}
		catch (const std::exception &)
		{
			bad_hash = true;
		}
		require(bad_hash && !corrupt.ready(), "A complete projection with the wrong hash must never publish state");
		auto invalid_state_offer = state_source.offer();
		invalid_state_offer["size"] = 4 * 1024 * 1024 + 1;
		phoenix::test::rejects(
		    [&]
		    {
			    StateTransfer rejected(invalid_state_offer);
		    },
		    "Oversized projections must fail before buffer allocation");
		auto encoded = encode_packet({"synthetic", "a", "b", "command", {{"value", 42}}}, key);
		// Independent Python hmac/hashlib result for this canonical schema-5 envelope.
		require(Json::parse(encoded).at("mac") == "feb17614d3bdf66b28702a41ca92f11b897685739b1fc7d25dffa11efaa9aba2",
		        "Wire authentication must match an independent HMAC implementation");
		require(decode_packet(encoded, "a", "b", "synthetic", key).has_value(),
		        "Correct session/route/key must admit the shared C++ packet");
		auto altered = Json::parse(encoded);
		auto old = altered;
		old["schema"] = 4;
		old["mac"] = "e04656f956fda1a8caeb6b78dabf7a9f86d8d996525dfe57e8012cb16a293df3";
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
		auto base =
		    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");
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
		PackTransfer across_packs(source.offer());
		const auto across_requests = across_packs.requests(0, 8);
		require(!across_requests.empty() && across_requests.size() <= 8 && across_requests.back().at("pack") == 1 &&
		            across_packs.requests(1, 8).empty(),
		        "One outstanding request budget must cover all offered archives together");
		PackTransfer receiver(source.offer());
		require(!receiver.ready() && receiver.received_bytes() == 0,
		        "Content must not be ready before transfer and full ordered resolution");
		auto first = receiver.requests(0, 4);
		require(first.size() == 4 && receiver.requests(1, 4).empty(),
		        "Sliding transfer window must bound outstanding chunk requests");
		require(receiver.requests(1999, 4).empty(), "Pack requests retain their longer retry deadline");
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
		bounded_gameplay_projection(receiver.catalog(), key);
		std::map<std::string, Bytes> cache;
		for (const auto &bytes : originals)
			cache[sha256(bytes)] = bytes;
		PackTransfer cached(source.offer(), cache);
		require(cached.ready() && cached.requests(0).empty(), "Verified cached archives must avoid redundant transfer");
		require(cached.uncached_packs().empty(), "Verified persistent cache hits must not be rewritten");
		const auto completed_chunk = source.chunk({{"pack", 0}, {"offset", 0}});
		require(
		    !receiver.accept(completed_chunk) && !cached.accept(completed_chunk),
		    "Completed and cached packs compare duplicates against their verified archive after assembly is released");
		StateSource setup_source(originals.front(), 4);
		StateTransfer setup_transfer(setup_source.offer(), true);
		for (unsigned round = 0; !setup_transfer.ready() && round < 100; ++round)
			for (const auto &request : setup_transfer.requests(round * 751))
				setup_transfer.accept(setup_source.chunk(request));
		require(setup_transfer.ready() && setup_transfer.archive() == originals.front(),
		        "Private setup bytes use shared assembly without projection JSON parsing");

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
		phoenix::test::rejects(
		    [&]
		    {
			    PackTransfer::validate_offer(bad_offer);
		    },
		    "Oversized offers must fail before allocating the advertised archive");
		PackTransfer conflict(source.offer());
		auto chunk = source.chunk({{"pack", 0}, {"offset", 0}});
		conflict.accept(chunk);
		chunk["data"] = std::string(chunk.at("data").get<std::string>().size(), 'A');
		bool refused = false;
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
