// Measure canonical permitted projections from a restored diagnostic checkpoint.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
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
