#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = combat_fixture();
	auto &e = source["entities"][0];
	e["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                           {"capacity", 1000000},
	                                           {"initial", 1000000},
	                                           {"charge_rate", 2000},
	                                           {"discharge_rate", 2000},
	                                           {"operation_heat_ppm", 100000}}})}};
	e["psychic_powers"] = {{"cloak", {{"kind", "cloak"}, {"label", "Cloak"}, {"strength_permille", 900}}},
	                       {"scramble",
	                        {{"kind", "scramble"},
	                         {"label", "Ghost ships"},
	                         {"strength_permille", 600},
	                         {"ghost_count", 2},
	                         {"scan_ticks", 6},
	                         {"speed_mm_s", 1000},
	                         {"offset_mm", 20000},
	                         {"spacing_mm", 10000},
	                         {"recovery_ticks", 5}}},
	                       {"ward", {{"kind", "ward"}, {"label", "Ward"}, {"strength_permille", 800}}}};
	Json commands = Json::array(
	         {{{"id", "apparatus"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"choir"}}}}),
	     seats = Json::array();
	e["personnel"] = {
	    {"departments", Json::array({{{"id", "signals"}, {"label", "Signals"}, {"station", "operations"}}})},
	    {"traits", Json::array()},
	    {"duties", Json::array()},
	    {"officers", Json::array()}};
	for (const auto &c : e["control_layout"]["controllers"])
		e["personnel"]["duties"].push_back({{"id", c.at("id")},
		                                    {"label", c.at("id")},
		                                    {"department", "signals"},
		                                    {"compartment", "fore"},
		                                    {"console", c.at("console")},
		                                    {"controller", c.at("id")}});
	for (const auto *team : {"team-a", "team-b"})
		e["personnel"]["duties"].push_back({{"id", std::string(team) + "-duty"},
		                                    {"label", team},
		                                    {"department", "signals"},
		                                    {"compartment", "fore"},
		                                    {"team", team}});
	e["personnel"]["duties"].push_back({{"id", "conductor"},
	                                    {"label", "Choir conductor"},
	                                    {"department", "signals"},
	                                    {"compartment", "fore"},
	                                    {"controller", "choir"},
	                                    {"console", "drive"}});
	for (const auto &id : {"cloak", "scramble", "ward"})
	{
		std::string key = id;
		commands.push_back({{"id", key + "-enabled"}, {"action", "intent_boolean"}, {"consumers", Json::array()}});
		commands.push_back({{"id", key + "-target"},
		                    {"action", "intent_integer"},
		                    {"minimum", 0},
		                    {"maximum", 1000000000},
		                    {"consumers", Json::array()}});
		seats.push_back({{"duty", key},
		                 {"controller", "choir"},
		                 {"console", "drive"},
		                 {"enabled", key + "-enabled"},
		                 {"target", key + "-target"}});
		e["personnel"]["duties"].push_back(
		    {{"id", key}, {"label", key}, {"department", "signals"}, {"compartment", "fore"}, {"choir", "choir"}});
		e["personnel"]["officers"].push_back({{"id", key},
		                                      {"name", key},
		                                      {"department", "signals"},
		                                      {"compartment", "fore"},
		                                      {"duty", key},
		                                      {"power", key},
		                                      {"traits", Json::array()}});
	}
	e["systems"]["choir"] = {{"kind", "psychic"},  {"compartment", "fore"}, {"power_store", "cell"},
	                         {"range_mm", 300000}, {"arc_mdeg", 90000},     {"powers", {"cloak", "scramble", "ward"}},
	                         {"seats", seats}};
	e["systems"]["radar"]["capabilities"] = {{"basic_identity", 0}, {"compartment_condition", 4}};
	for (auto &c : e["control_layout"]["controllers"])
		if (c.at("id") == "radar")
			c["commands"].push_back({{"id", "target"}, {"action", "sensor_target"}, {"consumers", {"radar"}}});
	e["control_layout"]["controllers"].push_back(
	    {{"id", "choir"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", commands}});
	e["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("choir");
	e["stations"][0]["consoles"][0]["feeds"].push_back("choir");
	e["stations"][0]["consoles"][1]["feeds"] = Json::array();
	source["maps"][0]["factions"] = Json::array({{{"id", "a"}, {"label", "A"}}, {{"id", "b"}, {"label", "B"}}});
	source["maps"][0]["relationships"] = Json::array({{{"a", "a"}, {"b", "b"}, {"relation", "hostile"}}});
	source["maps"][0]["entities"][0]["faction"] = "a";
	source["maps"][0]["entities"][1]["faction"] = "b";
	source["maps"][0]["entities"][2]["faction"] = "a";
	return source;
}
void ticks(World &w, int n = 1)
{
	while (n--)
		w.tick();
}
void command(World &w, std::uint64_t claim, EntityId ship, const std::string &ctrl, const std::string &channel,
             std::int64_t value)
{
	const auto observed = w.observe(claim);
	for (const auto &c : observed->gameplay.at("controllers"))
		if (c.at("id") == ctrl)
		{
			require(w.submit(claim, OperatingCommand{ship, {}, value, {}, ctrl, channel, c.at("generation")}),
			        "admitted psychic or equipment command");
			return;
		}
	throw std::runtime_error("missing Controller " + ctrl);
}
std::vector<EntityId> ghosts(const World &w, std::uint64_t claim)
{
	std::vector<EntityId> out;
	const auto observed = w.observe(claim);
	for (const auto &c : observed->contacts)
		if (c.id > 3 && c.id <= 1000000000ULL)
			out.push_back(c.id);
	return out;
}
void claim(World &w)
{
	require(w.claim(101, 1, "drive"), "claim source");
	require(w.claim(102, 2, "drive"), "claim victim");
	require(w.claim(103, 3, "drive"), "claim ally");
	require(w.claim(104, 2, "observer"), "claim unfed Console");
}

} // namespace
int main(int argc, char **argv)
{
	try
	{
		const auto source = fixture();
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map");
		claim(w);
		ticks(w);
		command(w, 101, 1, "choir", "scramble-target", 2);
		command(w, 101, 1, "choir", "scramble-enabled", 1);
		command(w, 101, 1, "choir", "cloak-enabled", 1);
		ticks(w, 3);
		require(ghosts(w, 102).size() == 2, "authored moving ghost group received");
		require(ghosts(w, 104).empty(), "unfed Console cannot observe ghosts");
		const auto first = w.save();
		ticks(w, 3);
		require(first.at("psychic").at("choirs")[0] != w.save().at("psychic").at("choirs")[0], "apparent ships move");
		require(ghosts(w, 103).empty(), "outside interference arc remains unaffected");
		const auto observed = w.observe(101);
		require(observed->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") == 900,
		        "concurrent cloak receives funded work");
		const auto victim = w.observe(102);
		require(std::none_of(victim->contacts.begin(), victim->contacts.end(),
		                     [](const auto &c)
		                     {
			                     return c.id == 1;
		                     }),
		        "cloak reduces actual contact acquisition");
		const auto pending = w.save();
		World resumed;
		resumed.restore(catalog, pending);
		ticks(w, 5);
		ticks(resumed, 5);
		require(w.save() == resumed.save(), "exact continuation during moving projection");
		command(w, 102, 2, "choir", "ward-target", 2);
		command(w, 102, 2, "choir", "ward-enabled", 1);
		ticks(w, 2);
		require(ghosts(w, 102).empty(), "self ward suppresses ghost reception");
		command(w, 102, 2, "choir", "ward-enabled", 0);
		ticks(w, 2);
		require(ghosts(w, 102).size() == 2, "ward loss restores undisproved projection");
		const auto ghost = ghosts(w, 102).at(0);
		command(w, 102, 2, "radar", "target", ghost);
		ticks(w, 7);
		const auto remaining = ghosts(w, 102);
		require(std::find(remaining.begin(), remaining.end(), ghost) == remaining.end(),
		        "detailed scan dispels apparent ship");
		ticks(w, 10);
		require(ghosts(w, 102).size() == 1, "dispel receipt prevents recreation within activation");
		const auto shot_target = ghosts(w, 102).at(0);
		command(w, 102, 2, "gun", "target", shot_target);
		bool hit = false;
		for (int i = 0; i < 40; ++i)
		{
			ticks(w);
			if (ghosts(w, 102).empty())
			{
				hit = true;
				break;
			}
		}
		require(hit, "actual paid weapon trajectory dispels a ghost");
		require(!w.observe(102)->gameplay.at("psychic").at("fading").empty(), "dispel fades instead of exploding");
		const auto post_hit = w.save();
		World after_hit;
		after_hit.restore(catalog, post_hit);
		ticks(w, 4);
		ticks(after_hit, 4);
		require(w.save() == after_hit.save(), "continuation preserves projectile and dispel receipts");
		command(w, 101, 1, "choir", "apparatus", 0);
		ticks(w);
		require(w.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats")[0].at("work") == 0,
		        "disabled apparatus supplies no power");
		command(w, 101, 1, "choir", "apparatus", 1);
		ticks(w);
		const auto roster = w.observe(101)->gameplay.at("personnel");
		command(w, 101, 1, "choir", "scramble-enabled", 1);
		OperatingCommand relieve{1, "personnel.relieve", 0, "scramble"};
		relieve.generation = roster.at("revision");
		require(w.submit(101, relieve), "relieve psychic officer through Personnel");
		ticks(w);
		require(!w.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats")[1].at("enabled").get<bool>(),
		        "reassignment clears previous power intent");
		const auto save = w.save();
		auto invalid = save;
		invalid["psychic"]["receptions"][0]["observer"] = 999;
		bool refused = false;
		try
		{
			w.restore(catalog, invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && w.save() == save, "malformed ghost save refused without writes");
		const auto own = w.observe(101);
		const auto clean = network::bounded_gameplay(own->gameplay);
		(void)clean;
		require(clean.at("psychic") == own->gameplay.at("psychic"), "psychic board survives permitted projection");
		const auto wire = network::observation_json(*w.observe(102));
		const auto remote = network::read_observation(wire, catalog);
		require(remote.gameplay.at("sensors") == w.observe(102)->gameplay.at("sensors"),
		        "ghost evidence survives network allowlists");
		{
			auto limited_source = fixture();
			limited_source["entities"][0]["resources"]["stores"][0]["discharge_rate"] = 50;
			const auto limited_catalog = load(limited_source);
			World limited;
			limited.load(limited_catalog, "test.map");
			claim(limited);
			ticks(limited);
			command(limited, 101, 1, "choir", "cloak-enabled", 1);
			command(limited, 101, 1, "choir", "scramble-target", 2);
			command(limited, 101, 1, "choir", "scramble-enabled", 1);
			ticks(limited, 3);
			const auto seats = limited.observe(101)->gameplay.at("psychic").at("choirs")[0].at("seats");
			require(seats[0].at("work") > 0 && seats[0].at("work") < 900 && seats[1].at("work") < 600,
			        "concurrent powers share limited actual supply");
		}
		{
			auto friendly_source = fixture();
			friendly_source["maps"][0]["entities"][2]["x_mm"] = 80000;
			friendly_source["maps"][0]["entities"][2]["y_mm"] = 20000;
			const auto friendly_catalog = load(friendly_source);
			World friendly;
			friendly.load(friendly_catalog, "test.map");
			claim(friendly);
			ticks(friendly);
			command(friendly, 101, 1, "choir", "scramble-target", 2);
			command(friendly, 101, 1, "choir", "scramble-enabled", 1);
			ticks(friendly, 2);
			require(ghosts(friendly, 103).size() == 2, "friendly receivers within footprint receive ghosts");
			command(friendly, 101, 1, "choir", "ward-target", 3);
			command(friendly, 101, 1, "choir", "ward-enabled", 1);
			ticks(friendly, 2);
			require(ghosts(friendly, 103).empty() && ghosts(friendly, 102).size() == 2,
			        "ward protects one allied ship independently of victim");
		}

		{
			auto injured_source = fixture();
			injured_source["entities"][0]["personnel"]["officers"][1]["condition"] = "minor";
			const auto injured_catalog = load(injured_source);
			World injured;
			injured.load(injured_catalog, "test.map");
			claim(injured);
			ticks(injured);
			command(injured, 101, 1, "choir", "scramble-target", 2);
			command(injured, 101, 1, "choir", "scramble-enabled", 1);
			ticks(injured, 3);
			require(ghosts(injured, 102).empty(), "injured specialist cannot project through healthy apparatus");
		}
		{
			auto duplicate_source = fixture();
			duplicate_source["entities"][0]["personnel"]["officers"][2]["power"] = "scramble";
			const auto duplicate_catalog = load(duplicate_source);
			World duplicate;
			duplicate.load(duplicate_catalog, "test.map");
			claim(duplicate);
			ticks(duplicate);
			command(duplicate, 101, 1, "choir", "scramble-target", 2);
			command(duplicate, 101, 1, "choir", "scramble-enabled", 1);
			command(duplicate, 101, 1, "choir", "ward-target", 2);
			command(duplicate, 101, 1, "choir", "ward-enabled", 1);
			ticks(duplicate, 3);
			require(ghosts(duplicate, 102).size() == 2, "overlapping scramblers use one strongest contribution");
			command(duplicate, 101, 1, "choir", "ward-target", 3);
			ticks(duplicate, 8);
			require(ghosts(duplicate, 102).size() == 2 && ghosts(duplicate, 103).size() == 2,
			        "duplicate powers can serve independent victims after recovery");
		}
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			// ZIP timestamps vary between fixture builds; compare simulation state.
			auto trace = w.save();
			trace.erase("packs");
			std::cout << sha256(json_bytes(trace)) << '\n';
		}
		else
			std::cout << "Psychic choir system checks passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
