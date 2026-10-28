module;
#include <nlohmann/json.hpp>
#include <span>
#include <string>

export module phoenix.control.providers;
export import phoenix.control.controllers;

export namespace phoenix
{
bool model_action_available(const ConsoleControllerRuntime &, const std::string &name,
                            std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> observed);
Json model_candidates(const ConsoleControllerRuntime &, std::span<const state_tree::Value> observed);
Json model_basis(const BridgeBlackboard &, const ConsoleControllerRuntime &);
Json build_controller_snapshot(const ControllerAddress &, KnowledgeTick tick, const ConsoleControllerRuntime &,
                               std::span<const state_tree::Value> observed, Json board, KnowledgeId actor,
                               const BridgeBlackboard &);
} // namespace phoenix
