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
			std::cerr << "Usage: phoenix_verify base.zip [mod.zip ...] (in load order)\n";
			return 2;
		}
		std::vector<phoenix::Pack> packs;
		for (int i = 1; i < argc; ++i)
			packs.push_back(phoenix::read_pack(phoenix::read_file(argv[i])));
		auto catalog = phoenix::resolve_packs(packs);
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
