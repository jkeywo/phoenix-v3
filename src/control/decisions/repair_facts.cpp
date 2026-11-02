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
std::vector<std::map<std::string, state_tree::Value>>
controller_repair_candidates(const ConsoleControllerRuntime &controller, const Observation &observed,
                             KnowledgeTick tick)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
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
	const auto &combat = combat_observation(observed);
	for (const auto &room : combat.at("compartments"))
	{
		const auto name = room.at("id").get<std::string>();
		compartments[room.at("index").get<std::int64_t>()] = name;
		auto &need = needs[name];
		const auto maximum = room.at("maximum").get<std::int64_t>();
		need.deficit =
		    maximum > 0
		        ? std::clamp<std::int64_t>((maximum - room.at("hp").get<std::int64_t>()) * 1000 / maximum, 0, 1000)
		        : 0;
		need.damaged = need.deficit > 0;
		need.disabled = room.value("disabled", false);
		need.inspected = room.value("inspected", false) || room.value("reported_inspection", false);
	}
	for (const auto &fault : combat.at("faults"))
		if (!fault.value("destroyed", false))
		{
			auto &need = needs[fault.at("compartment").get<std::string>()];
			need.fault = true;
			need.reported |= fault.contains("observed_tick");
		}
	for (const auto &report : observed.gameplay.value("repair_needs", Json::array()))
	{
		const auto room = compartments.find(report.at("compartment_index").get<std::int64_t>());
		if (room == compartments.end())
			continue;
		auto &need = needs[room->second];
		need.symptom = true;
		need.disabled |= report.value("structural", false);
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
	const auto teams = observed.gameplay.find("teams");
	if (teams == observed.gameplay.end())
		return result;
	for (const auto &team : *teams)
	{
		if (!team.value("repair", false) || team.value("work_operation", std::string{}) != "team_work")
			continue;
		const auto id = team.at("id").get<std::string>();
		if (!controller_operation_owns(controller, "team_assignment", id))
			continue;
		const bool authority =
		    !controller_operation_human(controller, "team_assignment") && !team.value("assignment_authority", false);
		const bool available = team.value("available", false) && !team.value("reserved", false);
		const auto location = team.at("location").get<std::int64_t>();
		const auto destination = team.at("destination").get<std::int64_t>();
		const bool busy = team.value("travel", 0LL) > 0 || (destination >= 0 && destination != location);
		const auto route = [&](std::int64_t target)
		{
			if (target == location && !team.value("travel", 0LL))
				return std::pair{true, std::int64_t{0}};
			for (const auto &path : team.value("routes", Json::array()))
				if (path.at("compartment") == target)
					return std::pair{path.at("reachable").get<bool>(), path.at("ticks").get<std::int64_t>()};
			return std::pair{false, std::int64_t{0}};
		};
		const auto add = [&](std::int64_t target, const Need &need, bool home, bool actionable)
		{
			if (target < 0 || static_cast<std::size_t>(target) >= team.at("compartments").size())
				return false;
			const auto [reachable, travel] = route(target);
			const bool continuing = destination == target;
			const bool eligible =
			    authority && available && reachable && team.value("departure_open", true) && (!home || !actionable);
			const auto name = team.at("compartments").at(target).get<std::string>();
			const auto reason = !authority                                          ? "human_assignment"
			                    : !available                                        ? "team_unavailable"
			                    : !reachable || !team.value("departure_open", true) ? "known_route_blocked"
			                    : home && actionable                                ? "known_actionable_assignment"
			                    : need.reported                                     ? "retained_inspection"
			                                                                        : "permitted_assignment";
			Facts facts;
			const auto text = [&](const char *key, const std::string &value)
			{
				facts[std::string("repair_candidate.") + key] = Value::string(value);
			};
			const auto integer = [&](const char *key, std::int64_t value)
			{
				facts[std::string("repair_candidate.") + key] = Value::integer(value);
			};
			const auto boolean = [&](const char *key, bool value)
			{
				facts[std::string("repair_candidate.") + key] = Value::boolean(value);
			};
			text("team", id);
			text("destination", "auto/" + name);
			text("compartment", name);
			text("kind", home ? "home" : "repair");
			text("reason", reason);
			integer("revision", team.at("revision"));
			integer("target", target + 1);
			integer("deficit", need.deficit);
			integer("request_priority", need.priority);
			integer("travel", continuing ? 0 : travel);
			integer("retask_cost", busy && !continuing ? 1 : 0);
			boolean("eligible", eligible);
			boolean("continuing", continuing);
			boolean("home", home);
			boolean("disabled", need.disabled);
			boolean("requested", need.requested);
			boolean("fault", need.fault);
			boolean("symptom", need.symptom);
			result.push_back(std::move(facts));
			return eligible;
		};
		bool actionable = false;
		for (std::size_t target = 0; target < team.at("compartments").size(); ++target)
		{
			const auto found = needs.find(team.at("compartments").at(target).get<std::string>());
			if (found == needs.end())
				continue;
			const auto &need = found->second;
			if (need.damaged || need.fault || need.symptom || need.requested)
				actionable |= add(static_cast<std::int64_t>(target), need, false, false);
		}
		const auto home = team.value("home", location);
		if (home != location || busy || (destination >= 0 && destination != home))
			add(home, {}, true, actionable);
	}
	return result;
}
CandidateAdapter repair_adapter()
{
	return {
	    "repair",
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
		    if (context.binding.candidate_source == "repair" &&
		        (!context.text(*context.record, "mission", "").empty() ||
		         (context.number(*context.record, "target") &&
		          context.number(*context.record, "target") != context.input.entity) ||
		         (!context.text(*context.record, "system", "").empty() &&
		          context.text(*context.record, "system", "") != context.fact("repair_candidate.team").text) ||
		         (!context.text(*context.record, "compartment", "").empty() &&
		          context.text(*context.record, "compartment", "") !=
		              context.fact("repair_candidate.compartment").text)))
			    return false;

		    if (context.binding.candidate_source == "requests")
		    {
			    const bool repair = context.fact("request.operation").text == "repair";
			    const bool scan = context.fact("request.operation").text == "scan";
			    const bool supply = context.fact("request.operation").text == "power" ||
			                        context.fact("request.operation").text == "cooling";
			    bool scan_system = false;
			    std::string compartment;
			    if (repair)
				    for (const auto &room : combat_observation(context.input.observation).at("compartments"))
					    if (room.at("index") == context.fact("request.target").number)
						    compartment = room.at("id");
			    const auto system = context.text(*context.record, "system", "");
			    if (scan && !system.empty())
				    if (const auto sensors = context.input.observation.gameplay.find("sensors");
				        sensors != context.input.observation.gameplay.end())
					    for (const auto &scanner : sensors->at("scanners"))
						    scan_system |= scanner.at("id") == system &&
						                   scanner.at("capabilities").contains(context.fact("request.capability").text);

			    if ((!context.text(*context.record, "mission", "").empty() &&
			         context.text(*context.record, "mission", "") != context.fact("request.mission").text) ||
			        (!system.empty() &&
			         (scan ? !scan_system
			               : !repair || !controller_operation_owns(context.controller, "team_assignment", system))) ||
			        (!context.text(*context.record, "compartment", "").empty() &&
			         (!repair || context.text(*context.record, "compartment", "") != compartment)) ||
			        (context.number(*context.record, "target") &&
			         context.number(*context.record, "target") != ((repair || supply)
			                                                           ? static_cast<std::int64_t>(context.input.entity)
			                                                           : context.fact("request.target").number)))
				    return false;
			    if (repair && !system.empty() && !context.fact("request.done").number)
			    {
				    bool available = false;
				    for (const auto &team : context.input.observation.gameplay.value("teams", Json::array()))
					    if (team.at("id") == system && team.value("repair", false) && team.value("available", false) &&
					        !team.value("reserved", false) && !team.value("assignment_authority", false))
						    for (std::size_t target = 0; target < team.at("compartments").size(); ++target)
							    if (team.at("compartments")[target] == compartment)
								    for (const auto &route : team.value("routes", Json::array()))
									    available |=
									        route.at("compartment") == target && route.value("reachable", false);
				    if (!available)
					    for (std::size_t i = 0; i < context.brain.program.registry.observations.size(); ++i)
					    {
						    const auto &name = context.brain.program.registry.observations[i].name;
						    if (name == "request.feasible")
							    context.values[i] = state_tree::Value::boolean(false);
						    if (name == "request.reason")
							    context.values[i] = state_tree::Value::string("scoped_repair_team_unavailable");
					    }
			    }
		    }
		    return true;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.binding.candidate_source == "repair")
			    candidate.target = context.fact("repair_candidate.target").number;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.binding.candidate_source == "repair" && !context.fact("repair_candidate.eligible").number)
			    candidate.reason = context.fact("repair_candidate.reason").text.empty()
			                           ? "no_owned_repair_assignment"
			                           : context.fact("repair_candidate.reason").text;
	    },
	    [](CandidateContext &context) -> Json
	    {
		    return {};
	    }};
}
} // namespace phoenix
