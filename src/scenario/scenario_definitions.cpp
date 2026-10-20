module;
#include <optional>
#include <cmath>
#include <variant>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

module phoenix.scenario;

namespace phoenix
{
#include "scenario_internal.hpp"

ScenarioDefinition compile_scenario(const Json &map, std::span<const std::string> entities,
                                    std::span<const std::string> slots)
{
	require(map.is_object(), "map must be an object");
	const Json empty = Json::object();
	const auto &source = map.contains("scenario") ? map.at("scenario") : empty;
	require(source.is_object(), "map.scenario must be an object");
	ScenarioDefinition out;
	out.entity_definitions.assign(entities.begin(), entities.end());
	out.ship_slots.assign(slots.begin(), slots.end());
	const auto &regions = map.contains("regions") ? list(map, "regions") : list(source, "regions");
	require(regions.size() <= 256, "map supports at most 256 regions");
	for (const auto &v : regions)
	{
		ScenarioRegion r;
		r.id = identifier(v, "id");
		r.centre = point(v);
		if (v.contains("display"))
		{
			const auto &d = v.at("display");
			require(d.is_object(), "region display must be an object");
			for (const auto *key : {"navigation", "radar"})
				require(!d.contains(key) || d.at(key).is_boolean(), "region display visibility must be boolean");
			const auto name = d.value("name", r.id);
			require(!name.empty() && name.size() <= 128, "region display name needs 1..128 characters");
			const auto colour = d.value("colour", Json::array({.5, .7, .9}));
			require(colour.is_array() && colour.size() == 3, "region display colour needs three channels");
			for (const auto &c : colour)
				require(c.is_number() && std::isfinite(c.get<double>()) && c >= 0 && c <= 1,
				        "invalid region display colour");
			const auto tags = d.value("tags", Json::array({"contact"}));
			require(tags.is_array() && tags.size() <= 32, "region tags need at most 32 entries");
			for (const auto &tag : tags)
				require(tag.is_string() && valid_identifier(tag.get<std::string>()), "invalid region tag");
			r.display = {{"navigation", d.value("navigation", false)},
			             {"radar", d.value("radar", false)},
			             {"name", name},
			             {"colour", colour},
			             {"tags", tags}};
		}
		const auto shape = text(v, "shape");
		if (shape == "circle" || shape == "annulus")
		{
			r.shape = shape == "circle" ? ScenarioRegionShape::Circle : ScenarioRegionShape::Annulus;
			r.radius = number(v, "radius_mm", 0, 1);
			if (r.shape == ScenarioRegionShape::Annulus)
			{
				r.inner_radius = number(v, "inner_radius_mm", 0, 1);
				require(r.inner_radius < r.radius, "annulus inner radius must be smaller than radius");
			}
		}
		else
		{
			require(shape == "rectangle", "unsupported region shape " + shape);
			r.shape = ScenarioRegionShape::Rectangle;
			r.half_width = number(v, "half_width_mm", 0, 1);
			r.half_height = number(v, "half_height_mm", 0, 1);
			require(valid_point({r.centre.x - r.half_width, r.centre.y - r.half_height}) &&
			            valid_point({r.centre.x + r.half_width, r.centre.y + r.half_height}),
			        "rectangle corners exceed coordinate range");
		}
		require((r.shape != ScenarioRegionShape::Rectangle && r.radius > 0) ||
		            (r.shape == ScenarioRegionShape::Rectangle && r.half_width > 0 && r.half_height > 0),
		        "region requires positive dimensions");
		if (v.contains("nebula"))
		{
			const auto &n = v.at("nebula");
			require(n.is_object(), "nebula must be an object");
			ScenarioRegion::Nebula fog;
			auto coefficient = [&](const char *key, double fallback, double maximum)
			{
				const auto value = n.value(key, fallback);
				require(std::isfinite(value) && value >= 0 && value <= maximum,
				        r.id + ".nebula." + key + " is outside supported range");
				return static_cast<std::int64_t>(std::round(value * 1000000));
			};
			fog.density = coefficient("density", .01, 100);
			fog.sensor_factor = coefficient("sensor_factor", 1, 100);
			fog.height = number(n, "half_height_mm", 100000, 1);
			const auto colour = n.value("colour", Json::array({.5, .3, .8}));
			require(colour.is_array() && colour.size() == 3, "nebula colour needs three channels");
			for (int i = 0; i < 3; ++i)
			{
				require(colour[i].is_number(), "nebula colour channel must be numeric");
				const auto c = colour[i].get<double>();
				require(std::isfinite(c) && c >= 0 && c <= 1, "nebula colour channel outside 0..1");
				fog.colour[i] = static_cast<float>(c);
			}
			r.nebula = fog;
		}
		if (v.contains("asteroids"))
		{
			const auto &a = v.at("asteroids");
			require(a.is_object(), r.id + ".asteroids must be an object");
			ScenarioRegion::Asteroids field;
			field.seed = unsigned_number(a.value("seed", Json(1)), "asteroid seed");
			field.spacing = number(a, "spacing_mm", 25000, 1000, 1000000);
			field.weight = number(a, "weight", 1000, 1, 1000000);
			const auto density = a.value("density", .0004);
			require(std::isfinite(density) && density >= 0 && density <= 1,
			        "asteroid density must be 0..1 per square metre");
			field.density = static_cast<std::int64_t>(std::round(density * 1000000));
			field.jitter = number(a, "jitter_mm", field.spacing / 3, 0, field.spacing / 2);
			field.clustering = number(a, "clustering_permille", 500, 0, 1000);
			field.noise_cells = number(a, "noise_cells", 8, 1, 1024);
			field.decorative_height = number(a, "decorative_height_mm", 20000, 1);
			field.decorative_fill = number(a, "decorative_fill_permille", 250, 0, 1000);
			field.spin_min = number(a, "spin_min_mdeg_s", 1000, 0, 360000);
			field.spin_max = number(a, "spin_max_mdeg_s", 10000, field.spin_min, 360000);
			field.visual_distance = number(a, "visual_distance_mm", 250000, 1000, 10000000);
			field.unload_margin = number(a, "unload_margin_mm", 50000, field.spacing, 10000000);
			auto types = [&](const char *key, auto &target)
			{
				for (const auto &t : list(a, key, 256))
				{
					ScenarioRegion::Asteroids::Type type;
					type.definition = identifier(t, "definition");
					require(std::find(entities.begin(), entities.end(), type.definition) != entities.end(),
					        "unknown asteroid definition " + type.definition);
					type.entry = t.value("sprite_entry", std::string{});
					require(type.entry.empty() || valid_identifier(type.entry), "invalid asteroid sprite entry");
					type.weight = number(t, "weight", 1000, 1, 1000000);
					target.push_back(std::move(type));
				}
			};
			types("types", field.types);
			types("decorative_types", field.decorative_types);
			require(!field.types.empty() || !field.decorative_types.empty(), "asteroid field needs types");
			for (const auto &previous : out.regions)
				require(!previous.asteroids || previous.asteroids->spacing == field.spacing,
				        "overlapping field composition requires one map lattice spacing");
			r.asteroids = std::move(field);
		}
		require(out.region_ids.emplace(r.id, static_cast<std::uint32_t>(out.regions.size())).second,
		        "duplicate region " + r.id);
		out.regions.push_back(std::move(r));
	}
	for (const auto &v : list(source, "spawn_points"))
		require(out.spawn_points.emplace(identifier(v, "id"), point(v)).second, "duplicate spawn point");
	for (const auto &v : list(source, "spawn_groups"))
	{
		ScenarioSpawnGroup group;
		group.id = identifier(v, "id");
		for (const auto &e : list(v, "entities"))
		{
			ScenarioSpawnPlacement p{text(e, "definition"), point(e), number(e, "heading_mdeg", 0, 0, 359999)};
			require(contains_name(out.entity_definitions, p.definition), "unresolved spawn definition " + p.definition);
			group.entities.push_back(std::move(p));
		}
		require(!group.entities.empty(), "spawn group must not be empty");
		require(out.spawn_groups.emplace(group.id, std::move(group)).second, "duplicate spawn group");
	}
	for (const auto &v : list(source, "objectives"))
	{
		ScenarioObjectiveDefinition d;
		d.id = identifier(v, "id");
		require(text(v, "type") == "reach_region", "unsupported objective type in " + d.id);
		const auto region = text(v, "region");
		require(out.region_ids.contains(region), "unresolved objective region " + region);
		d.region = out.region_ids.at(region);
		d.actor = actor(v.at("actor"));
		d.recipients = recipients(v.at("recipients"));
		d.title_key = text(v, "title_key");
		d.description_key = text(v, "description_key", true);
		d.competition = text(v, "competition", true);
		require(d.competition.empty() || valid_identifier(d.competition), "invalid objective competition");
		auto check_slot = [&](const ScenarioActor &a)
		{
			if (a.kind == ScenarioActorKind::ShipSlot)
				require(contains_name(out.ship_slots, a.name), "unresolved ship slot " + a.name);
		};
		check_slot(d.actor);
		check_slot(d.recipients.ship);
		require(out.objectives.emplace(d.id, std::move(d)).second, "duplicate objective definition");
	}
	std::map<std::string, std::vector<const ScenarioObjectiveDefinition *>> competitions;
	for (const auto &[id, objective] : out.objectives)
		if (!objective.competition.empty())
			competitions[objective.competition].push_back(&objective);
	for (const auto &[id, entries] : competitions)
	{
		require(entries.size() >= 2, "competition requires at least two objectives");
		for (std::size_t i = 0; i < entries.size(); ++i)
			for (std::size_t j = 0; j < i; ++j)
				require(entries[i]->region == entries[j]->region && entries[i]->actor != entries[j]->actor,
				        "competition requires distinct actors and a shared finish region");
	}
	for (const auto &v : list(source, "objective_sequences"))
	{
		ScenarioObjectiveSequence sequence;
		sequence.id = identifier(v, "id");
		sequence.repeat = v.value("repeat", false);
		for (const auto &entry : list(v, "objectives"))
		{
			require(entry.is_string() && out.objectives.contains(entry.get<std::string>()),
			        "unresolved sequence objective");
			sequence.objectives.push_back(entry.get<std::string>());
		}
		require(!sequence.objectives.empty(), "objective sequence must not be empty");
		const auto &first = out.objectives.at(sequence.objectives.front());
		for (const auto &id : sequence.objectives)
		{
			const auto &objective = out.objectives.at(id);
			require(objective.competition.empty(), "competition objectives cannot repeat in a sequence");
			require(objective.actor == first.actor && objective.recipients == first.recipients,
			        "sequence objectives must share actor and recipients");
		}
		require(out.objective_sequences.emplace(sequence.id, std::move(sequence)).second,
		        "duplicate objective sequence");
	}
	auto names = [&](const char *key, std::vector<std::string> &target)
	{
		for (const auto &v : list(source, key))
		{
			require(v.is_string() && valid_identifier(v.get<std::string>()),
			        std::string("invalid ") + key + " declaration");
			auto id = v.get<std::string>();
			require(!contains_name(target, id), "duplicate declaration " + id);
			target.push_back(std::move(id));
		}
	};
	names("events", out.events);
	names("publications", out.publications);
	if (source.contains("parameters"))
		out.parameters = parameters(source.at("parameters"));
	if (source.contains("limits"))
	{
		const auto &v = source.at("limits");
		require(v.is_object(), "limits must be object");
		out.spawn_per_tick = static_cast<std::uint32_t>(number(v, "spawn_per_tick", out.spawn_per_tick, 1, 4096));
		out.spawned_population =
		    static_cast<std::uint32_t>(number(v, "spawned_population", out.spawned_population, 1, 65536));
		out.maximum_instances = static_cast<std::uint32_t>(number(v, "instances", out.maximum_instances, 1, 4096));
		out.maximum_actions = static_cast<std::uint32_t>(number(v, "actions", out.maximum_actions, 1, 1000000));
		out.maximum_objectives = static_cast<std::uint32_t>(number(v, "objectives", out.maximum_objectives, 1, 65536));
		out.maximum_events = static_cast<std::uint32_t>(number(v, "events", out.maximum_events, 1, 1000000));
	}
	Json signature{
	    {"scenario", source}, {"regions", regions}, {"entities", out.entity_definitions}, {"slots", out.ship_slots}};
	const auto encoded = signature.dump();
	out.signature = sha256(Bytes(encoded.begin(), encoded.end()));
	return out;
}

} // namespace phoenix
