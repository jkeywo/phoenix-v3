#include "../support/assertions.hpp"
#include "../support/stock_snapshot.hpp"
#include "../support/craft_fixture.hpp"
#include <iostream>
#include <set>
import phoenix.simulation;
import phoenix.console_controls;
#include "../../src/network/projection.hpp"
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &w, int n)
{
	while (n--)
		w.tick();
}
Json fixture()
{
	auto s = craft_fixture();
	s["maps"][0]["factions"] = Json::array({{{"id", "crew"}, {"label", "Crew"}}});
	for (auto &e : s["entities"])
	{
		e["faction"] = "crew";
		const bool craft = e.contains("craft");
		e["logistics"] = {{"controller", craft ? "flight" : "team"},
		                  {"units_per_second", 100},
		                  {"batch_size", 1},
		                  {"materials", Json::array({{{"id", "parts"}}})},
		                  {"stores", Json::array({{{"id", "hold"},
		                                           {"compartment", "bridge"},
		                                           {"capacity", 7},
		                                           {"accepts", {"parts"}},
		                                           {"main", true},
		                                           {"initial", {{"parts", craft ? 0 : 4}}}}})}};
		e["docking"] = {{"ports", Json::array({{{"id", "port"},
		                                        {"controller", craft ? "flight" : "engine"},
		                                        {"compartment", "bridge"},
		                                        {"compatibility", "standard"},
		                                        {"x_mm", craft ? 1000 : -5000},
		                                        {"heading_mdeg", craft ? 0 : 180000},
		                                        {"capture_range_mm", 1000},
		                                        {"mating_ticks", 3},
		                                        {"separation_ticks", 3},
		                                        {"auto_accept_allies", true},
		                                        {"supply", true}}})}};
	}
	s["trees"].push_back(Json::parse(
	    R"({"id":"test.receipt","context":"scenario","initial":"wait","states":[{"id":"wait","entry":[{"action":"add_objective","arguments":["receipt",{"observation":"self.entity"},"receipt"]}]}]})"));
	s["maps"][0]["scenario"] = Json::parse(
	    R"({"tree":"test.receipt","bindings":{"actor":{"slot":"crew"}},"fact_bindings":[{"id":"quantity","kind":"inventory","subject":{"entity":1},"field":"hold","operation":"parts"}],"objectives":[{"id":"receipt","type":"condition","title_key":"Physical receipt","actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},"complete_when":{"fact":"quantity","op":"ge","value":7}}]})");
	return s;
}
OperatingCommand command(const World &w, std::uint64_t claim, const std::string &verb, const std::string &system,
                         const std::string &text, const std::string &controller)
{
	const auto o = *w.observe(claim);
	OperatingCommand c{o.own.id, "logistics." + verb, o.gameplay.at("logistics").at("revision"), system};
	c.console = o.console;
	c.controller = controller;
	c.text = text;
	for (const auto &ctrl : o.gameplay.at("controllers"))
		if (ctrl.at("id") == controller)
			c.generation = ctrl.at("generation");
	return c;
}
} // namespace
#include "../support/supply_cases.hpp"
int main(int argc, char **argv)
{
	try
	{
		supply_cases();
		// One shared fit receives a consignment only in the mission/slot selecting it.
		{
			auto source = fixture();
			source["maps"][0]["starting_stocks"] = Json::array(
			    {{{"ship_slot", "crew"}, {"definition", "test.away"}, {"stores", {{"hold", {{"parts", 7}}}}}}});
			auto alternate_fit = source["entities"][0];
			alternate_fit["id"] = "test.alternate";
			source["entities"].push_back(alternate_fit);
			source["maps"][0]["ship_slots"][0]["allowed_ships"].push_back("test.alternate");
			auto ordinary = source["maps"][0];
			ordinary["id"] = "test.ordinary";
			ordinary.erase("starting_stocks");
			source["maps"].push_back(ordinary);
			const auto catalog = load(source);
			World assigned, unassigned, alternate;
			assigned.load(catalog, "test.map", {{"crew", "test.away"}});
			unassigned.load(catalog, "test.ordinary", {{"crew", "test.away"}});
			alternate.load(catalog, "test.map", {{"crew", "test.alternate"}});
			auto quantity = [](const World &world, EntityId id)
			{
				const auto saved = world.save();
				for (const auto &body : saved.at("bodies"))
					if (body.at("body").at("id") == id)
						return saved_material_stock(body, 0).at("quantities").value("parts", 0);
				throw std::runtime_error("Missing stock owner");
			};
			require(quantity(assigned, 2) == 7 && quantity(assigned, 1) == 4 && quantity(unassigned, 2) == 4 &&
			            quantity(alternate, 2) == 4,
			        "Starting stocks are mission/slot scoped; NPCs and shared fits retain their own stock");
			World resumed;
			resumed.restore(catalog, assigned.save());
			require(resumed.save() == assigned.save(), "Starting-stock continuation is ordinary physical inventory");
			for (const auto &bad : {Json{{"missing", {{"parts", 1}}}}, Json{{"hold", {{"parts", 8}}}},
			                        Json{{"hold", {{"parts", true}}}}, Json{{"hold", {{"missing", 1}}}}})
			{
				auto invalid = source;
				invalid["maps"][0]["starting_stocks"][0]["stores"] = bad;
				bool rejected = false;
				try
				{
					(void)load(invalid);
				}
				catch (const std::exception &)
				{
					rejected = true;
				}
				require(rejected, "Reject incompatible or over-capacity starting stocks during production loading");
			}
		}

		{
			auto delegated = fixture();
			auto &ship = delegated["entities"][0];
			ship["logistics"]["cargo_authorisation_controller"] = "engine";
			for (auto &controller : ship["control_layout"]["controllers"])
				if (controller["id"] == "bay")
					controller["console"] = "observer";
			for (auto &duty : ship["personnel"]["duties"])
				if (duty.value("controller", std::string{}) == "bay")
				{
					duty["console"] = "observer";
					duty["department"] = "engineering";
				}
			for (auto &profile : ship["control_layout"]["profiles"])
				for (const auto *allocation : {"human", "ai"})
				{
					if (!profile["consoles"]["drive"].contains(allocation))
						continue;
					auto &from = profile["consoles"]["drive"].at(allocation);
					if (!from.is_array())
						continue;
					bool found = false;
					for (auto i = from.begin(); i != from.end();)
						if (*i == "bay")
						{
							i = from.erase(i);
							found = true;
						}
						else
							++i;
					if (found)
						profile["consoles"]["observer"][allocation].push_back("bay");
				}
			ship["stations"][1]["consoles"][0]["feeds"].push_back("bay");
			World permission;
			permission.load(load(delegated), "test.map", {{"crew", "test.away"}});
			require(permission.claim(101, 2, "drive") && permission.claim(102, 2, "observer"),
			        "Claim separated cargo departments");
			ticks(permission, 5);
			const auto craft =
			    permission.observe(101)->gameplay.at("logistics").at("cargo_craft")[0].at("id").get<EntityId>();
			const auto authorise = command(permission, 101, "craft_allow", std::to_string(craft), "1", "engine");
			auto remote = *permission.observe(101);
			remote.gameplay = network::bounded_gameplay(remote.gameplay);
			require(remote.gameplay.at("logistics").at("cargo_authorisation_controller") == "engine",
			        "Remote cargo projection preserves the consent Controller");
			require(eligible_console_command(remote, Json::object(), Json::object(), authorise).has_value(),
			        "Scoped semantic controls expose the delegated permission action");
			require(!permission.submit(102, command(permission, 102, "craft_allow", std::to_string(craft), "1", "bay")),
			        "Owning the utility bay does not substitute for delegated Tactical consent");
			require(permission.submit(101, authorise), "The delegated Controller may grant cargo consent");
			ticks(permission, 1);
			require(permission.observe(102)->gameplay.at("logistics").at("cargo").empty(),
			        "Standing consent does not launch a shuttle");
			const auto text = std::to_string(craft) + "/hold/1/hold/parts/3";
			require(!permission.submit(101, command(permission, 101, "deliver", "hold", text, "engine")),
			        "Cargo permission does not delegate Engineering's delivery authority");
			require(permission.submit(102, command(permission, 102, "deliver", "hold", text, "team")),
			        "Engineering orders the authorised utility craft through the existing cargo path");
		}
		auto source = fixture();
		source["maps"][0]["starting_stocks"] =
		    Json::array({{{"ship_slot", "crew"}, {"definition", "test.away"}, {"stores", {{"hold", {{"parts", 4}}}}}}});
		const auto catalog = load(source);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		require(w.claim(101, 2, "drive") && w.claim(102, 2, "observer"), "Cargo crew claims");
		ticks(w, 5);
		const auto craft = w.observe(102)->gameplay.at("logistics").at("cargo_craft")[0].at("id").get<EntityId>();
		const auto text = std::to_string(craft) + "/hold/1/hold/parts/3";
		require(!w.submit(102, command(w, 102, "deliver", "hold", text, "team")),
		        "Unapproved craft cannot be taken for cargo");
		require(w.submit(101, command(w, 101, "craft_allow", std::to_string(craft), "1", "bay")),
		        "Tactical grants standing supply permission");
		ticks(w, 1);
		require(w.submit(102, command(w, 102, "deliver", "hold", text, "team")),
		        "Engineering requests real cargo delivery");
		ticks(w, 4);
		require(!w.observe(102)->gameplay.at("logistics").at("cargo").empty(), "Cargo commitment is visible");
		require(w.save().at("scenario").at("runtime").at("objectives")[0].at("status") == 1,
		        "Dispatch and cargo reservations do not count as delivery");
		{
			World recalled;
			recalled.restore(catalog, w.save());
			ticks(recalled, 12);
			require(recalled.submit(102, command(recalled, 102, "cargo_cancel", "1", "", "team")),
			        "Cancel an accepted cargo sortie");
			ticks(recalled, 1);
			for (int i = 0; i < 1500 && recalled.save().at("logistics").at("cargo")[0].at("phase") != "complete"; ++i)
				recalled.tick();
			require(recalled.save().at("logistics").at("cargo")[0].at("phase") == "complete",
			        "Cancelled cargo returns through physical craft recovery");
			require(recalled.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 1,
			        "Cargo cancellation does not teleport supplies back to the source");
		}
		{
			World interrupted;
			interrupted.restore(catalog, w.save());
			for (int i = 0; i < 1000 && interrupted.save().at("logistics").at("cargo")[0].at("phase") != "outbound";
			     ++i)
				interrupted.tick();
			require(interrupted.save().at("logistics").at("cargo")[0].at("phase") == "outbound",
			        "Exercise a launched cargo commitment");
			require(!interrupted.submit(102, command(interrupted, 102, "deliver", "hold", text, "team")),
			        "A committed shuttle cannot accept a second cargo job");
			auto radio = [](World &world, int enabled)
			{
				const auto o = *world.observe(101);
				OperatingCommand c{o.own.id, {}, enabled, {}};
				c.console = "drive";
				c.controller = "radio";
				c.channel = "enabled";
				for (const auto &ctrl : o.gameplay.at("controllers"))
					if (ctrl.at("id") == "radio")
						c.generation = ctrl.at("generation");
				require(world.submit(101, c), "Comms operator changes radio power");
			};
			radio(interrupted, 0);
			ticks(interrupted, 30);
			World reconnected;
			reconnected.restore(catalog, interrupted.save());
			ticks(interrupted, 30);
			ticks(reconnected, 30);
			require(interrupted.save() == reconnected.save(),
			        "Cargo continuation remains exact during a communications outage");
			radio(interrupted, 1);
			radio(reconnected, 1);
			ticks(interrupted, 1);
			ticks(reconnected, 1);
			for (int i = 0; i < 4000 && interrupted.save().at("logistics").at("cargo")[0].at("phase") != "complete";
			     ++i)
			{
				interrupted.tick();
				reconnected.tick();
			}
			require(interrupted.save() == reconnected.save(),
			        "Cargo recovery resumes exactly after communications return");
			require(interrupted.save().at("logistics").at("cargo")[0].at("phase") == "complete",
			        "Interrupted cargo delivery unloads and recovers through the ordinary logistics path");
		}
		World resumed;
		resumed.restore(catalog, w.save());
		bool simultaneous_reservations = false;
		for (int i = 0; i < 4000; ++i)
		{
			w.tick();
			resumed.tick();
			const auto checkpoint = w.save();
			std::int64_t stock = 0;
			for (const auto &body : checkpoint.at("bodies"))
				if (body.at("body").at("id") == 1)
					stock = saved_material_stock(body, 0).at("quantities").value("parts", std::int64_t{0});
			require(checkpoint.at("scenario").at("runtime").at("objectives")[0].at("status") == (stock == 7 ? 2 : 1),
			        "Only physical full receipt completes the objective, including partial unloading");
			if (!simultaneous_reservations &&
			    checkpoint.at("logistics").at("cargo")[0].at("reserved").get<std::int64_t>() > 0)
				for (const auto &batch : checkpoint.at("logistics").at("batches"))
					if (batch.at("destination") == 1)
					{
						simultaneous_reservations = true;
						World checked;
						checked.restore(catalog, checkpoint);
						auto overfull = checkpoint;
						for (auto &body : overfull["bodies"])
							if (body.at("body").at("id") == 1)
								saved_material_stock(body, 0)["quantities"]["parts"] = 5;
						rejects(
						    [&]
						    {
							    checked.restore(catalog, overfull);
						    },
						    "Stock plus cargo and batch reservations cannot exceed capacity");
						require(checked.save() == checkpoint, "Reservation validation is transactional");
					}

			if (w.save() != resumed.save())
			{
				std::cerr << "tick " << i << " diff " << Json::diff(w.save(), resumed.save()).dump() << "\n";
				break;
			}
			if (w.save().at("logistics").at("cargo")[0].at("phase") == "complete")
				break;
		}
		require(w.save() == resumed.save(), "Cargo flight and docking resume exactly");
		if (w.save().at("logistics").at("cargo")[0].at("phase") != "complete")
		{
			const auto debug = w.save();
			std::cerr << debug.at("logistics").dump() << "\n";
			for (const auto &b : debug.at("bodies"))
				if (b.at("body").at("id") == craft)
					std::cerr << b.dump() << "\n";
			std::cerr << debug.at("craft").dump() << "\n";
		}
		require(simultaneous_reservations, "Exercise simultaneous cargo and batch receiving reservations");
		require(w.save().at("logistics").at("cargo")[0].at("phase") == "complete",
		        "Physical cargo sortie returns after unloading");
		require(w.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 1,
		        "Cargo leaves source stock");
		const auto save = w.save();
		World completed;
		completed.restore(catalog, save);
		require(completed.save() == save, "Completed delivery restores without reapplying starting cargo");
		std::int64_t received = -1;
		for (const auto &body : save.at("bodies"))
			if (body.at("body").at("id") == 1)
				received = saved_material_stock(body, 0).at("quantities").value("parts", std::int64_t{0});
		require(received == 7, "Cargo arrives at destination once");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			auto trace = save;
			trace.erase("packs");
			std::cout << trace.dump() << '\n';
		}
		else
			std::cout << "Physical cargo integration passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
