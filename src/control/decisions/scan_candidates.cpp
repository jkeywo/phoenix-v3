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
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_scan_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	std::set<std::string> attached;
	for (const auto &[role, systems] : controller.definition.equipment)
		attached.insert(systems.begin(), systems.end());
	std::map<EntityId, Facts> targets;
	for (const auto &contact : input.observation.contacts)
		if (contact.id != input.entity)
			targets[contact.id] = {
			    {"scan_candidate.target", Value::integer(contact.id)},
			    {"scan_candidate.distance", Value::integer(fixed::length({contact.x - input.observation.own.x,
			                                                              contact.y - input.observation.own.y}))},
			    {"scan_candidate.age", Value::integer(0)},
			    {"scan_candidate.positional", Value::boolean(true)},
			    {"scan_candidate.hostile", Value::boolean(contact.disposition == "hostile")}};
	for (const auto &bearing : input.observation.bearing_contacts)
		if (bearing.id != input.entity && bearing.observed_tick <= input.tick && !targets.contains(bearing.id))
			targets[bearing.id] = {{"scan_candidate.target", Value::integer(bearing.id)},
			                       {"scan_candidate.distance", Value::integer(0)},
			                       {"scan_candidate.age", Value::integer(input.tick - bearing.observed_tick)},
			                       {"scan_candidate.positional", Value::boolean(false)},
			                       {"scan_candidate.hostile", Value::boolean(false)}};
	std::vector<Facts> result;
	const auto sensors = input.observation.gameplay.find("sensors");
	if (sensors != input.observation.gameplay.end())
		for (const auto &scanner : sensors->at("scanners"))
		{
			const auto id = scanner.at("id").get<std::string>();
			if (!attached.contains(id))
				continue;
			for (const auto &[target, contact] : targets)
			{
				Facts facts = contact;
				const bool continuing = scanner.at("target") == target;
				const bool powered = scanner.value("powered", false);
				const bool in_range =
				    !facts.at("scan_candidate.positional").number ||
				    facts.at("scan_candidate.distance").number <= scanner.at("range_mm").get<std::int64_t>();
				std::int64_t remaining = 0, known = 0;
				if (continuing)
					for (const auto &progress : scanner.at("progress"))
						remaining += std::max<std::int64_t>(0, progress.at("required").get<std::int64_t>() -
						                                           progress.at("ticks").get<std::int64_t>());
				const Json *findings = nullptr;
				for (const auto &evidence : sensors->at("contacts"))
					if (evidence.at("id") == target)
					{
						findings = &evidence.at("findings");
						known = static_cast<std::int64_t>(findings->size());
					}
				std::int64_t missing = 0;
				KnowledgeTick oldest = input.tick;
				for (const auto &[capability, required] : scanner.at("capabilities").items())
					if (required.get<std::int64_t>() > 0 && capability != "own_arcs")
					{
						if (!findings || !findings->contains(capability))
							++missing;
						else
							oldest = std::min(oldest, findings->at(capability).at("observed").get<KnowledgeTick>());
					}

				bool requested = false;
				std::int64_t priority = 0;
				for (const auto &request : controller.incoming_requests)
					if (request.operation == OfficerOperation::Scan && request.status == OfficerStatus::Accepted &&
					    request.expires > input.tick && request.target == target &&
					    scanner.at("capabilities").contains(request.capability) &&
					    !scan_completion_evidence(input.observation, request))
					{
						requested = true;
						priority = std::max(priority, request.priority);
					}
				facts["scan_candidate.requested"] = Value::boolean(requested);
				facts["scan_candidate.request_priority"] = Value::integer(priority);
				facts["scan_candidate.scanner"] = Value::string(id);
				facts["scan_candidate.available"] = Value::boolean(powered && in_range);
				facts["scan_candidate.continuing"] = Value::boolean(continuing);
				facts["scan_candidate.blocked"] =
				    Value::boolean(continuing && !scanner.at("blocker").get<std::string>().empty());
				facts["scan_candidate.remaining"] = Value::integer(remaining);
				facts["scan_candidate.known"] = Value::integer(known);
				facts["scan_candidate.missing"] = Value::integer(missing);
				facts["scan_candidate.evidence_age"] = Value::integer(input.tick - oldest);
				facts["scan_candidate.reason"] = Value::string(!powered    ? "scanner_unavailable"
				                                               : !in_range ? "target_outside_scanner_range"
				                                                           : "permitted_scan_target");
				result.push_back(std::move(facts));
			}
		}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("scan_candidate."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["scan_candidate.reason"] = Value::string("no_permitted_scan_target");
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
		        if (context.binding.candidate_source == "scans" &&
		            (!context.text(*context.record, "mission", "").empty() ||
		             !context.text(*context.record, "compartment", "").empty() ||
		             (!context.text(*context.record, "system", "").empty() &&
		              context.text(*context.record, "system", "") != context.fact("scan_candidate.scanner").text) ||
		             (context.number(*context.record, "target") &&
		              context.number(*context.record, "target") != context.fact("scan_candidate.target").number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "scans" && candidate.reason == "binding_not_applicable")
		        {
			        if (!context.fact("scan_candidate.available").number)
				        candidate.reason = context.fact("scan_candidate.reason").text;
			        else if (context.behavior == "engage" && !context.fact("scan_candidate.hostile").number)
				        candidate.reason = "no_known_hostile";
		        }
		        if (context.binding.candidate_source == "scans")
			        candidate.target = context.fact("scan_candidate.target").number;
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
