module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
export module phoenix.gameplay.sensors.evidence;
export import phoenix.gameplay.sensors.state;
export import phoenix.knowledge.decision_combat;
export import phoenix.knowledge.consoles;
export namespace phoenix
{
std::map<EntityId, SensorContactDecision> collect_sensor_decision_evidence(const ConsoleAddress &,
                                                                           std::span<const SensorRuntime *const>,
                                                                           const GameplayKnowledge &,
                                                                           std::uint64_t tick);
// Sources must be the seat's permitted fitted scanner feed, in authored order.
std::map<EntityId, Json> collect_sensor_evidence(const ConsoleAddress &address,
                                                 std::span<const SensorRuntime *const> sources,
                                                 const GameplayKnowledge &knowledge, std::uint64_t tick);
} // namespace phoenix
