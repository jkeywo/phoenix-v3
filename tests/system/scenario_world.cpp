#include "../support/fixtures.hpp"
#include "../support/model_fixture.hpp"
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
