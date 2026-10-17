#include <phoenix/world.hpp>
#include <iostream>
int main()
{
	phoenix::World world;
	world.tick();
	std::cout << "Phoenix build check: Flecs tick " << world.ticks() << std::endl;
	return world.ticks() == 1 ? 0 : 1;
}
