module;
#include "native_headers.hpp"
module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
// Explicit provider/debug input is an already permitted supplied projection.
// Ordinary fixed-tick capture never calls this serialization adapter.
DecisionObservation decision_observation_from(const Observation &input)
{
	DecisionObservation result;
	result.own = input.own;
	result.contacts = input.contacts;
	result.bearing_contacts = input.bearing_contacts;
	result.requested_speed = input.requested_speed;
	result.requested_heading = input.requested_heading;
	result.engine_enabled = input.engine_enabled;
	result.radar_enabled = input.radar_enabled;
	result.console = input.console;
	result.controls = input.controls;
	result.feeds = input.feeds;
	result.observed_tick = input.observed_tick;
	for (const auto &source : input.systems)
	{
		DecisionSystemView row;
		row.id = source.id;
		row.kind = source.kind;
		row.requested = source.requested;
		row.maximum = source.maximum;
		row.enabled = source.enabled;
		row.controlled = source.controlled;
		row.telemetry = decode_telemetry(source.telemetry);
		row.status = source.status;
		row.effective_range = source.effective_range;
		row.radial_ranges = source.radial_ranges;
		row.reach_profile = source.reach_profile;
		row.reach_uncertainty_mm = source.reach_uncertainty_mm;
		row.reach_limited = source.reach_limited;
		row.shows = source.shows;
		row.contact_ids = source.contact_ids;
		result.systems.push_back(std::move(row));
	}
	const auto &data = input.gameplay;
	result.authority.priority_objectives = data.value("objective_priorities", std::set<KnowledgeId>{});
	result.authority.priority_revision = data.value("objective_priority_revision", std::uint64_t{});
	const auto alert = data.value("alert", Json::object());
	result.authority.alert = alert.value("id", std::string{});
	result.authority.alert_severity = alert.value("severity", std::int64_t{});
	result.authority.alert_maximum = data.value("alert_maximum", std::int64_t{});
	result.authority.stance = data.value("stance", Json::object()).value("id", std::string{});
	if (data.contains("combat"))
		result.combat = combat_decision_from_json(data.at("combat"));
	for (const auto &row : data.value("directed", Json::array()))
		if (row.contains("id") && row.contains("target"))
			result.combat.directed_targets[row.at("id").get<std::string>()] = row.at("target").get<std::int64_t>();
	if (data.contains("sensors"))
		result.sensors = sensor_decision_from_json(data.at("sensors"));
	result.machinery = machinery_decision_from_json(data, input.observed_tick);
	result.teams = data.value("teams", std::vector<DecisionTeam>{});
	if (data.contains("personnel"))
		result.personnel = data.at("personnel").get<DecisionPersonnel>();
	result.reported_patients = data.value("reported_patients", std::vector<DecisionPatient>{});
	result.repair_needs = data.value("repair_needs", std::vector<DecisionRepairNeed>{});
	if (data.contains("docking"))
		result.docking = docking_decision_from_json(data.at("docking"));
	if (data.contains("logistics"))
		result.logistics = logistics_decision_from_json(data.at("logistics"));
	if (data.contains("ftl"))
		result.ftl = ftl_decision_from_json(data.at("ftl"));
	result.away_missions = data.value("away_missions", std::vector<AwayMissionDecision>{});
	result.craft = data.value("craft", std::vector<CraftDecisionRow>{});
	if (data.contains("craft_self"))
		result.craft_self = data.at("craft_self").get<CraftSelfDecision>();
	if (data.contains("psychic"))
		result.psychic = data.at("psychic").get<PsychicDecisionSnapshot>();
	return result;
}
} // namespace phoenix::world_detail
