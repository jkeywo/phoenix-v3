#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
Json fixture()
{
	auto source = controller_fixture(true, 12000);
	auto &ship = source["entities"][0];
	ship["stations"].push_back(
	    {{"id", "other"},
	     {"consoles",
	      Json::array({{{"id", "outsider"}, {"definition", "test.console"}, {"systems", Json::array()}}})}});
	ship["personnel"] = {
	    {"departments", Json::array({{{"id", "sailing"}, {"label", "Sailing"}, {"station", "operations"}}})},
	    {"compartments", {"bridge", "engine-room"}},
	    {"connections", Json::array({{{"from", "bridge"}, {"to", "engine-room"}, {"ticks", 3}}})},
	    {"traits", Json::array({{{"id", "expert"}, {"label", "Expert"}, {"bonuses", {{"propulsion", 250}}}},
	                            {{"id", "steady"}, {"label", "Steady"}, {"bonuses", {{"propulsion", 250}}}},
	                            {{"id", "cautious"}, {"label", "Cautious"}, {"bonuses", {{"propulsion", -1000}}}}})},
	    {"duties", Json::array({{{"id", "port"},
	                             {"label", "Port watch"},
	                             {"controller", "port_drive"},
	                             {"department", "sailing"},
	                             {"compartment", "bridge"},
	                             {"effects", {{"port", {"propulsion"}}}}},
	                            {{"id", "starboard"},
	                             {"label", "Starboard watch"},
	                             {"controller", "starboard_drive"},
	                             {"department", "sailing"},
	                             {"compartment", "engine-room"},
	                             {"effects", {{"starboard", {"propulsion"}}}}}})},
	    {"officers", Json::array({{{"id", "a"},
	                               {"name", "A"},
	                               {"department", "sailing"},
	                               {"compartment", "bridge"},
	                               {"duty", "port"},
	                               {"traits", {"expert", "steady"}}},
	                              {{"id", "b"},
	                               {"name", "B"},
	                               {"department", "sailing"},
	                               {"compartment", "engine-room"},
	                               {"duty", "starboard"},
	                               {"traits", {"cautious"}}}})}};
	return source;
}
Json board(const World &w, std::uint64_t claim = 101)
{
	return w.observe(claim)->gameplay.at("personnel");
}
OperatingCommand command(const World &w, const std::string &action, const std::string &officer,
                         const std::string &duty = {})
{
	OperatingCommand c{1, "personnel." + action, 0, officer};
	c.text = duty;
	c.generation = board(w).at("revision");
	return c;
}
int main()
{
	try
	{
		auto source = fixture();
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "outsider"), "claim owning and foreign Stations");
		require(board(world)["duties"][0]["effects"][0]["multiplier"] == 1500,
		        "positive contributions sum before multiplying");
		require(board(world)["duties"][1]["effects"][0]["multiplier"] == 500,
		        "negative contributions remain reciprocal and positive");
		require(board(world, 102)["officers"].empty(), "foreign Station cannot see roster");
		auto swap = command(world, "swap", "a", "starboard");
		require(!world.can_submit(102, swap), "host refuses foreign Station reassignment");
		require(world.submit(101, swap), "swap accepted");
		world.tick();
		require(!board(world)["officers"][0]["present"].get<bool>(), "departing officer gives no bonus");
		require(board(world)["duties"][0]["effects"][0]["multiplier"] == 1000, "vacated duty uses baseline");
		require(!world.can_submit(101, swap), "stale revision refused");
		auto no_bonus = source;
		no_bonus["entities"][0]["personnel"]["officers"] = Json::array();
		World baseline, staffed;
		baseline.load(load(no_bonus), "test.map");
		staffed.load(catalog, "test.map");
		require(staffed.claim(111, 1, "drive"), "staffed machinery may be operated by human");
		for (int i = 0; i < 10; ++i)
		{
			baseline.tick();
			staffed.tick();
		}
		require(staffed.inspect()[0].vx > baseline.inspect()[0].vx,
		        "officer expertise reaches physical engine acceleration under human control");
		const auto before_claim = board(world);
		require(world.release(102) && world.claim(102, 1, "outsider"),
		        "other Station can change operator during travel");
		require(board(world) == before_claim, "claim changes never advance officer travel outside simulation ticks");
		const auto saved = world.save();
		World restored;
		restored.restore(catalog, saved);
		require(restored.observe(101).has_value(), "restored Console retains its claim");
		for (int i = 0; i < 5; ++i)
		{
			world.tick();
			restored.tick();
		}
		require(board(world) == board(restored), "restore continues travel identically");
		require(board(world)["officers"][0]["present"].get<bool>(), "officer arrives through compartment route");
		require(board(world)["duties"][1]["effects"][0]["multiplier"] == 1500, "bonus follows officer to new duty");
		auto relieve = command(world, "relieve", "a");
		require(world.submit(101, relieve), "relieve officer");
		world.tick();
		require(board(world)["duties"][1]["effects"][0]["multiplier"] == 1000, "relief removes only its contributions");
		for (const auto *mutation : {"duplicate", "department", "location", "slot"})
		{
			auto invalid = source;
			auto &p = invalid["entities"][0]["personnel"];
			if (std::string(mutation) == "duplicate")
				p["officers"][1]["duty"] = "port";
			if (std::string(mutation) == "department")
				p["duties"][0]["department"] = "unknown";
			if (std::string(mutation) == "location")
				p["connections"] = Json::array();
			if (std::string(mutation) == "slot")
				p["duties"][0]["effects"]["port"] = {"repair"};
			bool rejected = false;
			try
			{
				(void)load(invalid);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "invalid personnel rejected by real loader");
		}
		std::cout << "Personnel integration passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
