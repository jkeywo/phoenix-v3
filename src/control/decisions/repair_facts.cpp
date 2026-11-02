module;
#include <optional>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

module phoenix.control.decisions;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<CandidateFacts> controller_repair_candidates(const ConsoleControllerRuntime &controller,
                                                         const DecisionObservation &observed, KnowledgeTick tick)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	if (!controller_operation_owns(controller, "team_assignment", {}))
		return {};
	struct Need
	{
		std::int64_t deficit = 0, priority = 0;
		bool damaged = false, disabled = false, fault = false, symptom = false, requested = false, inspected = false,
		     reported = false;
	};
	std::map<std::string, Need> needs;
	std::map<std::int64_t, std::string> compartments;
	const auto &combat = observed.combat;
	for (const auto &room : combat.compartments)
	{
		const auto name = room.id;
		compartments[room.index] = name;
		auto &need = needs[name];
		const auto maximum = room.maximum;
		need.deficit = maximum > 0 ? std::clamp<std::int64_t>((maximum - room.hp) * 1000 / maximum, 0, 1000) : 0;
		need.damaged = need.deficit > 0;
		need.disabled = room.disabled;
		need.inspected = room.inspected || room.reported_inspection;
	}
	for (const auto &fault : combat.faults)
		if (!fault.destroyed)
		{
			auto &need = needs[fault.compartment];
			need.fault = true;
			need.reported |= fault.evidence_present;
		}
	for (const auto &report : observed.repair_needs)
	{
		const auto room = compartments.find(report.compartment_index);
		if (room == compartments.end())
			continue;
		auto &need = needs[room->second];
		need.symptom = true;
		need.disabled |= report.structural;
	}
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Repair && request.status == OfficerStatus::Accepted &&
		    request.expires > tick)
		{
			const auto room = compartments.find(request.target);
			if (room == compartments.end())
				continue;
			auto &need = needs[room->second];
			if (need.inspected && !need.damaged && !need.fault && !need.symptom)
				continue;
			need.requested = true;
			need.priority = std::max(need.priority, request.priority);
		}
	std::vector<Facts> result;
	for (const auto &team : observed.teams)
	{
		if (!team.repair || team.work_operation != "team_work")
			continue;
		const auto id = team.id;
		if (!controller_operation_owns(controller, "team_assignment", id))
			continue;
		const bool authority = !controller_operation_human(controller, "team_assignment") && !team.assignment_authority;
		const bool available = team.available && !team.reserved;
		const auto location = team.location;
		const auto destination = team.destination;
		const bool busy = team.travel > 0 || (destination >= 0 && destination != location);
		const auto route = [&](std::int64_t target)
		{
			if (target == location && !team.travel)
				return std::pair{true, std::int64_t{0}};
			for (const auto &path : team.routes)
				if (path.compartment == target)
					return std::pair{path.reachable, path.ticks};
			return std::pair{false, std::int64_t{0}};
		};
		const auto add = [&](std::int64_t target, const Need &need, bool home, bool actionable)
		{
			if (target < 0 || static_cast<std::size_t>(target) >= team.compartments.size())
				return false;
			const auto [reachable, travel] = route(target);
			const bool continuing = destination == target;
			const bool eligible = authority && available && reachable && team.departure_open && (!home || !actionable);
			const auto name = team.compartments.at(target);
			const auto reason = !authority                           ? "human_assignment"
			                    : !available                         ? "team_unavailable"
			                    : !reachable || !team.departure_open ? "known_route_blocked"
			                    : home && actionable                 ? "known_actionable_assignment"
			                    : need.reported                      ? "retained_inspection"
			                                                         : "permitted_assignment";
			Facts facts;
			const auto text = [&](CandidateField key, const std::string &value)
			{
				facts[key] = Value::string(value);
			};
			const auto integer = [&](CandidateField key, std::int64_t value)
			{
				facts[key] = Value::integer(value);
			};
			const auto boolean = [&](CandidateField key, bool value)
			{
				facts[key] = Value::boolean(value);
			};
			text(CandidateField::repair_candidate_team, id);
			text(CandidateField::repair_candidate_destination, "auto/" + name);
			text(CandidateField::repair_candidate_compartment, name);
			text(CandidateField::repair_candidate_kind, home ? "home" : "repair");
			text(CandidateField::repair_candidate_reason, reason);
			integer(CandidateField::repair_candidate_revision, team.revision);
			integer(CandidateField::repair_candidate_target, target + 1);
			integer(CandidateField::repair_candidate_deficit, need.deficit);
			integer(CandidateField::repair_candidate_request_priority, need.priority);
			integer(CandidateField::repair_candidate_travel, continuing ? 0 : travel);
			integer(CandidateField::repair_candidate_retask_cost, busy && !continuing ? 1 : 0);
			boolean(CandidateField::repair_candidate_eligible, eligible);
			boolean(CandidateField::repair_candidate_continuing, continuing);
			boolean(CandidateField::repair_candidate_home, home);
			boolean(CandidateField::repair_candidate_disabled, need.disabled);
			boolean(CandidateField::repair_candidate_requested, need.requested);
			boolean(CandidateField::repair_candidate_fault, need.fault);
			boolean(CandidateField::repair_candidate_symptom, need.symptom);
			result.push_back(std::move(facts));
			return eligible;
		};
		bool actionable = false;
		for (std::size_t target = 0; target < team.compartments.size(); ++target)
		{
			const auto found = needs.find(team.compartments.at(target));
			if (found == needs.end())
				continue;
			const auto &need = found->second;
			if (need.damaged || need.fault || need.symptom || need.requested)
				actionable |= add(static_cast<std::int64_t>(target), need, false, false);
		}
		const auto home = team.home;
		if (home != location || busy || (destination >= 0 && destination != home))
			add(home, {}, true, actionable);
	}
	return result;
}
CandidateAdapter repair_adapter()
{
	return {"repair",
	        "objective_parameter_outside_known_repair_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(cache.repair_options(), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::repair &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) != context.input.entity) ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::repair_candidate_team).text) ||
		             (!context.text(*context.record, ObjectiveField::compartment, "").empty() &&
		              context.text(*context.record, ObjectiveField::compartment, "") !=
		                  context.fact(CandidateField::repair_candidate_compartment).text)))
			        return false;

		        if (context.source() == CandidateSource::requests)
		        {
			        const bool repair = context.fact(CandidateField::request_operation).text == "repair";
			        const bool scan = context.fact(CandidateField::request_operation).text == "scan";
			        const bool supply = context.fact(CandidateField::request_operation).text == "power" ||
			                            context.fact(CandidateField::request_operation).text == "cooling";
			        bool scan_system = false;
			        std::string compartment;
			        if (repair)
				        for (const auto &room : context.input.observation.combat.compartments)
					        if (room.index == context.fact(CandidateField::request_target).number)
						        compartment = room.id;
			        const auto system = context.text(*context.record, ObjectiveField::system, "");
			        if (scan && !system.empty())
				        for (const auto &scanner : context.input.observation.sensors.scanners)
					        scan_system |=
					            scanner.id == system &&
					            scanner.capabilities.contains(context.fact(CandidateField::request_capability).text);

			        if ((!context.text(*context.record, ObjectiveField::mission, "").empty() &&
			             context.text(*context.record, ObjectiveField::mission, "") !=
			                 context.fact(CandidateField::request_mission).text) ||
			            (!system.empty() && (scan ? !scan_system
			                                      : !repair || !controller_operation_owns(
			                                                       context.controller, "team_assignment", system))) ||
			            (!context.text(*context.record, ObjectiveField::compartment, "").empty() &&
			             (!repair || context.text(*context.record, ObjectiveField::compartment, "") != compartment)) ||
			            (context.number(*context.record, ObjectiveField::target) &&
			             context.number(*context.record, ObjectiveField::target) !=
			                 ((repair || supply) ? static_cast<std::int64_t>(context.input.entity)
			                                     : context.fact(CandidateField::request_target).number)))
				        return false;
			        if (repair && !system.empty() && !context.fact(CandidateField::request_done).number)
			        {
				        bool available = false;
				        for (const auto &team : context.input.observation.teams)
					        if (team.id == system && team.repair && team.available && !team.reserved &&
					            !team.assignment_authority)
						        for (std::size_t target = 0; target < team.compartments.size(); ++target)
							        if (team.compartments[target] == compartment)
								        for (const auto &route : team.routes)
									        available |= route.compartment == target && route.reachable;
				        if (!available)
				        {
					        if (const auto slot =
					                context.brain.observation_layout
					                    ->candidate_slots[static_cast<std::size_t>(CandidateField::request_feasible)])
						        context.values[*slot] = state_tree::Value::boolean(false);
					        if (const auto slot =
					                context.brain.observation_layout
					                    ->candidate_slots[static_cast<std::size_t>(CandidateField::request_reason)])
						        context.values[*slot] = state_tree::Value::string("scoped_repair_team_unavailable");
				        }
			        }
		        }
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::repair)
			        candidate.target = context.fact(CandidateField::repair_candidate_target).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::repair &&
		            !context.fact(CandidateField::repair_candidate_eligible).number)
			        candidate.reason = context.fact(CandidateField::repair_candidate_reason).text.empty()
			                               ? "no_owned_repair_assignment"
			                               : context.fact(CandidateField::repair_candidate_reason).text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
} // namespace phoenix
