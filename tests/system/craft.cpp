#include "../support/assertions.hpp"
#include "../support/craft_fixture.hpp"
#include "../support/ftl_fixture.hpp"
#include <iostream>
#include <set>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
void ticks(World &w, int n)
{
	while (n-- > 0)
		w.tick();
}
Json missions(const World &w)
{
	return w.observe(101)->gameplay.at("away_missions");
}
OperatingCommand command(const World &w, const std::string &kind, const std::string &action, const std::string &system,
                         const std::string &text = {}, std::uint64_t claim = 101)
{
	const auto observation = *w.observe(claim);
	OperatingCommand c{observation.own.id, kind + "." + action, 0, system};
	c.console = observation.console;
	c.text = text;
	c.controller = kind == "craft"         ? "bay"
	               : claim == 102          ? "team"
	               : action == "transport" ? (text == "ship"               ? "dispatch"
	                                          : text.starts_with("craft:") ? "bay"
	                                                                       : "transport")
	                                       : "dispatch";
	for (const auto &ctrl : observation.gameplay.at("controllers"))
		if (ctrl.at("id") == c.controller)
			c.generation = ctrl.at("generation");
	const auto rows = observation.gameplay.at(kind == "craft" ? "craft" : "away_missions");
	for (const auto &row : rows)
		if (row.at("id") == system)
			c.value = row.at("revision");
	return c;
}
void assign(World &w, const std::string &transport)
{
	require(w.submit(101, command(w, "away", "open", "job")), "open mission");
	ticks(w, 1);
	require(w.submit(102, command(w, "away", "assign", "job", "repair/team:team", 102)), "assign real team");
	ticks(w, 1);
	require(w.submit(101, command(w, "away", "transport", "job", transport)), "select transport");
	ticks(w, 15);
}
int main(int argc, char **argv)
{
	try
	{
		Json trace = Json::array();
		auto input = craft_fixture();
		auto bad = input;
		bad["entities"][0]["systems"]["bay"]["slots"] = 1;
		rejects(
		    [&]
		    {
			    load(bad);
		    },
		    "prefilled craft cannot exceed bay slots");
		const auto catalog = load(input);
		{
			auto serial = armed_craft_fixture();
			serial["entities"][0]["systems"]["bay"]["slots"] = 8;
			serial["entities"][0]["systems"]["bay"]["initial"] =
			    Json::array({"test.interceptor", "test.interceptor", "test.interceptor", "test.interceptor",
			                 "test.bomber", "test.bomber", "test.shuttle", "test.shuttle"});
			serial["entities"][0]["systems"]["bay"]["transfer_ticks"] = 60;
			serial["entities"][0]["combat"]["half_height_mm"] = 45000;
			for (auto &craft : serial["entities"])
				if (craft.contains("craft"))
					craft["craft"]["cruise_altitude_mm"] = 100000;
			const auto content = load(serial);
			World running;
			running.load(content, "test.map", {{"crew", "test.away"}});
			require(running.claim(101, *running.ship_for_slot("crew"), "drive"), "serial bay claim");
			ticks(running, 4);
			const auto serial_view = *running.observe(101);
			for (const auto &craft : serial_view.gameplay.at("craft"))
				if (craft.at("role") != "shuttle")
					require(
					    running.submit(101, command(running, "craft", craft.at("role") == "bomber" ? "bomb" : "defend",
					                                craft.at("id"), craft.at("role") == "bomber" ? "1" : "")),
					    "enqueue multiple serial launches");
			std::set<EntityId> launched;
			for (int tick = 0; tick < 340; ++tick)
			{
				running.tick();
				const auto snapshot = running.save();
				for (const auto &row : snapshot.at("craft").at("entities"))
					if (row.at("state").at("phase") == "flying" && launched.insert(row.at("id").get<EntityId>()).second)
						for (const auto &body : snapshot.at("bodies"))
							if (body.at("body").at("id") == row.at("id"))
								require(body.at("vertical").at("desired") == 100000,
								        "newly enabled craft consumes its own altitude guidance on the launch tick");
			}
			World recovered;
			recovered.restore(content, running.save());
			require(recovered.save() == running.save(), "serial bay restores before queued launch completes");
			for (int i = 0; i < 100; ++i)
			{
				running.tick();
				recovered.tick();
			}
			require(recovered.save() == running.save(), "serial launch altitude and resources continue exactly");
		}
		{
			auto approach = craft_fixture();
			approach["entities"][0]["systems"]["bay"]["slots"] = 1;
			approach["entities"][0]["systems"]["bay"]["initial"] = Json::array({"test.shuttle"});
			approach["entities"][0]["systems"]["bay"]["docking_range_mm"] = 190000;
			approach["entities"][0]["systems"]["bay"]["docking_speed_mm_s"] = 6000;
			approach["entities"][0]["combat"]["half_height_mm"] = 45000;
			approach["maps"][0]["entities"] = Json::array();
			approach["maps"][0].erase("away_missions");
			approach["maps"][0].erase("conversations");
			for (auto &craft : approach["entities"])
				if (craft.contains("craft"))
				{
					craft["craft"]["cruise_altitude_mm"] = 100000;
					craft["craft"]["role"] = "interceptor";
				}
			const auto content = load(approach);
			World returning;
			returning.load(content, "test.map", {{"crew", "test.away"}});
			require(returning.claim(101, *returning.ship_for_slot("crew"), "drive"), "recovery bay claim");
			ticks(returning, 4);
			const auto craft = returning.observe(101)->gameplay.at("craft")[0].at("id").get<std::string>();
			require(returning.submit(101, command(returning, "craft", "defend", craft)), "launch recovery fixture");
			ticks(returning, 100);
			require(returning.submit(101, command(returning, "craft", "recall", craft)), "recall over large hull");
			for (int i = 0; i < 1000 && returning.observe(101)->gameplay.at("craft")[0].at("phase") != "docked"; ++i)
				returning.tick();
			const auto recovered = returning.observe(101)->gameplay.at("craft")[0];
			require(recovered.at("phase") == "docked" && recovered.at("condition") == recovered.at("maximum_condition"),
			        "recovery brakes above the carrier without striking its hull");
		}
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto ship = *w.ship_for_slot("crew");
		require(w.claim(101, ship, "drive") && w.claim(102, ship, "observer"), "claims");
		ticks(w, 4);
		const auto bay = w.observe(101)->gameplay.at("craft");
		require(bay.size() == 2, "two real dormant craft in own bay");
		require(w.inspect().size() == 2,
		        ("dormant craft have no independent scene bodies: " + std::to_string(w.inspect().size())).c_str());
		const auto id = bay[0].at("id").get<std::string>();
		require(!w.submit(101, command(w, "craft", "defend", id)), "utility shuttle has no default combat orders");
		require(network::read_observation(network::observation_json(*w.observe(101)), catalog).gameplay.at("craft") ==
		            bay,
		        "bounded flight projection roundtrip");
		assign(w, "craft:" + id);
		auto send = command(w, "away", "send", "job");
		require(w.submit(101, send), "launch shuttle transport");
		ticks(w, 1);
		require(!w.submit(101, send), "stale manifest cannot launch twice");
		World resumed;
		auto legacy = w.save();
		for (auto &row : legacy["craft"]["entities"])
		{
			row["state"]["energy_remainder"] = 7;
			row["state"]["ammunition_remainders"] = {3};
		}
		resumed.restore(catalog, legacy);
		require(resumed.save() == w.save(), "launch continuation restores exactly");
		for (int i = 0; i < 300 && missions(w)[0].at("phase") != "deployed"; ++i)
		{
			w.tick();
			resumed.tick();
		}
		require(missions(w)[0].at("phase") == "deployed", "physical shuttle delivery");
		require(w.save() == resumed.save(), "ordinary craft Controllers continue deterministically");
		require(w.inspect().size() == 3, "launched craft is an ordinary body");
		require(w.submit(101, command(w, "away", "recall", "job")), "recall party through selected shuttle");
		ticks(w, 1);
		for (int i = 0; i < 500 && missions(w)[0].at("phase") != "recovered"; ++i)
			w.tick();
		if (missions(w)[0].at("phase") != "recovered")
		{
			std::cerr << w.save()["craft"].dump() << "\n" << w.save()["away_missions"].dump() << "\n";
			const auto failed = w.save();
			for (const auto &body : failed.at("bodies"))
				if (body.at("body").at("id") == std::stoull(id))
					std::cerr << body.dump() << "\n";
		}
		require(missions(w)[0].at("phase") == "recovered", "pickup and docking recover personnel");
		require(w.inspect().size() == 2, "docked craft returns dormant");
		const auto saved = w.save();
		trace.push_back(saved);
		World recovered;
		recovered.restore(catalog, saved);
		require(recovered.save() == saved, "docked state and servicing restore");
		{
			auto damaged = saved;
			for (auto &b : damaged["bodies"])
				if (b.at("body").at("id") == std::stoull(id))
				{
					b["combat"]["compartments"][0]["hp"] = 60;
					b["resources"]["network"]["stores"][0]["energy"] = 1000;
				}
			World servicing;
			servicing.restore(catalog, damaged);
			ticks(servicing, 20);
			const auto after = servicing.save();
			for (const auto &b : after.at("bodies"))
				if (b.at("body").at("id") == std::stoull(id))
				{
					require(b.at("combat").at("compartments")[0].at("hp") == 60,
					        "docking and servicing preserve damage");
					require(b.at("resources").at("network").at("stores")[0].at("energy") > 1000,
					        "docked craft receive actual carrier energy");
				}
		}
		bad = saved;
		bad["craft"]["entities"][0]["state"]["phase"] = "teleported";
		rejects(
		    [&]
		    {
			    World invalid;
			    invalid.restore(catalog, bad);
		    },
		    "unknown craft phase rejected");
		bad = saved;
		bad.erase("craft");
		rejects(
		    [&]
		    {
			    World invalid;
			    invalid.restore(catalog, bad);
		    },
		    "craft-bearing saves require their roster");
		// Own-ship transport never supplies flight orders and requires physical proximity.
		auto manual = input;
		manual["maps"][0]["away_missions"][0]["deployment_range_mm"] = 3000;
		for (auto &c : manual["entities"][0]["control_layout"]["controllers"])
			if (c.at("id") == "engine")
				c["commands"].push_back(
				    {{"id", "speed"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", {"engine"}}});
		const auto manual_catalog = load(manual);
		World own;
		own.load(manual_catalog, "test.map", {{"crew", "test.away"}});
		require(own.claim(101, *own.ship_for_slot("crew"), "drive") &&
		            own.claim(102, *own.ship_for_slot("crew"), "observer"),
		        "manual claims");
		ticks(own, 4);
		assign(own, "ship");
		require(!own.submit(101, command(own, "away", "send", "job")), "own ship must be flown to mission");
		require(own.observe(101)->own.x == 0, "selecting ship never flies it automatically");
		OperatingCommand helm{*own.ship_for_slot("crew"), "speed_mm_s", 10000};
		helm.console = "drive";
		helm.controller = "engine";
		helm.channel = "speed";
		const auto helm_observation = *own.observe(101);
		for (const auto &c : helm_observation.gameplay.at("controllers"))
			if (c.at("id") == "engine")
				helm.generation = c.at("generation");
		require(own.submit(101, helm), "manual Helm flight");
		for (int t = 0; t < 200 && own.observe(101)->own.x < 7500; ++t)
			own.tick();
		helm.value = 0;
		require(own.submit(101, helm), "manual braking");
		ticks(own, 1);
		require(own.submit(101, command(own, "away", "send", "job")), "ship can deploy after manual approach");
		ticks(own, 3);
		require(missions(own)[0].at("phase") == "deployed", "ship deployment uses physical proximity");
		bad = input;
		bad["maps"][0]["away_missions"][0]["allow_ship_transport"] = false;
		const auto restricted = load(bad);
		World atmospheric;
		atmospheric.load(restricted, "test.map", {{"crew", "test.away"}});
		require(atmospheric.claim(101, *atmospheric.ship_for_slot("crew"), "drive"), "restricted claims");
		ticks(atmospheric, 4);
		require(atmospheric.submit(101, command(atmospheric, "away", "open", "job")), "restricted mission open");
		ticks(atmospheric, 1);
		require(!atmospheric.submit(101, command(atmospheric, "away", "transport", "job", "ship")),
		        "mission may explicitly exclude ship transport");
		{
			auto release = input;
			for (auto &tree : release["trees"])
				if (tree.at("id") == "test.away-tree")
				{
					tree["transport_capabilities"] = {"tools"};
					tree["states"][0]["transitions"][0]["conditions"].push_back(
					    {{"left", {{"observation", "transport.operational.tools"}}}, {"op", "eq"}, {"right", true}});
					tree["states"][1]["entry"].push_back(
					    {{"action", "release_transport"}, {"arguments", {"independent"}}});
				}
			const auto release_catalog = load(release);
			World ferry;
			ferry.load(release_catalog, "test.map", {{"crew", "test.away"}});
			require(ferry.claim(101, *ferry.ship_for_slot("crew"), "drive") &&
			            ferry.claim(102, *ferry.ship_for_slot("crew"), "observer"),
			        "ferry claims");
			ticks(ferry, 4);
			const auto fleet = ferry.observe(101)->gameplay.at("craft");
			const auto first = fleet[0].at("id").get<std::string>(), second = fleet[1].at("id").get<std::string>();
			assign(ferry, "craft:" + first);
			require(ferry.submit(101, command(ferry, "away", "send", "job")), "ferry launch");
			ticks(ferry, 30);
			require(ferry.save().at("away_missions")[0].at("transport_released") == true,
			        "tree checks working tools then releases independent transport");
			require(ferry.submit(101, command(ferry, "away", "transport", "job", "craft:" + second)),
			        "select a different compatible pickup shuttle");
			ticks(ferry, 1);
			require(ferry.submit(101, command(ferry, "away", "recall", "job")), "dispatch replacement pickup");
			for (int t = 0; t < 500 && missions(ferry)[0].at("phase") != "recovered"; ++t)
				ferry.tick();
			require(missions(ferry)[0].at("phase") == "recovered",
			        "replacement shuttle physically recovers independent party");
		}
		{
			const auto armed = load(armed_craft_fixture());
			World flight;
			flight.load(armed, "test.map", {{"crew", "test.away"}});
			require(flight.claim(101, *flight.ship_for_slot("crew"), "drive"), "flight operations claim");
			ticks(flight, 8);
			std::string bomber, interceptor;
			const auto flight_board = *flight.observe(101);
			for (const auto &c : flight_board.gameplay.at("craft"))
			{
				if (c.at("role") == "bomber")
					bomber = c.at("id");
				if (c.at("role") == "interceptor")
					interceptor = c.at("id");
			}
			require(!bomber.empty() && !interceptor.empty(), "authored combat roles");
			const auto stocked = flight.save();
			std::int64_t ammunition = 0;
			for (const auto &b : stocked.at("bodies"))
			{
				const auto body_id = b.at("body").at("id").get<EntityId>();
				if (body_id == *flight.ship_for_slot("crew"))
					ammunition += b.at("combat").at("systems").at("supply").at("ammo").get<std::int64_t>();
				if (body_id == std::stoull(bomber) || body_id == std::stoull(interceptor))
					ammunition += b.at("combat").at("systems").at("magazine").at("ammo").get<std::int64_t>();
			}
			require(ammunition == 24, "docked magazine transfer conserves finite carrier and craft ammunition");
			require(flight.submit(101, command(flight, "craft", "defend", interceptor)), "interceptor defence order");
			ticks(flight, 5);
			require(flight.submit(101, command(flight, "craft", "bomb", bomber, "1")),
			        "bomber selected hostile target");
			ticks(flight, 5);
			World continuation;
			continuation.restore(armed, flight.save());
			bool returned = false, fired = false, intercepting = false;
			for (int t = 0; t < 700; ++t)
			{
				flight.tick();
				continuation.tick();
				const auto state = flight.save();
				for (const auto &b : state.at("bodies"))
				{
					if (b.at("body").at("id") == std::stoull(bomber))
						fired |= b.at("combat").at("systems").at("weapon").at("shots").get<int>() > 0;
					if (b.at("body").at("id") == std::stoull(interceptor))
						intercepting |=
						    b.at("combat").at("systems").at("weapon").at("intercept").get<std::uint64_t>() > 0;
				}
				for (const auto &c : state.at("craft").at("entities"))
					if (c.at("id") == std::stoull(bomber))
						returned = c.at("state").at("phase") == "docked";
				if (returned)
					break;
			}
			if (!fired || !returned)
				std::cerr << "bomber fired=" << fired << " returned=" << returned << " "
				          << flight.save().at("craft").dump() << '\n';
			require(fired && returned, "bomber fires finite salvo then returns without relaunch");
			require(intercepting, "interceptor prioritises actual nearby hostile missiles");
			require(flight.save() == continuation.save(), "combat sortie and servicing continuation parity");
			trace.push_back(flight.save());
			require(flight.submit(101, command(flight, "craft", "escort", interceptor,
			                                   std::to_string(*flight.ship_for_slot("crew")))),
			        "retask interceptor to selected escort");
			ticks(flight, 1);
			{
				auto separated = flight.save();
				const auto home = *flight.ship_for_slot("crew");
				for (auto &body : separated["bodies"])
					if (body.at("body").at("id") == home)
						body["body"]["position"]["x"] = 5000000;
				World unlinked;
				unlinked.restore(armed, separated);
				ticks(unlinked, 20);
				const auto guarded = unlinked.save();
				for (const auto &craft : guarded.at("craft").at("entities"))
					if (craft.at("id") == std::stoull(interceptor))
						require(craft.at("state").at("order") == "escort" &&
						            craft.at("state").at("rendezvous").at("x") != 5000000,
						        "escort retains accepted work and last reported anchor during observation loss");
			}
			require(flight.submit(101, command(flight, "craft", "recall", interceptor)), "combat recall");
			ticks(flight, 1);
		}
		{
			auto spatial = craft_fixture();
			spatial["entities"].back()["control_layout"]["profiles"].push_back(
			    {{"id", "manual"}, {"consoles", {{"drive", {{"human", {"flight"}}}}}}});
			spatial["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.shuttle"}, {"x_mm", 500000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			const auto spatial_catalog = load(spatial);
			World craft;
			craft.load(spatial_catalog, "test.map", {{"crew", "test.away"}});
			require(craft.prepare_console_profile({2, "drive"}, "manual") && craft.claim(201, 2, "drive"),
			        "ordinary independently spawned craft controls");
			OperatingCommand climb{2, "altitude_mm", 20000};
			climb.console = "drive";
			climb.controller = "flight";
			climb.channel = "height";
			const auto observed = *craft.observe(201);
			for (const auto &c : observed.gameplay.at("controllers"))
				if (c.at("id") == "flight")
					climb.generation = c.at("generation");
			require(craft.submit(201, climb), "owned vertical flight channel");
			ticks(craft, 20);
			require(craft.observe(201)->own.altitude > 0, "ordinary craft have physical vertical movement");
			const auto state = craft.save();
			World restored;
			restored.restore(spatial_catalog, state);
			require(restored.save() == state, "independent craft vertical continuation");
			trace.push_back(state);
		}
		{
			auto jumping = craft_fixture();
			fit_ftl(jumping, "helm", "drive");
			jumping["entities"].back()["craft"]["vertical_speed_mm_s"] = 1000;
			jumping["entities"].back()["craft"]["vertical_acceleration_mm_s2"] = 1000;
			for (auto &store : jumping["entities"][0]["resources"]["stores"])
				if (store.at("id") == "jump-cell")
					store["capacity"] = 20;
			const auto jump_catalog = load(jumping);
			World jump;
			jump.load(jump_catalog, "test.map", {{"crew", "test.away"}});
			const auto home = *jump.ship_for_slot("crew");
			require(jump.claim(101, home, "drive") && jump.claim(102, home, "observer"), "departure claims");
			ticks(jump, 4);
			const auto shuttle = jump.observe(101)->gameplay.at("craft")[0].at("id").get<std::string>();
			assign(jump, "craft:" + shuttle);
			require(jump.submit(101, command(jump, "away", "send", "job")), "launch before departure");
			ticks(jump, 8);
			OperatingCommand charge{home, "system_enabled", 1};
			charge.console = "navigator";
			charge.controller = "jump";
			charge.channel = "enabled";
			const auto navigation_observation = *jump.observe(101);
			Observation navigator;
			for (const auto &extra : navigation_observation.additional_consoles)
				if (extra.console == "navigator")
					navigator = extra;
			for (const auto &c : navigator.gameplay.at("controllers"))
				if (c.at("id") == "jump")
					charge.generation = c.at("generation");
			require(jump.submit(101, charge), "begin FTL charging");
			ticks(jump, 2);
			if (missions(jump)[0].at("phase") != "inbound" && missions(jump)[0].at("phase") != "recovered")
				std::cerr << jump.save()["craft"].dump() << "\n" << missions(jump).dump() << "\n";
			require(missions(jump)[0].at("phase") == "inbound" || missions(jump)[0].at("phase") == "recovered",
			        "charging recalls shuttle party");
			require(!jump.submit(101, command(jump, "away", "send", "job")),
			        "departure preparation prevents fresh shuttle launch");
			ticks(jump, 8);
			OperatingCommand depart{home, "ftl.depart", 0, "jump"};
			depart.console = "navigator";
			depart.controller = "jump";
			depart.generation = charge.generation;
			require(jump.submit(101, depart), "carrier may depart before craft finish returning");
			ticks(jump, 2);
			const auto abandoned = jump.save();
			bool flying = false, docked = false;
			for (const auto &row : abandoned.at("craft").at("entities"))
				if (row.at("state").at("home") == home)
				{
					flying |= row.at("state").at("phase") == "flying";
					docked |= row.at("state").at("phase") == "docked";
				}
			require(flying && docked, "deployed craft remain in world while docked craft depart safely");
			trace.push_back(abandoned);
		}
		{
			auto authored = armed_craft_fixture();
			for (auto &tree : authored["trees"])
				if (tree.at("id") == "test.bomber-flight.execute")
					tree["states"][0]["transitions"].insert(
					    tree["states"][0]["transitions"].begin(),
					    Json{{"target", "recall"},
					         {"conditions",
					          Json::array(
					              {{{"left", {{"observation", "craft.order"}}}, {"op", "eq"}, {"right", "bomb"}}})}});
			World custom;
			custom.load(load(authored), "test.map", {{"crew", "test.away"}});
			require(custom.claim(101, *custom.ship_for_slot("crew"), "drive"), "custom sortie policy claim");
			ticks(custom, 10);
			std::string bomber;
			const auto initial_craft = custom.observe(101)->gameplay.at("craft");
			for (const auto &c : initial_craft)
				if (c.at("role") == "bomber")
					bomber = c.at("id");
			require(custom.submit(101, command(custom, "craft", "bomb", bomber, "1")), "custom bomber order");
			ticks(custom, 120);
			const auto custom_state = custom.save();
			for (const auto &body : custom_state.at("bodies"))
				if (body.at("body").at("id") == std::stoull(bomber))
					require(body.at("combat").at("systems").at("weapon").at("shots") == 0,
					        "authored Controller action changes sortie sequencing without a runtime policy branch");
			const auto recovered_craft = custom.observe(101)->gameplay.at("craft");
			for (const auto &c : recovered_craft)
				if (c.at("id") == bomber)
					require(c.at("phase") == "docked", "authored recall returns a loaded bomber");
		}
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			for (auto &state : trace)
				state.erase("packs");
			std::cout << trace.dump() << '\n';
		}
		else
			std::cout << "PASS carried craft and physical away transport\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
