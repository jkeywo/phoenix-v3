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
			std::cout << w.save().dump() << '\n';
		else
			std::cout << "Material logistics integration passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
