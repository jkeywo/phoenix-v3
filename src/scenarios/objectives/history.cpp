module;
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

module phoenix.scenarios.objectives;

namespace phoenix
{
MissionTransition append_mission_transition(MissionLedger &ledger, std::uint64_t tick, std::uint64_t instance,
                                            std::size_t maximum_events, std::string kind, EntityId source,
                                            EntityId subject, std::string operation, std::string detail)
{
	if (ledger.transitions.size() >= maximum_events)
		throw std::runtime_error("Mission transition journal exhausted");
	if (!ledger.instance)
		ledger.instance = instance;
	MissionTransition transition{ledger.next++,    tick, source, subject, std::move(kind), std::move(operation),
	                             std::move(detail)};
	ledger.transitions.push_back(transition);
	return transition;
}
bool update_mission_state(MissionLedger &ledger, const std::string &key, const std::string &value)
{
	auto &old = ledger.states[key];
	if (old == value)
		return false;
	old = value;
	return true;
}
void validate_mission_history(const MissionLedger &m, std::uint64_t tick, std::span<const EntityId> identities,
                              bool valid_instance)
{
	const std::set<EntityId> entities(identities.begin(), identities.end());
	if (!m.next || m.transitions.size() > 16384 || m.states.size() > 65536 || m.lost.size() > entities.size())
		throw std::runtime_error("Invalid mission ledger bounds");
	std::uint64_t last = 0;
	for (const auto &t : m.transitions)
	{
		if (t.id <= last || t.id >= m.next || t.tick > tick || t.kind.size() > 128 || t.operation.size() > 256 ||
		    t.detail.size() > 512 || (t.subject && !entities.contains(t.subject)) ||
		    (t.source && !entities.contains(t.source)))
			throw std::runtime_error("Invalid mission transition");
		last = t.id;
	}
	if (m.instance && !valid_instance)
		throw std::runtime_error("Invalid mission event source");
}
} // namespace phoenix
