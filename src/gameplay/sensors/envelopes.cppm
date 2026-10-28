module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>
export module phoenix.gameplay.sensors.envelopes;
export import phoenix.gameplay.sensors.state;
export import phoenix.scenarios.environment;
export namespace phoenix
{
struct SensorEnvelope
{
	fixed::Vec origin;
	std::int64_t range = -1;
	std::vector<std::int64_t> distances;
	std::vector<SensorReachInterval> profile;
	std::int64_t uncertainty = 0;
	bool limited = false;
};
class SensorEnvelopes
{
public:
	const SensorEnvelope &get(EntityId ship, const std::string &name, fixed::Vec origin, std::int64_t range,
	                          std::span<const ScenarioRegion> regions);

private:
	std::map<std::pair<EntityId, std::string>, SensorEnvelope> values_;
};
} // namespace phoenix
