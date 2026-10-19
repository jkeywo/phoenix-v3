#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
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
import phoenix.session.execution;

using namespace phoenix;
using phoenix::test::require;
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
		auto pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
		const auto exact = write_pack(pack);
		auto source = make_host_world();
		source->add_pack(exact);
		const auto bundle =
		    export_fleet_setup(*source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot"}, "test-fleet");
		auto first = read_fleet_setup(bundle, "ship1"), other = read_fleet_setup(bundle, "ship2");
		require(first.world->content_packs().size() == 1 && first.world->content_packs().front().archive() == exact,
		        "Fleet setup must preserve the exact original archives");
		require(first.world->authoritative_world().save() == other.world->authoritative_world().save(),
		        "Different ship servers must begin with the same complete verified world");
		require(first.world->setup().local_slot != other.world->setup().local_slot,
		        "Each fixed server must own its distinct ship slot");
		require(!first.world->observation() && !other.world->observation(),
		        "Setup must not grant a player station claim");
		{
			const auto members = first.plan.at("members").get<std::map<std::string, std::string>>();
			GameExecution a(first.world, members), b(other.world, members);
			auto seat = a.seats("ship1").front().address;
			const auto token = random_token();
			Json claim{{"kind", "station-claim"},
			           {"owner", "crew"},
			           {"ship", seat.ship},
			           {"seat", seat.console},
			           {"lease", token}};
			require(!a.apply("ship2", claim), "a server cannot claim another server's ship");
			require(a.apply("ship1", claim) && b.apply("ship1", claim),
			        "committed executor admits the owning server's Console lease");
			Json request{{"kind", "command"},   {"owner", "crew"},        {"lease", token}, {"sequence", 1},
			             {"target", seat.ship}, {"action", "speed_mm_s"}, {"value", 1000}};
			require(a.apply("ship1", request) && b.apply("ship1", request) && !a.apply("ship1", request),
			        "committed requests obey lease ownership and sequence replay refusal");
			Json tick{{"kind", "tick"}, {"tick", 1}};
			require(a.apply("ship1", tick) && b.apply("ship1", tick),
			        "the shared executor advances only committed ticks");
			require(first.world->authoritative_world().save() == other.world->authoritative_world().save() &&
			            a.snapshot() == b.snapshot(),
			        "different server adapters reproduce identical world and lease state");
			a.absent("ship1");
			b.absent("ship1");
			require(!a.observe("ship1", "crew") && !a.apply("ship1", request),
			        "committed server absence clears Console authority and stale input");
			claim["owner"] = "replacement";
			require(!a.apply("ship1", claim), "disconnected replacement requires explicit confirmation");
			claim["replace"] = true;
			claim["lease"] = random_token();
			require(a.apply("ship1", claim) && !a.apply("ship1", request),
			        "confirmed replacement cannot revive the old lease");
		}
		phoenix::test::rejects(
		    [&]
		    {
			    PackSource accidental({bundle});
		    },
		    "A private fleet setup must never be advertised as a gameplay pack to clients");
		phoenix::test::rejects(
		    [&]
		    {
			    read_fleet_setup(bundle, "outsider");
		    },
		    "A setup cannot admit a server outside the fixed roster");
		auto invalid = read_pack(bundle).draft();
		auto plan = parse_json(invalid.files.at("fleet.json"), "fleet.json");
		plan["members"]["ship2"] = plan["members"]["ship1"];
		invalid.files["fleet.json"] = json_bytes(plan);
		bool refused = false;
		try
		{
			read_fleet_setup(write_pack(invalid), "ship1");
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Two fixed servers must not acquire authority over the same ship");
		// New lobby rosters may omit optional slots and retain mandatory AI-only ships.
		auto ai_slot = second;
		ai_slot["id"] = "test.ai";
		ai_slot["mandatory"] = true;
		ai_slot["x_mm"] = 100000;
		definitions["maps"][0]["ship_slots"].push_back(ai_slot);
		definitions["entities"][0]["controller"] = "test.patrol";
		definitions["trees"] =
		    Json::array({{{"id", "test.patrol"},
		                  {"initial", "run"},
		                  {"states", {{"run", {{"speed_mm_s", 1000}, {"ticks", 10}, {"next", "run"}}}}}}});
		pack.files["definitions.json"] = json_bytes(definitions);
		auto lobby_source = make_host_world();
		lobby_source->add_pack(write_pack(pack));
		const auto lobby_bundle =
		    export_fleet_setup(*lobby_source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot"}, "test-lobby",
		                       {{"organiser", "test.slot"}});
		auto ai_a = read_fleet_setup(lobby_bundle), ai_b = read_fleet_setup(lobby_bundle);
		require(ai_a.plan.at("members").size() == 1 && ai_a.world->setup().ships.size() == 2 &&
		            ai_a.world->setup().ships.contains("test.ai") && !ai_a.world->setup().ships.contains("test.second"),
		        "Mandatory AI ships are distinct from server votes; unclaimed optional ships are omitted");
		for (int i = 0; i < 12; ++i)
		{
			ai_a.world->tick();
			ai_b.world->tick();
		}
		require(ai_a.world->authoritative_world().save() == ai_b.world->authoritative_world().save(),
		        "Every host executes the same complete AI-inclusive world");
		const auto ai_id = *ai_a.world->authoritative_world().ship_for_slot("test.ai");
		bool ai_moving = false;
		for (const auto &body : ai_a.world->viewscreen())
			if (body.id == ai_id)
				ai_moving = body.vx > 0;
		require(ai_moving, "The unclaimed mandatory ship runs its authored AI on every host");
		auto ai_saved = read_fleet_setup(save_fleet_recovery(*ai_a.world, ai_a.plan));
		require(ai_saved.world->setup().ships == ai_a.world->setup().ships && ai_saved.plan.at("members").size() == 1,
		        "Recovery preserves AI ships without adding quorum members");
		WorkshopDocument workshop;
		workshop.load_pack(write_pack(pack));
		require(workshop.verify().maps.at("test.map").at("ship_slots").back().at("mandatory") == true,
		        "Workshop round-trip retains mandatory slot metadata");
		ai_slot["mandatory"] = "yes";
		definitions["maps"][0]["ship_slots"].push_back(ai_slot);
		pack.files["definitions.json"] = json_bytes(definitions);
		refused = false;
		try
		{
			auto invalid_content = make_host_world();
			invalid_content->add_pack(write_pack(pack));
		}
		catch (...)
		{
			refused = true;
		}
		require(refused, "Nonboolean mandatory metadata is refused by production content verification");
		std::cout
		    << "PASS exact fleet content, complete initial worlds, distinct ship authority and roster verification\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
