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
			auto &e = duplicate_source["entities"][0];
			e["systems"]["choir2"] = e["systems"]["choir"];
			e["systems"]["choir2"]["seats"] = Json::array({e["systems"]["choir"]["seats"][2]});
			e["systems"]["choir"]["seats"].erase(2);
			for (auto &d : e["personnel"]["duties"])
				if (d["id"] == "ward")
					d["choir"] = "choir2";
			for (auto &c : e["control_layout"]["controllers"])
				if (c["id"] == "choir")
					c["commands"][0]["consumers"].push_back("choir2");
			e["stations"][0]["consoles"][0]["feeds"].push_back("choir2");
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
			const auto overlapping = duplicate.save();
			for (const auto &g : overlapping.at("psychic").at("receptions"))
				if (g.at("source") == 1 && g.at("observer") == 2)
					require(g.at("choir") == "choir", "equal-strength choirs use stable ordering");
			command(duplicate, 101, 1, "choir", "ward-target", 3);
			ticks(duplicate, 8);
			require(ghosts(duplicate, 102).size() == 2 && ghosts(duplicate, 103).size() == 2,
			        "duplicate powers can serve independent victims after recovery");
		}
		// Outer-band apparent contacts share detection rules without granting scan evidence.
		{
			auto authored = fixture();
			auto projector = authored["entities"][0];
			projector["id"] = "test.projector";
			authored["entities"][0]["systems"]["radar"]["range_mm"] = 1000;
			authored["entities"][0]["systems"]["radar"]["long_range_mm"] = 400000;
			authored["entities"].push_back(projector);
			authored["maps"][0]["entities"][0]["definition"] = "test.projector";
			const auto local = load(authored);
			World outer;
			outer.load(local, "test.map");
			claim(outer);
			ticks(outer);
			command(outer, 101, 1, "choir", "scramble-target", 2);
			command(outer, 101, 1, "choir", "scramble-enabled", 1);
			command(outer, 101, 1, "choir", "cloak-enabled", 1);
			ticks(outer, 3);
			const auto observed = outer.observe(102);
			std::vector<EntityId> apparent;
			for (const auto &c : observed->bearing_contacts)
			{
				require(c.id != 1, "outer band respects psychic concealment");
				if (c.id > 3 && c.id <= 1000000000ULL)
					apparent.push_back(c.id);
			}
			require(apparent.size() == 2 && ghosts(outer, 102).empty(),
			        "outer apparent contacts expose bearings without positions");
			command(outer, 102, 2, "radar", "target", apparent.front());
			ticks(outer, 8);
			const auto scanned = outer.observe(102);
			for (const auto &c : scanned->gameplay.at("sensors").at("contacts"))
				if (std::find(apparent.begin(), apparent.end(), c.at("id").get<EntityId>()) != apparent.end())
					require(c.at("findings").empty(), "outer apparent contacts grant no detailed scan findings");
			command(outer, 101, 1, "choir", "cloak-enabled", 0);
			ticks(outer, 2);
			const auto visible = outer.observe(102);
			require(std::any_of(visible->bearing_contacts.begin(), visible->bearing_contacts.end(),
			                    [](const auto &c)
			                    {
				                    return c.id == 1;
			                    }),
			        "uncloaked real contacts return as outer-band bearings");
		}
		// Sensor admission and capability acquisition must match real contacts.
		for (bool hide_ships : {false, true})
		{
			auto authored = fixture();
			auto &radar = authored["entities"][0]["systems"]["radar"];
			radar["capabilities"] = Json::object();
			if (hide_ships)
				radar["shows"] = {"asteroid"};
			// The projector keeps a ship feed; the receiving hull filters ships.
			if (hide_ships)
			{
				auto enabled = authored;
				enabled["entities"][0]["systems"]["radar"]["shows"] = {"ship"};
				authored["entities"].push_back(enabled["entities"][0]);
				authored["entities"].back()["id"] = "test.projector";
				authored["maps"][0]["entities"][0]["definition"] = "test.projector";
				const auto filtered = load(authored);
				World view;
				view.load(filtered, "test.map");
				claim(view);
				ticks(view);
				command(view, 101, 1, "choir", "scramble-target", 2);
				command(view, 101, 1, "choir", "scramble-enabled", 1);
				ticks(view, 3);
				require(ghosts(view, 102).empty(), "ship filter excludes apparent contacts");
				require(view.observe(102)->gameplay.at("sensors").at("contacts").empty(),
				        "ship filter excludes apparent evidence");
			}
			else
			{
				const auto local = load(authored);
				World sensors;
				sensors.load(local, "test.map");
				claim(sensors);
				ticks(sensors);

				command(sensors, 101, 1, "choir", "scramble-target", 2);
				command(sensors, 101, 1, "choir", "scramble-enabled", 1);
				ticks(sensors, 3);
				require(ghosts(sensors, 102).size() == 2, "unidentified apparent bodies remain targetable");
				const auto observed = sensors.observe(102);
				for (const auto &c : observed->gameplay.at("sensors").at("contacts"))
					require(!c.at("findings").contains("basic_identity"), "identity requires the sensor capability");
			}
		}
		{
			auto authored = fixture();
			auto &e = authored["entities"][0];
			e["systems"]["radar2"] = e["systems"]["radar"];
			e["publications"] = Json::array({{{"id", "scan"},
			                                  {"source_console", "drive"},
			                                  {"schema", "phoenix.sensor.findings"},
			                                  {"subject", 4},
			                                  {"fields", {"payload"}},
			                                  {"live", false}}});
			e["stations"][0]["consoles"][0]["feeds"].push_back("radar2");
			for (auto &c : e["control_layout"]["controllers"])
				if (c.at("id") == "radar")
					c["commands"].push_back(
					    {{"id", "target2"}, {"action", "sensor_target"}, {"consumers", {"radar2"}}});
			const auto local = load(authored);
			World scans;
			scans.load(local, "test.map");
			claim(scans);
			ticks(scans);
			command(scans, 101, 1, "choir", "scramble-target", 2);
			command(scans, 101, 1, "choir", "scramble-enabled", 1);
			ticks(scans, 2);
			const auto target = ghosts(scans, 102).at(0);
			command(scans, 102, 2, "radar", "target", target);
			ticks(scans, 3);
			command(scans, 102, 2, "radar", "target", 0);
			command(scans, 102, 2, "radar", "target2", target);
			ticks(scans, 3);
			require(ghosts(scans, 102).size() == 2, "switching scanners does not combine their exposure");
			const auto middle = scans.save();
			World continued;
			continued.restore(local, middle);
			ticks(scans, 3);
			ticks(continued, 3);
			require(ghosts(scans, 102).size() == 1, "second scanner completes its own detailed scan");
			require(scans.save() == continued.save(), "per-scanner exposure survives exact continuation");
			const auto evidence = scans.observe(102)->gameplay.at("sensors").at("contacts");
			bool finding = false;
			for (const auto &c : evidence)
				if (c.at("id") == target)
					finding = c.at("findings").contains("finding:dispelled");
			require(finding, "dispel finding uses retained sensor evidence");
			require(scans.submit(102, OperatingCommand{2, "publish.scan", 0, {}}),
			        "publish apparent finding through ordinary knowledge");
			ticks(scans);
			const auto shared = scans.observe(104)->gameplay.at("sensors");
			require(shared.at("scanners").empty() && !shared.at("contacts").empty(),
			        "unfed Console receives only deliberately shared finding");
			bool shared_finding = false;
			for (const auto &c : shared.at("contacts"))
				if (c.at("id") == target)
					shared_finding = c.at("findings").contains("finding:dispelled");
			require(shared_finding, "apparent finding uses the existing publication and evidence path");
		}
		{
			auto authored = fixture();
			auto &e = authored["entities"][0];
			Json orders = Json::array();
			for (const auto &c : e["control_layout"]["controllers"])
				if (c.at("id") == "choir")
					for (const auto &ch : c.at("commands"))
						if (ch.at("id") != "apparatus")
							orders.push_back(ch);
			e["control_layout"]["controllers"].push_back(
			    {{"id", "orders"}, {"console", "drive"}, {"fallback", "hold"}, {"commands", orders}});
			e["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("orders");
			e["personnel"]["duties"].push_back({{"id", "planner"},
			                                    {"label", "Planner"},
			                                    {"department", "signals"},
			                                    {"compartment", "fore"},
			                                    {"controller", "orders"},
			                                    {"console", "drive"}});
			e["systems"]["choir"]["planning_controller"] = "orders";
			const auto local = load(authored);
			World reassigned;
			reassigned.load(local, "test.map");
			claim(reassigned);
			ticks(reassigned);
			command(reassigned, 101, 1, "choir", "scramble-enabled", 1);
			command(reassigned, 101, 1, "orders", "scramble-enabled", 1);
			command(reassigned, 101, 1, "orders", "cloak-enabled", 1);
			OperatingCommand relieve{1, "personnel.relieve", 0, "scramble"};
			relieve.generation = reassigned.observe(101)->gameplay.at("personnel").at("revision");
			require(reassigned.submit(101, relieve), "relieve officer with queued planning and execution writes");
			ticks(reassigned);
			const auto observation = reassigned.observe(101);
			for (const auto &c : observation->gameplay.at("controllers"))
				if (c.at("id") == "orders" || c.at("id") == "choir")
				{
					require(c.at("commands").at("scramble-enabled").at("value") == 0,
					        "reset clears both command owners");
					if (c.at("id") == "orders")
						require(c.at("commands").at("cloak-enabled").at("value") == 1,
						        "reset preserves unrelated seat writes");
				}
			const auto current = reassigned.save();
			auto old = current;
			old["psychic"].erase("version");
			bool rejected = false;
			try
			{
				reassigned.restore(local, old);
			}
			catch (const std::exception &ex)
			{
				rejected = std::string(ex.what()).find("start a new session") != std::string::npos;
			}
			require(rejected && reassigned.save() == current, "old psychic saves fail clearly without writes");
		}
		for (bool boundary : {false, true})
		{
			auto authored = fixture();
			authored["entities"][0]["psychic_powers"]["scramble"]["speed_mm_s"] = 1001;
			authored["entities"][0]["psychic_powers"]["scramble"]["turn_mdeg_s"] = 1;
			if (boundary)
			{
				authored["maps"][0]["entities"][0]["x_mm"] = 999900000;
				authored["maps"][0]["entities"][1]["x_mm"] = 999999990;
				authored["maps"][0]["entities"][2]["x_mm"] = -999900000;
			}
			const auto local = load(authored);
			World motion;
			motion.load(local, "test.map");
			claim(motion);
			ticks(motion);
			command(motion, 101, 1, "choir", "scramble-target", 2);
			command(motion, 101, 1, "choir", "scramble-enabled", 1);
			ticks(motion, 2);
			auto saved = motion.save();
			Json position;
			for (auto &body : saved["bodies"])
				if (body["body"]["id"] == 2)
				{
					position = body["body"]["position"];
					body["body"]["velocity"] = {{"x", 1001}, {"y", 0}};
					body["body"]["remainder"] = {{"x", 19}, {"y", -19}};
				}
			Json shapes;
			for (auto &choir : saved["psychic"]["choirs"])
				if (choir["ship"] == 1)
				{
					for (auto &shape : choir["state"]["seats"][1]["shapes"])
					{
						shape["x"] = position["x"];
						shape["y"] = position["y"];
						shape["vx"] = 1001;
						shape["vy"] = 0;
						shape["mx"] = 19;
						shape["my"] = -19;
						shape["heading"] = 0;
					}
					shapes = choir["state"]["seats"][1]["shapes"];
				}
			for (auto &g : saved["psychic"]["receptions"])
				if (g["source"] == 1)
					g["last"] = shapes[g["member"].get<std::size_t>()];
			motion.restore(local, saved);
			for (int n = 0; n < 24; ++n)
			{
				ticks(motion);
				const auto next = motion.save();
				Json actual;
				for (const auto &body : next.at("bodies"))
					if (body.at("body").at("id") == 2)
						actual = body.at("body");
				for (const auto &choir : next.at("psychic").at("choirs"))
					if (choir.at("ship") == 1)
						for (const auto &shape : choir.at("state").at("seats")[1].at("shapes"))
							require(shape.at("x") == actual.at("position").at("x") &&
							            shape.at("y") == actual.at("position").at("y") &&
							            shape.at("mx") == actual.at("remainder").at("x") &&
							            shape.at("my") == actual.at("remainder").at("y"),
							        "apparent and ordinary motion share fractional integration and world bounds");
			}
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
