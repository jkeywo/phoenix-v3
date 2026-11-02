// Measure canonical permitted projections from a restored diagnostic checkpoint.
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.simulation;
import phoenix.data;
import phoenix.contracts;
import phoenix.content.packs;
import phoenix.content.verification;
import phoenix.networking.projection;
import phoenix.wire;
using namespace phoenix;

int main(int argc, char **argv)
try
{
	if (argc != 5)
		throw std::runtime_error("Usage: phoenix_projection_sizes pack checkpoint slot output");
	const auto catalog = resolve_packs({read_pack(read_file(argv[1]))});
	World world;
	world.restore(catalog, Json::parse(read_file(argv[2])));
	// Rebuild transient decision diagnostics through an ordinary fixed tick.
	world.tick();
	const auto ship = world.ship_for_slot(argv[3]);
	if (!ship)
		throw std::runtime_error("Checkpoint has no selected ship slot");
	Json report{{"tick", world.ticks()}, {"scope", "Permitted Console observation JSON, without state envelope"}};
	std::uint64_t route = 1;
	struct Sample
	{
		std::uint64_t route;
		std::unique_ptr<StateSource> source;
		std::unique_ptr<StateTransfer> transfer;
	};
	std::map<std::string, Sample> samples;
	const auto receive = [](StateTransfer &transfer, const StateSource &source)
	{
		for (std::uint64_t round = 0; !transfer.ready() && round < 1000; ++round)
			for (const auto &request : transfer.requests(round * 751))
				transfer.accept(source.chunk(request));
		if (!transfer.ready())
			throw std::runtime_error("Diagnostic transfer did not complete");
	};
	for (const auto &console : world.consoles(*ship))
	{
		if (!world.claim(route, *ship, console.id))
			continue;
		const auto view = world.observe(route++);
		if (!view)
			throw std::runtime_error("Claimed Console has no permitted observation");
		const auto payload = network::observation_json(*view);
		std::map<std::string, std::uint64_t> totals, counts;
		const auto visit = [&](const auto &self, const Json &value, const std::string &path, unsigned depth) -> void
		{
			totals[path] += value.dump().size();
			++counts[path];
			if (depth == 7)
				return;
			if (value.is_object())
				for (const auto &[key, child] : value.items())
					self(self, child, path + "/" + key, depth + 1);
			else if (value.is_array())
				for (const auto &child : value)
					self(self, child, path + "/[]", depth + 1);
		};
		visit(visit, payload, "$", 0);
		std::vector<std::pair<std::string, std::uint64_t>> ordered(totals.begin(), totals.end());
		std::sort(ordered.begin(), ordered.end(),
		          [](const auto &a, const auto &b)
		          {
			          return a.second > b.second;
		          });
		auto &rows = report["consoles"][console.id];
		rows["bytes"] = payload.dump().size();
		rows["sections"] = Json::array();
		for (const auto &[path, bytes] : ordered)
			if (bytes >= 256)
				rows["sections"].push_back({{"path", path}, {"bytes", bytes}, {"occurrences", counts.at(path)}});
		auto source = std::make_unique<StateSource>(payload, 1);
		auto transfer = std::make_unique<StateTransfer>(source->offer());
		receive(*transfer, *source);
		if (transfer->state() != payload)
			throw std::runtime_error("Initial binary projection differs from permitted observation");
		rows["wire"]["initial_cbor_bytes"] = source->offer().at("size");
		rows["wire"]["updates"] = Json::array();
		samples.emplace(console.id, Sample{route - 1, std::move(source), std::move(transfer)});
	}
	using Clock = std::chrono::steady_clock;
	for (unsigned update = 0; update < 10; ++update)
	{
		// Match the production 10 Hz projection schedule over 20 Hz fixed ticks.
		world.tick();
		world.tick();
		for (auto &[console, sample] : samples)
		{
			const auto view = world.observe(sample.route);
			if (!view)
				throw std::runtime_error("Claimed Console lost observation during sampling");
			const auto payload = network::observation_json(*view);
			const auto start = Clock::now();
			auto source = std::make_unique<StateSource>(payload, update + 2, sample.source.get());
			const auto encoded = Clock::now();
			auto transfer = std::make_unique<StateTransfer>(source->offer(), false, sample.transfer.get());
			receive(*transfer, *source);
			const auto decoded = Clock::now();
			if (transfer->state() != payload)
				throw std::runtime_error("Binary delta differs from complete permitted observation");
			StateSource full(payload, update + 2);
			report["consoles"][console]["wire"]["updates"].push_back(
			    {{"tick", world.ticks()},
			     {"full_json_bytes", payload.dump().size()},
			     {"full_cbor_bytes", full.offer().at("size")},
			     {"transfer_bytes", source->offer().at("size")},
			     {"encoding", source->offer().at("encoding")},
			     {"encode_ms", std::chrono::duration<double, std::milli>(encoded - start).count()},
			     {"decode_ms", std::chrono::duration<double, std::milli>(decoded - encoded).count()},
			     {"complete_projection_equal", true}});
			sample.source = std::move(source);
			sample.transfer = std::move(transfer);
		}
	}
	std::ofstream output(argv[4]);
	output.exceptions(std::ios::badbit | std::ios::failbit);
	output << report.dump(2) << '\n';
	std::cout << "Projection size breakdown written\n";
}
catch (const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return 1;
}
