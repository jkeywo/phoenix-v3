#include "../support/assertions.hpp"
#include "../support/combat_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
Json fixture()
{
	auto s = combat_fixture();
	auto &e = s["entities"][0];
	e["systems"]["mag"]["initial"] = 0;
	e["systems"]["mag"]["reload"] = {{"strategy", "none"}};
	e["logistics"] = Json::parse(R"({"controller":"team-a","units_per_second":100,"concurrency":2,"batch_size":2,
 "materials":[{"id":"round"},{"id":"parts","volume":2}],
 "stores":[{"id":"hold","compartment":"aft","controller":"team-a","capacity":20,"main":true,"accepts":["round","parts"],"initial":{"round":5,"parts":3}},
 {"id":"magazine","system":"mag","compartment":"fore","controller":"mag","capacity":3,"accepts":["round"],"targets":{"round":3}},
 {"id":"kit","compartment":"fore","controller":"team-b","capacity":4,"accepts":["parts"],"targets":{"parts":2}}]})");
	return s;
}
std::int64_t total(const World &w, const std::string &material)
{
	const auto save = w.save();
	std::int64_t n = 0;
	for (const auto &b : save.at("bodies"))
		if (b.contains("inventory"))
		{
			for (const auto &store : b.at("inventory").at("stores"))
				n += store.at("quantities").value(material, std::int64_t{0});
			if (material == "round")
				n += b.at("combat").at("systems").at("mag").at("ammo").get<std::int64_t>();
		}
	for (const auto &b : save.at("logistics").at("batches"))
		if (b.at("material") == material)
			n += b.at("quantity").get<std::int64_t>();
	return n;
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		{
			auto source = fixture();
			auto &entity = source["entities"][0];
			auto &layout = entity["control_layout"];
			layout["station_complexity"] = true;
			layout["manual_workloads"] = {{"observer", {{"logistics", 3}}}};
			layout["controllers"].push_back({{"id", "logistics"},
			                                 {"console", "observer"},
			                                 {"fallback", "hold"},
			                                 {"commands", Json::array({{{"id", "automatic"},
			                                                            {"action", "intent_boolean"},
			                                                            {"initial", 1},
			                                                            {"consumers", Json::array()}}})}});
			layout["profiles"][0]["stars"] = 1;
			layout["profiles"][0]["consoles"]["observer"]["ai"].push_back("logistics");
			auto full = layout["profiles"][0];
			full["id"] = "stock";
			full["stars"] = 3;
			full["consoles"]["observer"]["ai"] = Json::array();
			full["consoles"]["observer"]["human"].push_back("logistics");
			const auto full_index = layout["profiles"].size();
			layout["profiles"].push_back(full);
			entity["logistics"]["controller"] = "logistics";
			entity["logistics"]["stores"][0]["controller"] = "logistics";
			World workload;
			workload.load(load(source), "test.map");
			require(workload.claim(101, 1, "observer"), "Claim repair and logistics workload");
			auto command = [&](const std::string &controller, const std::string &action)
			{
				const auto view = *workload.observe(101);
				OperatingCommand c{1, action, view.gameplay.at("logistics").at("revision"), "hold"};
				c.controller = controller;
				for (const auto &row : view.gameplay.at("controllers"))
					if (row.at("id") == controller)
						c.generation = row.at("generation");
				return c;
			};
			auto repair = command("team-a", "");
			repair.system.clear();
			repair.channel = "assignment";
			repair.value = 2;
			require(workload.submit(101, repair), "One-star player retains individual repair deployment");
			require(!workload.submit(101, command("team-a", "logistics.hold")) &&
			            !workload.submit(101, command("logistics", "logistics.hold")),
			        "Repair authority never grants one-star stock control");
			require(workload.submit(101, {1, "preset", static_cast<std::int64_t>(full_index), {}}),
			        "Select full stock workload");
			workload.tick();
			require(!workload.submit(101, command("team-a", "logistics.hold")) &&
			            workload.submit(101, command("logistics", "logistics.hold")),
			        "Three-star stock remains owned by its dedicated Controller");
			workload.tick();
			require(workload.observe(101)->gameplay.at("logistics").at("stores")[0].at("held") == true,
			        "Full stock hold takes effect");
			require(workload.submit(101, {1, "preset", 0, {}}), "Return stock management to assistance");
			workload.tick();
			workload.tick();
			require(workload.observe(101)->gameplay.at("logistics").at("stores")[0].at("held") == false,
			        "Downgrade resumes automatic stock assistance");
		}
		{
			auto source = fixture();
			auto &entity = source["entities"][0];
			entity.erase("logistics");
			entity["combat"]["access_controller"] = "team-a";
			const auto catalog = load(source);
			World access;
			access.load(catalog, "test.map");
			require(access.claim(101, 1, "observer"), "Claim compartment access without inventory");
			const auto board = access.observe(101)->gameplay.at("logistics");
			require(board.at("stores").empty() && !board.at("routes").empty(), "Access controls do not require stores");
			OperatingCommand c{1, "logistics.route", board.at("revision"), "fore"};
			c.console = "observer";
			c.controller = "team-a";
			c.text = "aft/close";
			const auto observed = *access.observe(101);
			for (const auto &ctrl : observed.gameplay.at("controllers"))
				if (ctrl.at("id") == c.controller)
					c.generation = ctrl.at("generation");
			require(access.submit(101, c), "Close route on a ship without inventories");
			access.tick();
			require(!access.submit(101, c), "Access edits reject obsolete revisions");
			const auto saved = access.save();
			require(!saved.at("bodies")[0].contains("inventory") &&
			            !saved.at("bodies")[0].at("combat").at("closed_routes").empty(),
			        "Compartments own route state");
			World resumed;
			resumed.restore(catalog, saved);
			access.tick();
			resumed.tick();
			require(access.save() == resumed.save(), "Independent access state resumes");
			auto old = saved;
			old["schema"] = 16;
			const auto before = access.save();
			rejected(
			    [&]
			    {
				    access.restore(catalog, old);
			    },
			    "start a new session");
			require(access.save() == before, "Retired save refusal is transactional");
		}
		{
			auto source = fixture();
			const auto catalog = load(source);
			World blocked;
			blocked.load(catalog, "test.map");
			require(blocked.claim(101, 1, "observer"), "Claim routing controls");
			auto command = [&](const std::string &action, const std::string &system, const std::string &text)
			{
				const auto o = *blocked.observe(101);
				OperatingCommand c{1, "logistics." + action, o.gameplay.at("logistics").at("revision"), system};
				c.console = "observer";
				c.controller = "team-a";
				c.text = text;
				for (const auto &ctrl : o.gameplay.at("controllers"))
					if (ctrl.at("id") == c.controller)
						c.generation = ctrl.at("generation");
				return c;
			};
			require(blocked.submit(101, command("route", "fore", "aft/close")), "Close delivery route deliberately");
			for (int i = 0; i < 20; ++i)
				blocked.tick();
			require(blocked.observe(101)->gameplay.at("logistics").at("stores")[0].at("quantities").at("round") == 5,
			        "Blocked waiting requests leave source stock untouched");
			require(blocked.submit(101, command("route", "fore", "aft/open")), "Authorise route reopening");
			for (int i = 0; i < 4; ++i)
				blocked.tick();
			const auto before = blocked.save();
			require(blocked.submit(101, command("route", "fore", "aft/close")), "Close route with cargo in transit");
			for (int i = 0; i < 20; ++i)
				blocked.tick();
			require(total(blocked, "round") == 15 && total(blocked, "parts") == 9,
			        "Route blockage preserves physical batch quantities");
			require(blocked.observe(101)->gameplay.at("logistics").at("stores")[1].at("quantities").at("round") == 0,
			        "Closed route strands the batch instead of delivering through a seal");
			require(blocked.submit(101, command("route", "fore", "aft/open")), "Reopen a stranded batch route");
			for (int i = 0; i < 40; ++i)
				blocked.tick();
			require(blocked.observe(101)->gameplay.at("logistics").at("stores")[1].at("quantities").at("round") == 3,
			        "Stranded cargo resumes and arrives once");
		}
		{
			auto source = fixture();
			auto &stores = source["entities"][0]["logistics"]["stores"];
			stores[0]["initial"]["parts"] = 1;
			auto later = stores[2];
			later["id"] = "later";
			stores.push_back(later);
			World scarce;
			scarce.load(load(source), "test.map");
			require(scarce.claim(101, 1, "observer"), "Claim scarce supply fixture");
			for (int i = 0; i < 40; ++i)
				scarce.tick();
			const auto stocks = scarce.observe(101)->gameplay.at("logistics").at("stores");
			require(stocks[2].at("quantities").at("parts") == 1 && stocks[3].at("quantities").at("parts") == 0,
			        "First eligible request receives scarce parts without priority reordering");
		}
		{
			auto source = fixture();
			source["trees"].push_back(Json::parse(
			    R"({"id":"test.supply-ai","context":"controller","logistics_commands":true,"initial":"hold","states":[{"id":"hold","entry":[{"action":"logistics.hold","arguments":["hold","",{"observation":"logistics.revision"}]}]}]})"));
			auto &e = source["entities"][0];
			for (auto &ctrl : e["control_layout"]["controllers"])
				if (ctrl["id"] == "team-a")
				{
					ctrl.erase("fallback");
					ctrl["tree"] = "test.supply-ai";
				}
			e["control_layout"]["profiles"][0]["consoles"]["observer"] = {{"human", {"team-b"}}, {"ai", {"team-a"}}};
			World automatic;
			automatic.load(load(source), "test.map");
			require(automatic.claim(101, 1, "observer"), "Claim Console with delegated stock Controller");
			automatic.tick();
			require(automatic.observe(101)->gameplay.at("logistics").at("stores")[0].at("held") == true,
			        "AI uses the same scoped revision and stock command admission as humans");
		}
		auto source = fixture();
		const auto catalog = load(source);
		const auto map = source["maps"][0]["id"].get<std::string>();
		World w;
		w.load(catalog, map);
		require(w.claim(101, 1, "observer"), "Claim material stores");
		const auto initial = w.save();
		const auto rounds = total(w, "round"), parts = total(w, "parts");
		for (int i = 0; i < 4; ++i)
			w.tick();
		require(!w.save().at("logistics").at("batches").empty(), "Dispatch retains physical batches");
		World resumed;
		resumed.restore(catalog, w.save());
		for (int i = 0; i < 60; ++i)
		{
			w.tick();
			resumed.tick();
		}
		require(w.save() == resumed.save(), "In-transit deliveries resume exactly");
		const auto board = w.observe(101)->gameplay.at("logistics");
		require(board.at("stores").size() == 3, "Engineering sees all stores");
		require(w.claim(102, 1, "drive"), "Claim local stock console");
		require(w.observe(102)->gameplay.at("logistics").at("stores").size() == 1,
		        "Local operators see local stock only");
		require(total(w, "round") == rounds && total(w, "parts") == parts, "Dispatch and arrival conserve quantities");
		require(w.save().at("logistics").at("batches").empty(), "Deliveries arrive once");
		resumed.restore(catalog, w.save());
		auto bad = w.save();
		bad["bodies"][0]["inventory"]["stores"][0]["quantities"]["round"] = 999;
		bool refused = false;
		try
		{
			resumed.restore(catalog, bad);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && resumed.save() == w.save(), "Invalid inventory restore is transactional");
		auto invalid = source;
		invalid["entities"][0]["logistics"]["stores"][0]["capacity"] = 1;
		refused = false;
		try
		{
			(void)load(invalid);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Loader rejects shared capacity overflow");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			auto trace = w.save();
			trace.erase("packs");
			std::cout << trace.dump() << '\n';
		}
		else
			std::cout << "Material logistics integration passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
