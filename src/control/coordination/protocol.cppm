module;
#include <functional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>

export module phoenix.control.protocol;
export import phoenix.control.controllers;
export import phoenix.knowledge;

export namespace phoenix
{
struct OfficerProtocolPorts
{
	std::function<void(KnowledgeId)> acknowledge;
	std::function<void(const OfficerRequest &)> reply;
};
// Consume only this Controller's supplied frozen inbox. The two output ports
// retain synchronous admission/acknowledgement order and expose no native lookup.
void process_officer_protocol(EntityId, KnowledgeTick now, ConsoleControllerRuntime &,
                              std::span<const CoordinationDelivery>, const CompiledKnowledge &,
                              std::span<const KnowledgeId> known_peers, const OfficerProtocolPorts &);
struct OfficerActionPorts
{
	std::function<KnowledgeId(const std::string &console, const std::string &controller)> peer;
	std::function<const Observation &()> observation;
	std::function<KnowledgeId(const OfficerRequest &, bool reply)> send;
};
bool officer_coordination_action(ConsoleControllerRuntime &, KnowledgeTick now, const std::string &,
                                 std::span<const state_tree::Value>, const OfficerActionPorts &);
} // namespace phoenix
