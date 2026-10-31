module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
namespace
{
using MedicalFacts = std::vector<state_tree::Value>;
std::int64_t clinical_urgency(const Json &patient)
{
	const auto condition = patient.at("condition").get<std::string>();
	return condition == "severe" ? (patient.value("stabilised", false) ? 600 : 1000) : condition == "minor" ? 500 : 0;
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
                                                                          const Observation &observed)
{
	using state_tree::Value;
	const auto teams = observed.gameplay.find("teams");
	if (teams == observed.gameplay.end())
		return {};
	// Every row is already scoped by the Console producer. Combine only those
	// reports, retaining newest provenance if several owned teams report a patient.
	std::map<std::string, Json> patients;
	const auto version = [](const Json &patient)
	{
		return std::tuple{patient.value("revision", 0ULL), patient.value("observed_tick", 0ULL),
		                  patient.value("source", std::string{})};
	};
	for (const auto &team : *teams)
		for (const auto &patient : team.at("patients"))
		{
			const auto id = patient.at("id").get<std::string>();
			const auto previous = patients.find(id);
			if (previous == patients.end() || version(patient) > version(previous->second))
				patients[id] = patient;
		}
	std::map<std::int64_t, std::int64_t> needs;
	for (const auto &[id, patient] : patients)
		if (patient.at("facility").get<std::string>().empty() && patient.value("escort", std::string{}).empty())
		{
			const auto urgency = clinical_urgency(patient);
			if (urgency)
				needs[patient.at("location").get<std::int64_t>()] =
				    std::max(needs[patient.at("location").get<std::int64_t>()], urgency);
		}
	std::vector<MedicalFacts> result;
	for (const auto &team : *teams)
	{
		const auto id = team.at("id").get<std::string>();
		if (team.value("work_operation", std::string{}) != "medical" || team.value("reserved", false) ||
		    !team.contains("compartments"))
			continue;
		const bool assign = controller_operation_owns(controller, "team_assignment", id) &&
		                    !controller_operation_human(controller, "team_assignment");
		const bool continue_work =
		    controller_operation_owns(controller, "medical", id) && !controller_operation_human(controller, "medical");
		if (!assign && !continue_work)
			continue;
		const auto location = team.at("location").get<std::int64_t>();
		const auto home = team.value("home", location);
		const auto assigned = team.value("assigned_compartment", -1LL);
		const auto destination = team.at("destination").get<std::int64_t>();
		const auto goal = assigned >= 0 ? assigned : destination >= 0 ? destination : location;
		const bool order = !team.at("order").get<std::string>().empty() && team.at("activity") != "complete";
		const bool busy = order || (team.value("travel", 0LL) > 0 && assigned >= 0);
		const bool available = team.value("available", true);
		const bool departure = team.value("departure_open", true);
		const bool held = team.value("assignment_authority", false);
		bool facility_available = false;
		for (const auto &facility : team.value("facilities", Json::array()))
			facility_available |= facility.value("available", false) && facility.value("reachable", false);
		std::int64_t escort_urgency = 0;
		for (const auto &[patient_id, patient] : patients)
			if (patient.value("escort", std::string{}) == id)
				escort_urgency = std::max(escort_urgency, clinical_urgency(patient));
		const auto route = [&](std::int64_t target)
		{
			if (target == location && !team.value("travel", 0LL))
				return std::pair{true, std::int64_t{0}};
			for (const auto &route : team.value("routes", Json::array()))
				if (route.at("compartment") == target)
					return std::pair{route.at("reachable").get<bool>(), route.at("ticks").get<std::int64_t>()};
			return std::pair{false, std::int64_t{0}};
		};
		const auto add = [&](std::int64_t target, std::int64_t urgency, bool continuing, bool physical_work)
		{
			if (target < 0 || static_cast<std::size_t>(target) >= team.at("compartments").size())
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
			facts[1] = Value::string("auto/" + team.at("compartments").at(target).get<std::string>());
			facts[2] = Value::integer(team.at("revision"));
			facts[3] = Value::boolean(feasible);
			facts[4] = Value::integer(target + 1);
			facts[5] = Value::integer(urgency);
			facts[6] = Value::integer(continuing ? 0 : ticks);
			facts[7] = Value::integer(!continuing && busy ? (escort_urgency > 0 ? 2 : 1) : 0);
			facts[8] = Value::boolean(continuing);
			facts[9] = Value::string(team.at("compartments").at(target));
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
		if (!actionable && busy && team.value("completed", false) == false && team.value("work_done", false))
			actionable |= add(goal, 0, true, true); // Let the admitted workflow publish its completion first.
		{
			const bool already_home = location == home && !team.value("travel", 0LL) &&
			                          (assigned < 0 || assigned == home) && (destination < 0 || destination == home);
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
		if (const auto policy = controller.brain->objective_scoring.find("default");
		    policy != controller.brain->objective_scoring.end())
		{
			const auto &w = policy->second;
			candidate.policy = {w[0], w[1], w[2], w[3], w[4], w[5]};
		}
		candidates.push_back(std::move(candidate));
	}
	if (const auto selected = select_objective(candidates))
		std::copy(options[*selected].begin(), options[*selected].end(), values.begin() + base);
}
} // namespace phoenix
