#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.simulation;

int main()
{
	phoenix::World world;
	world.tick();
	std::cout << "Phoenix build check: Flecs tick " << world.ticks() << std::endl;
	return world.ticks() == 1 ? 0 : 1;
}
