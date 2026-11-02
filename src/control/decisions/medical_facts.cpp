module;
#include <optional>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <vector>

module phoenix.control.decisions;

namespace phoenix
{
#include "candidate_adapters.hpp"
namespace
{
using MedicalFacts = std::vector<state_tree::Value>;
std::int64_t clinical_urgency(const DecisionPatient &patient)
{
	const auto condition = patient.condition;
	return condition == "severe" ? (patient.stabilised ? 600 : 1000) : condition == "minor" ? 500 : 0;
}
const char *clinical_condition(std::int64_t urgency)
{
	return urgency >= 1000 ? "severe" : urgency >= 600 ? "stabilised" : urgency > 0 ? "minor" : "home";
}
MedicalFacts empty_medical_facts()
{
	using state_tree::Value;
	return {Value::string(""),
	        Value::string(""),
	        Value::integer(0),
	        Value::boolean(false),
	        Value::integer(0),
	        Value::integer(0),
	        Value::integer(0),
	        Value::integer(0),
	        Value::boolean(false),
	        Value::string(""),
	        Value::string("no_known_actionable_assignment"),
	        Value::string("home")};
}
} // namespace
std::vector<std::vector<state_tree::Value>> controller_medical_candidates(const ConsoleControllerRuntime &controller,
                                                                          const DecisionObservation &observed)
{
	using state_tree::Value;
	const auto &teams = observed.teams;
	if (teams.empty())
		return {};
	// Every row is already scoped by the Console producer. Combine only those
	// reports, retaining newest provenance if several owned teams report a patient.
	std::map<std::string, DecisionPatient> patients;
	const auto version = [](const auto &patient)
	{
		return std::tuple{patient.observed_tick, patient.revision, -patient.uncertainty, patient.source};
	};
	const auto remember = [&](const auto &patient)
	{
		const auto id = patient.id;
		const auto previous = patients.find(id);
		if (previous == patients.end() || version(patient) > version(previous->second))
		{
			DecisionPatient row;
			row.id = patient.id;
			row.name = patient.name;
			row.condition = patient.condition;
			row.facility = patient.facility;
			row.escort = patient.escort;
			row.source = patient.source;
			row.location = patient.location;
			row.uncertainty = patient.uncertainty;
			row.stabilised = patient.stabilised;
			row.observed_tick = patient.observed_tick;
			row.revision = patient.revision;
			patients[id] = std::move(row);
		}
	};
	for (const auto &team : teams)
		for (const auto &patient : team.patients)
			remember(patient);
	for (const auto &patient : observed.reported_patients)
		remember(patient);
	std::map<std::int64_t, std::int64_t> needs;
	for (const auto &[id, patient] : patients)
		if (patient.facility.empty() && patient.escort.empty())
		{
			const auto urgency = clinical_urgency(patient);
			if (urgency)
				needs[patient.location] = std::max(needs[patient.location], urgency);
		}
	std::vector<MedicalFacts> result;
	for (const auto &team : teams)
	{
		const auto id = team.id;
		if (team.work_operation != "medical" || team.reserved || team.compartments.empty())
			continue;
		const bool assign = controller_operation_owns(controller, "team_assignment", id) &&
		                    !controller_operation_human(controller, "team_assignment");
		const bool continue_work =
		    controller_operation_owns(controller, "medical", id) && !controller_operation_human(controller, "medical");
		if (!assign && !continue_work)
			continue;
		const auto location = team.location;
		const auto home = team.home;
		const auto assigned = team.assigned_compartment;
		const auto destination = team.destination;
		const auto goal = assigned >= 0 ? assigned : destination >= 0 ? destination : location;
		const bool order = !team.order.empty() && team.activity != "complete";
		const bool busy = order || (team.travel > 0 && assigned >= 0);
		const bool available = team.available;
		const bool departure = team.departure_open;
		const bool held = team.assignment_authority;
		bool facility_available = false;
		for (const auto &facility : team.facilities)
			facility_available |= facility.available && facility.reachable;
		std::int64_t escort_urgency = 0;
		for (const auto &[patient_id, patient] : patients)
			if (patient.escort == id)
				escort_urgency = std::max(escort_urgency, clinical_urgency(patient));
		const auto route = [&](std::int64_t target)
		{
			if (target == location && !team.travel)
				return std::pair{true, std::int64_t{0}};
			for (const auto &route : team.routes)
				if (route.compartment == target)
					return std::pair{route.reachable, route.ticks};
			return std::pair{false, std::int64_t{0}};
		};
		const auto add = [&](std::int64_t target, std::int64_t urgency, bool continuing, bool physical_work)
		{
			if (target < 0 || static_cast<std::size_t>(target) >= team.compartments.size())
				return false;
			const auto [reachable, ticks] = route(target);
			const bool recovery = urgency == 600;
			const bool capacity = !recovery || facility_available;
			const bool authority = continuing ? (urgency == 0 ? assign && !held : continue_work) : assign && !held;
			const bool reachable_now = continuing ? route(destination >= 0 ? destination : target).first : reachable;
			const bool feasible = authority && available && departure && reachable_now && capacity && physical_work;
			const char *reason = !authority                     ? "human_assignment_or_work"
			                     : !available                   ? "team_unavailable"
			                     : !departure || !reachable_now ? "known_route_blocked"
			                     : !capacity                    ? "facility_unavailable"
			                     : !physical_work               ? "no_current_work"
			                                                    : "permitted_assignment";
			auto facts = empty_medical_facts();
			facts[0] = Value::string(id);
			facts[1] = Value::string("auto/" + team.compartments.at(target));
			facts[2] = Value::integer(team.revision);
			facts[3] = Value::boolean(feasible);
			facts[4] = Value::integer(target + 1);
			facts[5] = Value::integer(urgency);
			facts[6] = Value::integer(continuing ? 0 : ticks);
			facts[7] = Value::integer(!continuing && busy ? (escort_urgency > 0 ? 2 : 1) : 0);
			facts[8] = Value::boolean(continuing);
			facts[9] = Value::string(team.compartments.at(target));
			facts[10] = Value::string(reason);
			facts[11] = Value::string(clinical_condition(urgency));
			result.push_back(std::move(facts));
			return feasible;
		};
		bool actionable = false;
		for (const auto &[target, urgency] : needs)
		{
			const bool continuing = target == goal && (busy || assigned == target);
			actionable |= add(target, urgency, continuing, true);
		}
		if (escort_urgency > 0 && !needs.contains(goal))
			actionable |= add(goal, escort_urgency, true, true);
		if (!actionable && busy && team.completed == false && team.work_done)
			actionable |= add(goal, 0, true, true); // Let the admitted workflow publish its completion first.
		{
			const bool already_home = location == home && !team.travel && (assigned < 0 || assigned == home) &&
			                          (destination < 0 || destination == home);
			if (!already_home)
				add(home, 0, busy && goal == home, true);
		}
	}
	return result;
}
void append_controller_medical_facts(const ConsoleControllerRuntime &controller,
                                     const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	if (!controller.brain->medical_base)
		return;
	const auto base = *controller.brain->medical_base;
	const auto empty = empty_medical_facts();
	std::copy(empty.begin(), empty.end(), values.begin() + base);
	const auto options = controller_medical_candidates(controller, input.observation);
	std::vector<ObjectiveCandidate> candidates;
	for (const auto &facts : options)
	{
		ObjectiveCandidate candidate;
		candidate.target = facts[4].number;
		candidate.action = facts[0].text + "/" + facts[1].text;
		// Legacy direct policies submit new assignments only; the scored path
		// separately binds continuation and safe retasking candidates.
		candidate.feasible = facts[3].number && !facts[8].number && !facts[7].number;
		candidate.tier = ObjectiveTier::DirectiveOther;
		candidate.factors = {facts[5].number, facts[5].number ? 1000 : 1, 0, facts[6].number, 0};
		const auto &weights = controller.brain->default_objective_scoring;
		candidate.policy = {weights[0], weights[1], weights[2], weights[3], weights[4], weights[5]};
		candidates.push_back(std::move(candidate));
	}
	if (const auto selected = select_objective(candidates))
		std::copy(options[*selected].begin(), options[*selected].end(), values.begin() + base);
}
CandidateAdapter medical_adapter()
{
	return {"medical",
	        "objective_parameter_outside_known_medical_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        std::vector<DecisionCandidateFrame> result;
		        for (const auto &values : cache.medical_options())
			        result.push_back({values, false});
		        return result;
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::medical && !context.option.empty())
		        {
			        const auto base = *context.brain.medical_base;
			        std::copy(context.option.begin(), context.option.end(), context.values.begin() + base);
			        if (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
			            (context.number(*context.record, ObjectiveField::target) &&
			             context.number(*context.record, ObjectiveField::target) !=
			                 static_cast<std::int64_t>(context.input.entity)) ||
			            (!context.text(*context.record, ObjectiveField::compartment, "").empty() &&
			             context.text(*context.record, ObjectiveField::compartment, "") != context.option[9].text) ||
			            (!context.text(*context.record, ObjectiveField::system, "").empty() &&
			             context.text(*context.record, ObjectiveField::system, "") != context.option[0].text))
				        return false;
		        }
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::medical && !context.option.empty())
			        candidate.target = context.option[4].number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::medical && context.brain.medical_base &&
		            !context.values[*context.brain.medical_base + 3].number)
			        candidate.reason = context.values[*context.brain.medical_base + 10].text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
} // namespace phoenix
