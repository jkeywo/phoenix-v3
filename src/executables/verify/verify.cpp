#include <iostream>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

import phoenix.content.packs;
import phoenix.content.verification;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 2)
		{
			std::cerr << "Usage: phoenix_verify [--inventory output.json] base.zip [mod.zip ...] (in load order)\n";
			return 2;
		}
		std::string inventory;
		int first_pack = 1;
		if (argc >= 4 && std::string(argv[1]) == "--inventory")
		{
			inventory = argv[2];
			first_pack = 3;
		}
		std::vector<phoenix::Pack> packs;
		for (int i = first_pack; i < argc; ++i)
			packs.push_back(phoenix::read_pack(phoenix::read_file(argv[i])));
		auto catalog = phoenix::resolve_packs(packs);
		if (!inventory.empty())
		{
			phoenix::Json entities = phoenix::Json::object();
			for (const auto &[id, entity] : catalog.entities)
				entities[id] = entity;
			entities["$consoles"] = catalog.consoles;
			phoenix::write_file(inventory, phoenix::json_bytes(entities));
		}

		for (const auto &[id, hash] : catalog.ordered_packs)
			std::cout << id << " " << hash << '\n';
		std::cout << "Verified " << catalog.entities.size() << " entity definitions, " << catalog.maps.size()
		          << " maps, " << catalog.consoles.size() << " consoles\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "Verification failed: " << error.what() << '\n';
		return 1;
	}
}
