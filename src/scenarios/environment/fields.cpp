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
// Portable exact 64x64 -> 128 comparison, also supported by MSVC and WASM.
// Geometry coordinates are bounded so signed dot/cross products fit int64.
struct Wide
{
	std::uint64_t high, low;
};
Wide product(std::uint64_t a, std::uint64_t b)
{
	const auto a0 = static_cast<std::uint32_t>(a);
	const auto a1 = a >> 32;
	const auto b0 = static_cast<std::uint32_t>(b);
	const auto b1 = b >> 32;
	const std::uint64_t p0 = static_cast<std::uint64_t>(a0) * b0;
	const std::uint64_t p1 = static_cast<std::uint64_t>(a0) * b1;
	const std::uint64_t p2 = a1 * b0, p3 = a1 * b1;
	const auto middle = (p0 >> 32) + static_cast<std::uint32_t>(p1) + static_cast<std::uint32_t>(p2);
	return {p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32), (middle << 32) | static_cast<std::uint32_t>(p0)};
}
bool less_equal(Wide a, Wide b)
{
	return a.high < b.high || (a.high == b.high && a.low <= b.low);
}
std::int64_t cross(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool on_segment(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c)
{
	return cross(a, b, c) == 0 && c.x >= std::min(a.x, b.x) && c.x <= std::max(a.x, b.x) && c.y >= std::min(a.y, b.y) &&
	       c.y <= std::max(a.y, b.y);
}
bool intersect(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c, ScenarioPoint d)
{
	const auto x = cross(a, b, c), y = cross(a, b, d), z = cross(c, d, a), w = cross(c, d, b);
	if ((x == 0 && on_segment(a, b, c)) || (y == 0 && on_segment(a, b, d)) || (z == 0 && on_segment(c, d, a)) ||
	    (w == 0 && on_segment(c, d, b)))
		return true;
	return ((x < 0 && y > 0) || (x > 0 && y < 0)) && ((z < 0 && w > 0) || (z > 0 && w < 0));
}

} // namespace
bool scenario_valid_point(ScenarioPoint p)
{
	return p.x >= -scenario_coordinate_limit && p.x <= scenario_coordinate_limit && p.y >= -scenario_coordinate_limit &&
	       p.y <= scenario_coordinate_limit;
}

bool scenario_contains(const ScenarioRegion &r, ScenarioPoint p)
{
	require(scenario_valid_point(p) && scenario_valid_point(r.centre), "position exceeds exact geometry range");
	const auto dx = p.x - r.centre.x, dy = p.y - r.centre.y;
	if (r.shape == ScenarioRegionShape::Rectangle)
		return dx >= -r.half_width && dx <= r.half_width && dy >= -r.half_height && dy <= r.half_height;
	const auto distance = dx * dx + dy * dy;
	return distance <= r.radius * r.radius &&
	       (r.shape != ScenarioRegionShape::Annulus || distance >= r.inner_radius * r.inner_radius);
}
bool scenario_crosses(const ScenarioRegion &r, ScenarioPoint from, ScenarioPoint to)
{
	if (scenario_contains(r, from) || scenario_contains(r, to))
		return true;
	if (r.shape == ScenarioRegionShape::Annulus)
	{
		auto outer = r;
		outer.shape = ScenarioRegionShape::Circle;
		const auto inside_hole = [&](ScenarioPoint p)
		{
			const auto x = p.x - r.centre.x, y = p.y - r.centre.y;
			return x * x + y * y < r.inner_radius * r.inner_radius;
		};
		return !(inside_hole(from) && inside_hole(to)) && scenario_crosses(outer, from, to);
	}
	if (r.shape == ScenarioRegionShape::Rectangle)
	{
		const std::array corners{ScenarioPoint{r.centre.x - r.half_width, r.centre.y - r.half_height},
		                         ScenarioPoint{r.centre.x + r.half_width, r.centre.y - r.half_height},
		                         ScenarioPoint{r.centre.x + r.half_width, r.centre.y + r.half_height},
		                         ScenarioPoint{r.centre.x - r.half_width, r.centre.y + r.half_height}};
		for (std::size_t i = 0; i < 4; ++i)
			if (intersect(from, to, corners[i], corners[(i + 1) % 4]))
				return true;
		return false;
	}
	const auto dx = to.x - from.x, dy = to.y - from.y;
	const auto px = r.centre.x - from.x, py = r.centre.y - from.y;
	const auto length = dx * dx + dy * dy, projection = px * dx + py * dy;
	if (!length || projection <= 0 || projection >= length)
		return false;
	const auto signed_area = dx * py - dy * px;
	const auto area = static_cast<std::uint64_t>(signed_area < 0 ? -signed_area : signed_area);
	return less_equal(product(area, area),
	                  product(static_cast<std::uint64_t>(r.radius * r.radius), static_cast<std::uint64_t>(length)));
}
namespace
{
std::uint64_t integer_root(std::uint64_t n)
{
	std::uint64_t result = 0, bit = std::uint64_t{1} << 62;
	while (bit > n)
		bit >>= 2;
	while (bit)
	{
		if (n >= result + bit)
		{
			n -= result + bit;
			result = (result >> 1) + bit;
		}
		else
			result >>= 1;
		bit >>= 2;
	}
	return result;
}
} // namespace
namespace
{
// Shared clipping retains the historical integer interpolation and rounding.
std::optional<ScenarioRegionInterval> clipped_region_interval(const ScenarioRegion &r, ScenarioPoint a, ScenarioPoint b,
                                                              std::int64_t length, bool strict = false)
{
	const auto dx = b.x - a.x, dy = b.y - a.y;
	// Q30 segment parameters keep interpolation products within signed 64 bits.
	constexpr auto unit = scenario_segment_unit;
	std::int64_t low = 0, high = unit;
	if (r.shape == ScenarioRegionShape::Rectangle)
	{
		auto clip = [&](std::int64_t origin, std::int64_t delta, std::int64_t centre, std::int64_t half)
		{
			if (!delta)
				return origin >= centre - half && origin <= centre + half;
			auto lo = (centre - half - origin) * unit / delta;
			auto hi = (centre + half - origin) * unit / delta;
			if (lo > hi)
				std::swap(lo, hi);
			low = std::max(low, lo);
			high = std::min(high, hi);
			return low < high;
		};
		if (!clip(a.x, dx, r.centre.x, r.half_width) || !clip(a.y, dy, r.centre.y, r.half_height))
			return std::nullopt;
	}
	else
	{
		// Project using normalized segment direction to avoid multiplying a
		// squared world distance by the parameter scale.
		const auto ux = dx * unit / length, uy = dy * unit / length;
		const auto along = ((r.centre.x - a.x) * ux + (r.centre.y - a.y) * uy) / unit;
		const auto middle = std::clamp(along * unit / length, std::int64_t{0}, unit);
		auto inside = [&](std::int64_t t)
		{
			const ScenarioPoint point{a.x + dx * t / unit, a.y + dy * t / unit};
			if (!strict)
				return scenario_contains(r, point);
			const auto x = point.x - r.centre.x, y = point.y - r.centre.y;
			return x * x + y * y < r.radius * r.radius;
		};
		if (!inside(middle))
			return std::nullopt;
		std::int64_t lo = 0, hi = middle;
		while (lo < hi)
		{
			const auto mid = lo + (hi - lo) / 2;
			if (inside(mid))
				hi = mid;
			else
				lo = mid + 1;
		}
		low = lo;
		lo = middle;
		hi = unit;
		while (lo < hi)
		{
			const auto mid = lo + (hi - lo + 1) / 2;
			if (inside(mid))
				lo = mid;
			else
				hi = mid - 1;
		}
		high = lo;
	}
	return ScenarioRegionInterval{low, high};
}
} // namespace
std::vector<ScenarioRegionInterval> scenario_region_intervals(const ScenarioRegion &r, ScenarioPoint a, ScenarioPoint b)
{
	require(scenario_valid_point(a) && scenario_valid_point(b), "path exceeds coordinate range");
	const auto dx = b.x - a.x, dy = b.y - a.y;
	const auto length = static_cast<std::int64_t>(integer_root(dx * dx + dy * dy));
	if (!length)
		return scenario_contains(r, a) ? std::vector<ScenarioRegionInterval>{{0, scenario_segment_unit}}
		                               : std::vector<ScenarioRegionInterval>{};
	auto outer = r;
	if (outer.shape == ScenarioRegionShape::Annulus)
		outer.shape = ScenarioRegionShape::Circle;
	const auto interval = clipped_region_interval(outer, a, b, length);
	if (!interval)
		return {};
	if (r.shape != ScenarioRegionShape::Annulus)
		return {*interval};
	outer.radius = r.inner_radius;
	const auto hole = clipped_region_interval(outer, a, b, length, true);
	if (!hole || hole->end <= interval->begin || hole->begin >= interval->end)
		return {*interval};
	std::vector<ScenarioRegionInterval> result;
	if (interval->begin < hole->begin)
		result.push_back({interval->begin, hole->begin});
	if (hole->end < interval->end)
		result.push_back({hole->end, interval->end});
	return result;
}
std::int64_t scenario_path_length(const ScenarioRegion &r, ScenarioPoint a, ScenarioPoint b)
{
	require(scenario_valid_point(a) && scenario_valid_point(b), "path exceeds coordinate range");
	if (r.shape == ScenarioRegionShape::Annulus)
	{
		auto circle = r;
		circle.shape = ScenarioRegionShape::Circle;
		const auto outer = scenario_path_length(circle, a, b);
		circle.radius = r.inner_radius;
		return std::max<std::int64_t>(0, outer - scenario_path_length(circle, a, b));
	}
	const auto dx = b.x - a.x, dy = b.y - a.y;
	const auto length = static_cast<std::int64_t>(integer_root(dx * dx + dy * dy));
	if (!length)
		return 0;
	const auto interval = clipped_region_interval(r, a, b, length);
	return interval ? length * (interval->end - interval->begin) / scenario_segment_unit : 0;
}
bool scenario_sensor_visible(std::span<const ScenarioRegion> regions, ScenarioPoint from, ScenarioPoint to,
                             std::int64_t range)
{
	const auto dx = to.x - from.x, dy = to.y - from.y;
	const auto distance = static_cast<std::int64_t>(integer_root(dx * dx + dy * dy));
	if (distance > range || range <= 0)
		return false;
	if (!distance)
		return true;
	// Compare optical depth to the remaining range budget before multiplying.
	const auto budget = (range - distance) * 1000000 / distance;
	std::int64_t depth = 0;
	for (const auto &r : regions)
		if (r.nebula && r.nebula->density && r.nebula->sensor_factor)
		{
			const auto path = scenario_path_length(r, from, to);
			const auto raw = path * r.nebula->density / 1000;
			if (raw / 1000000 > (budget - depth) / r.nebula->sensor_factor)
				return false;
			const auto contribution =
			    (raw / 1000000) * r.nebula->sensor_factor + (raw % 1000000) * r.nebula->sensor_factor / 1000000;
			if (contribution > budget - depth)
				return false;
			depth += contribution;
		}
	return true;
}
std::uint64_t asteroid_hash(std::uint64_t value)
{
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}
static std::int64_t floor_cell(std::int64_t n, std::int64_t spacing)
{
	return n >= 0 ? n / spacing : -((-n + spacing - 1) / spacing);
}
static EntityId field_id(std::int64_t x, std::int64_t y, int layer)
{
	auto zig = [](std::int64_t n) -> std::uint64_t
	{
		return n >= 0 ? n * 2 : -n * 2 - 1;
	};
	if (zig(x) >= (1ULL << 21) || zig(y) >= (1ULL << 21))
		throw std::runtime_error("Asteroid lattice exceeds supported coordinate range");
	return (EntityId{1} << 52) | (zig(x) << 23) | (zig(y) << 2) | static_cast<unsigned>(layer);
}
std::uint64_t asteroid_map_seed(std::string_view name)
{
	std::uint64_t seed = 1;
	for (const unsigned char c : name)
		seed = asteroid_hash(seed ^ c);
	return seed;
}
std::vector<GeneratedAsteroid> generate_asteroid_cell(std::span<const ScenarioRegion> regions, std::uint64_t field_seed,
                                                      std::int64_t x, std::int64_t y)
{
	std::vector<GeneratedAsteroid> result;
	std::int64_t spacing = 0;
	for (const auto &r : regions)
		if (r.asteroids)
		{
			spacing = r.asteroids->spacing;
			break;
		}
	if (!spacing)
		return result;
	const ScenarioPoint centre{x * spacing + spacing / 2, y * spacing + spacing / 2};
	std::vector<const ScenarioRegion *> covering;
	std::int64_t weight = 0, density = 0;
	for (const auto &r : regions)
		if (r.asteroids && scenario_contains(r, centre))
		{
			covering.push_back(&r);
			weight += r.asteroids->weight;
			density += r.asteroids->density * r.asteroids->weight;
		}
	if (covering.empty())
		return result;
	const auto cell_hash = asteroid_hash(field_id(x, y, 0) ^ field_seed);
	auto pick = static_cast<std::int64_t>(cell_hash % weight);
	const ScenarioRegion *chosen = covering.front();
	for (const auto *r : covering)
	{
		chosen = r;
		pick -= r->asteroids->weight;
		if (pick < 0)
			break;
	}
	const auto &a = *chosen->asteroids;
	const auto random = asteroid_hash(cell_hash ^ a.seed);
	const auto cluster = static_cast<std::int64_t>(
	    asteroid_hash(field_id(floor_cell(x, a.noise_cells), floor_cell(y, a.noise_cells), 0) ^ a.seed) % 2001);
	// Expected occupancy per cell; density is millionths of rocks/m².
	const auto base = std::min<std::int64_t>(1000000, (density / weight) * spacing / 1000 * spacing / 1000);
	const auto fill =
	    std::min<std::int64_t>(1000000, base * (1000 - a.clustering + a.clustering * cluster / 1000) / 1000);
	for (int layer = 0; layer < 3; ++layer)
	{
		const auto id = field_id(x, y, layer);
		const auto &types = layer == 0 ? a.types : a.decorative_types;
		const auto rng = asteroid_hash(random + layer);
		if (types.empty() ||
		    static_cast<std::int64_t>(rng % 1000000) >= (layer ? fill * a.decorative_fill / 1000 : fill))
			continue;
		std::int64_t type_weight = 0;
		for (const auto &t : types)
			type_weight += t.weight;
		auto tp = static_cast<std::int64_t>(asteroid_hash(rng) % type_weight);
		const auto *type = &types.front();
		for (const auto &t : types)
		{
			type = &t;
			tp -= t.weight;
			if (tp < 0)
				break;
		}
		const auto jitter = [&](std::uint64_t salt)
		{
			return static_cast<std::int64_t>(asteroid_hash(rng ^ salt) % (2 * a.jitter + 1)) - a.jitter;
		};
		ScenarioPoint position{centre.x + jitter(17), centre.y + jitter(31)};
		if (!scenario_contains(*chosen, position))
			continue;
		GeneratedAsteroid rock;
		rock.decorative = layer != 0;
		auto &s = rock.scene;
		s.id = id;
		s.definition = type->definition;
		s.x = position.x;
		s.y = position.y;
		s.heading = 0;
		s.field_instance = true;
		s.visual_height_mm = layer == 0 ? 0 : layer == 1 ? a.decorative_height : -a.decorative_height;
		s.rotation_mdeg = asteroid_hash(rng ^ 41) % 360000;
		s.spin_mdeg_s =
		    (a.spin_min + static_cast<std::int64_t>(asteroid_hash(rng ^ 59) % (a.spin_max - a.spin_min + 1))) *
		    ((rng & 0x100000) ? 1 : -1);
		rock.entry = type->entry;
		result.push_back(std::move(rock));
	}
	return result;
}
Json region_markers(std::span<const ScenarioRegion> regions)
{
	Json out = Json::array();
	for (const auto &r : regions)
		if (r.display.value("navigation", false) || r.display.value("radar", false))
		{
			Json v = r.display;
			v["id"] = r.id;
			v["x"] = r.centre.x;
			v["y"] = r.centre.y;
			v["shape"] = r.shape == ScenarioRegionShape::Rectangle ? "rectangle"
			             : r.shape == ScenarioRegionShape::Annulus ? "annulus"
			                                                       : "circle";
			v["radius"] = r.radius;
			v["inner_radius"] = r.inner_radius;
			v["half_width"] = r.half_width;
			v["half_height"] = r.half_height;
			out.push_back(std::move(v));
		}
	return out;
}
Json region_environment(std::span<const ScenarioRegion> regions, double simulation_time)
{
	Json result{{"nebulae", Json::array()}, {"simulation_time", simulation_time}};
	for (const auto &r : regions)
		if (r.nebula)
			result["nebulae"].push_back({{"shape", static_cast<int>(r.shape)},
			                             {"x", r.centre.x / 1000.0},
			                             {"y", r.centre.y / 1000.0},
			                             {"radius", r.radius / 1000.0},
			                             {"inner_radius", r.inner_radius / 1000.0},
			                             {"half_width", r.half_width / 1000.0},
			                             {"half_height", r.half_height / 1000.0},
			                             {"height", r.nebula->height / 1000.0},
			                             {"density", r.nebula->density / 1000000.0},
			                             {"colour", r.nebula->colour}});
	return result;
}
} // namespace phoenix
