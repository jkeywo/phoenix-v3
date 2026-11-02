module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

export module phoenix.control.execution;
export import phoenix.control.controllers;
export import phoenix.knowledge.decision;
export import phoenix.world.state;

export namespace phoenix
{
// The interpreter receives the same permitted snapshot used by the Console and
// emits ordinary admitted outputs. Neither port offers arbitrary World access.
struct ControllerActionPorts
{
	std::function<const DecisionObservation &()> observation;
	std::function<void(const state_tree::Output &, std::span<const state_tree::Value>, const state_tree::Program *)>
	    output;
	std::function<state_tree::CommandAttempt(std::vector<ControllerChannelState> &, const state_tree::Program &,
	                                         const state_tree::Output &)>
	    command;
};
// Successful sends are bounded private continuation, never a source of new evidence.
void remember_controller_report(ConsoleControllerRuntime &, const std::string &, const KnowledgeRecord &,
                                KnowledgeTick);
std::vector<state_tree::Value> controller_action_observations(const ConsoleControllerRuntime &,
                                                              std::span<const state_tree::Value>,
                                                              const ControllerActionPorts &);
void cancel_controller_action(ConsoleControllerRuntime &, const std::string &reason, std::span<const state_tree::Value>,
                              const ControllerActionPorts &);
bool select_controller_action(ConsoleControllerRuntime &, const std::string &name, const std::string &invocation,
                              std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> source,
                              std::uint64_t generation, const ControllerActionPorts &);
void run_controller_action(ConsoleControllerRuntime &, std::span<const state_tree::Value>,
                           const ControllerActionPorts &);
void step_controller_policy(ConsoleControllerRuntime &, std::span<const state_tree::Value>,
                            const ControllerActionPorts &);
using ControllerArgumentCheck = std::function<bool(std::size_t controller, std::size_t channel, OperatingAction,
                                                   std::int64_t, std::span<const std::int64_t> arguments)>;
struct ControllerCommandChecks
{
	std::function<bool(const ControllerProposal &)> channel_valid;
	ControllerArgumentCheck arguments_valid;
	std::function<bool(const ControllerCommandSpec &, const ControllerProposal &)> same_effect;
};
bool commit_controller_command(EntityControls &, const ControllerProposal &, const ControllerCommandChecks &);
state_tree::CommandAttempt attempt_controller_command(const ConsoleControllerRuntime &,
                                                      std::vector<ControllerChannelState> &, const ControllerProposal &,
                                                      const ControllerCommandChecks &);
Json save_controllers(const EntityControls &);
void restore_controllers(EntityControls &, const Json &, const ControllerArgumentCheck &,
                         std::span<const KnowledgeId> known_peers);
} // namespace phoenix
