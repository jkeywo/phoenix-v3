module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

module phoenix.scenarios.environment.streaming;

namespace phoenix
{
namespace
{
std::int64_t floor_cell(std::int64_t n, std::int64_t spacing)
{
	return n >= 0 ? n / spacing : -((-n + spacing - 1) / spacing);
}
} // namespace
AsteroidWindow AsteroidWindowCache::prepare(std::span<const AsteroidFocus> foci, std::int64_t spacing) const
{
	AsteroidWindow result;
	for (const auto &focus : foci)
	{
		const auto minx = floor_cell(focus.position.x - focus.radius, spacing);
		const auto maxx = floor_cell(focus.position.x + focus.radius, spacing);
		const auto miny = floor_cell(focus.position.y - focus.radius, spacing);
		const auto maxy = floor_cell(focus.position.y + focus.radius, spacing);
		if (maxx - minx > 512 || maxy - miny > 512)
			throw std::runtime_error("Asteroid streaming window exceeds 512 cells: increase lattice spacing");
		result.windows.push_back({minx, maxx, miny, maxy});
	}
	result.changed = result.windows != windows_;
	if (result.changed)
		for (const auto &[minx, maxx, miny, maxy] : result.windows)
			for (auto x = minx; x <= maxx; ++x)
				for (auto y = miny; y <= maxy; ++y)
				{
					result.cells.emplace(x, y);
					if (result.cells.size() > 65536)
						throw std::runtime_error("Asteroid fleet streaming exceeds 65536 cells");
				}
	return result;
}
bool AsteroidWindowCache::contains(std::int64_t x, std::int64_t y) const
{
	return cells_.contains({x, y});
}
void AsteroidWindowCache::commit(AsteroidWindow window)
{
	windows_ = std::move(window.windows);
	cells_ = std::move(window.cells);
}
void AsteroidWindowCache::clear()
{
	windows_.clear();
	cells_.clear();
}
Json encode_asteroid_field(const FieldRock &r)
{
	return {{"id", r.scene.id},
	        {"definition", r.scene.definition},
	        {"x", r.scene.x},
	        {"y", r.scene.y},
	        {"vx", r.velocity.x},
	        {"vy", r.velocity.y},
	        {"rx", r.remainder.x},
	        {"ry", r.remainder.y},
	        {"heading", r.scene.heading},
	        {"entry", r.scene.sprite_entry},
	        {"height", r.scene.visual_height_mm},
	        {"rotation", r.scene.rotation_mdeg},
	        {"spin", r.scene.spin_mdeg_s},
	        {"decorative", r.decorative},
	        {"loaded", r.loaded}};
}
FieldRock decode_asteroid_field(const Json &v)
{
	FieldRock r;
	auto &s = r.scene;
	s.id = v.at("id");
	s.definition = v.at("definition");
	s.x = v.at("x");
	s.y = v.at("y");
	s.heading = v.at("heading");
	s.sprite_entry = v.at("entry");
	s.visual_height_mm = v.at("height");
	s.rotation_mdeg = v.at("rotation");
	s.spin_mdeg_s = v.at("spin");
	s.field_instance = true;
	r.velocity = {v.at("vx"), v.at("vy")};
	r.remainder = {v.at("rx"), v.at("ry")};
	r.decorative = v.at("decorative");
	return r;
}
void validate_asteroid_field(const FieldRock &r, std::span<const ScenarioRegion> regions, bool known,
                             std::size_t sprite_entries, bool duplicate)
{
	const auto &s = r.scene;
	bool allowed = false;
	for (const auto &region : regions)
		if (region.asteroids)
		{
			const auto &types = r.decorative ? region.asteroids->decorative_types : region.asteroids->types;
			for (const auto &type : types)
				if (type.definition == s.definition)
					allowed = true;
		}
	const auto bounded = [](std::int64_t value, std::int64_t limit)
	{
		return value >= -limit && value <= limit;
	};
	if (!allowed || s.id >= (EntityId{1} << 53) || (s.id & 3) == 3 || r.decorative != ((s.id & 3) != 0) ||
	    s.heading < 0 || s.heading >= 360000 || !bounded(s.visual_height_mm, scenario_coordinate_limit) ||
	    !bounded(r.velocity.x, scenario_coordinate_limit) || !bounded(r.velocity.y, scenario_coordinate_limit) ||
	    !bounded(r.remainder.x, simulation_tick_hz - 1) || !bounded(r.remainder.y, simulation_tick_hz - 1) ||
	    s.id < (EntityId{1} << 52) || !known || s.sprite_entry >= sprite_entries ||
	    !bounded(s.x, scenario_coordinate_limit) || !bounded(s.y, scenario_coordinate_limit) || s.rotation_mdeg < 0 ||
	    s.rotation_mdeg >= 360000 || !bounded(s.spin_mdeg_s, 360000) || duplicate)
		throw std::runtime_error("Invalid saved asteroid instance");
}
} // namespace phoenix
