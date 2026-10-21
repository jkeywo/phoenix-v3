#include "../support/assertions.hpp"
#include "../support/boarding_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json &body(Json &save, EntityId id)
{
	for (auto &row : save["bodies"])
		if (row["body"]["id"] == id)
			return row;
	throw std::runtime_error("missing boarding body");
}
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
OperatingCommand order(const World &w, const std::string &team, const std::string &text)
{
	const auto view = w.observe(102);
	for (const auto &row : view->gameplay.at("teams"))
		if (row["id"] == team)
			for (const auto &c : view->gameplay.at("controllers"))
				if (c["id"] == team)
				{
					OperatingCommand command{2, "team.order", row.at("revision"), team};
					command.controller = team;
					command.generation = c.at("generation");
					command.text = text;
					return command;
				}
	throw std::runtime_error("missing boarding team");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto catalog = load(boarding_fixture());
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		require(world.claim(101, 2, "drive") && world.claim(102, 2, "observer"), "claim security fixture");
		auto saved = world.save();
		auto &security = body(saved, 2)["combat"]["systems"]["security"];
		security["condition"] = "injured";
		world.restore(catalog, saved);
		require(world.submit(102, order(world, "medical", "treat/workshop/team:security")),
		        "Medical accepts an injured team");
		ticks(world, 2);
		saved = world.save();
		require(body(saved, 2)["combat"]["systems"]["security"]["treatment"] == 2000,
		        "team treatment is real per-tick work");
		World resumed;
		resumed.restore(catalog, saved);
		ticks(world, 10);
		ticks(resumed, 10);
		require(world.save() == resumed.save(), "team injury and treatment resume identically");
		saved = world.save();
		require(body(saved, 2)["combat"]["systems"]["security"]["condition"] == "healthy",
		        "team treatment heals injury");
		body(saved, 2)["combat"]["systems"]["security"]["condition"] = "lost";
		world.restore(catalog, saved);
		require(!world.submit(102, order(world, "medical", "treat/workshop/team:security")),
		        "Medical cannot resurrect a lost team");
		auto invalid = saved;
		body(invalid, 2)["combat"]["systems"]["security"]["condition"] = "unknown";
		rejects(
		    [&]
		    {
			    World bad;
			    bad.restore(catalog, invalid);
		    },
		    "unknown team condition rejected");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			auto trace = world.save();
			trace.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "PASS generic security teams, casualties and shared Medical care\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
