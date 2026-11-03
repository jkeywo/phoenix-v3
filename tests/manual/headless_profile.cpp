// Explicit runtime measurements; authored content is never a CI timing gate.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.simulation;
import phoenix.content.packs;
import phoenix.content.verification;
using namespace phoenix;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 6)
			throw std::runtime_error(
			    "Usage: phoenix_headless_profile pack.zip scenario ship ticks output-prefix [child.zip]");
		using Clock = std::chrono::steady_clock;
		const auto starting = Clock::now();
		std::vector<Pack> packs{read_pack(read_file(argv[1]))};
		if (argc > 6)
			packs.push_back(read_pack(read_file(argv[6])));
		const auto loaded = Clock::now();
		const auto verified = verify_packs(std::move(packs));
		const auto &catalog = verified->catalog();
		const auto resolved = Clock::now();
		World world;
		const std::string scenario = argv[2], ship = argv[3], output = argv[5];
		const unsigned count = std::stoul(argv[4]);
		const auto slot = catalog.maps.at(scenario).at("ship_slots").at(0).at("id").get<std::string>();
		world.load(*verified, scenario, {{slot, ship}});
		const auto constructed = Clock::now();
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
		const nlohmann::json report{
		    {"scenario", scenario},
		    {"ship", ship},
		    {"ticks", world.ticks()},
		    {"units", "milliseconds"},
		    {"total", sum},
		    {"mean", count ? Json(sum / count) : Json{}},
		    {"p50", count ? Json(samples[(count - 1) / 2]) : Json{}},
		    {"p95", count ? Json(samples[(count - 1) * 95 / 100]) : Json{}},
		    {"p99", count ? Json(samples[(count - 1) * 99 / 100]) : Json{}},
		    {"max", count ? Json(samples.back()) : Json{}},
		    {"archive_ms", std::chrono::duration<double, std::milli>(loaded - starting).count()},
		    {"catalogue_ms", std::chrono::duration<double, std::milli>(resolved - loaded).count()},
		    {"world_ms", std::chrono::duration<double, std::milli>(constructed - resolved).count()}};
		write(".json", report);
		std::cout << "PHOENIX_HEADLESS_PROFILE " << report.dump() << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
