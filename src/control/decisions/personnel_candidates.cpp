module;
#include <optional>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>
#include <variant>

module phoenix.control.decisions;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_personnel_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                                const KnowledgeRecord &objective)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	std::vector<Facts> result;
	const auto board = input.observation.gameplay.find("personnel");
	const auto text = [&](const char *name)
	{
		const auto found = objective.value.fields.find(input.definitions.field(objective.value.schema, name));
		return found != objective.value.fields.end() && std::get_if<std::string>(&found->second)
		           ? std::get<std::string>(found->second)
		           : std::string{};
	};
	const auto target = objective.value.fields.find(input.definitions.field(objective.value.schema, "target"));
	const bool target_matches = target == objective.value.fields.end() || !std::get_if<std::int64_t>(&target->second) ||
	                            std::get<std::int64_t>(target->second) == 0 ||
	                            std::get<std::int64_t>(target->second) == static_cast<std::int64_t>(input.entity);
	const bool scope = target_matches && text("mission").empty() && text("compartment").empty();
	const bool authority = board != input.observation.gameplay.end() &&
	                       board->value("controller", std::string{}) == controller.definition.id &&
	                       controller_operation_owns(controller, "personnel", {}) &&
	                       !controller_operation_human(controller, "personnel");
	if (board != input.observation.gameplay.end())
		for (const auto &duty : board->at("duties"))
		{
			const auto id = duty.at("id").get<std::string>();
			if (!text("system").empty() && text("system") != id)
				continue;
			bool filled = false;
			for (const auto &officer : board->at("officers"))
				filled |=
				    officer.at("duty") == id && officer.value("mobile", false) && !officer.value("reserved", true);
			if (filled)
				continue; // An accepted assignment includes physical travel to its duty.
			std::set<std::string> roles;
			for (const auto &effect : duty.at("effects"))
				roles.insert(effect.at("slot"));
			for (const auto &officer : board->at("officers"))
			{
				if (!officer.at("duty").get<std::string>().empty() || officer.at("department") != duty.at("department"))
					continue; // Readiness does not poach another accepted duty.
				bool route = false;
				std::int64_t travel = 0, fit = 0;
				for (const auto &path : officer.value("assignment_routes", Json::array()))
					if (path.at("duty") == id)
					{
						route = path.at("reachable").get<bool>() && path.at("eligible").get<bool>();
						travel = path.at("travel");
					}
				for (const auto &trait : officer.at("traits"))
					for (const auto &contribution : trait.at("contributions"))
						if (roles.contains(contribution.at("role").get<std::string>()))
							fit += contribution.at("delta_permille").get<std::int64_t>();
				const bool available = officer.value("mobile", false) && !officer.value("reserved", true) &&
				                       !duty.value("assignment_reserved", true);
				const bool eligible = scope && authority && available && route;
				result.push_back(
				    {{"personnel_candidate.eligible", Value::boolean(eligible)},
				     {"personnel_candidate.officer", Value::string(officer.at("id"))},
				     {"personnel_candidate.duty", Value::string(id)},
				     {"personnel_candidate.department", Value::string(duty.at("department"))},
				     {"personnel_candidate.revision", Value::integer(board->at("revision"))},
				     {"personnel_candidate.travel", Value::integer(travel)},
				     {"personnel_candidate.fit", Value::integer(std::clamp<std::int64_t>(1000 + fit, 0, 1000000))},
				     {"personnel_candidate.reason",
				      Value::string(!scope       ? "unsupported_personnel_scope"
				                    : !authority ? "personnel_not_delegated"
				                    : !available ? "officer_or_duty_unavailable"
				                    : !route     ? "known_assignment_route_or_qualification_unavailable"
				                                 : "permitted_departmental_roster")}});
			}
		}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("personnel_candidate."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["personnel_candidate.reason"] = Value::string("no_known_vacant_duty_or_available_officer");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter personnel_adapter()
{
	return {"personnel",
	        "objective_parameter_outside_addressed_request_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(controller_personnel_candidates(controller, input, record), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "personnel" && candidate.reason == "binding_not_applicable" &&
		            !context.fact("personnel_candidate.eligible").number)
			        candidate.reason = context.fact("personnel_candidate.reason").text;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.binding.candidate_source == "personnel")
		        {
			        Json evidence{{"source", "departmental_roster"},
			                      {"observed_tick", context.input.observation.observed_tick}};
			        for (const auto *name :
			             {"officer", "duty", "department", "eligible", "fit", "travel", "revision", "reason"})
			        {
				        const auto &value = context.fact((std::string("personnel_candidate.") + name).c_str());
				        if (value.type == state_tree::Type::Text)
					        evidence[name] = value.text;
				        else if (value.type == state_tree::Type::Boolean)
					        evidence[name] = value.number != 0;
				        else
					        evidence[name] = value.number;
			        }
			        return evidence;
		        }
		        return {};
	        }};
}
} // namespace phoenix
