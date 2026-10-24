// Explicit runtime measurements; authored content is never a CI timing gate.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.simulation;
import phoenix.content;
using namespace phoenix;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 6)
			throw std::runtime_error(
			    "Usage: phoenix_headless_profile pack.zip scenario ship ticks output-prefix [child.zip]");
		std::vector<Pack> packs{read_pack(read_file(argv[1]))};
		if (argc > 6)
			packs.push_back(read_pack(read_file(argv[6])));
		const auto catalog = resolve_packs(packs);
		World world;
		const std::string scenario = argv[2], ship = argv[3], output = argv[5];
		const unsigned count = std::stoul(argv[4]);
		if (!count)
			throw std::runtime_error("Profile requires at least one tick");
		const auto slot = catalog.maps.at(scenario).at("ship_slots").at(0).at("id").get<std::string>();
		world.load(catalog, scenario, {{slot, ship}});
		using Clock = std::chrono::steady_clock;
		std::vector<double> samples;
		samples.reserve(count);
		for (unsigned i = 0; i < count; ++i)
		{
			const auto start = Clock::now();
			world.tick();
			samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
		}
		// Save outside the timed region for exact before/after continuation comparison.
		auto write = [&](const std::string &suffix, const nlohmann::json &value)
		{
			std::ofstream file(output + suffix);
			file.exceptions(std::ios::badbit | std::ios::failbit);
			file << value.dump() << '\n';
		};
		write(".state.json", world.save());
		std::sort(samples.begin(), samples.end());
		double sum = 0;
		for (const auto value : samples)
			sum += value;
		const nlohmann::json report{{"scenario", scenario},
		                            {"ship", ship},
		                            {"ticks", world.ticks()},
		                            {"units", "milliseconds"},
		                            {"total", sum},
		                            {"mean", sum / count},
		                            {"p50", samples[(count - 1) / 2]},
		                            {"p95", samples[(count - 1) * 95 / 100]},
		                            {"p99", samples[(count - 1) * 99 / 100]},
		                            {"max", samples.back()}};
		write(".json", report);
		std::cout << "PHOENIX_HEADLESS_PROFILE " << report.dump() << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
