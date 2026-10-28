module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <algorithm>
module phoenix.gameplay.sensors.coverage;
import phoenix.gameplay.combat.state;
namespace phoenix
{
bool sensor_coverage_contains(const SensorCoverage &coverage, fixed::Vec target, std::int64_t range,
                              std::span<const ScenarioRegion> regions)
{
	if (!coverage.mount_enabled || !range ||
	    !scenario_sensor_visible(regions, {coverage.origin.x, coverage.origin.y}, {target.x, target.y}, range))
		return false;
	return coverage.arc == 360000 ||
	       std::abs(combat_delta(combat_bearing(target - coverage.origin), coverage.heading)) <= coverage.arc / 2;
}
SensorContact sensor_coverage_contact(const SensorCoverage &coverage, fixed::Vec target, std::int64_t distance,
                                      std::int64_t visibility, std::span<const ScenarioRegion> regions)
{
	const auto normal = coverage.normal * visibility / 1000;
	if (distance <= normal && sensor_coverage_contains(coverage, target, coverage.normal, regions))
		return SensorContact::Position;
	const auto outer = coverage.outer * visibility / 1000;
	if (outer > normal && distance <= outer && sensor_coverage_contains(coverage, target, coverage.outer, regions))
		return SensorContact::Bearing;
	return SensorContact::None;
}

SensorContactAssessment assess_sensor_contact(const SensorCoverage &coverage, std::int64_t source_altitude,
                                              const BodyView &body, std::int64_t visibility,
                                              std::span<const ScenarioRegion> regions)
{
	const auto delta = fixed::Vec{body.x, body.y} - coverage.origin;
	const auto z = body.altitude - source_altitude;
	const auto distance =
	    static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(delta, delta) + z * z)));
	const auto surface_distance = std::max<std::int64_t>(0, distance - body.radius);
	const fixed::Vec surface = distance ? coverage.origin + fixed::Vec{delta.x * surface_distance / distance,
	                                                                   delta.y * surface_distance / distance}
	                                    : coverage.origin;
	return {sensor_coverage_contact(coverage, surface, surface_distance, visibility, regions), distance};
}
} // namespace phoenix
