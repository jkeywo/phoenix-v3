module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
void append_controller_medical_facts(const ConsoleControllerRuntime &controller,
                                     const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	if (!controller.brain->medical_base)
		return;
	const auto base = *controller.brain->medical_base;
	using state_tree::Value;
	values.at(base) = Value::string("");
	values.at(base + 1) = Value::string("");
	values.at(base + 2) = Value::integer(0);
	values.at(base + 3) = Value::boolean(false);
	values.at(base + 4) = Value::integer(0);
	values.at(base + 5) = Value::integer(0);
	struct Assignment
	{
		std::string team, destination;
		std::int64_t revision;
	};
	std::vector<Assignment> assignments;
	std::vector<ObjectiveCandidate> candidates;
	for (const auto &team : input.observation.gameplay.value("teams", Json::array()))
	{
		const auto id = team.at("id").get<std::string>();
		if (team.value("work_operation", std::string{}) != "medical" || team.value("reserved", false) ||
		    team.value("assignment_authority", false) || !team.at("order").get<std::string>().empty() ||
		    team.value("travel", 0LL) || !team.contains("compartments") ||
		    !controller_operation_owns(controller, "team_assignment", id) ||
		    controller_operation_human(controller, "team_assignment"))
			continue;
		const auto location = team.at("location").get<std::int64_t>();
		auto destination = team.value("home", location);
		std::int64_t urgency = 0;
		// Local snapshots and explicitly scoped department reports are the only
		// patient evidence here. This helper has no physical roster or World port.
		for (const auto &patient : team.at("patients"))
		{
			const auto condition = patient.at("condition").get<std::string>();
			const auto need = condition == "severe" ? 1000 : condition == "minor" ? 500 : 0;
			if (!need || !patient.at("facility").get<std::string>().empty())
				continue;
			const auto at = patient.at("location").get<std::int64_t>();
			if (need > urgency || (need == urgency && at < destination))
			{
				urgency = need;
				destination = at;
			}
		}
		// Do not reset an admitted local treatment/escort, or reissue an
		// unchanged assignment every tick. New results arrive next frame.
		if ((destination == location && team.value("assigned_compartment", -1LL) == destination) ||
		    (destination == location && !urgency) || destination < 0 ||
		    static_cast<std::size_t>(destination) >= team.at("compartments").size())
			continue;
		ObjectiveCandidate candidate;
		candidate.target = destination + 1;
		candidate.action = id + "/auto";
		candidate.tier = ObjectiveTier::DirectiveOther;
		candidate.feasible = true;
		candidate.factors.urgency = urgency;
		candidate.factors.progress = urgency ? 1000 : 1;
		candidate.factors.travel = destination == location ? 0 : 1;
		if (const auto policy = controller.brain->objective_scoring.find("default");
		    policy != controller.brain->objective_scoring.end())
		{
			const auto &w = policy->second;
			candidate.policy = {w[0], w[1], w[2], w[3], w[4], w[5]};
		}
		candidates.push_back(candidate);
		assignments.push_back({id, "auto/" + team.at("compartments").at(destination).get<std::string>(),
		                       team.at("revision").get<std::int64_t>()});
	}
	if (const auto selected = select_objective(candidates))
	{
		const auto &assignment = assignments[*selected];
		values.at(base) = Value::string(assignment.team);
		values.at(base + 1) = Value::string(assignment.destination);
		values.at(base + 2) = Value::integer(assignment.revision);
		values.at(base + 3) = Value::boolean(true);
		values.at(base + 4) = Value::integer(candidates[*selected].target);
		values.at(base + 5) = Value::integer(candidates[*selected].factors.urgency);
	}
}
} // namespace phoenix
