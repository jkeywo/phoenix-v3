module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

export module phoenix.control.decisions;
export import phoenix.control.objectives;
export import phoenix.control.controllers;
export import phoenix.world.state;
export import phoenix.knowledge.projections;
export import phoenix.content.effects.types;

export namespace phoenix
{
struct ControllerDecisionContext
{
	KnowledgeTick tick = 0;
	std::set<std::string> owned_systems, intercepting_weapons;
	std::int64_t deceleration = 1, altitude_offset = 0;
	bool escort = false;
	fixed::Vec escort_anchor;
	std::int64_t engagement_range = 0;
};
// Borrowed immutable inputs to compiled Controller bindings. Knowledge rules
// expose staleness/schema metadata, never a lookup of arbitrary physical truth.
struct ControllerObservationSources
{
	EntityId entity;
	KnowledgeTick tick;
	const std::set<KnowledgeId> &priority_objectives;
	const EntityControls &controls;
	const Observation &observation;
	const GameplayKnowledgeProjection &board;
	const ConsoleDecisionFrame &frame;
	const CompiledKnowledge &definitions;
	std::span<const CoordinationDelivery> inbox;
	std::function<bool(const KnowledgeRecord &, KnowledgeTick)> stale;
	std::function<std::string(KnowledgeId)> request_key_name;
};
// Adds only currently permitted retained local inspections to a private decision view.
std::optional<Observation> controller_inspection_observation(const Observation &, const GameplayKnowledgeProjection &,
                                                             const CompiledKnowledge &, KnowledgeTick);
std::vector<std::map<std::string, state_tree::Value>>
controller_contact_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &);
std::optional<ControllerObjectiveChoice> evaluate_controller_objectives(const ConsoleControllerRuntime &,
                                                                        const ControllerObservationSources &,
                                                                        std::span<const state_tree::Value>, Json &);
void append_controller_observations(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                    std::vector<state_tree::Value> &);
std::vector<std::vector<state_tree::Value>> controller_medical_candidates(const ConsoleControllerRuntime &,
                                                                          const Observation &);
std::vector<std::map<std::string, state_tree::Value>> controller_repair_candidates(const ConsoleControllerRuntime &,
                                                                                   const Observation &, KnowledgeTick);
void append_controller_medical_facts(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                     std::vector<state_tree::Value> &);
void append_controller_workflow_facts(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                      std::vector<state_tree::Value> &);
void append_controller_execution_facts(EntityId, const ConsoleControllerRuntime &, const EntityControls &,
                                       const Observation &, std::span<const AbilityControlDefinition>,
                                       std::span<const ProcessControlDefinition>, std::vector<state_tree::Value> &);
const Json &combat_observation(const Observation &);
void controller_decision_facts(const ConsoleControllerRuntime &, const Observation &, const ControllerDecisionContext &,
                               std::vector<state_tree::Value> &);
void controller_report_facts(const ConsoleControllerRuntime &, const Observation &, const ControllerDecisionContext &,
                             std::vector<state_tree::Value> &);
} // namespace phoenix
