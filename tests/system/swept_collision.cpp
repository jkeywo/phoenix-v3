#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

import phoenix.simulation;

using namespace phoenix;
namespace
{
using phoenix::test::require;
Json transform(int id, const char *definition, std::int64_t x, std::int64_t y = 0, std::int64_t heading = 0)
{
	return {{"id", id}, {"definition", definition}, {"x_mm", x}, {"y_mm", y}, {"heading_mdeg", heading}};
}
Json fixture(bool moving_box, bool target_box, bool dynamic_target, std::int64_t target_heading = 0,
             std::int64_t target_y = 0)
{
	const Json render{{"mode", "camera"}, {"images", Json::array({"sweep-test:assets/pixel.png"})}, {"scale", 1}};
	Json mover{
	    {"id", "sweep.mover"},
	    {"render", render},
	    {"systems", {{"engine", {{"kind", "engine"}, {"max_speed_mm_s", 4000000}, {"acceleration_mm_s2", 80000000}}}}},
	    {"console", "sweep.console"}};
	mover["collision"] =
	    moving_box ? Json{{"shape", "box"}, {"half_width_mm", 100}, {"half_length_mm", 100}, {"mass_grams", 1000}}
	               : Json{{"shape", "circle"}, {"radius_mm", 100}, {"mass_grams", 1000}};
	Json target{{"id", "sweep.target"}, {"render", render}, {"systems", Json::object()}};
	target["collision"] =
	    target_box ? Json{{"shape", "box"},
	                      {"half_width_mm", 1},
	                      {"half_length_mm", 5000},
	                      {"mass_grams", dynamic_target ? 1000 : 0}}
	               : Json{{"shape", "circle"}, {"radius_mm", 500}, {"mass_grams", dynamic_target ? 1000 : 0}};
	if (dynamic_target)
	{
		target["systems"] = mover["systems"];
		target["console"] = "sweep.console";
	}
	return {{"schema", 1},
	        {"entities", Json::array({mover, target})},
	        {"consoles", Json::array({{{"id", "sweep.console"}, {"widgets", Json::array()}}})},
	        {"maps", Json::array({{{"id", "sweep.map"},
	                               {"entities", Json::array({transform(1, "sweep.mover", -100000),
	                                                         transform(2, "sweep.target", dynamic_target ? 100000 : 0,
	                                                                   target_y, target_heading)})},
	                               {"ship_slots", Json::array()}}})}};
}
Catalog load(const Json &definition)
{
	auto draft = phoenix::test::fixture_pack(json_bytes(definition), "sweep-test");
	return resolve_packs({read_pack(write_pack(draft))});
}
void drive(World &world, std::uint64_t claimant, EntityId entity)
{
	require(world.claim(claimant, entity), "synthetic flight Console must admit a claim");
	require(world.submit(claimant, {entity, "speed_mm_s", 4000000}),
	        "synthetic high-speed request must use ordinary admission");
}
} // namespace
int main()
{
	try
	{
		for (const auto &shapes :
		     std::vector<std::pair<bool, bool>>{{false, false}, {false, true}, {true, false}, {true, true}})
			for (const auto heading : {0, 45000})
			{
				World world;
				world.load(load(fixture(shapes.first, shapes.second, false, heading)), "sweep.map");
				drive(world, 101, 1);
				world.tick();
				const auto bodies = world.inspect();
				require(bodies[0].x < 0 && bodies[0].x > -10000,
				        "high-speed sweep must stop at the actual circle or rotated box rather than tunnel or stop at "
				        "its broadphase bounds");
				require(bodies[1].x == 0 && bodies[1].y == 0, "static collision target must remain fixed");
				require(bodies[0].vx < 4000000, "first contact must use the ordinary collision velocity response");
			}
		{
			World world;
			world.load(load(fixture(false, false, true, 180000)), "sweep.map");
			drive(world, 101, 1);
			drive(world, 102, 2);
			world.tick();
			const auto bodies = world.inspect();
			require(bodies[0].x < bodies[1].x,
			        "opposing moving actors must collide using relative movement before swapping sides");
			require(bodies[0].vx == 0 && bodies[1].vx == 0,
			        "equal opposing masses must share the existing inelastic normal response");
		}
		{
			World world;
			world.load(load(fixture(false, true, false, 45000, 5000)), "sweep.map");
			drive(world, 101, 1);
			world.tick();
			require(world.inspect()[0].x == 100000 && world.inspect()[0].vx == 4000000,
			        "a broadphase overlap outside the actual thin box must remain a clear near miss");
		}
		{
			auto data = fixture(false, true, false);
			data["maps"][0]["entities"][1]["x_mm"] = 30000;
			data["maps"][0]["entities"].push_back(transform(3, "sweep.target", 0, 0, 45000));
			World world;
			world.load(load(data), "sweep.map");
			drive(world, 101, 1);
			world.tick();
			const auto actor = world.inspect()[0];
			require(actor.x < 1000 && actor.vx > 0 && actor.vy != 0,
			        "nearer diagonal impact must determine velocity before a farther lower-ID vertical obstacle");
		}

		{
			auto data = fixture(false, true, false, 45000);
			const auto catalog = load(data);
			World uninterrupted;
			uninterrupted.load(catalog, "sweep.map");
			drive(uninterrupted, 101, 1);
			uninterrupted.tick();
			World resumed;
			resumed.restore(catalog, uninterrupted.save());
			for (int i = 0; i < 4; ++i)
			{
				uninterrupted.tick();
				resumed.tick();
			}
			require(uninterrupted.inspect()[0].x == resumed.inspect()[0].x &&
			            uninterrupted.inspect()[0].y == resumed.inspect()[0].y &&
			            uninterrupted.inspect()[0].vx == resumed.inspect()[0].vx &&
			            uninterrupted.inspect()[0].vy == resumed.inspect()[0].vy,
			        "swept contact and subsequent movement must continue identically after recovery");
		}
		std::cout << "swept collision integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
