module;
#include <optional>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <set>
#include <string>
#include <vector>

module phoenix.control.decisions;
import phoenix.knowledge.decision_combat;
import phoenix.control.candidate_facts;
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<CandidateFacts> controller_scan_candidates(const ConsoleControllerRuntime &controller,
                                                       const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	std::set<std::string> attached;
	for (const auto &[role, systems] : controller.definition.equipment)
		attached.insert(systems.begin(), systems.end());
	std::map<EntityId, Facts> targets;
	for (const auto &contact : input.observation.contacts)
		if (contact.id != input.entity)
			targets[contact.id] = {
			    {CandidateField::scan_candidate_target, Value::integer(contact.id)},
			    {CandidateField::scan_candidate_distance,
			     Value::integer(
			         fixed::length({contact.x - input.observation.own.x, contact.y - input.observation.own.y}))},
			    {CandidateField::scan_candidate_age, Value::integer(0)},
			    {CandidateField::scan_candidate_positional, Value::boolean(true)},
			    {CandidateField::scan_candidate_hostile, Value::boolean(contact.disposition == "hostile")}};
	for (const auto &bearing : input.observation.bearing_contacts)
		if (bearing.id != input.entity && bearing.observed_tick <= input.tick && !targets.contains(bearing.id))
			targets[bearing.id] = {
			    {CandidateField::scan_candidate_target, Value::integer(bearing.id)},
			    {CandidateField::scan_candidate_distance, Value::integer(0)},
			    {CandidateField::scan_candidate_age, Value::integer(input.tick - bearing.observed_tick)},
			    {CandidateField::scan_candidate_positional, Value::boolean(false)},
			    {CandidateField::scan_candidate_hostile, Value::boolean(false)}};
	std::vector<Facts> result;
	const auto &sensors = input.observation.sensors;
	for (const auto &scanner : sensors.scanners)
	{
		const auto id = scanner.id;
		if (!attached.contains(id))
			continue;
		for (const auto &[target, contact] : targets)
		{
			Facts facts = contact;
			const bool continuing = scanner.target == target;
			const bool powered = scanner.powered;
			const bool in_range = !facts.at(CandidateField::scan_candidate_positional).number ||
			                      facts.at(CandidateField::scan_candidate_distance).number <= scanner.range_mm;
			std::int64_t remaining = 0, known = 0;
			if (continuing)
				for (const auto &progress : scanner.progress)
					remaining += std::max<std::int64_t>(0, progress.required - progress.ticks);
			const std::map<std::string, SensorFindingDecision> *findings = nullptr;
			for (const auto &evidence : sensors.contacts)
				if (evidence.id == target)
				{
					findings = &evidence.findings;
					known = static_cast<std::int64_t>(findings->size());
				}
			std::int64_t missing = 0;
			KnowledgeTick oldest = input.tick;
			for (const auto &[capability, required] : scanner.capabilities)
				if (required > 0 && capability != "own_arcs")
				{
					if (!findings || !findings->contains(capability))
						++missing;
					else
						oldest = std::min(oldest, findings->at(capability).observed);
				}

			bool requested = false;
			std::int64_t priority = 0;
			for (const auto &request : controller.incoming_requests)
				if (request.operation == OfficerOperation::Scan && request.status == OfficerStatus::Accepted &&
				    request.expires > input.tick && request.target == target &&
				    scanner.capabilities.contains(request.capability) &&
				    !scan_completion_evidence(input.observation, request))
				{
					requested = true;
					priority = std::max(priority, request.priority);
				}
			facts[CandidateField::scan_candidate_requested] = Value::boolean(requested);
			facts[CandidateField::scan_candidate_request_priority] = Value::integer(priority);
			facts[CandidateField::scan_candidate_scanner] = Value::string(id);
			facts[CandidateField::scan_candidate_available] = Value::boolean(powered && in_range);
			facts[CandidateField::scan_candidate_continuing] = Value::boolean(continuing);
			facts[CandidateField::scan_candidate_blocked] = Value::boolean(continuing && !scanner.blocker.empty());
			facts[CandidateField::scan_candidate_remaining] = Value::integer(remaining);
			facts[CandidateField::scan_candidate_known] = Value::integer(known);
			facts[CandidateField::scan_candidate_missing] = Value::integer(missing);
			facts[CandidateField::scan_candidate_evidence_age] = Value::integer(input.tick - oldest);
			facts[CandidateField::scan_candidate_reason] = Value::string(!powered    ? "scanner_unavailable"
			                                                             : !in_range ? "target_outside_scanner_range"
			                                                                         : "permitted_scan_target");
			result.push_back(std::move(facts));
		}
	}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::scan_candidate);
		empty[CandidateField::scan_candidate_reason] = Value::string("no_permitted_scan_target");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter scans_adapter()
{
	return {"scans",
	        "objective_parameter_outside_known_scan_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(cache.scan_options(), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::scans &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::scan_candidate_scanner).text) ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::scan_candidate_target).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::scans && candidate.reason == "binding_not_applicable")
		        {
			        if (!context.fact(CandidateField::scan_candidate_available).number)
				        candidate.reason = context.fact(CandidateField::scan_candidate_reason).text;
			        else if (context.behavior == "engage" &&
			                 !context.fact(CandidateField::scan_candidate_hostile).number)
				        candidate.reason = "no_known_hostile";
		        }
		        if (context.source() == CandidateSource::scans)
			        candidate.target = context.fact(CandidateField::scan_candidate_target).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
} // namespace phoenix
