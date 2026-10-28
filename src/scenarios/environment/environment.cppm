module;
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module phoenix.scenarios.environment;
export import phoenix.data;
export import phoenix.contracts;

export namespace phoenix
{
inline constexpr std::int64_t scenario_coordinate_limit = 1000000000;
// Shared scenario authoring/restore string validation.
std::string read_scenario_text(const Json &, const char *field, bool optional = false);
enum class ScenarioRegionShape
{
	Circle,
	Rectangle,
	Annulus
};
struct ScenarioPoint
{
	std::int64_t x = 0, y = 0;
	bool operator==(const ScenarioPoint &) const = default;
};
struct ScenarioRegion
{
	std::string id;
	ScenarioRegionShape shape = ScenarioRegionShape::Circle;
	ScenarioPoint centre;
	std::int64_t radius = 0, half_width = 0, half_height = 0;
	std::int64_t inner_radius = 0;
	struct Nebula
	{
		std::array<float, 3> colour{.5f, .3f, .8f};
		// Millionths per metre and millionths respectively; authoritative
		// sampling never depends on floating point arithmetic.
		std::int64_t density = 0, sensor_factor = 1000000, height = 100000;
	};
	std::optional<Nebula> nebula;
	Json display = Json::object();
	struct Asteroids
	{
		struct Type
		{
			std::string definition, entry;
			std::int64_t weight = 1000;
		};
		std::vector<Type> types, decorative_types;
		std::uint64_t seed = 1;
		std::int64_t spacing = 25000, density = 400, weight = 1000;
		std::int64_t jitter = 10000, clustering = 500, noise_cells = 8;
		std::int64_t decorative_height = 20000, decorative_fill = 250;
		std::int64_t spin_min = 1000, spin_max = 10000;
		std::int64_t visual_distance = 250000, unload_margin = 50000;
	};
	std::optional<Asteroids> asteroids;
};
bool scenario_contains(const ScenarioRegion &region, ScenarioPoint point);
bool scenario_crosses(const ScenarioRegion &region, ScenarioPoint from, ScenarioPoint to);
inline constexpr std::int64_t scenario_segment_unit = 1LL << 30;
struct ScenarioRegionInterval
{
	std::int64_t begin = 0, end = 0;
};
// Ordered Q30 spans of the executed segment; an annulus can yield two spans.
std::vector<ScenarioRegionInterval> scenario_region_intervals(const ScenarioRegion &region, ScenarioPoint from,
                                                              ScenarioPoint to);
std::int64_t scenario_path_length(const ScenarioRegion &region, ScenarioPoint from, ScenarioPoint to);
bool scenario_sensor_visible(std::span<const ScenarioRegion> regions, ScenarioPoint from, ScenarioPoint to,
                             std::int64_t range);
struct GeneratedAsteroid
{
	SceneBody scene{};
	std::string entry;
	bool decorative = false;
};
std::uint64_t asteroid_hash(std::uint64_t value);
std::uint64_t asteroid_map_seed(std::string_view name);
std::vector<GeneratedAsteroid> generate_asteroid_cell(std::span<const ScenarioRegion> regions, std::uint64_t seed,
                                                      std::int64_t x, std::int64_t y);
Json region_markers(std::span<const ScenarioRegion> regions);
Json region_environment(std::span<const ScenarioRegion> regions, double simulation_time);
// Shared exact-coordinate validation for scenario authoring, movement and fields.
bool scenario_valid_point(ScenarioPoint);
struct ScenarioEnvironmentDefinition
{
	std::vector<ScenarioRegion> regions;
	std::map<std::string, std::uint32_t> region_ids;
};
ScenarioEnvironmentDefinition compile_scenario_environment(const Json &map,
                                                           std::span<const std::string> entity_definitions);
} // namespace phoenix
