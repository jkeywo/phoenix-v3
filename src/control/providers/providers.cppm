module;
#include <nlohmann/json.hpp>
#include <span>
#include <cstddef>
#include <optional>
#include <vector>
#include <string>

export module phoenix.control.providers;
export import phoenix.control.controllers;

export namespace phoenix
{
struct ModelOperation
{
	ControllerCandidateOperationKind kind = ControllerCandidateOperationKind::Coordination;
	std::string name;
	std::optional<std::size_t> action;
	std::vector<state_tree::Value> arguments;
};
struct ModelCandidate
{
	std::string id, description, deviation;
	std::vector<ModelOperation> operations;
};
bool model_action_available(const ConsoleControllerRuntime &, std::size_t action,
                            std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> observed);
std::vector<ModelCandidate> evaluate_model_candidates(const ConsoleControllerRuntime &,
                                                      std::span<const state_tree::Value> observed);
// Outward provider wire encoding; these JSON values never supply execution arguments.
Json model_candidate_json(const ConsoleControllerRuntime &, const ModelCandidate &);
Json model_candidates(const ConsoleControllerRuntime &, std::span<const state_tree::Value> observed);
Json model_basis(const BridgeBlackboard &, const ConsoleControllerRuntime &);
Json build_controller_snapshot(const ControllerAddress &, KnowledgeTick tick, const ConsoleControllerRuntime &,
                               std::span<const state_tree::Value> observed, Json board, KnowledgeId actor,
                               const BridgeBlackboard &, bool full_diagnostics = false);
} // namespace phoenix
