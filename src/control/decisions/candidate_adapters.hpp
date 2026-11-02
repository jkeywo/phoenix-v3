#pragma once
const KnowledgeValue *objective_field(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                      const KnowledgeRecord &, ObjectiveField);
std::string objective_text(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                           const KnowledgeRecord &, ObjectiveField, const std::string &fallback = "");
std::int64_t objective_number(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                              const KnowledgeRecord &, ObjectiveField, std::int64_t fallback = 0);
CandidateFacts empty_candidate_facts(const ConsoleControllerRuntime &, CandidateGroup);
const RuntimeObjectiveBinding &compiled_objective_binding(const ConsoleControllerRuntime &,
                                                          const ControllerObjectiveBinding &);
struct DecisionCandidateFrame
{
	std::vector<state_tree::Value> values;
	bool complete = false;
};
struct CandidateContext
{
	const ConsoleControllerRuntime &controller;
	const ControllerObservationSources &input;
	const ControllerObjectiveBinding &binding;
	const KnowledgeRecord *record;
	const ControllerBrain &brain;
	const std::string &behavior;
	std::vector<state_tree::Value> &values;
	std::span<const state_tree::Value> option;
	CandidateSource source() const
	{
		return compiled_objective_binding(controller, binding).source;
	}
	const state_tree::Value &fact(CandidateField) const;
	std::string text(const KnowledgeRecord &, ObjectiveField, const std::string &) const;
	std::int64_t number(const KnowledgeRecord &, ObjectiveField, std::int64_t fallback = 0) const;
};
struct CandidateCache
{
	const ConsoleControllerRuntime &controller;
	const ControllerObservationSources &input;
	std::span<const state_tree::Value> source;
	mutable std::optional<std::vector<std::vector<state_tree::Value>>> medical_values;
	mutable std::optional<std::vector<ObjectiveFacts>> repair_values, request_values, away_values, contact_values,
	    armament_values, scan_values;
	const std::vector<std::vector<state_tree::Value>> &medical_options() const;
	const std::vector<ObjectiveFacts> &repair_options() const;
	const std::vector<ObjectiveFacts> &request_options() const;
	const std::vector<ObjectiveFacts> &away_options() const;
	const std::vector<ObjectiveFacts> &contact_options() const;
	const std::vector<ObjectiveFacts> &armament_options() const;
	const std::vector<ObjectiveFacts> &scan_options() const;
	CandidateCache(const ConsoleControllerRuntime &, const ControllerObservationSources &,
	               std::span<const state_tree::Value>);
};
struct CandidateAdapter
{
	const char *name, *scope_reason;
	bool contact_geometry;
	std::vector<DecisionCandidateFrame> (*enumerate)(const CandidateCache &, const ConsoleControllerRuntime &,
	                                                 const ControllerObservationSources &,
	                                                 const ControllerObjectiveBinding &, const KnowledgeRecord &,
	                                                 std::span<const state_tree::Value>);
	bool (*prepare)(CandidateContext &);
	void (*describe)(CandidateContext &, ObjectiveCandidate &);
	void (*failed)(CandidateContext &, ObjectiveCandidate &);
	Json (*evidence)(CandidateContext &);
};
const CandidateAdapter &candidate_adapter(CandidateSource);

std::vector<DecisionCandidateFrame> candidate_frames(const std::vector<ObjectiveFacts> &,
                                                     const ConsoleControllerRuntime &,
                                                     std::span<const state_tree::Value>);
CandidateAdapter single_adapter();
CandidateAdapter medical_adapter();
CandidateAdapter craft_adapter();
CandidateAdapter requests_adapter();
CandidateAdapter away_adapter();
CandidateAdapter repair_adapter();
CandidateAdapter contacts_adapter();
CandidateAdapter armament_adapter();
CandidateAdapter scans_adapter();
CandidateAdapter reports_adapter();
CandidateAdapter docks_adapter();
CandidateAdapter personnel_adapter();
CandidateAdapter delivery_adapter();
