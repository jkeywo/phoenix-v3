// Explicit authored AI journey. Outcomes and timings are evidence, never balance assertions.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
		if (argc < 6 || argc > 8)
			throw std::runtime_error("Usage: phoenix_objective_smoke base.zip scenario ship ticks output-prefix "
			                         "[child.zip|-] [checkpoint.json]");
		std::vector<Pack> packs{read_pack(read_file(argv[1]))};
		if (argc > 6 && std::string(argv[6]) != "-")
			packs.push_back(read_pack(read_file(argv[6])));
		const auto catalog = resolve_packs(packs);
		const std::string scenario = argv[2], output = argv[5];
		const auto slot = catalog.maps.at(scenario).at("ship_slots").at(0).at("id").get<std::string>();
		World world;
		if (argc > 7)
			world.restore(catalog, parse_json(read_file(argv[7]), "manual AI journey checkpoint"));
		else
			world.load(catalog, scenario, {{slot, argv[3]}});
		const auto ship = world.ship_for_slot(slot).value();
		const auto count = std::stoull(argv[4]);
		std::ofstream trace(output + ".trace.jsonl");
		trace.exceptions(std::ios::badbit | std::ios::failbit);
		auto capture = [&]
		{
			nlohmann::json row{
			    {"tick", world.ticks()}, {"board", world.public_board(ship)}, {"controllers", nlohmann::json::array()}};
			for (const auto &body : world.inspect())
				for (const auto &controller : world.controller_ai(body.id))
				{
					if (!controller.at("eligible").get<bool>())
						continue;
					ControllerAddress address;
					address.entity = body.id;
					address.console = controller.at("console").get<std::string>();
					address.controller = controller.at("controller").get<std::string>();
					const auto snapshot = world.controller_decision_snapshot(address, true);
					if (snapshot.is_null())
						throw std::runtime_error("Missing eligible Controller snapshot: " + address.console + "/" +
						                         address.controller);
					row["controllers"].push_back(snapshot);
				}
			trace << row.dump() << std::endl;
		};
		capture();
		for (std::uint64_t i = 0; i < count; ++i)
		{
			world.tick();
			if ((i + 1) % 200 == 0)
			{
				capture();
				std::ofstream(output + ".checkpoint.json") << world.save().dump();
				std::cout << "SIM_TICK " << world.ticks() << std::endl;
				if (std::filesystem::exists(output + ".stop"))
					break;
			}
		}
		capture();
		std::ofstream final(output + ".state.json");
		final.exceptions(std::ios::badbit | std::ios::failbit);
		final << world.save().dump() << '\n';
		std::cout << "RECORDED AI journey at tick " << world.ticks() << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
