#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
import phoenix.knowledge.read_models;
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
	    {"compartments", {"bridge", "engine-room", "junction"}},
	    {"connections", Json::array({{{"from", "bridge"}, {"to", "engine-room"}, {"ticks", 9}},
	                                 {{"from", "bridge"}, {"to", "junction"}, {"ticks", 1}},
	                                 {{"from", "junction"}, {"to", "engine-room"}, {"ticks", 2}}})},
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
		{
			auto source = fixture();
			auto &layout = source["entities"][0]["control_layout"];
			layout["station_complexity"] = true;
			layout["profiles"][0]["stars"] = 3;
			auto low = layout["profiles"][0];
			low["id"] = "assisted";
			low["stars"] = 1;
			layout["profiles"].push_back(low);
			World watch;
			watch.load(load(source), "test.map");
			require(watch.claim(101, 1, "drive"), "Claim assisted personnel watch");
			require(fixture_native_submit(watch, 101, command(watch, "relieve", "a")),
			        "Three stars admits officer reassignment");
			watch.tick();
			require(fixture_native_submit(watch, 101, {1, "preset", 2, {}}), "Select one-star personnel assistance");
			watch.tick();
			require(!board(watch).at("manual").get<bool>() &&
			            !fixture_native_submit(watch, 101, command(watch, "relieve", "a")),
			        "One star hides and authoritatively refuses manual reassignment");
			watch.tick();
			require(board(watch).at("officers")[0].at("duty") == "port",
			        "Assistance restores an available duty officer");
		}
		{
			auto large = fixture();
			auto &ship = large["entities"][0];
			for (int i = 2; i < 129; ++i)
			{
				const auto id = "watch-" + std::to_string(i);
				ship["systems"][id] = ship["systems"]["port"];
				ship["control_layout"]["controllers"].push_back(
				    {{"id", id},
				     {"console", "drive"},
				     {"fallback", "hold"},
				     {"commands",
				      Json::array(
				          {{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", {id}}}})}});
				for (auto &profile : ship["control_layout"]["profiles"])
					profile["consoles"]["drive"][profile["id"] == "direct" ? "human" : "ai"].push_back(id);
				ship["personnel"]["duties"].push_back({{"id", id},
				                                       {"label", "Watch"},
				                                       {"controller", id},
				                                       {"department", "sailing"},
				                                       {"compartment", "bridge"},
				                                       {"effects", Json::object()}});
			}
			const auto content = load(large);
			World large_watch;
			large_watch.load(content, "test.map");
			require(large_watch.claim(101, 1, "drive"), "claim large synthetic watch");
			const auto roster = board(large_watch);
			require(roster["duties"].size() == 129 && personnel_projection::board(roster) == roster,
			        "large watch duties pass production loading and permitted network projection");
		}
		{
			auto high = fixture();
			high["effects"] = Json::array();
			auto &traits = high["entities"][0]["personnel"]["traits"];
			for (auto &trait : traits)
			{
				trait.erase("bonuses");
				trait["effects"] = Json::array();
			}
			for (int effect = 0; effect < 6; ++effect)
			{
				const auto id = "test.high-" + std::to_string(effect);
				Json contributions = Json::object();
				for (int n = 0; n < 64; ++n)
					contributions["part-" + std::to_string(n)] = {{"property", "propulsion"},
					                                              {"delta_permille", 100000}};
				high["effects"].push_back({{"id", id}, {"kind", "modifier"}, {"contributions", contributions}});
				traits[effect / 3]["effects"].push_back(id);
			}
			high["effects"].push_back(
			    {{"id", "test.low"},
			     {"kind", "modifier"},
			     {"contributions", {{"penalty", {{"property", "propulsion"}, {"delta_permille", -100000}}}}}});
			traits[2]["effects"].push_back("test.low");
			World legal_bounds;
			legal_bounds.load(load(high), "test.map");
			require(legal_bounds.claim(101, 1, "drive"), "claim legal property bound fixture");
			const auto roster = board(legal_bounds);
			require(roster["duties"][0]["effects"][0]["multiplier"] == 38401000 &&
			            roster["duties"][1]["effects"][0]["multiplier"] == 0 &&
			            personnel_projection::board(roster) == roster,
			        "Personnel projection preserves legal signed contribution bounds and large additive totals");

			auto &ship = high["entities"][0];
			ship["systems"]["port"]["max_speed_mm_s"] = 1000000000;
			ship["control_layout"]["controllers"][0]["commands"][0]["initial"] = 1000000000;
			ship["control_layout"]["controllers"][1]["commands"][0]["initial"] = 0;
			// Independent source identities all bind one engine; separate Stations
			// keep each permitted roster within the ordinary projection byte budget.
			for (int i = 1; i < 32; ++i)
			{
				const auto id = "source-" + std::to_string(i);
				const auto station = "ward-" + std::to_string((i - 1) / 2);
				ship["systems"][id] = {{"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 1}};
				if (i % 2)
				{
					ship["stations"].push_back({{"id", station},
					                            {"consoles", Json::array({{{"id", station},
					                                                       {"definition", "test.console"},
					                                                       {"systems", Json::array()}}})}});
					ship["personnel"]["departments"].push_back(
					    {{"id", station}, {"label", station}, {"station", station}});
				}
				ship["control_layout"]["controllers"].push_back(
				    {{"id", id},
				     {"console", station},
				     {"fallback", "hold"},
				     {"commands",
				      Json::array(
				          {{{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", {id}}}})}});
				for (auto &profile : ship["control_layout"]["profiles"])
					profile["consoles"][station]["human"].push_back(id);
				ship["personnel"]["duties"].push_back({{"id", id},
				                                       {"label", id},
				                                       {"controller", id},
				                                       {"department", station},
				                                       {"compartment", "bridge"},
				                                       {"effects", {{"port", {"propulsion"}}}}});
				ship["personnel"]["officers"].push_back({{"id", id},
				                                         {"name", id},
				                                         {"department", station},
				                                         {"duty", id},
				                                         {"compartment", "bridge"},
				                                         {"traits", {"expert", "steady"}}});
			}
			World many_sources;
			many_sources.load(load(high), "test.map");
			require(many_sources.claim(101, 1, "drive"), "claim large aggregate officer fixture");
			const auto many_roster = board(many_sources);
			require(many_roster["duties"][0]["effects"][0]["multiplier"] == 1228801000 &&
			            personnel_projection::board(many_roster) == many_roster,
			        "legal roster aggregate above one billion retains its requested factor in Personnel");
			auto capped = high;
			capped["entities"][0]["personnel"]["officers"] = Json::array();
			capped["entities"][0]["systems"]["port"]["acceleration_mm_s2"] = 1000000000;
			World native_limit;
			native_limit.load(load(capped), "test.map");
			many_sources.tick();
			native_limit.tick();
			require(many_sources.inspect()[0].vx > 0 && many_sources.inspect()[0].vx == native_limit.inspect()[0].vx,
			        "large additive aggregates saturate at the native physical acceleration limit without overflow");
		}
		auto source = fixture();
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "outsider"), "claim owning and foreign Stations");
		require(board(world)["duties"][0]["effects"][0]["multiplier"] == 1500,
		        "positive contributions sum before multiplying");
		require(board(world)["duties"][1]["effects"][0]["multiplier"] == 0,
		        "negative contributions add and can stop work");
		require(board(world, 102)["officers"].empty(), "foreign Station cannot see roster");
		const auto projected = board(world);
		require(personnel_projection::board(projected) == projected, "personnel projection preserves permitted roster");
		for (const auto *field : {"multiplier", "remaining", "bonus"})
		{
			auto invalid = projected;
			if (std::string(field) == "multiplier")
				invalid["duties"][0]["effects"][0]["multiplier"] = 1500.5;
			if (std::string(field) == "remaining")
				invalid["officers"][0]["remaining"] = 0.5;
			if (std::string(field) == "bonus")
				invalid["officers"][0]["traits"][0]["contributions"][0]["delta_permille"] = 250.5;
			bool rejected = false;
			try
			{
				(void)personnel_projection::board(invalid);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "personnel projection rejects fractional numeric state");
		}

		auto swap = command(world, "swap", "a", "starboard");
		require(!world.can_submit(102, swap), "host refuses foreign Station reassignment");
		require(fixture_native_submit(world, 101, swap), "swap accepted");
		world.tick();
		require(!board(world)["officers"][0]["present"].get<bool>(), "departing officer gives no bonus");
		require(board(world)["officers"][0]["destination"] == "junction" &&
		            board(world)["officers"][0]["remaining"] == 1,
		        "officer follows the cheaper multi-hop compartment route");
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
		World arrived_restore;
		arrived_restore.restore(catalog, world.save());
		require(board(arrived_restore) == board(world), "restore rebuilds present officer bonuses without a tick");

		auto relieve = command(world, "relieve", "a");
		require(fixture_native_submit(world, 101, relieve), "relieve officer");
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
