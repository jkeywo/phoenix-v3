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
struct SensorContactInput
{
	BodyView body;
	bool apparent = false;
	SensorContact detection = SensorContact::Position;
	bool focused_only = false;
};

struct SensorContactAssessment
{
	SensorContact detection = SensorContact::None;
	std::int64_t distance = 0;
};
// Geometric admission only. The caller retains directed-target and focused
// coverage lookups behind the ordinary detection rejection, in original order.
SensorContactAssessment assess_sensor_contact(const SensorCoverage &, std::int64_t source_altitude, const BodyView &,
                                              std::int64_t visibility, std::span<const ScenarioRegion>);
} // namespace phoenix
