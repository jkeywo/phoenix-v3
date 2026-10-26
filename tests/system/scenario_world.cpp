#include "../support/active_effects.hpp"
#include "../support/fixtures.hpp"
#include "../support/model_fixture.hpp"
#include "../support/combat_fixture.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import phoenix.simulation;
import phoenix.gameplay_scenario;
import phoenix.session.local;

namespace
{
using namespace phoenix;
void check(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
Json action(std::string name, Json args)
{
	return {{"action", std::move(name)}, {"arguments", std::move(args)}};
}
Catalog load_fixture(const Json &data)
{
	auto pack = phoenix::test::fixture_pack(json_bytes(data), "test-core");
	return resolve_packs({read_pack(write_pack(pack))});
}
void nebula_paths()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "fog fixture");
	auto &map = data["maps"][0];
	map["regions"] = Json::array({{{"id", "fog"},
	                               {"shape", "annulus"},
	                               {"x_mm", 5000},
	                               {"y_mm", 0},
	                               {"radius_mm", 3000},
	                               {"inner_radius_mm", 1000},
	                               {"nebula", {{"density", 3}, {"sensor_factor", 1}, {"colour", {.2, .3, .4}}}}}});
	auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	check(world.claim(99, *world.ship_for_slot("test.slot")), "fog fixture claim failed");
	check(world.observe(99)->contacts.empty(), "intervening fog failed to attenuate sensors outside the region");
	const auto observed = *world.observe(99);
	const auto radar = std::find_if(observed.systems.begin(), observed.systems.end(),
	                                [](const auto &s)
	                                {
		                                return s.kind == OperatingSystem::Radar;
	                                });
	check(radar != observed.systems.end() && radar->radial_ranges.size() == 128,
	      "Sensor did not publish a bounded directional reach envelope");

	check(radar->radial_ranges[64] < radar->effective_range && radar->radial_ranges[0] >= radar->effective_range - 1000,
	      "Directional sensor reach failed to distinguish blocked and clear bearings");
	const std::vector<std::string> names{"test.ship", "test.variant"};
	const auto definition = compile_scenario(map, names);
	const auto &r = definition.regions.front();
	check(!scenario_contains(r, {5000, 0}) && scenario_contains(r, {7000, 0}), "annulus hole containment failed");
	check(!scenario_crosses(r, {4500, 0}, {5500, 0}) && scenario_crosses(r, {0, 0}, {10000, 0}),
	      "annulus swept crossing failed");
	const auto spans = scenario_region_intervals(r, {0, 0}, {10000, 0});
	const auto near = [](std::int64_t value, std::int64_t numerator)
	{
		return std::abs(value - scenario_segment_unit * numerator / 10) < scenario_segment_unit / 1000;
	};
	check(spans.size() == 2 && near(spans[0].begin, 2) && near(spans[0].end, 4) && near(spans[1].begin, 6) &&
	          near(spans[1].end, 8),
	      "annulus sweep retains both ordered occupied intervals around its hole");
	check(scenario_region_intervals(r, {4500, 0}, {5500, 0}).empty(),
	      "movement wholly inside the annulus hole has no occupied interval");
	const auto stationary = scenario_region_intervals(r, {7000, 0}, {7000, 0});
	check(stationary.size() == 1 && stationary[0].begin == 0 && stationary[0].end == scenario_segment_unit &&
	          scenario_region_intervals(r, {5000, 0}, {5000, 0}).empty(),
	      "stationary interval follows sampled ring membership");
	const auto into_hole = scenario_region_intervals(r, {7000, 0}, {5000, 0});
	check(into_hole.size() == 1 && into_hole[0].begin == 0 && into_hole[0].end < scenario_segment_unit,
	      "annulus sweep starting inside retains exit before reaching the hole");
	const auto path = scenario_path_length(r, {0, 0}, {10000, 0});
	check(path >= 3998 && path <= 4002, "annulus path did not subtract the hole");
	check(scenario_sensor_visible(definition.regions, {0, 0}, {10000, 0}, 140000) &&
	          !scenario_sensor_visible(definition.regions, {0, 0}, {10000, 0}, 100000),
	      "sensor optical-depth threshold is incorrect");
	auto overlap = definition.regions;
	overlap.push_back(r);
	check(!scenario_sensor_visible(overlap, {0, 0}, {10000, 0}, 200000), "overlapping cloud depths did not add");
	for (auto &cloud : overlap)
		cloud.nebula->density = 0;
	check(scenario_sensor_visible(overlap, {0, 0}, {10000, 0}, 10000), "zero-density cloud changed detection");
	ScenarioRegion box;
	box.shape = ScenarioRegionShape::Rectangle;
	box.centre = {5000, 0};
	box.half_width = 1000;
	box.half_height = 2000;
	check(scenario_path_length(box, {0, 0}, {10000, 0}) >= 1998 &&
	          scenario_path_length(box, {0, 0}, {10000, 0}) <= 2002,
	      "rectangle path clipping failed");
	const auto box_spans = scenario_region_intervals(box, {0, 0}, {10000, 0});
	check(box_spans.size() == 1 && near(box_spans[0].begin, 4) && near(box_spans[0].end, 6),
	      "rectangle interval retains existing clipped entry and exit");
	auto circle = r;
	circle.shape = ScenarioRegionShape::Circle;
	const auto circle_spans = scenario_region_intervals(circle, {10000, 0}, {0, 0});
	check(circle_spans.size() == 1 && near(circle_spans[0].begin, 2) && near(circle_spans[0].end, 8),
	      "circle interval is ordered in executed movement direction");
	World resumed;
	resumed.restore(catalog, world.save());
	resumed.clear_claims();
	check(resumed.claim(99, *resumed.ship_for_slot("test.slot")), "restored fog claim failed");
	check(resumed.observe(99)->contacts.empty(), "restored fog lost sensor attenuation");
	map["regions"][0]["nebula"]["sensor_factor"] = 0;
	catalog = load_fixture(data);
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	check(world.claim(99, *world.ship_for_slot("test.slot")), "clear fixture claim failed");
	check(world.observe(99)->contacts.size() == 1, "visual-only fog changed contacts");
	map["regions"][0]["display"] = {
	    {"navigation", true}, {"radar", true}, {"name", "Test ring"}, {"colour", {.1, .2, .3}}, {"tags", {"cloud"}}};
	data["entities"][0]["tags"] = {"ship", "friendly"};
	data["entities"][0]["systems"]["radar"]["shows"] = {"ship", "cloud"};
	auto observe = [&]()
	{
		auto c = load_fixture(data);
		world.load(c, "test.map", {{"test.slot", "test.ship"}});
		check(world.claim(99, *world.ship_for_slot("test.slot")), "tag claim failed");
		return *world.observe(99);
	};
	auto tagged = observe();
	check(tagged.contacts.size() == 1 && tagged.regions.size() == 1 && tagged.regions[0]["name"] == "Test ring",
	      "tag union or public region marker lost");
	const auto selected = std::find_if(tagged.systems.begin(), tagged.systems.end(),
	                                   [](const auto &v)
	                                   {
		                                   return v.kind == OperatingSystem::Radar;
	                                   });
	check(selected->contact_ids.size() == 1 && selected->shows.size() == 2, "per-radar contact selection lost");
	data["entities"][0]["systems"]["radar"]["shows"] = Json::array();
	check(observe().contacts.empty(), "empty radar shows matched a contact");
	data["entities"][0]["systems"]["radar"]["shows"] = {"asteroid"};
	check(observe().contacts.empty(), "unmatched radar tags exposed a contact");
	data["entities"][0]["systems"]["radar"]["shows"] = {"ship"};
	data["entities"][1]["tags"] = Json::array();
	check(observe().contacts.empty(), "untagged entity matched radar");
}
void narrow_sensor_profiles()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "narrow sensor fixture");
	auto &map = data["maps"][0];
	map["entities"] = Json::array();
	map["ship_slots"][0]["x_mm"] = 0;
	data["entities"][0]["systems"]["radar"]["range_mm"] = 300000;
	for (const auto *shape : {"circle", "rectangle", "annulus"})
	{
		Json region = {
		    {"id", "narrow"},        {"shape", shape},         {"x_mm", 99970},
		    {"y_mm", 2454},          {"radius_mm", 1000},      {"half_width_mm", 500},
		    {"half_height_mm", 500}, {"inner_radius_mm", 500}, {"nebula", {{"density", 100}, {"sensor_factor", 100}}}};
		if (std::string(shape) == "annulus")
		{
			region["x_mm"] = 0;
			region["y_mm"] = 0;
			region["radius_mm"] = 30000;
			region["inner_radius_mm"] = 20000;
		}
		map["regions"] = Json::array({region});
		const auto catalog = load_fixture(data);
		World world;
		world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
		check(world.claim(99, *world.ship_for_slot("test.slot")), "narrow sensor claim failed");
		const auto observation = *world.observe(99);
		const auto radar = std::find_if(observation.systems.begin(), observation.systems.end(),
		                                [](const auto &s)
		                                {
			                                return s.kind == OperatingSystem::Radar;
		                                });
		check(!radar->reach_profile.empty() && radar->reach_profile.size() <= 1024 &&
		          radar->reach_profile.back().end_mdeg == 360000,
		      "adaptive profile does not cover full bearing range");
		const auto &regions = catalog.scenarios.at("test.map").regions;
		std::size_t index = 0;
		for (std::int64_t angle = 0; angle < 360000; ++angle)
		{
			while (angle >= radar->reach_profile[index].end_mdeg)
				++index;
			const auto reach = radar->reach_profile[index].range_mm;
			const double a = angle * 3.14159265358979323846 / 180000;
			const ScenarioPoint target{static_cast<std::int64_t>(std::cos(a) * reach),
			                           static_cast<std::int64_t>(std::sin(a) * reach)};
			check(scenario_sensor_visible(regions, {0, 0}, target, radar->effective_range),
			      "adaptive sensor profile exceeds true reach between bearings");
		}
		const auto at = std::find_if(radar->reach_profile.begin(), radar->reach_profile.end(),
		                             [](const auto &s)
		                             {
			                             return s.end_mdeg > 1406;
		                             });
		check(at->range_mm < radar->effective_range / 2, "adaptive profile missed narrow blocker or annulus shell");
	}
}
void asteroid_streaming()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "field fixture");
	// Streamed inert rocks have no fitted machinery; ordinary combat sensing must still be safe.
	data["entities"][0]["systems"]["radar"]["shows"] = {"contact", "asteroid", "ship"};
	data["entities"][0]["systems"]["radar"]["broad_capabilities"] = {{"basic_identity", 0}, {"contact_arcs", 0}};
	const Json frame{{"name", "front"}, {"rect", {0, 0, 1, 1}}, {"pivot", {.5, .5}}};
	const Json sprite{{"albedo", "test-core:assets/pixel.png"},
	                  {"entries", Json::array({{{"name", "first"}, {"frames", Json::array({frame})}},
	                                           {{"name", "second"}, {"frames", Json::array({frame})}}})}};
	data["entities"].push_back({{"id", "test.rock"},
	                            {"render", {{"mode", "camera"}, {"scale", 2}, {"sprite", sprite}}},
	                            {"systems", Json::object()},
	                            {"collision", {{"shape", "circle"}, {"radius_mm", 500}, {"mass_grams", 0}}}});
	auto &map = data["maps"][0];
	map["regions"] = Json::array(
	    {{{"id", "belt"},
	      {"shape", "annulus"},
	      {"x_mm", 0},
	      {"y_mm", 0},
	      {"radius_mm", 40000},
	      {"inner_radius_mm", 20000},
	      {"asteroids",
	       {{"seed", 91},
	        {"spacing_mm", 10000},
	        {"jitter_mm", 0},
	        {"density", .01},
	        {"clustering_permille", 0},
	        {"visual_distance_mm", 50000},
	        {"decorative_fill_permille", 1000},
	        {"types", Json::array({{{"definition", "test.rock"}, {"sprite_entry", "second"}}})},
	        {"decorative_types", Json::array({{{"definition", "test.rock"}, {"sprite_entry", "first"}}})}}}}});
	const auto catalog = load_fixture(data);
	const auto compiled = compile_sprite(sprite, catalog, "test sprite");
	check(sprite_entry_index(compiled, "second") == 1 && compiled.entries.size() == 2, "generic sprite entries lost");
	World first, second;
	for (auto *w : {&first, &second})
		w->load(catalog, "test.map", {{"test.slot", "test.ship"}});
	check(first.save() == second.save(), "field generation differs between identical worlds");
	std::uint64_t fingerprint = 1469598103934665603ULL;
	for (const unsigned char c : first.save().at("asteroid_fields").dump())
		fingerprint = (fingerprint ^ c) * 1099511628211ULL;
	std::cout << "Field deterministic fingerprint: " << fingerprint << '\n';
	const auto scene = first.scene();
	std::size_t physical = 0, decorative = 0;
	for (const auto &b : scene)
		if (b.field_instance)
		{
			check(b.rotation_mdeg >= 0 && b.rotation_mdeg < 360000 && std::abs(b.spin_mdeg_s) >= 1000,
			      "field rotation parameters invalid");
			check(b.x * b.x + b.y * b.y >= 20000LL * 20000, "rock generated in empty ring hole");
			if (b.visual_height_mm)
			{
				++decorative;
				check(b.sprite_entry == 0, "decorative entry selection lost");
			}
			else
			{
				++physical;
				check(b.sprite_entry == 1, "physical entry selection lost");
			}
		}
	check(physical > 0 && decorative == physical * 2, "field layers not generated");
	check(first.inspect().size() == physical + 2, "decorative rock leaked into physical world");
	for (int i = 0; i < 4; ++i)
		first.tick();
	second.restore(catalog, first.save());
	for (int i = 0; i < 4; ++i)
	{
		first.tick();
		second.tick();
	}
	check(first.save() == second.save(), "field recovery changed continuation");
	const auto home = first.save();
	auto away = home;
	for (auto &b : away["bodies"])
		b["body"]["position"]["x"] = b["body"]["position"]["x"].get<std::int64_t>() + 1000000;
	first.restore(catalog, away);
	first.tick();
	auto unloaded = first.save();
	check(std::none_of(unloaded["asteroid_fields"].begin(), unloaded["asteroid_fields"].end(),
	                   [](const auto &v)
	                   {
		                   return v.at("loaded").template get<bool>();
	                   }),
	      "distant fields failed to unload");
	for (auto &b : unloaded["bodies"])
		b["body"]["position"]["x"] = b["body"]["position"]["x"].get<std::int64_t>() - 1000000;
	first.restore(catalog, unloaded);
	first.tick();
	check(first.save()["asteroid_fields"] == home["asteroid_fields"],
	      "unload and revisit changed stable asteroid state");
	auto split = home;
	split["bodies"][0]["body"]["position"]["x"] = 1000000;
	first.restore(catalog, split);
	first.tick();
	check(first.scene().size() >= physical + decorative, "separated ships lost the other ship's field neighborhood");
	auto overlap = data;
	auto duplicate = overlap["maps"][0]["regions"][0];
	duplicate["id"] = "overlap";
	overlap["maps"][0]["regions"].push_back(duplicate);
	auto overlapping = load_fixture(overlap);
	first.load(overlapping, "test.map", {{"test.slot", "test.ship"}});
	check(first.scene().size() == scene.size(), "overlapping fields duplicated rocks");
	auto bad = data;
	bad["maps"][0]["regions"][0]["asteroids"]["types"][0]["sprite_entry"] = "missing";
	bool rejected = false;
	try
	{
		load_fixture(bad);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	check(rejected, "invalid field entry accepted");
}
void field_collision_safety()
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "field collision fixture");
	data["entities"][0]["systems"]["engine"] = {{"max_speed_mm_s", 4000000}, {"acceleration_mm_s2", 80000000}};
	data["entities"][0]["systems"]["radar"]["range_mm"] = 1000;
	data["entities"].push_back(
	    {{"id", "test.rock"},
	     {"render",
	      {{"mode", "camera"},
	       {"scale", 2},
	       {"sprite",
	        {{"albedo", "test-core:assets/pixel.png"},
	         {"frames", Json::array({{{"name", "front"}, {"rect", {0, 0, 1, 1}}, {"pivot", {.5, .5}}}})}}}}},
	     {"systems", Json::object()},
	     {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 0}}}});
	auto &map = data["maps"][0];
	map["entities"] = Json::array();
	map["ship_slots"][0]["x_mm"] = 0;
	map["ship_slots"][0]["y_mm"] = 5000;
	map["regions"] = Json::array({{{"id", "rock-cell"},
	                               {"shape", "rectangle"},
	                               {"x_mm", 105000},
	                               {"y_mm", 5000},
	                               {"half_width_mm", 1000},
	                               {"half_height_mm", 1000},
	                               {"asteroids",
	                                {{"spacing_mm", 10000},
	                                 {"jitter_mm", 0},
	                                 {"density", 1},
	                                 {"clustering_permille", 0},
	                                 {"visual_distance_mm", 1000},
	                                 {"types", Json::array({{{"definition", "test.rock"}}})}}}}});
	auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	const auto ship = *world.ship_for_slot("test.slot");
	check(world.inspect().size() == 1, "small distant rock should begin outside streaming window");
	check(world.claim(99, ship) && world.submit(99, {ship, "speed_mm_s", 4000000}), "high-speed field request refused");
	world.tick();
	const auto bodies = world.inspect();
	const auto own = std::find_if(bodies.begin(), bodies.end(),
	                              [&](const auto &b)
	                              {
		                              return b.id == ship;
	                              });
	check(bodies.size() == 2 && own->x > 100000 && own->x < 105000 && own->vx < 4000000,
	      "new propulsion velocity tunneled through unstreamed field rock");
	for (bool box : {false, true})
	{
		data["entities"].back()["collision"] =
		    box ? Json{{"shape", "box"}, {"half_width_mm", 110000}, {"half_length_mm", 1000}, {"mass_grams", 0}}
		        : Json{{"shape", "circle"}, {"radius_mm", 110000}, {"mass_grams", 0}};
		catalog = load_fixture(data);
		world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
		check(world.inspect().size() == 2, "large field collider surface intersects ship but rock was not activated");
	}

	data["entities"].back()["collision"] = {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 1000}};
	data["entities"][0]["systems"]["steering"]["turn_mdeg_s"] = 360000;
	catalog = load_fixture(data);
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	const auto moving_ship = *world.ship_for_slot("test.slot");
	check(world.claim(99, moving_ship) && world.submit(99, {moving_ship, "speed_mm_s", 4000000}),
	      "moving-rock drive request refused");
	world.tick();
	auto changed = world.save().at("asteroid_fields").at(0);
	check(changed.at("vx").get<std::int64_t>() > 0, "collision did not mutate generated rock motion");
	check(world.submit(99, {moving_ship, "heading_mdeg", 90000}), "departure heading refused");
	bool unloaded = false;
	for (int tick = 0; tick < 150; ++tick)
	{
		world.tick();
		changed = world.save().at("asteroid_fields").at(0);
		if (!changed.at("loaded").get<bool>())
		{
			unloaded = true;
			break;
		}
	}
	check(unloaded, "collision-mutated moving rock failed to unload during actual ship departure");
	World resumed;
	resumed.restore(catalog, world.save());
	check(resumed.save().at("asteroid_fields") == world.save().at("asteroid_fields"),
	      "unloaded physical state changed during recovery");
	resumed.clear_claims();
	check(resumed.claim(99, moving_ship), "recovered moving-rock claim refused");
	bool revisited = false;
	for (int tick = 0; tick < 300; ++tick)
	{
		const auto bodies = world.inspect();
		const auto own = std::find_if(bodies.begin(), bodies.end(),
		                              [&](const auto &b)
		                              {
			                              return b.id == moving_ship;
		                              });
		const double dx = changed.at("x").get<std::int64_t>() - own->x,
		             dy = changed.at("y").get<std::int64_t>() - own->y;
		const auto heading =
		    static_cast<std::int64_t>(std::fmod(std::atan2(dy, dx) * 180000 / 3.14159265358979323846 + 360000, 360000));
		const auto speed = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::hypot(dx, dy) * 5), 100000, 4000000);
		for (auto *w : {&world, &resumed})
		{
			check(w->submit(99, {moving_ship, "heading_mdeg", heading}) &&
			          w->submit(99, {moving_ship, "speed_mm_s", speed}),
			      "revisit command refused");
			w->tick();
		}
		const auto state = world.save().at("asteroid_fields");
		check(state == resumed.save().at("asteroid_fields"), "moving-rock continuation diverged after recovery");
		if (state.at(0).at("loaded").get<bool>())
		{
			check(state[0]["id"] == changed["id"] && state[0]["entry"] == changed["entry"] &&
			          state[0]["spin"] == changed["spin"] && state[0]["vx"] == changed["vx"],
			      "revisit regenerated a collision-mutated rock");
			revisited = true;
			break;
		}
	}
	check(revisited, "actual return flight failed to revisit unloaded rock");
}
void competition_resolution()
{
	Json map = {{"regions",
	             Json::array({{{"id", "finish"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 100}}})}};
	for (int actor = 1; actor <= 2; ++actor)
		map["scenario"]["objectives"].push_back({{"id", "race-" + std::to_string(actor)},
		                                         {"type", "reach_region"},
		                                         {"region", "finish"},
		                                         {"competition", "race"},
		                                         {"actor", {{"entity", actor}}},
		                                         {"recipients", {{"ship", {{"entity", actor}}}}},
		                                         {"title_key", "Finish"}});
	for (const bool tie : {false, true})
	{
		ScenarioRuntime runtime(compile_scenario(map, {}));
		const auto instance = runtime.create_instance("race");
		runtime.begin_tick(100, {}, {{1, {-1000, 0}}, {2, {-1000, 0}}});
		for (int actor = 1; actor <= 2; ++actor)
			runtime.add_objective({instance, 1, static_cast<std::uint32_t>(actor)}, "race-" + std::to_string(actor),
			                      "ship-" + std::to_string(actor));
		ScenarioRuntime restored(compile_scenario(map, {}));
		restored.restore(runtime.save());
		const std::vector<ScenarioMotionSegment> movement{{1, {-1000, 0}, {1000, 0}},
		                                                  {2, {-1000, 0}, {tie ? 1000 : -500, 0}}};
		for (auto *r : {&runtime, &restored})
		{
			r->evaluate(movement);
			check(r->mission_complete(), "Race did not finish after swept arrival");
			for (const auto &[id, objective] : r->objectives())
				check(objective.outcome == (tie                           ? "draw"
				                            : objective.actor.entity == 1 ? "won"
				                                                          : "lost"),
				      "Race result depends on iteration order");
		}
		check(runtime.save() == restored.save(), "Race recovery diverged");
		const auto count = runtime.pending_publications().size();
		runtime.evaluate(movement);
		check(count == runtime.pending_publications().size(), "Race result was published twice");
	}
}
void delayed_competition()
{
	auto data = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "delayed race fixture");
	auto &map = data["maps"][0];
	map["entities"] = Json::array();
	map["ship_slots"][0]["x_mm"] = -10000;
	map["ship_slots"].push_back(
	    {{"id", "other"}, {"allowed_ships", {"test.ship"}}, {"x_mm", -10000}, {"y_mm", 20000}, {"heading_mdeg", 0}});
	map["regions"] =
	    Json::array({{{"id", "finish"}, {"shape", "circle"}, {"x_mm", 10000}, {"y_mm", 0}, {"radius_mm", 1000}}});
	map["scenario"] = {{"tree", "test.race-a"},
	                   {"bindings", {{"actor", {{"slot", "test.slot"}}}}},
	                   {"situations", Json::array({{{"id", "other"},
	                                                {"tree", "test.race-b"},
	                                                {"bindings", {{"actor", {{"slot", "other"}}}}},
	                                                {"start", true}}})}};
	for (const auto &[name, slot] :
	     std::vector<std::pair<std::string, std::string>>{{"a", "test.slot"}, {"b", "other"}})
	{
		map["scenario"]["objectives"].push_back({{"id", name},
		                                         {"type", "reach_region"},
		                                         {"region", "finish"},
		                                         {"competition", "race"},
		                                         {"actor", {{"slot", slot}}},
		                                         {"recipients", {{"ship", {{"slot", slot}}}}},
		                                         {"title_key", "Finish"}});
		data["trees"].push_back(
		    {{"id", "test.race-" + name},
		     {"context", "scenario"},
		     {"initial", "wait"},
		     {"states",
		      Json::array({{{"id", "wait"},
		                    {"transitions",
		                     Json::array({{{"target", "run"},
		                                   {"conditions", Json::array({{{"left", {{"observation", "scenario.elapsed"}}},
		                                                                {"op", "ge"},
		                                                                {"right", 100}}})}}})}},
		                   {{"id", "run"},
		                    {"entry", Json::array({action(
		                                  "add_objective",
		                                  Json::array({name, {{"observation", "self.entity"}}, "finish"}))})}}})}});
	}
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}, {"other", "test.ship"}});
	for (int i = 0; i < 100; ++i)
		world.tick();
	check(world.save().at("scenario").at("runtime").at("objectives").empty(),
	      "race objective appeared before five seconds");
	World resumed;
	resumed.restore(catalog, world.save());
	world.tick();
	resumed.tick();
	check(world.save() == resumed.save(), "five-second delay changed after recovery");
	const auto objectives = world.save().at("scenario").at("runtime").at("objectives");
	check(objectives.size() == 2 && objectives[0].at("created") == 100 && objectives[1].at("created") == 100,
	      "both race objectives must activate at five seconds");
}
Catalog fixture(bool fitted = false, bool models = false)
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic World scenario");
	data["entities"][0]["systems"]["engine"] = {{"max_speed_mm_s", 100000}, {"acceleration_mm_s2", 2000000}};
	if (fitted)
	{
		data["entities"][0]["systems"]["engine"]["kind"] = "engine";
		data["entities"][0]["systems"]["engine"]["power_store"] = "cell";
		data["entities"][0]["systems"]["engine"]["power_rate"] = 20;
		data["entities"][0]["resources"] = Json::parse(
		    R"({"stores":[{"id":"cell","capacity":1000,"initial":1000,"charge_rate":100,"discharge_rate":100}]})");
	}
	auto &map = data["maps"][0];
	map["entities"] = Json::array(
	    {{{"id", 1}, {"definition", "test.variant"}, {"x_mm", 1000000}, {"y_mm", 20000}, {"heading_mdeg", 0}}});
	map["ship_slots"][0]["x_mm"] = -2000;
	map["regions"] =
	    Json::array({{{"id", "destination"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 100}}});
	map["scenario"] = {
	    {"tree", "test.scenario"},
	    {"bindings", {{"actor", {{"slot", "test.slot"}}}}},
	    {"parameters", {{"spawn_y", 10000}}},
	    {"objectives", Json::array({{{"id", "reach"},
	                                 {"type", "reach_region"},
	                                 {"region", "destination"},
	                                 {"actor", {{"slot", "test.slot"}}},
	                                 {"recipients", {{"ship", {{"slot", "test.slot"}}}}},
	                                 {"title_key", "test.objective.reach"}}})},
	    {"situations",
	     Json::array({{{"id", "upper-traffic"}, {"tree", "test.spawner"}, {"parameters", {{"spawn_y", 10000}}}},
	                  {{"id", "lower-traffic"}, {"tree", "test.spawner"}, {"parameters", {{"spawn_y", -10000}}}}})}};
	data["trees"] = Json::array(
	    {{{"id", "test.scenario"},
	      {"context", "scenario"},
	      {"initial", "travelling"},
	      {"states",
	       Json::array(
	           {{{"id", "travelling"},
	             {"entry", Json::array({action("start_situation", Json::array({"upper-traffic", "upper"})),
	                                    action("start_situation", Json::array({"lower-traffic", "lower"})),
	                                    action("add_objective",
	                                           Json::array({"reach", {{"observation", "self.entity"}}, "arrival"}))})},
	             {"transitions",
	              Json::array({{{"target", "complete"},
	                            {"conditions", Json::array({{{"left", {{"observation", "objective.state"}}},
	                                                         {"op", "eq"},
	                                                         {"right", 2}}})}}})}},
	            {{"id", "complete"},
	             {"entry", Json::array({action("finish_scenario", Json::array({true, "arrived"}))})}}})}},
	     {{"id", "test.spawner"},
	      {"context", "scenario"},
	      {"parameters", {{"spawn_y", {{"type", "integer"}, {"default", 10000}}}}},
	      {"initial", "spawn"},
	      {"states",
	       Json::array(
	           {{{"id", "spawn"},
	             {"running",
	              Json::array({action(
	                  "spawn_entity",
	                  Json::array({"test.variant", "", 50000, {{"parameter", "spawn_y"}}, 0, "traffic"}))})}}})}}});
	if (models)
	{
		for (auto &entity : data["entities"])
			entity["render"] = {
			    {"scale", 1},
			    {"radar_icon", "ship"},
			    {"model", {{"asset", "test-core:assets/triangle.glb"}}},
			    {"attachments", {{"engine_test", {{"position", {0, 0, 0}}, {"direction", {0, 0, 1}}}}}}};
		auto pack = phoenix::test::fixture_pack(json_bytes(data), "test-core");
		pack.manifest["base"] = {{"render_mode", "models"}};
		pack.files["assets/triangle.glb"] = phoenix::test::model_fixture();
		return resolve_packs({read_pack(write_pack(pack))});
	}
	return load_fixture(data);
}
Json reader_fixture()
{
	auto data =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "scoped scenario record reader");
	data["schema"] = 4;
	data["consoles"][0]["widgets"] = Json::array();
	auto &ship = data["entities"][0];
	ship.erase("console");
	ship["stations"] = Json::parse(R"json([
		{"id":"bridge","consoles":[
			{"id":"main","definition":"test.console","systems":["engine","steering"],"knowledge_roles":["test.reader"]},
			{"id":"private","definition":"test.console","systems":["radar"],"knowledge_roles":["test.private"]}
		]}
	])json");
	data["knowledge"] = Json::parse(R"json({"roles":["test.reader","test.private"],"schemas":[
		{"id":"test.signal","producers":["phoenix.scenario"],"readers":["test.reader"],"ship_publishable":true,"stale_after_ticks":1,
		 "fields":[{"id":"amount","type":"integer","publishable":true},{"id":"flag","type":"boolean","publishable":true},{"id":"detail","type":"text","publishable":true}]}
	]})json");
	data["maps"][0]["scenario"] = Json::parse(R"json({
		"tree":"test.main-reader","bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"},
		"publications":["test.signal"],"situations":[
			{"id":"publisher","tree":"test.publisher","bindings":{"actor":{"slot":"test.slot"}}},
			{"id":"second","tree":"test.reader","start":true,"bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"}},
			{"id":"other-ship","tree":"test.reader","start":true,"bindings":{"actor":{"entity":1},"knowledge_console":"main"}},
			{"id":"expiry","tree":"test.expiry","start":true,"bindings":{"actor":{"slot":"test.slot"},"knowledge_console":"main"}}
		]
	})json");
	data["trees"] = Json::parse(R"json([
		{"id":"test.reader","context":"scenario","initial":"wait","record_bindings":[
			{"name":"known.amount","schema":"test.signal","field":"amount","scope":"ship"},
			{"name":"known.flag","schema":"test.signal","field":"flag","scope":"ship"},
			{"name":"known.detail","schema":"test.signal","field":"detail","scope":"ship"},
			{"name":"known.console","schema":"test.signal","field":"amount","scope":"console"},
			{"name":"known.other","schema":"test.signal","field":"amount","scope":"ship","subject":1}
		],"states":[
			{"id":"wait","transitions":[{"target":"done","conditions":[
				{"left":{"observation":"known.amount"},"op":"eq","right":7},
				{"left":{"observation":"known.flag"},"op":"eq","right":true},
				{"left":{"observation":"known.detail"},"op":"eq","right":"ready"}
			]}]},
			{"id":"done","entry":[{"action":"finish_scenario","arguments":[true,"read permitted evidence"]}]}
		]},
		{"id":"test.main-reader","extends":"test.reader","overrides":[{"state":"wait","entry":[{"action":"start_situation","arguments":["publisher","publisher-instance"]}]}]},
		{"id":"test.publisher","context":"scenario","initial":"publish","states":[{"id":"publish","entry":[
			{"action":"publish_scenario_message","arguments":["test.signal",{"observation":"self.entity"},7,true,"ready"]}
		]}]},
		{"id":"test.expiry","extends":"test.reader","initial":"wait","overrides":[
			{"state":"wait","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]}]}
		],"states":[
			{"id":"wait","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]}]},
			{"id":"observed","transitions":[{"target":"observed","conditions":[{"left":{"observation":"known.amount"},"op":"present"}]},{"target":"done"}]},
			{"id":"done","entry":[{"action":"finish_scenario","arguments":[true,"stale evidence removed"]}]}
		]}
	])json");
	auto referenced_reader = data["trees"][0];
	referenced_reader["id"] = "test.read-signal";
	data["trees"].push_back(std::move(referenced_reader));
	data["trees"][0]["states"][0].erase("transitions");
	data["trees"][0]["states"][0]["running"] = Json::array({{{"call", "test.read-signal"}}});
	return data;
}
void spawned_equipment_continuation(bool models = false)
{
	const auto catalog = fixture(true, models);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	world.tick();
	world.tick();
	check(world.inspect().size() == 4 && world.scene().size() == 4,
	      "Scenario spawns must create bodies with equipment while keeping child identities private");
	std::vector<EntityId> spawned;
	const auto player = *world.ship_for_slot("test.slot");
	for (const auto &body : world.inspect())
		if (body.id != 1 && body.id != player)
			spawned.push_back(body.id);
	check(spawned.size() == 2, "Reusable situations did not create two independently fitted ships");
	for (std::size_t i = 0; i < spawned.size(); ++i)
	{
		check(world.claim(200 + i, spawned[i]), "Dynamically created equipment has no usable Console binding");
		check(world.submit(200 + i, {spawned[i], "speed_mm_s", 2000, "engine"}),
		      "Dynamically created equipment refused an ordinary command");
	}
	world.tick();
	if (models)
	{
		const auto scene = world.scene();
		check(std::any_of(scene.begin(), scene.end(),
		                  [&](const auto &body)
		                  {
			                  return std::find(spawned.begin(), spawned.end(), body.id) != spawned.end() &&
			                         body.forward_thrust > 0;
		                  }),
		      "Dynamically spawned models expose actual paid engine thrust to presentation");
	}
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	check(resumed.save() == saved, "Spawned equipment or resource state did not round trip");
	for (int tick = 0; tick < 4; ++tick)
	{
		world.tick();
		resumed.tick();
	}
	check(world.save() == resumed.save() && world.inspect().size() == 4,
	      "Dynamic equipment must execute once and preserve continuation after recovery");
	for (const auto id : spawned)
		for (const auto &body : world.inspect())
			if (body.id == id)
				check(body.vx > 0, "Spawned engine did not execute through the production equipment query");
}
void scenario_record_readers()
{
	const auto data = reader_fixture();
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {{"test.slot", "test.ship"}});
	world.tick();
	World resumed;
	resumed.restore(catalog, world.save());
	for (int i = 0; i < 7; ++i)
	{
		world.tick();
		resumed.tick();
	}
	const auto state = world.save();
	bool main = false, second = false, other = false, expired = false;
	for (const auto &instance : state.at("scenario").at("runtime").at("instances"))
	{
		const bool success = instance.at("finished").get<bool>() && instance.at("succeeded").get<bool>();
		if (instance.at("name") == "main")
			main = success;
		if (instance.at("name") == "second")
			second = success;
		if (instance.at("name") == "other-ship")
			other = success;
		if (instance.at("name") == "expiry")
			expired = success;
	}
	check(main && second && expired && !other,
	      "Scenario readers lost typed evidence, scope/subject isolation, independent situations, or staleness");
	check(state == resumed.save(), "Scoped scenario readers diverged after World recovery");
	auto refused = [&](Json invalid, const char *message)
	{
		bool failed = false;
		try
		{
			(void)load_fixture(invalid);
		}
		catch (const std::exception &)
		{
			failed = true;
		}
		check(failed, message);
	};
	auto invalid = data;
	invalid["maps"][0]["scenario"]["bindings"].erase("knowledge_console");
	refused(invalid, "Scenario record reader without explicit Console was admitted");
	invalid = data;
	invalid["maps"][0]["scenario"]["bindings"]["knowledge_console"] = "private";
	refused(invalid, "Scenario record reader bypassed selected Console roles");
	invalid = data;
	invalid["maps"][0]["scenario"]["bindings"]["knowledge_console"] = "missing";
	refused(invalid, "Scenario record reader selected nonexistent Console");
	invalid = data;
	invalid["maps"][0]["scenario"]["situations"][1]["bindings"].erase("knowledge_console");
	refused(invalid, "Reusable situation omitted its explicit record reader");
	auto dynamic = data;
	dynamic["entities"].push_back(
	    {{"id", "test.unrelated-private"},
	     {"extends", "test.ship"},
	     {"stations",
	      Json::parse(
	          R"json([{"id":"bridge","consoles":[{"id":"main","definition":"test.console","systems":["engine","steering","radar"],"knowledge_roles":["test.private"]}]}])json")}});
	dynamic["maps"][0]["scenario"]["bindings"]["actor"] = {{"handle", "traffic"}};
	(void)load_fixture(dynamic);
	// Typed ingress also suppresses an older live record after the latest stream
	// record was withdrawn. This uses a compiled production reader and real keys.
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	const auto bodies = world.inspect();
	bridge.begin_tick(10, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	const auto readers = bridge.knowledge_readers();
	check(!readers.empty(), "Compiled scenario did not request scoped evidence");
	KnowledgeRecord live;
	live.value.schema = catalog.knowledge_definition.schema("test.signal");
	live.value.subject = readers[0].ship;
	live.value.provenance.observed_tick = 10;
	live.value.provenance.revision = 1;
	live.value.fields[catalog.knowledge_definition.field(live.value.schema, "amount")] = std::int64_t{7};
	auto withdrawn = live;
	withdrawn.value.withdrawn = true;
	withdrawn.value.provenance.revision = 2;
	std::vector<ScenarioKnowledgeSnapshot> snapshots{{readers[0], {}, {live}}};
	bridge.supply_knowledge(snapshots);
	snapshots[0].shared[0].value.fields.begin()->second = std::int64_t{99};
	const auto frozen = bridge.observations(readers[0].instance);
	const auto offset = static_cast<std::size_t>(GameplayTreeObservation::Count);
	check(frozen.at(offset).number == 7 && frozen.at(offset + 3).type == state_tree::Type::Missing &&
	          frozen.at(offset + 4).type == state_tree::Type::Missing,
	      "Scenario snapshot was mutable or ignored record scope/subject filters");
	bool replaced = false;
	try
	{
		bridge.supply_knowledge(snapshots);
	}
	catch (const std::exception &)
	{
		replaced = true;
	}
	check(replaced, "Scenario evidence was replaced within one immutable tick");
	bridge.begin_tick(11, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	snapshots[0].shared = {live, withdrawn};
	bridge.supply_knowledge(snapshots);
	const auto values = bridge.observations(readers[0].instance);
	check(values.at(static_cast<std::size_t>(GameplayTreeObservation::Count)).type == state_tree::Type::Missing,
	      "Withdrawn latest scenario evidence resurrected older history");
	bridge.begin_tick(12, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	auto independent = live;
	independent.value.provenance.source = 99;
	independent.value.provenance.observed_tick = 12;
	withdrawn.value.provenance.observed_tick = 12;
	snapshots[0].shared = {independent, withdrawn};
	bridge.supply_knowledge(snapshots);
	check(bridge.observations(readers[0].instance).at(offset).number == 7,
	      "Withdrawing one source suppressed an independent live scenario evidence stream");
	bridge.begin_tick(13, {{"test.slot", *world.ship_for_slot("test.slot")}}, bodies);
	snapshots[0].shared = {live};
	bridge.supply_knowledge(snapshots);
	check(bridge.observations(readers[0].instance).at(offset).type == state_tree::Type::Missing,
	      "Scenario stale evidence remained available");
}
void independent_shield_world()
{
	auto data = phoenix::test::combat_fixture();
	data["entities"][0]["stations"][0]["consoles"][0]["shield_feed"] = true;
	data["effects"] = Json::parse(R"json([{"id":"test.blessing","kind":"shield","duration_ticks":5,
  "contributions":{"bubble":{"capacity":10,"initial":7,"sustain_energy_per_second":0,
   "recharge_per_second":1,"off_recharge_per_second":0,"warmup_ticks":1,
   "modifiers":{"recharge":500,"protection":-500}}}}])json");
	data["maps"][0]["scenario"] = {{"tree", "test.blessing-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	data["trees"].push_back(Json::parse(R"json({"id":"test.blessing-tree","context":"scenario","initial":"apply",
  "states":[{"id":"apply","entry":[
    {"action":"apply_shield_effect","arguments":[{"observation":"self.entity"},"test.blessing","first"]},
    {"action":"apply_shield_effect","arguments":[{"observation":"self.entity"},"test.blessing","second"]}],
    "transitions":[{"target":"refresh","conditions":[{"left":{"observation":"tick"},"op":"ge","right":2}]}]},
   {"id":"refresh","entry":[{"action":"refresh_shield_effect","arguments":[{"observation":"self.entity"},"first"]}],
    "transitions":[{"target":"remove","conditions":[{"left":{"observation":"tick"},"op":"ge","right":4}]}]},
   {"id":"remove","entry":[{"action":"remove_shield_effect","arguments":[{"observation":"self.entity"},"second"]}]}]})json"));
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {});
	check(world.claim(900, 1, "drive"), "Cannot claim shield test ship");
	world.tick();
	auto saved = world.save();
	auto &contributions = saved["bodies"][0]["shields"]["contributions"];
	check(contributions.size() == 2, "Production scenario failed to grant first shields to an unfitted ship");
	check(contributions[0]["quoted_rate"] == 1500 && contributions[0]["protection"] == 500 &&
	          contributions[0]["quoted_sustain"] == 0,
	      "Independent shield process ignored named modifiers or invented an equipment payer");
	const auto first_id = contributions[0].at("effect_id");
	check(contributions[0].at("expiry") == 5, "Initial scenario grant expiry not based on processing tick zero");
	// Resume a depleted reserve, then let the production action refresh it.
	for (auto &c : contributions)
		c["state"]["strength"] = 3;
	World resumed;
	resumed.restore(catalog, saved);
	resumed.clear_claims();
	check(resumed.claim(900, 1, "drive"), "Cannot reclaim temporary shield observation");
	check(resumed.claim(901, 1, "observer"), "Cannot claim shield-restricted observer");
	// World evaluates scenario work at the current tick, then advances its clock.
	// Processing ticks 1 and 2 reaches the authored refresh condition tick >= 2.
	resumed.tick();
	resumed.tick();
	const auto refreshed = resumed.save()["bodies"][0]["shields"]["contributions"];
	check(refreshed.size() == 2 && refreshed[0].at("effect_id") == first_id && refreshed[0]["state"]["strength"] == 3,
	      "Refreshing a scenario shield duplicated or refilled its reserve");
	check(refreshed[0].at("expiry") == 7 && refreshed[1].at("expiry") == 5,
	      "Scenario refresh did not selectively extend the named grant at tick two");
	const auto observed = resumed.observe(900)->gameplay.at("combat").at("shields");
	check(observed.size() == 2 && !observed[0].at("controllable").get<bool>(),
	      "Temporary shield missing from common observation or granted control implicitly");
	check(!observed[0].contains("source") && !observed[0].contains("definition"),
	      "Temporary shield projection leaked source bookkeeping");
	check(resumed.observe(901)->gameplay.at("combat").at("shields").empty(),
	      "Console without authored shield feed observed temporary grants");
	for (int i = 0; i < 2; ++i)
		resumed.tick();
	check(resumed.save()["bodies"][0]["shields"]["contributions"].size() == 1,
	      "Selective scenario removal affected an independent grant");
	World continued;
	continued.restore(catalog, resumed.save());
	resumed.clear_claims();
	continued.clear_claims();
	for (int i = 0; i < 5; ++i)
	{
		resumed.tick();
		continued.tick();
	}
	check(resumed.save()["bodies"][0]["shields"]["contributions"].empty(), "Finite shield grant did not expire");
	check(resumed.save() == continued.save(), "Scenario grant expiry replayed or diverged after restore");
}
void shield_action_receipts()
{
	const auto catalog = fixture();
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	const auto instance = bridge.controllers().begin()->first;
	const ScenarioActionToken token{instance, 500, 0};
	std::vector<state_tree::Value> args{{state_tree::Type::Entity, 1, {}},
	                                    {state_tree::Type::Text, 0, "test.temporary-shield"},
	                                    {state_tree::Type::Text, 0, "blessing"}};
	const auto receipt = bridge.execute(token, GameplayTreeAction::ApplyShieldEffect, args);
	check(receipt.result.status == ScenarioActionStatus::Pending && bridge.pending_effects().size() == 1,
	      "Scenario shield action did not produce physical pending work");
	bridge.execute(token, GameplayTreeAction::ApplyShieldEffect, args);
	check(bridge.pending_effects().size() == 1, "Replay duplicated pending shield work");
	GameplayScenarioBridge resumed;
	resumed.restore(catalog, "test.map", bridge.save());
	check(resumed.pending_effects().size() == 1, "Pending shield request lost across resume");
	resumed.confirm_effect(token);
	check(resumed.receipt(token)->result.status == ScenarioActionStatus::Succeeded,
	      "Physical grant confirmation missing");
	GameplayScenarioBridge committed;
	committed.restore(catalog, "test.map", resumed.save());
	committed.execute(token, GameplayTreeAction::ApplyShieldEffect, args);
	check(committed.pending_effects().empty(), "Committed shield creation replayed on resume");
	const ScenarioActionToken refresh{instance, 501, 0};
	committed.execute(refresh, GameplayTreeAction::RefreshShieldEffect,
	                  {{state_tree::Type::Entity, 1, {}}, {state_tree::Type::Text, 0, "blessing"}});
	committed.confirm_effect(refresh, "target removed");
	check(committed.receipt(refresh)->result.status == ScenarioActionStatus::Failed,
	      "Physical failure was not retained");
}
void independent_modifier_world()
{
	auto data = phoenix::test::combat_fixture();
	data["effects"] = Json::parse(R"json([
 {"id":"test.range","kind":"modifier","duration_ticks":5,
  "contributions":{"first":{"property":"sensors","delta_permille":100},"second":{"property":"sensors","delta_permille":150}}},
 {"id":"test.incompatible","kind":"modifier","duration_ticks":5,
  "contributions":{"range":{"property":"sensors","delta_permille":1000},
                   "cycle":{"property":"cycling","delta_permille":1000}}}])json");
	data["maps"][0]["scenario"] = {{"tree", "test.modifier-tree"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	data["trees"].push_back(Json::parse(R"json({"id":"test.modifier-tree","context":"scenario","initial":"apply",
  "states":[{"id":"apply","entry":[
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"radar","test.range","first"]},
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"radar","test.range","first"]},
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"radar","test.range","second"]},
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"radar","test.incompatible","bad"]}],
    "transitions":[{"target":"refresh","conditions":[{"left":{"observation":"tick"},"op":"ge","right":2}]}]},
   {"id":"refresh","entry":[
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"radar","test.range","first"]},
    {"action":"refresh_modifier_effect","arguments":[{"observation":"self.entity"},"radar","first"]}],
    "transitions":[{"target":"remove","conditions":[{"left":{"observation":"tick"},"op":"ge","right":4}]}]},
   {"id":"remove","entry":[{"action":"remove_modifier_effect","arguments":[{"observation":"self.entity"},"radar","second"]}]}]})json"));
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {});
	check(world.claim(900, 1, "drive"), "Cannot claim modifier test ship");
	const auto range = [](const World &current)
	{
		const auto view = current.observe(900);
		const auto radar = std::find_if(view->systems.begin(), view->systems.end(),
		                                [](const auto &system)
		                                {
			                                return system.kind == OperatingSystem::Radar;
		                                });
		check(radar != view->systems.end(), "Modifier test radar projection missing");
		return radar->effective_range;
	};
	const auto body_count = world.inspect().size();
	world.tick();
	const auto applications = phoenix::test::modifier_applications(world.save(), 1);
	check(applications.size() == 2 && world.inspect().size() == body_count,
	      "Each multi-property source has one independent application and creates no scene body");
	const auto before_invalid_restore = world.save();
	for (const auto *mutation : {"duplicate", "owner", "equipment", "missing-owner", "extra", "old-schema"})
	{
		auto invalid = before_invalid_restore;
		auto &effects = invalid["active_effects"];
		const std::string kind = mutation;
		if (kind == "duplicate")
			effects["modifiers"].push_back(effects["modifiers"][0]);
		if (kind == "owner")
			effects["modifiers"][0]["owner"] = 999;
		if (kind == "equipment")
			effects["modifiers"][0]["equipment"] = "missing";
		if (kind == "missing-owner")
			effects["modifier_owners"].erase(0);
		if (kind == "extra")
			effects["modifiers"][0]["unexpected"] = 1;
		if (kind == "old-schema")
			invalid["schema"] = 26;
		bool rejected = false;
		try
		{
			world.restore(catalog, invalid);
		}
		catch (const std::exception &)
		{
			rejected = true;
		}
		check(rejected && world.save() == before_invalid_restore,
		      "Malformed effect restoration must leave the live world unchanged");
	}
	check(range(world) == 300000,
	      "Scenario modifiers did not add once, or failed multi-property apply partially committed");
	const auto saved = world.save();
	World resumed;
	resumed.restore(catalog, saved);
	// Claims are part of this saved world. Keep the same control history on both
	// sides; clearing only the restored claims introduces a Controller transition.
	check(resumed.observe(900).has_value(), "Restored modifier test ship lost its saved claim");
	for (int i = 0; i < 2; ++i)
	{
		world.tick();
		resumed.tick();
	}
	check(range(resumed) == 300000, "Same-source modifier refresh stacked a second copy");
	for (int i = 0; i < 2; ++i)
	{
		world.tick();
		resumed.tick();
	}
	check(range(resumed) == 250000, "Selective modifier removal removed an independent source");
	world.tick();
	resumed.tick();
	check(range(resumed) == 250000, "Modifier refresh failed to extend the original expiry");
	for (int i = 0; i < 3; ++i)
	{
		world.tick();
		resumed.tick();
	}
	check(range(resumed) == 200000, "Timed modifier did not expire at its extended deadline");
	if (world.save() != resumed.save())
		throw std::runtime_error("Timed modifier continuation diverged or replayed after restore: " +
		                         Json::diff(world.save(), resumed.save()).dump());
}
void modifier_target_lifetime()
{
	auto data = phoenix::test::combat_fixture();
	data["effects"] = Json::parse(R"json([
 {"id":"test.short-shield","kind":"shield","duration_ticks":3,
  "contributions":{"bubble":{"capacity":10,"initial":4,"recharge_per_second":0,
     "off_recharge_per_second":0,"sustain_energy_per_second":0,"warmup_ticks":1}}},
 {"id":"test.long-modifier","kind":"modifier","duration_ticks":20,
  "contributions":{"recharge":{"property":"recharge","delta_permille":250}}}])json");
	data["maps"][0]["scenario"] = {{"tree", "test.target-lifetime"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	data["trees"].push_back(Json::parse(R"json({"id":"test.target-lifetime","context":"scenario","initial":"apply",
  "states":[{"id":"apply","entry":[
    {"action":"apply_shield_effect","arguments":[{"observation":"self.entity"},"test.short-shield","first"]},
    {"action":"apply_shield_effect","arguments":[{"observation":"self.entity"},"test.short-shield","second"]},
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"effect:1:bubble","test.long-modifier","first"]},
    {"action":"apply_modifier_effect","arguments":[{"observation":"self.entity"},"effect:2:bubble","test.long-modifier","second"]}],
    "transitions":[{"target":"remove","conditions":[{"left":{"observation":"tick"},"op":"ge","right":1}]}]},
   {"id":"remove","entry":[{"action":"remove_shield_effect","arguments":[{"observation":"self.entity"},"first"]}]}]})json"));
	const auto catalog = load_fixture(data);
	World world;
	world.load(catalog, "test.map", {});
	world.tick();
	const auto granted = world.save();
	check(phoenix::test::modifier_applications(granted, 1).size() == 2,
	      "Modifier could not bind to independently granted shield contributions");
	world.tick();
	const auto removed = world.save();
	check(removed["bodies"][0]["shields"]["contributions"].size() == 1 &&
	          phoenix::test::modifier_applications(removed, 1).size() == 1 &&
	          phoenix::test::modifier_applications(removed, 1)[0]["equipment"] == "effect:2:bubble",
	      "Removing a shield left an orphan modifier or removed an unrelated target");
	World resumed;
	resumed.restore(catalog, removed);
	for (int i = 0; i < 2; ++i)
	{
		world.tick();
		resumed.tick();
	}
	const auto expired = world.save();
	check(expired["bodies"][0]["shields"]["contributions"].empty() &&
	          phoenix::test::modifier_applications(expired, 1).empty(),
	      "Shield expiry left an orphan property target before the modifier deadline");
	check(expired == resumed.save(), "Shield-target expiry diverged after selective-removal restore");
	World after_expiry;
	after_expiry.restore(catalog, expired);
	world.tick();
	after_expiry.tick();
	check(world.save() == after_expiry.save(), "Expired shield modifier target prevented faithful restoration");
}
void combined_shield_properties()
{
	auto data = phoenix::test::combat_fixture();
	data["effects"] = Json::parse(R"json([
 {"id":"test.shield","kind":"shield","contributions":{"main":{"capacity":100,"initial":0,
    "recharge_per_second":20,"off_recharge_per_second":0,"sustain_energy_per_second":0,"warmup_ticks":1}}},
 {"id":"test.officer-recharge","kind":"modifier",
  "contributions":{"recharge":{"property":"recharge","delta_permille":250}}},
 {"id":"test.timed-recharge","kind":"modifier","duration_ticks":4,
  "contributions":{"recharge":{"property":"recharge","delta_permille":250}}}])json");
	auto &ship = data["entities"][0];
	ship["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                              {"capacity", 100000},
	                                              {"initial", 100000},
	                                              {"charge_rate", 100000},
	                                              {"discharge_rate", 100000}}})}};
	ship["systems"]["shield"] = {{"kind", "shield"},
	                             {"compartment", "fore"},
	                             {"shield_effect", "test.shield"},
	                             {"power_store", "cell"},
	                             {"faults", Json::array({{{"id", "output"},
	                                                      {"symptom", "Slow recharge"},
	                                                      {"diagnosis", "Output damaged"},
	                                                      {"chance_permille", 1000},
	                                                      {"damage_threshold", 1},
	                                                      {"effectiveness_permille", 500},
	                                                      {"repair_work", 100}}})}};
	ship["control_layout"]["controllers"].push_back(
	    {{"id", "shield"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array(
	          {{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"shield"}}}})}});
	ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("shield");
	ship["stations"][0]["consoles"][0]["feeds"].push_back("shield");
	ship["personnel"] = Json::parse(R"json({
 "departments":[{"id":"engineering","label":"Engineering","station":"operations"}],
 "traits":[{"id":"engineer","label":"Engineer","effects":["test.officer-recharge"]}],
 "duties":[{"id":"shield-watch","label":"Shield watch","department":"engineering","compartment":"fore",
   "controller":"shield","bindings":{"shield":["recharge"]}}],
 "officers":[{"id":"engineer","name":"Engineer","department":"engineering","compartment":"fore",
   "duty":"shield-watch","traits":["engineer"]}]})json");
	for (const auto &controller : ship["control_layout"]["controllers"])
		if (controller["id"] != "shield")
			ship["personnel"]["duties"].push_back({{"id", controller["id"]},
			                                       {"label", controller["id"]},
			                                       {"department", "engineering"},
			                                       {"compartment", "fore"},
			                                       {"controller", controller["id"]},
			                                       {"console", controller["console"]}});
	for (const auto &[id, system] : ship["systems"].items())
		if (system.value("kind", std::string{}) == "repair_team")
			ship["personnel"]["duties"].push_back({{"id", id + "-specialist"},
			                                       {"label", id + " specialist"},
			                                       {"department", "engineering"},
			                                       {"compartment", system["compartment"]},
			                                       {"team", id}});
	data["maps"][0]["scenario"] = {{"tree", "test.combine-shield"}, {"bindings", {{"actor", {{"entity", 1}}}}}};
	data["trees"].push_back(Json::parse(R"json({"id":"test.combine-shield","context":"scenario","initial":"apply",
 "states":[{"id":"apply","entry":[{"action":"apply_modifier_effect",
  "arguments":[{"observation":"self.entity"},"shield","test.timed-recharge","boost"]}]}]})json"));
	const auto catalog = load_fixture(data);
	World initial;
	initial.load(catalog, "test.map", {});
	auto checkpoint = initial.save();
	checkpoint["bodies"][0]["combat"]["faults"]["shield"][0]["active"] = true;
	World world;
	world.restore(catalog, checkpoint);
	check(world.claim(900, 1, "drive"), "Cannot claim combined shield-property test");
	world.tick();
	const auto combined = world.save();
	check(combined["bodies"][0]["shields"]["contributions"][0]["quoted_rate"] == 20000,
	      "Officer +25%, scenario +25% and fault -50% did not combine against shield recharge base once");
	const auto effect_id = combined["bodies"][0]["shields"]["contributions"][0]["effect_id"];
	World resumed;
	resumed.restore(catalog, combined);
	for (int i = 0; i < 5; ++i)
	{
		world.tick();
		resumed.tick();
	}
	const auto after = world.save();
	check(after["bodies"][0]["shields"]["contributions"][0]["quoted_rate"] == 15000 &&
	          after["bodies"][0]["shields"]["contributions"][0]["effect_id"] == effect_id &&
	          after["bodies"][0]["shields"]["contributions"][0]["state"]["strength"].get<int>() > 0,
	      "Scenario expiry failed to preserve the officer/fault contributions and native shield reserve");
	check(after == resumed.save(), "Combined shield property continuation diverged after restore");
}
void modifier_action_receipts()
{
	const auto catalog = fixture();
	GameplayScenarioBridge bridge;
	bridge.load(catalog, "test.map");
	const auto instance = bridge.controllers().begin()->first;
	const ScenarioActionToken token{instance, 500, 0};
	std::vector<state_tree::Value> args{{state_tree::Type::Entity, 1, {}},
	                                    {state_tree::Type::Text, 0, "radar"},
	                                    {state_tree::Type::Text, 0, "test.temporary-modifier"},
	                                    {state_tree::Type::Text, 0, "blessing"}};
	const auto receipt = bridge.execute(token, GameplayTreeAction::ApplyModifierEffect, args);
	check(receipt.result.status == ScenarioActionStatus::Pending && bridge.pending_effects().size() == 1,
	      "Scenario modifier action did not produce physical pending work");
	bridge.execute(token, GameplayTreeAction::ApplyModifierEffect, args);
	check(bridge.pending_effects().size() == 1, "Replay duplicated pending modifier work");
	GameplayScenarioBridge resumed;
	resumed.restore(catalog, "test.map", bridge.save());
	check(resumed.pending_effects().size() == 1, "Pending modifier request lost across resume");
	const auto before_duplicate = resumed.save();
	auto duplicate = before_duplicate;
	auto cross_family = duplicate.at("modifier_requests")[0];
	cross_family.erase("equipment");
	duplicate["shield_requests"].push_back(cross_family);
	bool rejected = false;
	try
	{
		resumed.restore(catalog, "test.map", duplicate);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	check(rejected && resumed.save() == before_duplicate,
	      "A token duplicated across effect families must reject replacement without disturbing pending work");
	resumed.confirm_effect(token);
	check(resumed.receipt(token)->result.status == ScenarioActionStatus::Succeeded,
	      "Physical grant confirmation missing");
	GameplayScenarioBridge committed;
	committed.restore(catalog, "test.map", resumed.save());
	committed.execute(token, GameplayTreeAction::ApplyModifierEffect, args);
	check(committed.pending_effects().empty(), "Committed modifier creation replayed on resume");
	const ScenarioActionToken refresh{instance, 501, 0};
	committed.execute(refresh, GameplayTreeAction::RefreshModifierEffect,
	                  {{state_tree::Type::Entity, 1, {}},
	                   {state_tree::Type::Text, 0, "radar"},
	                   {state_tree::Type::Text, 0, "blessing"}});
	committed.confirm_effect(refresh, "target removed");
	check(committed.receipt(refresh)->result.status == ScenarioActionStatus::Failed,
	      "Physical failure was not retained");
}
std::vector<Json> objective_records(const Observation &observation)
{
	std::vector<Json> out;
	for (const char *scope : {"records", "shared"})
		for (const auto &record : observation.gameplay.value(scope, Json::array()))
			if (record.value("schema", std::string{}) == "phoenix.objective")
				out.push_back(record);
	return out;
}
} // namespace

#include "../manual/scenario_probe.hpp"

int main(int argc, char **argv)
{
	try
	{
		if (argc == 6 && std::string(argv[1]) == "--smoke-gameover")
			return scenario_probe(argv[2], argv[3], std::stoi(argv[4]), argv[5]);
		if (argc == 5 && std::string(argv[1]) == "--smoke-pack")
			return scenario_probe(argv[2], argv[3], std::stoi(argv[4]));
		shield_action_receipts();
		modifier_action_receipts();
		independent_modifier_world();
		modifier_target_lifetime();
		combined_shield_properties();
		independent_shield_world();
		const auto catalog = fixture();
		World original;
		original.load(catalog, "test.map", {{"test.slot", "test.ship"}});
		const auto player = original.ship_for_slot("test.slot");
		check(player.has_value(), "Scenario actor slot did not resolve to a physical ship");
		check(original.claim(100, *player), "Scenario player Console claim failed");
		check(original.claim(101, 1), "Independent ship Console claim failed");
		check(original.submit(100, {*player, "speed_mm_s", 100000}), "High-speed experiment command was not admitted");
		original.tick();
		const auto saved = original.save();
		check(saved.contains("scenario"), "World did not persist its authoritative scenario continuation");
		World resumed;
		resumed.restore(catalog, saved);
		// Transport recovery clears/reclaims ordinary human authority explicitly;
		// both runs continue with identical current orders and physical state.
		for (auto *world : {&original, &resumed})
		{
			world->clear_claims();
			check(world->claim(100, *player) && world->claim(101, 1),
			      "Recovered scenario Console could not be reclaimed");
		}
		for (int i = 0; i < 3; ++i)
		{
			original.tick();
			resumed.tick();
		}
		const auto bodies = original.inspect();
		check(bodies.size() == 4, "Independent situation instances did not each construct one real ECS entity");
		std::vector<EntityId> spawned;
		bool upper = false, lower = false;
		for (const auto &b : bodies)
		{
			if (b.id == 1 || b.id == *player)
				continue;
			spawned.push_back(b.id);
			upper = upper || b.y == 10000;
			lower = lower || b.y == -10000;
			check(b.definition == "test.variant", "Physical spawn used the wrong verified entity definition");
		}
		check(spawned.size() == 2 && spawned[0] != spawned[1] && upper && lower,
		      "Real spawns lost stable identity or per-instance parameter placement");
		const auto player_view = original.observe(100), other_view = original.observe(101);
		check(player_view.has_value() && other_view.has_value(), "Scenario projections lost admitted Consoles");
		const auto records = objective_records(*player_view);
		check(!records.empty(), "Physically completed objective did not reach its permitted ship blackboard");
		check(objective_records(*other_view).empty(), "Scenario objective leaked to another ship");
		const auto continuation = original.save();
		const auto &scenario = continuation.at("scenario").at("runtime");
		check(scenario.at("objectives").size() == 1 &&
		          scenario.at("objectives")[0].at("status") == static_cast<unsigned>(ObjectiveStatus::Completed) &&
		          scenario.at("objectives")[0].at("completed_actor") == *player,
		      "Actual Flecs movement crossed the region without physical objective completion");
		bool succeeded = false;
		for (const auto &instance : scenario.at("instances"))
			if (instance.at("name") == "main")
				succeeded = instance.at("finished").get<bool>() && instance.at("succeeded").get<bool>();
		check(succeeded, "Scenario tree did not consume physical completion and finish successfully");
		check(continuation == resumed.save(), "Dynamic World/scenario resume differs from uninterrupted continuation");
		const auto publication_count = records.size();
		for (int i = 0; i < 3; ++i)
		{
			original.tick();
			resumed.tick();
		}
		check(original.inspect().size() == 4 && resumed.inspect().size() == 4,
		      "Completed one-shot spawn was replayed after recovery");
		check(objective_records(*original.observe(100)).size() == publication_count,
		      "Terminal objective repeatedly published completion");
		check(original.save() == resumed.save(),
		      "Later physical continuation diverged after restored scenario effects");
		scenario_record_readers();
		nebula_paths();
		narrow_sensor_profiles();
		asteroid_streaming();
		field_collision_safety();
		competition_resolution();
		delayed_competition();
		spawned_equipment_continuation();
		spawned_equipment_continuation(true);
		std::cout << "World scenario spawn/objective/recovery integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
