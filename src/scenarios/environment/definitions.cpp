module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.scenarios.environment;
namespace phoenix
{
namespace
{
void require(bool valid, const std::string &message)
{
	if (!valid)
		throw std::runtime_error("scenario: " + message);
}
std::uint64_t unsigned_number(const Json &v, const char *description)
{
	return checked_unsigned(v, 0, std::numeric_limits<std::uint64_t>::max(), std::string("scenario: ") + description);
}
std::int64_t number(const Json &v, const char *field, std::int64_t fallback = 0,
                    std::int64_t minimum = -scenario_coordinate_limit, std::int64_t maximum = scenario_coordinate_limit)
{
	return json_integer(v, field, fallback, minimum, maximum, "scenario");
}
std::string text(const Json &v, const char *field, bool optional = false)
{
	if (optional && !v.contains(field))
		return {};
	require(v.contains(field) && v.at(field).is_string(), std::string(field) + " must be text");
	auto s = v.at(field).get<std::string>();
	require(!s.empty() && s.size() <= 256, std::string(field) + " must contain 1..256 characters");
	return s;
}
std::string identifier(const Json &v, const char *field)
{
	auto s = text(v, field);
	require(valid_identifier(s), std::string(field) + " is not an authored identifier: " + s);
	return s;
}
const Json &list(const Json &v, const char *field, std::size_t maximum = 4096)
{
	static const Json empty = Json::array();
	if (!v.contains(field))
		return empty;
	require(v.at(field).is_array() && v.at(field).size() <= maximum, std::string(field) + " exceeds its array limit");
	return v.at(field);
}
ScenarioPoint point(const Json &v)
{
	return {number(v, "x_mm"), number(v, "y_mm")};
}

} // namespace
ScenarioEnvironmentDefinition compile_scenario_environment(const Json &map, std::span<const std::string> entities)
{
	require(map.is_object(), "map must be an object");
	const Json empty = Json::object();
	const auto &source = map.contains("scenario") ? map.at("scenario") : empty;
	require(source.is_object(), "map.scenario must be an object");
	ScenarioEnvironmentDefinition out;
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
			require(scenario_valid_point({r.centre.x - r.half_width, r.centre.y - r.half_height}) &&
			            scenario_valid_point({r.centre.x + r.half_width, r.centre.y + r.half_height}),
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
	return out;
}
} // namespace phoenix
