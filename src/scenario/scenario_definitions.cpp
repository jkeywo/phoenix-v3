module;
#include <optional>
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
	for (const auto &v : regions)
	{
		ScenarioRegion r;
		r.id = identifier(v, "id");
		r.centre = point(v);
		const auto shape = text(v, "shape");
		if (shape == "circle")
		{
			r.shape = ScenarioRegionShape::Circle;
			r.radius = number(v, "radius_mm", 0, 1);
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
		require((r.shape == ScenarioRegionShape::Circle && r.radius > 0) ||
		            (r.shape == ScenarioRegionShape::Rectangle && r.half_width > 0 && r.half_height > 0),
		        "region requires positive dimensions");
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
		auto check_slot = [&](const ScenarioActor &a)
		{
			if (a.kind == ScenarioActorKind::ShipSlot)
				require(contains_name(out.ship_slots, a.name), "unresolved ship slot " + a.name);
		};
		check_slot(d.actor);
		check_slot(d.recipients.ship);
		require(out.objectives.emplace(d.id, std::move(d)).second, "duplicate objective definition");
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
