module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
export module phoenix.gameplay.sensors.validation;
export import phoenix.gameplay.sensors.state;
export import phoenix.content.sensing.types;
export import phoenix.content.combat.types;
export import phoenix.gameplay.psychic.state;
export namespace phoenix
{
struct SensorRestoreContext
{
	EntityId ship, selected;
	std::uint64_t tick;
	const SensorDefinition &definition;
	const DirectedDefinition *directed;
	const std::set<EntityId> &physical_entities;
	const std::map<EntityId, GhostReception> &ghosts;
	const std::map<EntityId, std::int64_t> &ghost_scan_ticks;
	std::span<const SensorFinding> findings;
};
SensorRuntime read_sensor_runtime(const Json &row, const SensorRestoreContext &context);
Json save_sensor_runtime(EntityId ship, const std::string &system, const SensorRuntime &state);
} // namespace phoenix
