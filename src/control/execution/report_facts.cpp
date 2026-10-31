module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

module phoenix.control.execution;
import phoenix.gameplay.flight;

namespace phoenix
{
void controller_report_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                             const ControllerDecisionContext &context, std::vector<state_tree::Value> &values)
{
	// Summaries of permitted observations only. Authored trees own triggers, wording and repetition.
	std::array<std::int64_t, 28> facts{};
	std::string finding_text;
	std::uint64_t finding_tick = 0;
	const auto owns_operation = [&](const std::string &operation, const std::string &equipment)
	{
		return controller_operation_owns(controller, operation, equipment);
	};
	bool lookout =
	    std::any_of(observed.systems.begin(), observed.systems.end(),
	                [&](const auto &system)
	                {
		                return system.kind == OperatingSystem::Radar && context.owned_systems.contains(system.id);
	                });
	if (const auto sensors = observed.gameplay.find("sensors"); sensors != observed.gameplay.end())
		for (const auto &scanner : sensors->at("scanners"))
			lookout |= context.owned_systems.contains(scanner.at("id"));
	if (lookout)
		for (const auto &contact : observed.contacts)
			if (contact.disposition == "hostile" && contact.radar_icon == RadarIcon::ship)
				++facts[0];
	facts[1] = context.point_defence_target ? 1 : 0;
	for (const auto &system : observed.systems)
		if (context.owned_systems.contains(system.id))
		{
			facts[3] += system.telemetry.contains("thermal_stopped") && system.telemetry.at("thermal_stopped") != 0;
			facts[4] += system.telemetry.contains("supply_limited") && system.telemetry.at("supply_limited") != 0;
		}
	const auto &combat = combat_observation(observed);
	std::set<std::int64_t> repair_locations;
	for (const auto &weapon : combat.at("weapons"))
		if (context.owned_systems.contains(weapon.at("id").get<std::string>()) &&
		    !weapon.at("ammunition_options").empty() &&
		    std::none_of(weapon.at("ammunition_options").begin(), weapon.at("ammunition_options").end(),
		                 [](const auto &option)
		                 {
			                 return option.at("available").template get<bool>();
		                 }))
			++facts[2];
	const auto rows = [&](const char *name) -> const Json &
	{
		static const Json empty = Json::array();
		const auto found = observed.gameplay.find(name);
		return found == observed.gameplay.end() ? empty : *found;
	};
	std::set<std::string> known_patients;
	for (const auto &team : rows("teams"))
		if (owns_operation(team.value("work_operation", team.value("repair", false) ? "team_work" : "medical"),
		                   team.at("id")))
		{
			const auto activity = team.value("activity", std::string{}), work = team.value("work", std::string{});
			facts[6] += activity == "treat" || activity == "stabilise" || work == "Repairing system" ||
			            work == "Repairing hull";
			facts[7] += activity == "blocked" || work == "blocked" || work == "no_parts" || work == "Unavailable";
			for (const auto &patient : team.value("patients", Json::array()))
				known_patients.insert(patient.at("id").get<std::string>());
			facts[21] += team.value("completed", false);
			if (team.value("repair", false) && team.value("travel", 0) == 0 && !team.value("reserved", false) &&
			    work != "Unavailable")
				repair_locations.insert(team.value("location", 0LL) + 1);
		}
	facts[8] = known_patients.size();
	for (const auto &fault : combat.at("faults"))
		facts[5] += repair_locations.contains(fault.value("compartment_index", 0LL));
	for (const auto &compartment : combat.at("compartments"))
		facts[5] +=
		    repair_locations.contains(compartment.at("index")) && compartment.at("hp") < compartment.at("maximum");
	for (const auto &craft : rows("craft"))
		if (owns_operation("craft", craft.at("bay")))
		{
			const auto phase = craft.at("phase").get<std::string>();
			facts[9] += phase == "flying" || phase == "recovering";
			facts[10] += phase == "docked";
			facts[11] += phase == "lost";
			facts[12] += (phase == "flying" || phase == "recovering") && !craft.at("linked").get<bool>();
			facts[13] += !craft.value("blocker", std::string{}).empty();
		}
	for (const auto &ftl : rows("ftl"))
	{
		facts[14] += ftl.value("ready", false);
		facts[15] += ftl.value("enabled", false) && !ftl.value("blocker", std::string{}).empty();
	}
	for (const auto &mission : rows("away_missions"))
		if (owns_operation("mission", mission.value("dispatch_system", std::string{})))
		{
			const auto phase = mission.value("phase", std::string{});
			facts[16] += phase == "deployed";
			facts[17] += phase == "recovered";
			facts[18] += phase == "failed";
		}
	if (const auto sensors = observed.gameplay.find("sensors"); sensors != observed.gameplay.end())
	{
		for (const auto &scanner : sensors->at("scanners"))
			if (context.owned_systems.contains(scanner.at("id")) && scanner.value("target", 0) != 0)
				facts[25] += !scanner.value("blocker", std::string{}).empty();
		for (const auto &contact : sensors->at("contacts"))
			for (const auto &[capability, finding] : contact.at("findings").items())
			{
				if (lookout && (capability.starts_with("finding:") || capability == "findings"))
				{
					facts[19] += capability == "findings" ? finding.at("data").size() : 1;
					const auto remember = [&](const Json &row, std::uint64_t tick)
					{
						if (tick >= finding_tick)
						{
							finding_tick = tick;
							finding_text = row.value("text", std::string{});
						}
					};
					if (capability == "findings")
						for (const auto &row : finding.at("data"))
							remember(row, row.value("observed", std::uint64_t{0}));
					else
						remember(finding.at("data"), finding.value("observed", std::uint64_t{0}));
				}
			}
	}
	for (const auto &system : observed.systems)
		if (owns_operation("conversation", system.id) && !system.enabled)
			++facts[20];
	for (const auto &shield : combat.at("shields"))
		if (context.owned_systems.contains(shield.at("id")) && shield.value("capacity", 0LL) > 0)
			facts[22] += shield.value("strength", 0LL) == 0;
	if (const auto psychic = observed.gameplay.find("psychic"); psychic != observed.gameplay.end())
		for (const auto &choir : psychic->at("choirs"))
			for (const auto &seat : choir.at("seats"))
				if (seat.at("controller") == controller.definition.id && seat.value("enabled", false))
					facts[23] += !seat.value("blocker", std::string{}).empty();
	if (const auto docking = observed.gameplay.find("docking");
	    docking != observed.gameplay.end() && docking->contains("ports"))
		for (const auto &port : docking->at("ports"))
			if (controller.definition.operations.contains("docking") &&
			    port.at("controller") == controller.definition.id)
				facts[24] += port.at("phase") == "docked";
	for (const auto &mission : rows("away_missions"))
		if (owns_operation("mission", mission.value("dispatch_system", std::string{})) ||
		    owns_operation("conversation", ""))
			facts[26] += mission.at("phase") == "available";
	facts[27] = values.at(controller.brain->own[ControllerOwnField::Blocked]).number != 0;
	for (std::size_t i = 0; i < facts.size(); ++i)
		values.at(controller.brain->decision_base + 19 + i) = state_tree::Value::integer(facts[i]);
	values.at(controller.brain->decision_base + 47) = state_tree::Value::string(observed.console);
	values.at(controller.brain->decision_base + 48) = state_tree::Value::string(finding_text);
}
} // namespace phoenix
