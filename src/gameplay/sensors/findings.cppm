module;
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
export module phoenix.gameplay.sensors.findings;
export import phoenix.gameplay.sensors.state;
export import phoenix.content.sensing.types;
export namespace phoenix
{
struct SensorFindingUpdate
{
	bool ready = false, changed = false, discovered = false;
};
class SensorFindings
{
public:
	using Discoveries = std::set<std::tuple<EntityId, EntityId, std::string>>;
	void configure(std::vector<SensorFinding> definitions);
	bool empty() const;
	const std::vector<SensorFinding> &definitions() const;
	const Discoveries &discoveries() const;
	void restore_discoveries(Discoveries discoveries);
	SensorFindingUpdate advance(SensorRuntime &state, SensorTrack &track, const std::string &mode, EntityId ship,
	                            EntityId target, const SensorFinding &finding, std::int64_t finding_ticks,
	                            std::uint64_t tick);
	Json forwarded_payload(EntityId ship, const SensorFinding &finding, std::uint64_t tick) const;

private:
	std::vector<SensorFinding> definitions_;
	Discoveries discoveries_;
};
std::int64_t sensor_ghost_scan_duration(const SensorDefinition &definition, std::int64_t profile_ticks);
bool advance_ghost_acquisition(SensorRuntime &state, SensorTrack &track, const std::string &mode, EntityId target,
                               std::int64_t duration);
void record_sensor_dispel_sample(SensorTrack &track, std::int64_t duration, std::uint64_t tick);
} // namespace phoenix
