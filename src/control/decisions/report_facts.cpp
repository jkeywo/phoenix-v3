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

module phoenix.control.decisions;
import phoenix.gameplay.flight;

namespace phoenix
{
void controller_report_facts(const ConsoleControllerRuntime &controller, const DecisionObservation &observed,
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
	for (const auto &scanner : observed.sensors.scanners)
		lookout |= context.owned_systems.contains(scanner.id);
	if (lookout)
		for (const auto &contact : observed.contacts)
			if (contact.disposition == "hostile" && contact.radar_icon == RadarIcon::ship)
				++facts[0];
	for (const auto &contact : observed.contacts)
		if (contact.id >= 7000000000000000ULL && contact.id < 8000000000000000ULL &&
		    contact.disposition != "friendly" &&
		    fixed::dot({contact.x - observed.own.x, contact.y - observed.own.y},
		               {contact.vx - observed.own.vx, contact.vy - observed.own.vy}) < 0)
			++facts[1];
	for (const auto &system : observed.systems)
		if (context.owned_systems.contains(system.id))
		{
			facts[3] += system.telemetry.contains(TelemetryField::thermal_stopped) &&
			            system.telemetry.at(TelemetryField::thermal_stopped) != 0;
			facts[4] += system.telemetry.contains(TelemetryField::supply_limited) &&
			            system.telemetry.at(TelemetryField::supply_limited) != 0;
		}
	const auto &combat = observed.combat;
	std::set<std::int64_t> repair_locations;
	for (const auto &weapon : combat.weapons)
		if (context.owned_systems.contains(weapon.id) && !weapon.ammunition_options.empty() &&
		    std::none_of(weapon.ammunition_options.begin(), weapon.ammunition_options.end(),
		                 [](const auto &option)
		                 {
			                 return option.available;
		                 }))
			++facts[2];
	std::set<std::string> known_patients;
	for (const auto &team : observed.teams)
		if (owns_operation(team.work_operation, team.id))
		{
			const auto activity = team.activity, work = team.work;
			facts[6] += activity == "treat" || activity == "stabilise" || work == "Repairing system" ||
			            work == "Repairing hull";
			facts[7] += activity == "blocked" || work == "blocked" || work == "no_parts" || work == "Unavailable";
			for (const auto &patient : team.patients)
				known_patients.insert(patient.id);
			facts[21] += team.completed;
			if (team.repair && team.travel == 0 && !team.reserved && work != "Unavailable")
				repair_locations.insert(team.location + 1);
		}
	facts[8] = known_patients.size();
	for (const auto &fault : combat.faults)
		facts[5] += repair_locations.contains(fault.compartment_index);
	for (const auto &compartment : combat.compartments)
		facts[5] += repair_locations.contains(compartment.index) && compartment.hp < compartment.maximum;
	for (const auto &craft : observed.craft)
		if (owns_operation("craft", craft.bay))
		{
			const auto phase = craft.phase;
			facts[9] += phase == "flying" || phase == "recovering";
			facts[10] += phase == "docked";
			facts[11] += phase == "lost";
			facts[12] += (phase == "flying" || phase == "recovering") && !craft.linked;
			facts[13] += !craft.blocker.empty();
		}
	for (const auto &ftl : observed.ftl)
	{
		facts[14] += ftl.ready;
		facts[15] += ftl.enabled && !ftl.blocker.empty();
	}
	for (const auto &mission : observed.away_missions)
		if (owns_operation("mission", mission.dispatch_system))
		{
			const auto phase = mission.phase;
			facts[16] += phase == "deployed";
			facts[17] += phase == "recovered";
			facts[18] += phase == "failed";
		}
	for (const auto &scanner : observed.sensors.scanners)
		if (context.owned_systems.contains(scanner.id) && scanner.target != 0)
			facts[25] += !scanner.blocker.empty();
	for (const auto &contact : observed.sensors.contacts)
		for (const auto &[capability, finding] : contact.findings)
			if (lookout && (capability.starts_with("finding:") || capability == "findings"))
			{
				facts[19] += capability == "findings" ? finding.report_findings.size() : 1;
				for (const auto &row : finding.report_findings)
					if (row.observed >= finding_tick)
					{
						finding_tick = row.observed;
						finding_text = row.text;
					}
			}

	for (const auto &system : observed.systems)
		if (owns_operation("conversation", system.id) && !system.enabled)
			++facts[20];
	for (const auto &shield : combat.shields)
		if (context.owned_systems.contains(shield.id) && shield.capacity > 0)
			facts[22] += shield.strength == 0;
	for (const auto &choir : observed.psychic.choirs)
		for (const auto &seat : choir.seats)
			if (seat.controller == controller.definition.id && seat.enabled)
				facts[23] += !seat.blocker.empty();
	for (const auto &port : observed.docking.ports)
		if (controller.definition.operations.contains("docking") && port.controller == controller.definition.id)
			facts[24] += port.phase == "docked";
	for (const auto &mission : observed.away_missions)
		if (owns_operation("mission", mission.dispatch_system) || owns_operation("conversation", ""))
			facts[26] += mission.phase == "available";
	facts[27] = values.at(controller.brain->own[ControllerOwnField::Blocked]).number != 0;
	const auto base = controller.brain->coordination_base - facts.size() - 2;
	for (std::size_t i = 0; i < facts.size(); ++i)
		values.at(base + i) = state_tree::Value::integer(facts[i]);
	values.at(base + facts.size()) = state_tree::Value::string(observed.console);
	values.at(base + facts.size() + 1) = state_tree::Value::string(finding_text);
}
} // namespace phoenix
