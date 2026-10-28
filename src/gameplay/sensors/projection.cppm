module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
export module phoenix.gameplay.sensors.projection;
export import phoenix.gameplay.sensors.state;
export import phoenix.content.sensing.types;
export namespace phoenix
{
struct SensorScannerProjection
{
	std::string id;
	SensorDefinition definition;
	const SensorRuntime *state = nullptr;
	EntityId selected = 0;
	std::int64_t heading = 0, range = 0;
	fixed::Vec origin;
	bool powered = false;
	std::optional<std::string> mode;
};
// Inputs are captured only from the Console's permitted fitted scanner feeds.
Json project_sensor_board(std::span<const SensorScannerProjection> scanners, const std::map<EntityId, Json> &evidence,
                          std::uint64_t tick);
} // namespace phoenix
