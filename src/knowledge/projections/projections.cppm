module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.knowledge.projections;
export import phoenix.control.controllers;
export import phoenix.knowledge;

export namespace phoenix
{
struct GameplayKnowledgeProjection
{
	KnowledgeId console = 0;
	std::vector<KnowledgeRecord> records, shared;
	std::vector<CoordinationDelivery> messages;
	std::vector<CoordinationDelivery> notifications;
};
using ControllerCommandSections = std::vector<std::vector<ControllerChannelState>>;
using ProjectionTelemetry = std::map<std::pair<EntityId, std::size_t>, std::map<std::string, std::int64_t>>;
using ProjectionContacts = std::map<std::pair<EntityId, std::string>, std::vector<std::pair<BodyView, int>>>;

// One tick's frozen decision inputs. Readers cannot mutate another Controller's
// snapshot or accidentally observe writes produced later in the same tick.
class ConsoleDecisionFrame
{
public:
	void clear();
	void freeze_commands(EntityId ship, ControllerCommandSections);
	void freeze_observation(const ConsoleAddress &, Observation);
	void freeze_knowledge(const ConsoleAddress &, GameplayKnowledgeProjection);
	void freeze_inbox(const ControllerAddress &, std::vector<CoordinationDelivery>);
	const std::map<EntityId, ControllerCommandSections> &commands() const;
	const std::map<ConsoleAddress, Observation> &observations() const;
	const std::map<ConsoleAddress, GameplayKnowledgeProjection> &knowledge() const;
	const std::map<ControllerAddress, std::vector<CoordinationDelivery>> &inboxes() const;

private:
	std::map<EntityId, ControllerCommandSections> commands_;
	std::map<ConsoleAddress, Observation> observations_;
	std::map<ConsoleAddress, GameplayKnowledgeProjection> knowledge_;
	std::map<ControllerAddress, std::vector<CoordinationDelivery>> inboxes_;
};

// Presentation snapshots last until the next mutation, independently of the
// decision frame. Telemetry/contact caches are workspaces for permitted capture.
class ConsolePresentationCache
{
public:
	void clear();
	std::shared_ptr<const Observation> find(std::uint64_t claimant) const;
	void remember(std::uint64_t claimant, std::shared_ptr<const Observation>);
	ProjectionTelemetry &telemetry();
	ProjectionContacts &contacts();

private:
	ProjectionTelemetry telemetry_;
	ProjectionContacts contacts_;
	std::map<std::uint64_t, std::shared_ptr<const Observation>> observations_;
};
} // namespace phoenix
