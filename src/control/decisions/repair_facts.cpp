module;
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
} // namespace phoenix
