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

import phoenix.content.documents;
import phoenix.fleet_setup;
import phoenix.wire;
import phoenix.session.execution;

using namespace phoenix;
using phoenix::test::require;
int main(int argc, char **argv)
{
	try
	{
		if (argc == 3 && std::string(argv[1]) == "--verify-pack")
		{
			auto content = make_host_world();
			content->add_pack(read_file(argv[2]));
			for (const auto &[id, map] : content->catalog().maps)
			{
				try
				{
					for (const auto &slot : map.at("ship_slots"))
					{
						const std::string slot_id = slot.at("id");
						const auto ship =
						    slot.value("default_ship", slot.at("allowed_ships").front().get<std::string>());
						read_fleet_setup(export_fleet_setup(*content, {id, {{slot_id, ship}}, slot_id}, "verification",
						                                    {{"host", slot_id}}));
					}
					GameSetup gm{id};
					gm.gm_host = true;
					read_fleet_setup(export_fleet_setup(*content, gm, "verification", {{"host", ""}}));
					std::cout << "Verified ship and GM setup: " << id << '\n';
				}
				catch (const std::exception &error)
				{
					throw std::runtime_error(id + ": " + error.what());
				}
			}
			return 0;
		}
		auto definitions =
		    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fleet fixture");
		auto second = definitions["maps"][0]["ship_slots"][0];
		second["id"] = "test.second";
		second["x_mm"] = 50000;
		second["allowed_ships"].push_back("test.variant");
		second["default_ship"] = "test.variant";
		definitions["maps"][0]["ship_slots"].push_back(second);
		auto pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
		const auto exact = write_pack(pack);
		{
			auto seeking = definitions;
			seeking["entities"][0].erase("console");
			seeking["entities"][0]["stations"] = Json::parse(R"json([
				{"id":"bridge","consoles":[
					{"id":"main","definition":"test.console","systems":["engine","steering","radar"]},
					{"id":"navigation","definition":"test.seeking","systems":[]}
				]}
			])json");
			seeking["consoles"].push_back(
			    {{"id", "test.seeking"}, {"human_seeking", true}, {"widgets", Json::array()}});
			auto seeking_source = make_host_world();
			seeking_source->add_pack(write_pack(phoenix::test::fixture_pack(json_bytes(seeking), "test-core")));
			auto prepared = read_fleet_setup(export_fleet_setup(*seeking_source,
			                                                    {"test.map", {{"test.slot", "test.ship"}}, "test.slot"},
			                                                    "seeking-test", {{"organiser", "test.slot"}}));
			auto &world = prepared.world->authoritative_world();
			const auto ship = *world.ship_for_slot("test.slot");
			require(!world.can_claim(100, ship, "navigation") && world.claim(100, ship, "main") &&
			            world.observe(100)->additional_consoles.size() == 1,
			        "Fleet setup admits primary seats while human-seeking consoles follow their operator");
		}
		auto source = make_host_world();
		source->add_pack(exact);
		{
			GameSetup gm{"test.map"};
			gm.gm_host = true;
			const auto gm_bundle = export_fleet_setup(*source, gm, "gm-test", {{"organiser", ""}});
			auto solo = read_fleet_setup(gm_bundle);
			require(solo.world->setup().gm_host && solo.world->setup().local_slot.empty() &&
			            solo.world->setup().ships.size() == 2 &&
			            solo.world->setup().ships.at("test.second") == "test.variant" && !solo.world->observation(),
			        "GM hosting fills default AI ships without occupying a player slot");
			GameExecution execution(solo.world, {{"organiser", ""}});
			require(execution.seats("organiser").empty() &&
			            execution.apply("organiser", Json{{"kind", "tick"}, {"tick", 1}}),
			        "GM replication advances the world without player Console authority");
			auto resumed = read_fleet_setup(save_fleet_recovery(*solo.world, solo.plan));
			require(resumed.world->setup().gm_host && resumed.world->ticks() == 1,
			        "GM-only recovery preserves the world and the absence of a player slot");
			gm.ships["test.slot"] = "test.ship";
			const auto mixed = export_fleet_setup(*source, gm, "gm-mixed", {{"organiser", ""}, {"crew", "test.slot"}});
			auto organiser = read_fleet_setup(mixed, "organiser"), crew = read_fleet_setup(mixed, "crew");
			require(organiser.world->setup().gm_host && !crew.world->setup().gm_host &&
			            crew.world->setup().local_slot == "test.slot" &&
			            organiser.world->authoritative_world().save() == crew.world->authoritative_world().save(),
			        "A joining ship host owns its slot while the GM remains independent");
			GameExecution mixed_execution(organiser.world, {{"organiser", ""}, {"crew", "test.slot"}});
			require(mixed_execution.seats("organiser").empty() && !mixed_execution.seats("crew").empty(),
			        "Only the ship host receives the joined ship's Console leases");
		}
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
			const auto command_view = a.observe("ship1", "crew", 1);
			require(command_view.has_value(), "Claimed fleet lease projects widget ownership");
			for (const auto &controller : command_view->gameplay.at("controllers"))
				for (const auto &[channel, field] : controller.at("commands").items())
					if (field.at("action") == "speed_mm_s")
					{
						request["controller"] = controller.at("id");
						request["channel"] = channel;
						request["generation"] = controller.at("generation");
					}
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
		phoenix::test::navigation_policy(definitions["entities"][0], "test.patrol");
		definitions["trees"] = Json::parse(R"json([
  {
    "id": "test.patrol",
    "context": "controller",
    "initial": "run",
    "command_bindings": [
      {
        "channel": "speed_mm_s",
        "type": "integer",
        "semantics": "setting"
      }
    ],
    "states": [
      {
        "id": "run",
        "running": [
          {
            "action": "write.speed_mm_s",
            "arguments": [
              1000
            ],
            "once": true
          }
        ],
        "transitions": [
          {
            "target": "run",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 10
              }
            ]
          }
        ]
      }
    ]
  }
])json");
		pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
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
