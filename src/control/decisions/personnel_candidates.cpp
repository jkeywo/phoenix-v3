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
std::vector<CandidateFacts> controller_personnel_candidates(const ConsoleControllerRuntime &controller,
                                                            const ControllerObservationSources &input,
                                                            const KnowledgeRecord &objective)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	std::vector<Facts> result;
	const auto &board = input.observation.personnel;
	const auto text = [&](ObjectiveField field)
	{
		return objective_text(controller, input, objective, field, "");
	};
	const auto target = objective_number(controller, input, objective, ObjectiveField::target);
	const bool target_matches = target == 0 || target == static_cast<std::int64_t>(input.entity);
	const bool scope =
	    target_matches && text(ObjectiveField::mission).empty() && text(ObjectiveField::compartment).empty();
	const bool authority = board.present && board.controller == controller.definition.id &&
	                       controller_operation_owns(controller, "personnel", {}) &&
	                       !controller_operation_human(controller, "personnel");
	if (board.present)
		for (const auto &duty : board.duties)
		{
			const auto id = duty.id;
			if (!text(ObjectiveField::system).empty() && text(ObjectiveField::system) != id)
				continue;
			bool filled = false;
			for (const auto &officer : board.officers)
				filled |= officer.duty == id && officer.mobile && !officer.reserved;
			if (filled)
				continue; // An accepted assignment includes physical travel to its duty.
			std::set<std::string> roles;
			for (const auto &effect : duty.effects)
				roles.insert(effect.slot);
			for (const auto &officer : board.officers)
			{
				if (!officer.duty.empty() || officer.department != duty.department)
					continue; // Readiness does not poach another accepted duty.
				bool route = false;
				std::int64_t travel = 0, fit = 0;
				for (const auto &path : officer.assignment_routes)
					if (path.duty == id)
					{
						route = path.reachable && path.eligible;
						travel = path.travel;
					}
				for (const auto &trait : officer.traits)
					for (const auto &contribution : trait.contributions)
						if (roles.contains(contribution.role))
							fit += contribution.delta_permille;
				const bool available = officer.mobile && !officer.reserved && !duty.assignment_reserved;
				const bool eligible = scope && authority && available && route;
				result.push_back({{CandidateField::personnel_candidate_eligible, Value::boolean(eligible)},
				                  {CandidateField::personnel_candidate_officer, Value::string(officer.id)},
				                  {CandidateField::personnel_candidate_duty, Value::string(id)},
				                  {CandidateField::personnel_candidate_department, Value::string(duty.department)},
				                  {CandidateField::personnel_candidate_revision, Value::integer(board.revision)},
				                  {CandidateField::personnel_candidate_travel, Value::integer(travel)},
				                  {CandidateField::personnel_candidate_fit,
				                   Value::integer(std::clamp<std::int64_t>(1000 + fit, 0, 1000000))},
				                  {CandidateField::personnel_candidate_reason,
				                   Value::string(!scope       ? "unsupported_personnel_scope"
				                                 : !authority ? "personnel_not_delegated"
				                                 : !available ? "officer_or_duty_unavailable"
				                                 : !route     ? "known_assignment_route_or_qualification_unavailable"
				                                              : "permitted_departmental_roster")}});
			}
		}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::personnel_candidate);
		empty[CandidateField::personnel_candidate_reason] = Value::string("no_known_vacant_duty_or_available_officer");
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
		        if (context.source() == CandidateSource::personnel && candidate.reason == "binding_not_applicable" &&
		            !context.fact(CandidateField::personnel_candidate_eligible).number)
			        candidate.reason = context.fact(CandidateField::personnel_candidate_reason).text;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.source() == CandidateSource::personnel)
		        {
			        Json evidence{{"source", "departmental_roster"},
			                      {"observed_tick", context.input.observation.observed_tick}};
			        const std::pair<const char *, CandidateField> fields[] = {
			            {"officer", CandidateField::personnel_candidate_officer},
			            {"duty", CandidateField::personnel_candidate_duty},
			            {"department", CandidateField::personnel_candidate_department},
			            {"eligible", CandidateField::personnel_candidate_eligible},
			            {"fit", CandidateField::personnel_candidate_fit},
			            {"travel", CandidateField::personnel_candidate_travel},
			            {"revision", CandidateField::personnel_candidate_revision},
			            {"reason", CandidateField::personnel_candidate_reason}};
			        for (const auto &[name, field] : fields)
			        {
				        const auto &value = context.fact(field);
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
