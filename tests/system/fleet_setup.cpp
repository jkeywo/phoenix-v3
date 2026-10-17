#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.fleet_setup;
import phoenix.wire;

using namespace phoenix;
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	try
	{
		auto definitions =
		    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fleet fixture");
		auto second = definitions["maps"][0]["ship_slots"][0];
		second["id"] = "test.second";
		second["x_mm"] = 50000;
		definitions["maps"][0]["ship_slots"].push_back(second);
		Pack pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = json_bytes(definitions);
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		const auto exact = write_pack(pack);
		auto source = make_host_world();
		source->add_pack(exact);
		const auto bundle =
		    export_fleet_setup(*source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot"}, "test-fleet");
		auto first = read_fleet_setup(bundle, "ship1"), other = read_fleet_setup(bundle, "ship2");
		require(first.world->content_archives() == std::vector<Bytes>{exact},
		        "Fleet setup must preserve the exact original archives");
		require(first.world->authoritative_world().save() == other.world->authoritative_world().save(),
		        "Different ship servers must begin with the same complete verified world");
		require(first.world->setup().local_slot != other.world->setup().local_slot,
		        "Each fixed server must own its distinct ship slot");
		require(!first.world->observation() && !other.world->observation(),
		        "Setup must not grant a player station claim");
		bool private_refused = false;
		try
		{
			PackSource accidental({bundle});
		}
		catch (const std::exception &)
		{
			private_refused = true;
		}
		require(private_refused, "A private fleet setup must never be advertised as a gameplay pack to clients");
		bool refused = false;
		try
		{
			read_fleet_setup(bundle, "outsider");
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "A setup cannot admit a server outside the fixed roster");
		auto invalid = read_pack(bundle);
		auto plan = parse_json(invalid.files.at("fleet.json"), "fleet.json");
		plan["members"]["ship2"] = plan["members"]["ship1"];
		invalid.files["fleet.json"] = json_bytes(plan);
		refused = false;
		try
		{
			read_fleet_setup(write_pack(invalid), "ship1");
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Two fixed servers must not acquire authority over the same ship");
		std::cout
		    << "PASS exact fleet content, complete initial worlds, distinct ship authority and roster verification\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
