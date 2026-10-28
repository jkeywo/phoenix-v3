module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module phoenix.knowledge.projections;

namespace phoenix
{
void ConsoleDecisionFrame::clear()
{
	knowledge_.clear();
	observations_.clear();
	commands_.clear();
	inboxes_.clear();
}
void ConsoleDecisionFrame::freeze_commands(EntityId ship, ControllerCommandSections commands)
{
	commands_.emplace(ship, std::move(commands));
}
void ConsoleDecisionFrame::freeze_observation(const ConsoleAddress &address, Observation observation)
{
	observations_.emplace(address, std::move(observation));
}
void ConsoleDecisionFrame::freeze_knowledge(const ConsoleAddress &address, GameplayKnowledgeProjection knowledge)
{
	knowledge_.emplace(address, std::move(knowledge));
}
void ConsoleDecisionFrame::freeze_inbox(const ControllerAddress &address, std::vector<CoordinationDelivery> inbox)
{
	inboxes_.emplace(address, std::move(inbox));
}
const std::map<EntityId, ControllerCommandSections> &ConsoleDecisionFrame::commands() const
{
	return commands_;
}
const std::map<ConsoleAddress, Observation> &ConsoleDecisionFrame::observations() const
{
	return observations_;
}
const std::map<ConsoleAddress, GameplayKnowledgeProjection> &ConsoleDecisionFrame::knowledge() const
{
	return knowledge_;
}
const std::map<ControllerAddress, std::vector<CoordinationDelivery>> &ConsoleDecisionFrame::inboxes() const
{
	return inboxes_;
}
void ConsolePresentationCache::clear()
{
	observations_.clear();
	telemetry_.clear();
	contacts_.clear();
}
std::shared_ptr<const Observation> ConsolePresentationCache::find(std::uint64_t claimant) const
{
	const auto found = observations_.find(claimant);
	return found == observations_.end() ? std::shared_ptr<const Observation>{} : found->second;
}
void ConsolePresentationCache::remember(std::uint64_t claimant, std::shared_ptr<const Observation> observation)
{
	observations_.emplace(claimant, std::move(observation));
}
ProjectionTelemetry &ConsolePresentationCache::telemetry()
{
	return telemetry_;
}
ProjectionContacts &ConsolePresentationCache::contacts()
{
	return contacts_;
}
} // namespace phoenix
