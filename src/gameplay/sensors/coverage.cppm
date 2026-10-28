module;
#include <cstdint>
#include <span>
export module phoenix.gameplay.sensors.coverage;
export import phoenix.world.state;
export import phoenix.scenarios.environment;
export namespace phoenix
{
struct SensorCoverage
{
	fixed::Vec origin;
	std::int64_t normal = 0, outer = 0, heading = 0, arc = 360000;
	bool mount_enabled = true;
};
enum class SensorContact
{
	None,
	Bearing,
	Position
};
bool sensor_coverage_contains(const SensorCoverage &coverage, fixed::Vec target, std::int64_t range,
                              std::span<const ScenarioRegion> regions);
SensorContact sensor_coverage_contact(const SensorCoverage &coverage, fixed::Vec target, std::int64_t distance,
                                      std::int64_t visibility, std::span<const ScenarioRegion> regions);
} // namespace phoenix
