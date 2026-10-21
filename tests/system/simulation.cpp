#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.simulation;

using namespace phoenix;
using phoenix::test::load;
using phoenix::test::require;
Json instance(int id, const std::string &definition, int x, int heading = 0)
{
	return {{"id", id}, {"definition", definition}, {"x_mm", x}, {"y_mm", 0}, {"heading_mdeg", heading}};
}
int main(int argc, char **)
{
	try
	{
		auto fixture =
		    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fixture");
		fixture["maps"][0]["entities"] = Json::array(
		    {instance(1, "test.ship", 0), instance(2, "test.ship", 10000), instance(3, "test.ship", 200000)});
		auto catalog = load(fixture);
		{
			auto basic = fixture;
			basic["maps"][0]["entities"] = Json::array({instance(1, "test.ship", 0)});
			basic["entities"][0]["systems"]["engine"]["acceleration_mm_s2"] = 2001;
			basic["entities"][0]["systems"]["steering"]["turn_mdeg_s"] = 90001;
			auto named = basic;
			for (const char *kind : {"engine", "steering", "radar"})
				named["entities"][0]["systems"][kind]["kind"] = kind;
			const auto legacy_content = load(basic), fitted_content = load(named);
			World legacy, fitted, resumed_legacy, resumed_fitted;
			legacy.load(legacy_content, "test.map");
			fitted.load(fitted_content, "test.map");
			require(legacy.claim(101, 1) && fitted.claim(101, 1), "Both machinery representations admit a pilot");
			for (int tick = 0; tick < 41; ++tick)
			{
				std::vector<World *> worlds{&legacy, &fitted};
				if (tick >= 7)
				{
					worlds.push_back(&resumed_legacy);
					worlds.push_back(&resumed_fitted);
				}
				for (auto *world : worlds)
				{
					if (tick == 0 || tick == 22)
					{
						require(world->submit(101, {1, "speed_mm_s", 7000}) &&
						            world->submit(101, {1, "heading_mdeg", tick == 0 ? 90000 : 250000}) &&
						            world->submit(101, {1, "engine_enabled", 1}),
						        "Movement requests admitted");
					}
					if (tick == 13)
						require(world->submit(101, {1, "engine_enabled", 0}), "Coasting request admitted");
					world->tick();
				}
				const auto a = legacy.inspect().at(0), b = fitted.inspect().at(0);
				require(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy && a.heading == b.heading &&
				            legacy.scene().at(0).forward_thrust == fitted.scene().at(0).forward_thrust,
				        "Legacy and fitted basic machinery must produce identical motion and exhaust");
				if (tick == 6)
				{
					resumed_legacy.restore(legacy_content, legacy.save());
					resumed_fitted.restore(fitted_content, fitted.save());
				}
				if (tick >= 7)
					require(legacy.save() == resumed_legacy.save() && fitted.save() == resumed_fitted.save(),
					        "Each machinery representation must preserve fractional rates through resume");
			}
		}
		{
			auto symbols = fixture;
			symbols["entities"][0]["render"]["radar_icon"] = "ship";
			symbols["entities"][1]["render"]["radar_icon"] = "asteroid";
			symbols["entities"][1]["render"]["radar_sprite"] = "test-core:assets/pixel.png";
			symbols["maps"][0]["entities"][1]["definition"] = "test.variant";
			World radar_symbols;
			radar_symbols.load(load(symbols), "test.map");
			require(radar_symbols.claim(101, 1), "symbol fixture claims the normal radar Console");
			const auto view = radar_symbols.observe(101);
			require(view->own.radar_icon == RadarIcon::ship && view->contacts.size() == 1 &&
			            view->contacts[0].radar_icon == RadarIcon::asteroid,
			        "radar projection supplies only visible contacts and their authored public symbols");
			require(view->contacts[0].radar_sprite == radar_sprite_id(catalog.assets.at("test-core:assets/pixel.png")),
			        "detected contacts carry the public radar PNG address through production simulation");
			radar_symbols.submit(101, {1, "radar_enabled", 0});
			radar_symbols.tick();
			require(radar_symbols.observe(101)->contacts.empty(), "symbol metadata must not bypass radar disable");
		}
		{
			World propulsion;
			propulsion.load(catalog, "test.map");
			require(propulsion.claim(101, 1), "synthetic drive Console can be claimed");
			require(propulsion.submit(101, {1, "speed_mm_s", 1000}), "forward thrust request admitted");
			propulsion.tick();
			require(propulsion.scene().at(0).forward_thrust > 0,
			        "applied forward propulsion must produce exhaust output");
			for (int i = 0; i < 100; ++i)
				propulsion.tick();
			require(propulsion.scene().at(0).forward_thrust == 0, "coasting at requested speed must not invent thrust");
			propulsion.submit(101, {1, "speed_mm_s", 0});
			propulsion.tick();
			require(propulsion.scene().at(0).forward_thrust == 0, "braking must not draw rear exhaust");
			propulsion.submit(101, {1, "engine_enabled", 0});
			propulsion.tick();
			require(propulsion.scene().at(0).forward_thrust == 0,
			        "disabled engines must clear visual propulsion output");
		}
		{
			auto stations = fixture;
			stations["schema"] = 2;
			auto &ship = stations["entities"][0];
			ship.erase("console");
			stations["consoles"].push_back({{"id", "test.empty"}, {"widgets", Json::array()}});
			ship["stations"] = Json::array(
			    {{{"id", "bridge"},
			      {"consoles",
			       Json::array({{{"id", "helm"}, {"definition", "test.empty"}, {"systems", {"engine", "steering"}}},
			                    {{"id", "sensors"}, {"definition", "test.empty"}, {"systems", {"radar"}}},
			                    {{"id", "observer"},
			                     {"definition", "test.empty"},
			                     {"systems", Json::array()},
			                     {"feeds", {"radar"}}}})}}});
			stations["trees"] = Json::parse(R"json([
  {
    "id": "test.autopilot",
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
              3000
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
			ship["motion_model"] = "vector";
			ship["controllers"] = {{"engine", "test.autopilot"}};
			auto content = load(stations);
			World world;
			world.load(content, "test.map");
			require(!world.claim(1, 1), "multiple Consoles require an explicit seat");
			require(world.claim(1, 1, "helm") && world.claim(2, 1, "sensors") && world.claim(3, 1, "observer") &&
			            world.claim(4, 2, "helm"),
			        "different Consoles and ships can be crewed independently");
			require(!world.claim(5, 1, "helm") && !world.submit(2, {1, "speed_mm_s", 1000}) &&
			            !world.submit(1, {1, "radar_enabled", 0}) && !world.submit(3, {1, "radar_enabled", 0}),
			        "Console ownership and read-only feeds constrain commands");
			require(!world.submit(1, {1, "speed_mm_s", 1000, "radar"}) &&
			            world.submit(1, {1, "speed_mm_s", 1000, "engine"}),
			        "qualified commands cannot alias another System");
			require(world.observe(1)->contacts.empty() && world.observe(2)->contacts.size() == 1 &&
			            world.observe(3)->contacts.size() == 1,
			        "radar projection follows explicit feeds");
			world.tick();
			require(world.observe(1)->requested_speed == 1000 && world.observe(2)->requested_speed == 0,
			        "controllers yield only owned Systems and feeds hide unbound requests");
			world.submit(1, {1, "speed_mm_s", 2000});
			world.submit(2, {1, "radar_enabled", 0});
			world.release(2);
			world.tick();
			require(world.observe(1)->requested_speed == 2000 && !world.observe(3)->contacts.empty(),
			        "release cancels only its own queued requests");
			world.release(1);
			world.tick();
			require(world.claim(1, 1, "helm") && world.observe(1)->requested_speed == 3000,
			        "released System controller resumes independently");
			auto saved = world.save();
			require(saved["schema"] == 10, "explicit Stations use versioned save state");
			World resumed;
			resumed.restore(content, saved);
			require(resumed.save() == saved, "Console claims and independent controllers survive round-trip");
			world.release(1);
			resumed.release(1);
			for (int i = 0; i < 5; ++i)
			{
				world.tick();
				resumed.tick();
			}
			require(world.save() == resumed.save(), "per-System continuation is deterministic");
			auto invalid = stations;
			invalid["entities"][0]["stations"][0]["consoles"][1]["systems"].push_back("engine");
			phoenix::test::rejects(
			    [&]
			    {
				    load(invalid);
			    },
			    "authored System ownership cannot overlap");
		}
		{
			auto instances = fixture;
			auto &ship = instances["entities"][0];
			ship.erase("console");
			ship["systems"]["port"] = ship["systems"]["engine"];
			ship["systems"]["starboard"] = ship["systems"]["engine"];
			ship["systems"]["port"]["kind"] = "engine";
			ship["systems"]["starboard"]["kind"] = "engine";
			ship["systems"].erase("engine");
			instances["consoles"].push_back({{"id", "test.empty"}, {"widgets", Json::array()}});
			ship["stations"] = Json::array(
			    {{{"id", "bridge"},
			      {"consoles",
			       Json::array(
			           {{{"id", "port-seat"}, {"definition", "test.empty"}, {"systems", {"port", "steering"}}},
			            {{"id", "starboard-seat"}, {"definition", "test.empty"}, {"systems", {"starboard", "radar"}}},
			            {{"id", "observer"},
			             {"definition", "test.empty"},
			             {"systems", Json::array()},
			             {"feeds", {"port"}}}})}}});
			instances["trees"] = Json::parse(R"json([
  {
    "id": "test.drive",
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
              3000
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
			ship["motion_model"] = "vector";
			ship["controllers"] = {{"starboard", "test.drive"}};
			const auto data = load(instances);
			const auto public_data = resolve_packs({console_assets(data, "test.ship")});
			require(resolve_systems(public_data, "test.ship").size() == 4 && public_data.trees.empty() &&
			            public_data.maps.empty(),
			        "restricted assets retain exact instance bindings and exclude controllers/maps");
			World world;
			world.load(data, "test.map");
			require(world.claim(1, 1, "port-seat") && world.claim(2, 1, "starboard-seat") &&
			            world.claim(3, 1, "observer"),
			        "same-kind instances can have different owning Consoles");
			require(world.submit(1, {1, "speed_mm_s", 1000, "port"}) &&
			            !world.submit(1, {1, "speed_mm_s", 2000, "starboard"}) &&
			            !world.submit(3, {1, "speed_mm_s", 1000, "port"}),
			        "instance ownership controls both qualified commands and read-only feeds");
			world.tick();
			require(world.observe(1)->systems.size() == 2 && world.observe(3)->systems.size() == 1 &&
			            world.observe(3)->systems[0].requested == 1000,
			        "a Console receives only its bound instance state");
			world.release(2);
			world.tick();
			require(world.observe(3)->systems[0].requested == 1000,
			        "the sibling's ordinary controller cannot write the human's instance");
			auto saved = world.save();
			require(saved["schema"] == 10 && saved["bodies"][0]["systems"]["starboard"]["speed"] == 3000,
			        "instance save records preserve independently controlled machinery");
			World resumed;
			resumed.restore(data, saved);
			require(resumed.save() == saved, "fitted instance and controller state round trips");
			for (int corruption = 0; corruption < 5; ++corruption)
			{
				auto bad = saved;
				auto &states = bad["bodies"][0]["systems"];
				if (corruption == 0)
					states.erase("port");
				if (corruption == 1)
					states["port"]["speed"] = 1000000000;
				if (corruption == 2)
					states["radar"]["speed"] = 10;
				if (corruption == 3)
					states["port"]["acceleration_remainder"] = 20;
				if (corruption == 4)
					states["starboard"]["tree"]["state"] = "missing";
				bool rejected = false;
				try
				{
					resumed.restore(data, bad);
				}
				catch (const std::exception &)
				{
					rejected = true;
				}
				require(rejected && resumed.save() == saved, "bad instance continuation refuses transactionally");
			}
			for (int i = 0; i < 15; ++i)
			{
				world.tick();
				resumed.tick();
			}
			require(world.save() == resumed.save(), "instance continuation matches uninterrupted execution");
			resumed.clear_claims();
			require(resumed.claim(1, 1, "port-seat") && resumed.observe(1)->systems[0].requested == 1000,
			        "recovery clears live claims without changing instance intent");
			auto combined = instances;
			auto &seats = combined["entities"][0]["stations"][0]["consoles"];
			seats[0]["systems"].push_back("starboard");
			seats[1]["systems"] = {"radar"};
			World both;
			both.load(load(combined), "test.map");
			both.claim(1, 1, "port-seat");
			require(!both.submit(1, {1, "speed_mm_s", 1000}) && both.submit(1, {1, "speed_mm_s", 1000, "port"}),
			        "ambiguous unqualified commands fail rather than choose a same-kind sibling");
		}
		Json trace = Json::array();
		World engine;
		engine.load(catalog, "test.map");
		require(engine.can_claim(101, 1) && engine.can_claim(102, 1) && !engine.observe(101),
		        "checking console eligibility must not acquire ownership");
		require(engine.claim(101, 1), "a vacant ship must admit one console claim");
		require(!engine.claim(102, 1) && !engine.submit(102, {1, "speed_mm_s", 1000}),
		        "unclaimed/other-console commands must have no effect");
		require(!engine.submit(101, {2, "speed_mm_s", 1000}) && !engine.submit(101, {1, "speed_mm_s", 999999}),
		        "wrong ship and requests beyond capability must be refused");
		require(!engine.submit(101, {1, "unknown", 0}) && !engine.submit(101, {1, "engine_enabled", 2}) &&
		            !engine.submit(101, {1, "radar_enabled", -1}) && !engine.submit(101, {1, "heading_mdeg", 360000}),
		        "external requests must obey the supported action types and ranges");
		require(engine.observe(101)->contacts.size() == 1 && engine.observe(101)->contacts[0].id == 2,
		        "radar must expose the in-range contact and omit the out-of-range entity");
		engine.submit(101, {1, "speed_mm_s", 5000});
		for (int i = 0; i < 20; ++i)
			engine.tick();
		require(engine.inspect()[0].vx > 0 && engine.inspect()[0].x > 0,
		        "admitted engine request must accelerate and move through Flecs execution");
		engine.submit(101, {1, "engine_enabled", 0});
		engine.submit(101, {1, "heading_mdeg", 90000});
		engine.tick();
		auto coast = engine.inspect()[0];
		for (int i = 0; i < 5; ++i)
			engine.tick();
		require(engine.inspect()[0].vx == coast.vx && engine.inspect()[0].heading > coast.heading,
		        "engine disable must preserve velocity while steering continues");
		engine.submit(101, {1, "radar_enabled", 0});
		engine.tick();
		require(engine.observe(101)->contacts.empty(), "radar disable must remove its live observations");
		require(!engine.observe(999), "no console claim means no permitted ship projection");
		require(engine.release(101) && engine.claim(303, 1) && !engine.submit(101, {1, "speed_mm_s", 1000}),
		        "release/reclaim must invalidate commands from the previous console");
		for (int shape = 0; shape < 3; ++shape)
		{
			auto data = fixture;
			data["entities"][1].erase("extends");
			data["entities"][1] = data["entities"][0];
			data["entities"][1]["id"] = "test.other";
			if (shape >= 1)
				data["entities"][1]["collision"] = {
				    {"shape", "box"}, {"half_width_mm", 1000}, {"half_length_mm", 1000}, {"mass_grams", 10000}};
			if (shape == 2)
				data["entities"][0]["collision"] = data["entities"][1]["collision"];
			data["maps"][0]["entities"] =
			    Json::array({instance(1, "test.ship", -3000), instance(2, "test.other", 3000, 180000)});
			World collision;
			collision.load(load(data), "test.map");
			collision.claim(1, 1);
			collision.claim(2, 2);
			collision.submit(1, {1, "speed_mm_s", 10000});
			collision.submit(2, {2, "speed_mm_s", 10000});
			for (int tick = 0; tick < 80; ++tick)
				collision.tick();
			trace.push_back(collision.save().at("bodies"));
			auto bodies = collision.inspect();
			require(bodies[0].x < bodies[1].x && bodies[1].x - bodies[0].x >= 1998,
			        "circle-circle/circle-box/box-box contacts must prevent crossing");
			require(std::abs(bodies[0].vx + bodies[1].vx) <= 1,
			        "equal opposing finite masses must conserve net momentum in a controlled axial collision");
		}
		for (bool moving_box : {false, true})
		{
			auto data = fixture;
			data["entities"][1] = data["entities"][0];
			data["entities"][1]["id"] = "test.wall";
			data["entities"][1]["collision"] = {
			    {"shape", "box"}, {"half_width_mm", 2000}, {"half_length_mm", 500}, {"mass_grams", 0}};
			if (moving_box)
				data["entities"][0]["collision"] = {
				    {"shape", "box"}, {"half_width_mm", 1000}, {"half_length_mm", 1000}, {"mass_grams", 10000}};
			data["maps"][0]["entities"] =
			    Json::array({instance(1, "test.ship", -5000), instance(2, "test.wall", 0, 45000)});
			World angled;
			angled.load(load(data), "test.map");
			angled.claim(1, 1);
			angled.submit(1, {1, "speed_mm_s", 4000});
			for (int tick = 0; tick < 35; ++tick)
				angled.tick();
			trace.push_back(angled.save().at("bodies"));
			auto bodies = angled.inspect();
			require(bodies[0].y != 0 && bodies[1].x == 0 && bodies[1].y == 0,
			        "oriented box normals must deflect both circles and boxes while static geometry stays anchored");
		}
		auto ai_data = fixture;
		phoenix::test::navigation_policy(ai_data["entities"][0], "test.patrol");
		ai_data["trees"] = Json::parse(R"json([
  {
    "id": "test.patrol",
    "context": "controller",
    "initial": "advance",
    "command_bindings": [
      {
        "channel": "heading_mdeg",
        "type": "integer",
        "semantics": "setting"
      },
      {
        "channel": "radar_enabled",
        "type": "boolean",
        "semantics": "setting"
      },
      {
        "channel": "speed_mm_s",
        "type": "integer",
        "semantics": "setting"
      }
    ],
    "states": [
      {
        "id": "advance",
        "running": [
          {
            "action": "write.heading_mdeg",
            "arguments": [
              45000
            ],
            "once": true
          },
          {
            "action": "write.radar_enabled",
            "arguments": [
              true
            ],
            "once": true
          },
          {
            "action": "write.speed_mm_s",
            "arguments": [
              4000
            ],
            "once": true
          }
        ],
        "transitions": [
          {
            "target": "turn",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 9
              }
            ]
          }
        ]
      },
      {
        "id": "turn",
        "running": [
          {
            "action": "write.heading_mdeg",
            "arguments": [
              135000
            ],
            "once": true
          },
          {
            "action": "write.radar_enabled",
            "arguments": [
              false
            ],
            "once": true
          },
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
            "target": "advance",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 7
              }
            ]
          }
        ]
      }
    ]
  }
])json");
		auto ai_content = load(ai_data);
		for (int boundary : {0, 9, 16})
		{
			World before, after;
			before.load(ai_content, "test.map");
			for (int tick = 0; tick < boundary; ++tick)
				before.tick();
			after.restore(ai_content, before.save());
			before.tick();
			after.tick();
			require(before.save() == after.save(),
			        "unentered initial and transitioned tree states must resume with the same entry outputs");
		}
		World radar_tree;
		radar_tree.load(ai_content, "test.map");
		for (int tick = 0; tick < 10; ++tick)
			radar_tree.tick();
		require(radar_tree.claim(7, 1) && radar_tree.observe(7)->contacts.empty(),
		        "a delegated tree's radar output must affect the same permitted observation path");
		World continuous;
		continuous.load(ai_content, "test.map");
		for (int tick = 0; tick < 7; ++tick)
			continuous.tick();
		auto checkpoint = continuous.save();
		World resumed;
		resumed.restore(ai_content, checkpoint);
		for (int tick = 0; tick < 100; ++tick)
		{
			continuous.tick();
			resumed.tick();
		}
		require(continuous.save() == resumed.save(), "interrupted state-tree work, integer remainders and motion must "
		                                             "resume identically to an uninterrupted run");
		require(continuous.inspect()[0].x != 0 && continuous.inspect()[0].heading != 0,
		        "delegated tree must actually drive engine and steering");
		auto changed = continuous.save();
		changed["bodies"][0]["body"]["remainder"]["x"] = std::numeric_limits<std::int64_t>::min();
		bool refused = false;
		try
		{
			resumed.restore(ai_content, changed);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && continuous.save() == resumed.save(),
		        "invalid restoration must preserve the previous live state");

		// Mixed capabilities and allocation order must not change physical ordering.
		auto mixed = ai_data;
		for (int kind = 0; kind < 5; ++kind)
		{
			auto definition = fixture["entities"][0];
			definition["id"] = "test.kind" + std::to_string(kind);
			definition.erase("console");
			if (kind < 3)
			{
				definition["systems"] = Json::object();
				const std::string capability = kind == 0 ? "engine" : kind == 1 ? "steering" : "radar";
				definition["systems"][capability] = fixture["entities"][0]["systems"][capability];
			}
			if (kind == 3)
				definition["systems"] = Json::object();
			if (kind == 4)
			{
				definition["collision"] = {
				    {"shape", "box"}, {"half_width_mm", 800}, {"half_length_mm", 1200}, {"mass_grams", 0}};
				phoenix::test::navigation_policy(definition, "test.patrol");
			}
			mixed["entities"].push_back(definition);
		}
		mixed["entities"][0]["systems"]["engine"]["acceleration_mm_s2"] = 2003;
		mixed["entities"][0]["systems"]["steering"]["turn_mdeg_s"] = 90007;
		mixed["maps"][0]["entities"] = Json::array();
		for (int id = 1; id <= 18; ++id)
		{
			auto placed = instance(id, id % 6 == 0 ? "test.ship" : "test.kind" + std::to_string(id % 6 - 1),
			                       (id % 6) * 1100 - 3000, (id * 17003) % 360000);
			placed["y_mm"] = (id / 6) * 1100 - 1500;
			mixed["maps"][0]["entities"].push_back(placed);
		}
		auto mixed_content = load(mixed);
		World ordered, reordered;
		ordered.load(mixed_content, "test.map");
		std::reverse(mixed["maps"][0]["entities"].begin(), mixed["maps"][0]["entities"].end());
		reordered.load(load(mixed), "test.map");
		for (int tick = 0; tick < 45; ++tick)
		{
			for (auto *world : {&ordered, &reordered})
			{
				if (tick == 3)
				{
					require(world->claim(77, 6), "mixed-world ship must be claimable");
					world->submit(77, {6, "speed_mm_s", 2333});
					world->submit(77, {6, "heading_mdeg", 274001});
				}
				if (tick == 11)
					world->release(77);
				if (tick == 16)
					world->claim(88, 12);
				if (tick == 21)
					world->clear_claims();
				world->tick();
			}
			require(ordered.save().at("bodies") == reordered.save().at("bodies"),
			        "entity allocation and capability layout must not change collision/tree/motion order");
			trace.push_back(ordered.save().at("bodies"));
		}

		for (int schema = 1; schema < 10; ++schema)
		{
			auto legacy = ordered.save();
			legacy["schema"] = schema;
			bool refused = false;
			try
			{
				World old;
				old.restore(mixed_content, legacy);
			}
			catch (const std::exception &)
			{
				refused = true;
			}
			require(refused, "legacy authority saves require explicit migration");
		}
		World mixed_resume;
		mixed_resume.restore(mixed_content, ordered.save());
		for (int tick = 0; tick < 30; ++tick)
		{
			ordered.tick();
			mixed_resume.tick();
			require(ordered.save() == mixed_resume.save(), "mixed-capability worlds must resume exactly");
			trace.push_back(mixed_resume.save().at("bodies"));
		}
		trace.push_back(continuous.save().at("bodies"));

		// Resolving the first pair moves entity 1 into a previously distant
		// higher-ID pair. A broad phase must discover it during the same pass.
		auto chain_data = fixture;
		chain_data["entities"][1] = fixture["entities"][0];
		chain_data["entities"][1]["id"] = "test.anchor";
		chain_data["entities"][1]["systems"] = Json::object();
		chain_data["entities"][1]["collision"]["mass_grams"] = 0;
		chain_data["entities"][0]["systems"]["radar"]["range_mm"] = 2100;
		chain_data["maps"][0]["entities"] = Json::array(
		    {instance(1, "test.ship", 0), instance(2, "test.anchor", -1500), instance(3, "test.ship", 2400)});
		World chain;
		chain.load(load(chain_data), "test.map");
		chain.claim(1, 1);
		require(chain.observe(1)->contacts.size() == 1, "pre-collision radar must omit the initially distant body");
		chain.tick();
		// Equal-mass splits push entity 3 by 51, 26, 13 and 6 mm in the
		// four passes. Deferring a newly discovered pair to the next pass differs.
		require(chain.inspect()[0].x == 495 && chain.inspect()[1].x == -1500 && chain.inspect()[2].x == 2496,
		        "collision correction must discover newly overlapping later pairs in the same pass");
		require(chain.observe(1)->contacts.size() == 2,
		        "radar index must use corrected positions immediately after collision resolution");
		trace.push_back(chain.save().at("bodies"));

		// Sparse space, negative coordinates and inclusive integer radar bounds.
		auto radar_data = chain_data;
		radar_data["entities"][0]["systems"]["radar"]["range_mm"] = 100000;
		radar_data["entities"][1]["collision"]["radius_mm"] = 1;
		radar_data["maps"][0]["entities"] =
		    Json::array({instance(1, "test.ship", -500000), instance(2, "test.anchor", -400000),
		                 instance(3, "test.anchor", -399999), instance(4, "test.anchor", -600000),
		                 instance(5, "test.anchor", -600001)});
		for (int id = 6; id < 134; ++id)
			radar_data["maps"][0]["entities"].push_back(instance(id, "test.anchor", id * 1000000));
		auto radar_content = load(radar_data);
		World indexed_radar;
		indexed_radar.load(radar_content, "test.map");
		indexed_radar.claim(1, 1);
		auto visible = indexed_radar.observe(1)->contacts;
		require(visible.size() == 2 && visible[0].id == 2 && visible[1].id == 4,
		        "radar index must be ready on load and include exact negative-coordinate range boundaries");
		indexed_radar.submit(1, {1, "speed_mm_s", 10000});
		for (int tick = 0; tick < 8; ++tick)
			indexed_radar.tick();
		visible = indexed_radar.observe(1)->contacts;
		require(visible.size() == 2 && visible[0].id == 2 && visible[1].id == 3,
		        "radar candidates must follow movement across range boundaries");
		World radar_restored;
		radar_restored.restore(radar_content, indexed_radar.save());
		visible = radar_restored.observe(1)->contacts;
		require(visible.size() == 2 && visible[0].id == 2 && visible[1].id == 3,
		        "restoration must rebuild radar candidates before the first tick");
		trace.push_back(indexed_radar.save().at("bodies"));

		// Degenerate populations and very large bounds cannot rely on grid-sized
		// expansion or a minimum number of leaves.
		auto edge_data = radar_data;
		edge_data["maps"][0]["entities"] = Json::array();
		World edge;
		edge.load(load(edge_data), "test.map");
		edge.tick();
		require(edge.inspect().empty(), "empty spatial indexes must support ticks and inspection");
		edge_data["maps"][0]["entities"] = Json::array({instance(1, "test.ship", -1000000000)});
		edge.load(load(edge_data), "test.map");
		edge.claim(1, 1);
		edge.tick();
		require(edge.observe(1)->contacts.empty(), "a one-leaf index must exclude the observing ship");
		edge_data["entities"][1]["collision"]["radius_mm"] = 1000000000;
		edge_data["maps"][0]["entities"] =
		    Json::array({instance(1, "test.ship", 999999000), instance(2, "test.anchor", 0)});
		edge.load(load(edge_data), "test.map");
		edge.tick();
		require(edge.inspect()[0].x == 1000000000 && edge.inspect()[1].x == 0,
		        "large collider bounds must retain contacts at the supported coordinate limit");
		if (argc > 1)
			std::cout << trace.dump() << '\n';
		else
			std::cout << "PASS motion, admission, radar, collision pairs and state-tree save/resume\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
