module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <span>
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
} // namespace phoenix
