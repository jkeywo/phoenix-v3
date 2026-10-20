#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto source = controller_fixture();
	auto ship = source["entities"][0];
	ship["id"] = "test.combat";
	ship.erase("motion_model");
	ship["tags"] = {"ship"};
	ship["render"]["radar_icon"] = "ship";
	ship["render"]["sprite"] = {
	    {"albedo", "test-core:assets/pixel.png"},
	    {"radar_icon", "ship"},
	    {"frames",
	     Json::array({{{"name", "single"},
	                   {"rect", {0, 0, 1, 1}},
	                   {"pivot", {0, 0}},
	                   {"azimuth", 0},
	                   {"depth_scale", 0},
	                   {"depth_offset", 0},
	                   {"attachments", {{"mount", {{"x", 0}, {"y", 0}, {"layer", 1}, {"depth_offset", 0}}}}}}})}};
	ship["render"]["images"] = Json::array();
	ship["render"]["mode"] = "camera";
	ship["collision"] = {{"shape", "box"}, {"mass_grams", 1000000}, {"half_width_mm", 10000}, {"half_length_mm", 5000}};
	const Json projectile = {{"visual", "test.visual"}, {"speed_mm_s", 100000}, {"lifetime_ticks", 50},
	                         {"penetration", 1000},     {"damage", 40},         {"damage_per_m", 2}};
	ship["systems"] = {
	    {"radar", {{"kind", "radar"}, {"range_mm", 200000}, {"shows", {"ship", "asteroid"}}, {"compartment", "fore"}}},
	    {"mount",
	     {{"kind", "turret"},
	      {"arc_mdeg", 270000},
	      {"traverse_mdeg_s", 90000},
	      {"marker", "mount"},
	      {"visual", "test.visual"},
	      {"compartment", "fore"}}},
	    {"mag",
	     {{"kind", "magazine"},
	      {"capacity", 3},
	      {"reload", {{"strategy", "infinite"}, {"ticks", 10}, {"amount", 2}}},
	      {"compartment", "fore"}}},
	    {"gun",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"second",
	     {{"kind", "weapon"},
	      {"turret", "mount"},
	      {"magazine", "mag"},
	      {"arc_mdeg", 10000},
	      {"fire_ticks", 10},
	      {"projectile", projectile},
	      {"compartment", "fore"}}},
	    {"team-a", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}},
	    {"team-b", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "fore"}}}};
	ship["combat"] = {{"repair_console", "observer"},
	                  {"decks", Json::array({{{"id", "deck"},
	                                          {"weight", 1},
	                                          {"compartments", Json::array({{{"id", "fore"},
	                                                                         {"x_mm", 5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}},
	                                                                        {{"id", "aft"},
	                                                                         {"x_mm", -5000},
	                                                                         {"y_mm", 0},
	                                                                         {"half_width_mm", 5000},
	                                                                         {"half_length_mm", 5000},
	                                                                         {"hp", 100},
	                                                                         {"resistance_per_m", 10},
	                                                                         {"recovery_hp", 20}}})}}})},
	                  {"connections", Json::array({{{"from", "fore"}, {"to", "aft"}, {"travel_ticks", 10}}})},
	                  {"destruction",
	                   {{"visual", "test.visual"},
	                    {"sound", "test-core:assets/pixel.png"},
	                    {"growth_ticks", 5},
	                    {"fade_ticks", 10},
	                    {"size_mm", 10000}}}};
	Json controllers = Json::array(), drive = Json::array(), support = Json::array();
	for (auto it = ship["systems"].begin(); it != ship["systems"].end(); ++it)
	{
		const auto kind = it.value().at("kind");
		const std::string console = kind == "repair_team" ? "observer" : "drive";
		Json commands =
		    Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {it.key()}}}});
		if (kind == "weapon")
			commands.push_back({{"id", "target"}, {"action", "weapon_target"}, {"consumers", {it.key()}}});
		if (kind == "repair_team")
			commands.push_back({{"id", "assignment"}, {"action", "repair_compartment"}, {"consumers", {it.key()}}});
		controllers.push_back({{"id", it.key()}, {"console", console}, {"fallback", "hold"}, {"commands", commands}});
		(console == "drive" ? drive : support).push_back(it.key());
	}
	ship["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles",
	     Json::array(
	         {{{"id", "direct"}, {"consoles", {{"drive", {{"human", drive}}}, {"observer", {{"human", support}}}}}}})}};
	ship["stations"][0]["consoles"][0]["feeds"] = {"gun", "second", "mount", "mag", "radar"};
	ship["stations"][0]["consoles"][1]["feeds"] = {"team-a", "team-b", "radar"};
	Json visual = source["entities"][0];
	visual["id"] = "test.visual";
	visual.erase("control_layout");
	visual.erase("stations");
	visual["systems"] = Json::object();
	visual["collision"] = {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}};
	source["entities"] = Json::array({ship, visual});
	source["maps"][0]["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 2}, {"definition", "test.combat"}, {"x_mm", 60000}, {"y_mm", 0}, {"heading_mdeg", 0}},
	                 {{"id", 3}, {"definition", "test.combat"}, {"x_mm", 0}, {"y_mm", 60000}, {"heading_mdeg", 0}}});
	return source;
}
void claim(World &w)
{
	require(w.claim(101, 1, "drive") && w.claim(102, 1, "observer"), "claim combat seats");
}
OperatingCommand command(const World &w, std::uint64_t claimant, const std::string &controller,
                         const std::string &channel, std::int64_t value)
{
	const auto observation = w.observe(claimant);
	for (const auto &row : observation->gameplay.at("controllers"))
		if (row.at("id") == controller)
		{
			OperatingCommand c{1, {}, value, {}, controller, channel, row.at("generation")};
			return c;
		}
	throw std::runtime_error("missing Controller");
}
void submit(World &w, std::uint64_t claimant, const std::string &controller, const std::string &channel,
            std::int64_t value)
{
	require(w.submit(claimant, command(w, claimant, controller, channel, value)), "admitted combat command");
}
Json state(const World &w, int id = 1)
{
	const auto snapshot = w.save();
	for (const auto &body : snapshot.at("bodies"))
		if (body.at("body").at("id") == id)
			return body.at("combat");
	throw std::runtime_error("missing combat state " + std::to_string(id) + " " + w.save().at("bodies").dump());
}
std::int64_t hp(const World &w, int id = 2)
{
	std::int64_t total = 0;
	const auto s = state(w, id);
	for (const auto &c : s.at("compartments"))
		total += c.at("hp").get<std::int64_t>();
	return total;
}
void advance(World &w, int ticks)
{
	for (int n = 0; n < ticks; ++n)
		w.tick();
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		auto source = fixture();
		auto catalog = load(source);
		const auto map = source["maps"][0]["id"].get<std::string>();
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			World world;
			world.load(catalog, map);
			claim(world);
			submit(world, 101, "gun", "target", 2);
			submit(world, 102, "team-a", "assignment", 2);
			advance(world, 6);
			World resumed;
			resumed.restore(catalog, world.save());
			advance(resumed, 25);
			auto continued = resumed.save();
			// Synthetic ZIP creation timestamps are not simulation state. Restore
			// above still validates the actual pack identity within each run.
			continued.erase("packs");
			std::cout << sha256(json_bytes(continued)) << '\n';
			return 0;
		}

		{
			World w;
			w.load(catalog, map);
			claim(w);
			require(!w.submit(102, command(w, 101, "gun", "target", 2)), "Support cannot fire Tactical weapons");
			submit(w, 101, "gun", "target", 2);
			advance(w, 12);
			require(hp(w) < 200, "physical shot crosses compartments and damages target");
			const auto saved = w.save();
			World resumed;
			resumed.restore(catalog, saved);
			advance(w, 15);
			advance(resumed, 15);
			require(w.save() == resumed.save(),
			        "projectile, magazine and Controller continuation resumes deterministically");
			submit(w, 101, "second", "target", 3);
			w.tick();
			const auto s = state(w);
			require(s.at("systems").at("gun").at("target") == 3 && s.at("systems").at("second").at("target") == 3,
			        "shared turret retargets all active weapons");
			submit(w, 101, "gun", "target", 2);
			w.tick();
			require(state(w).at("systems").at("second").at("target") == 2,
			        "reasserting a weapon's original target retakes the shared turret after sibling retargeting");
			submit(w, 101, "gun", "target", 0);
			w.tick();
			require(state(w).at("systems").at("second").at("target") == 2,
			        "cancelling one weapon retains its neighbour");
			const auto observer = w.observe(102);
			require(observer->gameplay.at("combat").at("compartments").size() == 2,
			        "repair Console sees structural map");
			require(w.observe(101)->gameplay.at("combat").at("compartments").empty(),
			        "Tactical does not receive Repair map truth");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["combat"]["decks"][0]["compartments"][1]["initial_hp"] = 0;
			const auto c = load(setup);
			World one, two;
			one.load(c, map);
			two.load(c, map);
			claim(one);
			claim(two);
			submit(one, 102, "team-a", "assignment", 2);
			submit(two, 102, "team-a", "assignment", 2);
			submit(two, 102, "team-b", "assignment", 2);
			advance(one, 30);
			advance(two, 30);
			const auto a = state(one).at("compartments")[1].at("hp").get<int>(),
			           b = state(two).at("compartments")[1].at("hp").get<int>();
			require(a > 0 && b > a && b < 2 * a, "two teams repair faster with diminishing returns after travel");
			require(!state(two).at("compartments")[1].at("disabled").get<bool>(),
			        "structural recovery clears disabled latch at threshold");
			auto bad = two.save();
			bad["bodies"][0]["combat"]["systems"]["team-a"]["travel"] = -1;
			bool refused = false;
			try
			{
				two.restore(c, bad);
			}
			catch (...)
			{
				refused = true;
			}
			require(refused, "malformed team continuation rejected transactionally");
		}
		{
			auto setup = fixture();
			setup["entities"][0]["systems"]["gun"]["bearing_mdeg"] = 30000;
			setup["entities"][0]["systems"]["gun"]["y_mm"] = 5000;
			setup["entities"][0]["systems"]["gun"]["arc_mdeg"] = 2000;
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 30);
			require(state(w).at("systems").at("gun").at("shots").get<int>() >= 2,
			        "turret compensates for hosted gun bearing and sustains automatic fire");
		}
		{
			auto setup = fixture();
			setup["entities"][0]["systems"]["mag"]["reload"]["strategy"] = "none";
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 100);
			require(state(w).at("systems").at("mag").at("ammo") == 0 &&
			            state(w).at("systems").at("gun").at("shots") == 3,
			        "finite no-reload magazine exhausts exactly");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 1000},
			                                              {"initial", 1000},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["gun"]["power_store"] = "cell";
			ship["systems"]["gun"]["discrete_energy"] = 1;
			ship["systems"]["mag"]["power_store"] = "cell";
			ship["systems"]["mag"]["reload"] = {
			    {"strategy", "energy"}, {"ticks", 1000}, {"amount", 2}, {"energy_per_second", 20}};
			auto &rooms = setup["entities"][0]["combat"]["decks"][0]["compartments"];
			setup["maps"][0]["combat_duel"] = true;
			setup["maps"][0]["entities"].erase(2);
			rooms.erase(1);
			rooms[0]["x_mm"] = 0;
			rooms[0]["half_width_mm"] = 10000;
			setup["entities"][0]["combat"]["connections"] = Json::array();
			auto &p = setup["entities"][0]["systems"]["gun"]["projectile"];
			p["penetration"] = 100;
			p["damage"] = 1000;
			p["damage_per_m"] = 200;
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 12);
			require(state(w, 2).at("destroyed_tick").get<std::int64_t>() >= 0, "all compartments zero destroys ship");
			require(w.public_board(1).at("combat_result") == "Victory",
			        "duel outcome survives physical ship loss on the public Viewscreen board");
			const auto scene = w.scene();
			require(std::any_of(scene.begin(), scene.end(),
			                    [](const auto &b)
			                    {
				                    return !b.sound.empty();
			                    }),
			        "destruction emits authored billboard and sound event");
			const auto snapshot = w.save();
			World restored;
			restored.restore(c, snapshot);
			advance(w, 10);
			advance(restored, 10);
			require(w.save() == restored.save(), "destruction continuation survives restoration");
			require(w.save().at("bodies")[0].at("resources").at("network").at("stores")[0].at("energy") ==
			            snapshot.at("bodies")[0].at("resources").at("network").at("stores")[0].at("energy"),
			        "ended duel stops weapon and reload energy costs while session continues");
			require(!state(w).at("systems").at("gun").at("ready").get<bool>(),
			        "ended duel clears stale weapon readiness");
		}
		{
			auto long_source = fixture();
			long_source["entities"][0]["systems"]["gun"]["fire_ticks"] = 1000;
			auto side_source = long_source;
			side_source["maps"][0]["entities"][1]["heading_mdeg"] = 90000;
			auto long_content = load(long_source), side_content = load(side_source);
			World longitudinal, transverse;
			longitudinal.load(long_content, map);
			transverse.load(side_content, map);
			claim(longitudinal);
			claim(transverse);
			submit(longitudinal, 101, "gun", "target", 2);
			submit(transverse, 101, "gun", "target", 2);
			advance(longitudinal, 20);
			advance(transverse, 20);
			require(hp(longitudinal) == 160 && hp(transverse) == 180,
			        "long hull path deposits full payload; transverse path deposits half without double-counting "
			        "compartment boundary");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 100},
			                                              {"initial", 10},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["gun"]["power_store"] = "cell";
			ship["systems"]["gun"]["discrete_energy"] = 10;
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 40);
			require(state(w).at("systems").at("gun").at("shots") == 1,
			        "energy-funded weapon launches only its affordable shot");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
			                                              {"capacity", 100},
			                                              {"initial", 5},
			                                              {"charge_rate", 1000},
			                                              {"discharge_rate", 1000}}})}};
			ship["systems"]["mag"]["initial"] = 0;
			ship["systems"]["mag"]["power_store"] = "cell";
			ship["systems"]["mag"]["reload"] = {
			    {"strategy", "energy"}, {"ticks", 10}, {"amount", 2}, {"energy_per_second", 20}};
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			advance(w, 20);
			require(state(w).at("systems").at("mag").at("ammo") == 0, "unfunded reload does not invent ammunition");
			const auto snapshot = w.save();
			World resumed;
			resumed.restore(c, snapshot);
			advance(w, 20);
			advance(resumed, 20);
			require(w.save() == resumed.save(), "partly funded reload progress resumes without refunds");
		}
		{
			auto invalid = fixture();
			invalid["entities"][0]["combat"]["connections"].push_back(
			    {{"from", "aft"}, {"to", "fore"}, {"travel_ticks", 5}});
			rejects(
			    [&]
			    {
				    load(invalid);
			    },
			    "duplicate undirected repair edges are refused");
		}
		{
			auto invalid = fixture();
			invalid["entities"][0]["systems"]["gun"]["magazine"] = "mount";
			bool refused = false;
			try
			{
				load(invalid);
			}
			catch (...)
			{
				refused = true;
			}
			require(refused, "incompatible magazine binding rejected");
		}
		{
			auto setup = fixture();
			setup["entities"][1]["collision"] = {{"shape", "circle"}, {"radius_mm", 4000}, {"mass_grams", 0}};
			setup["maps"][0]["entities"].push_back(
			    {{"id", 4}, {"definition", "test.visual"}, {"x_mm", 30000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			advance(w, 30);
			require(hp(w) == 200, "solid cover intercepts physical projectile before the target");
		}
		{
			auto setup = fixture();
			setup["entities"][1]["collision"] = {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 0}};
			setup["entities"][1]["render"]["sprite"] = setup["entities"][0]["render"]["sprite"];
			setup["entities"][1]["render"]["images"] = Json::array();
			setup["maps"][0]["regions"] =
			    Json::array({{{"id", "remote-cover"},
			                  {"shape", "circle"},
			                  {"x_mm", 605000},
			                  {"y_mm", 5000},
			                  {"radius_mm", 2000},
			                  {"asteroids",
			                   {{"spacing_mm", 10000},
			                    {"jitter_mm", 0},
			                    {"density", .01},
			                    {"clustering_permille", 0},
			                    {"visual_distance_mm", 1000},
			                    {"decorative_fill_permille", 0},
			                    {"types", Json::array({{{"definition", "test.visual"}}})}}}}});
			const auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			submit(w, 101, "gun", "target", 2);
			w.tick();
			auto saved = w.save();
			require(saved.value("asteroid_fields", Json::array()).empty() && saved.at("projectiles").size() == 1,
			        "remote physical cover starts outside all ship streaming windows");
			// Resume an in-flight shot near the remote field, independently of ships.
			saved["projectiles"][0]["position"] = {{"x", 600000}, {"y", 5000}};
			saved["projectiles"][0]["velocity"] = {{"x", 100000}, {"y", 0}};
			w.restore(c, saved);
			w.tick();
			require(w.save().at("projectiles").empty() && !w.save().at("asteroid_fields").empty(),
			        "projectile sweep loads and collides with distant solid asteroid cover");
		}
		{
			auto wide = fixture();
			wide["maps"][0]["entities"][2]["x_mm"] = -60000;
			auto narrow = wide;
			narrow["entities"][0]["systems"]["mount"]["arc_mdeg"] = 180000;
			auto a = load(wide), b = load(narrow);
			World top, bottom;
			top.load(a, map);
			bottom.load(b, map);
			claim(top);
			claim(bottom);
			submit(top, 101, "gun", "target", 3);
			submit(bottom, 101, "gun", "target", 3);
			advance(top, 65);
			advance(bottom, 65);
			require(state(top).at("systems").at("gun").at("shots").get<int>() > 0 &&
			            state(bottom).at("systems").at("gun").at("shots") == 0,
			        "270 degree turret reaches rear quarter while 180 degree turret cannot");
			require(state(bottom).at("systems").at("mount").at("heading").get<int>() <= 90000,
			        "traversal respects physical arc boundary");
			const auto projected = network::read_observation(network::observation_json(*bottom.observe(101)), b);
			require(projected.gameplay.at("combat").at("arcs").size() == 3,
			        "combat arc and readiness projection survives remote serialization");
		}
		{
			auto setup = fixture();
			auto &ship = setup["entities"][0];
			ship["systems"]["drive"] = {{"kind", "engine"},
			                            {"max_speed_mm_s", 100000},
			                            {"acceleration_mm_s2", 1000000},
			                            {"compartment", "fore"}};
			ship["control_layout"]["controllers"].push_back(
			    {{"id", "drive"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands",
			      Json::array(
			          {{{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"drive"}}},
			           {{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"drive"}}}})}});
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("drive");
			ship["stations"][0]["consoles"][0]["feeds"].push_back("drive");
			auto c = load(setup);
			World w;
			w.load(c, map);
			claim(w);
			advance(w, 5);
			require(hp(w, 1) == 200 && hp(w, 2) == 200, "stationary ships do not accumulate collision damage");
			submit(w, 101, "drive", "speed", 100000);
			advance(w, 30);
			require(hp(w, 1) < 200 && hp(w, 2) < 200,
			        "closing ship collision damages physical compartments on both ships");
		}
		{
			auto setup = fixture();
			for (auto &controller : setup["entities"][0]["control_layout"]["controllers"])
				if (controller.at("id") == "gun")
					controller["policy"] = "combat";
			auto visible = load(setup);
			World ai;
			ai.load(visible, map);
			advance(ai, 25);
			require(state(ai).at("systems").at("gun").at("shots").get<int>() > 0,
			        "unoccupied Tactical uses ordinary admitted combat policy against visible ship contacts");
			setup["entities"][0]["systems"]["radar"]["range_mm"] = 1000;
			auto hidden = load(setup);
			World blind;
			blind.load(hidden, map);
			advance(blind, 25);
			require(state(blind).at("systems").at("gun").at("shots") == 0,
			        "combat AI cannot target ships absent from its permitted sensor feed");
		}
		std::cout << "Combat system contracts passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
