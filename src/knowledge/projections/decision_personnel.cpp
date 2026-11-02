module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
module phoenix.knowledge.decision_personnel;
namespace phoenix
{
void to_json(Json &out, const DecisionPatient &row)
{
	out = {{"id", row.id},
	       {"name", row.name},
	       {"condition", row.condition},
	       {"facility", row.facility},
	       {"escort", row.escort},
	       {"source", row.source},
	       {"location", row.location},
	       {"stabilised", row.stabilised},
	       {"observed_tick", row.observed_tick},
	       {"revision", row.revision}};
	if (row.uncertainty_present)
		out["uncertainty"] = row.uncertainty;
	if (!row.compartment.empty())
		out["compartment"] = row.compartment;
	if (row.expires_tick)
		out["expires_tick"] = row.expires_tick;
}
void from_json(const Json &in, DecisionPatient &row)
{
	const DecisionPatient defaults;
	row.id = in.value("id", defaults.id);
	row.name = in.value("name", defaults.name);
	row.condition = in.value("condition", defaults.condition);
	row.facility = in.value("facility", defaults.facility);
	row.escort = in.value("escort", defaults.escort);
	row.source = in.value("source", defaults.source);
	row.location = in.value("location", defaults.location);
	row.uncertainty = in.value("uncertainty", defaults.uncertainty);
	row.stabilised = in.value("stabilised", defaults.stabilised);
	row.observed_tick = in.value("observed_tick", defaults.observed_tick);
	row.revision = in.value("revision", defaults.revision);
	row.uncertainty_present = in.contains("uncertainty");
	row.compartment = in.value("compartment", std::string{});
	row.expires_tick = in.value("expires_tick", 0ULL);
}
void to_json(Json &out, const DecisionRepairNeed &row)
{
	out = {{"system", row.system},
	       {"compartment_index", row.compartment_index},
	       {"observed_tick", row.observed_tick},
	       {"structural", row.structural}};
}
void from_json(const Json &in, DecisionRepairNeed &row)
{
	const DecisionRepairNeed defaults;
	row.system = in.value("system", defaults.system);
	row.compartment_index = in.value("compartment_index", defaults.compartment_index);
	row.observed_tick = in.value("observed_tick", defaults.observed_tick);
	row.structural = in.value("structural", defaults.structural);
}
void to_json(Json &out, const DecisionTeamRoute &row)
{
	out = {{"compartment", row.compartment}, {"ticks", row.ticks}, {"reachable", row.reachable}};
}
void from_json(const Json &in, DecisionTeamRoute &row)
{
	const DecisionTeamRoute defaults;
	row.compartment = in.value("compartment", defaults.compartment);
	row.ticks = in.value("ticks", defaults.ticks);
	row.reachable = in.value("reachable", defaults.reachable);
}
void to_json(Json &out, const DecisionFacility &row)
{
	out = {{"id", row.id},
	       {"compartment", row.compartment},
	       {"route_ticks", row.route_ticks},
	       {"available", row.available},
	       {"reachable", row.reachable}};
}
void from_json(const Json &in, DecisionFacility &row)
{
	const DecisionFacility defaults;
	row.id = in.value("id", defaults.id);
	row.compartment = in.value("compartment", defaults.compartment);
	row.route_ticks = in.value("route_ticks", defaults.route_ticks);
	row.available = in.value("available", defaults.available);
	row.reachable = in.value("reachable", defaults.reachable);
}
void to_json(Json &out, const DecisionTeamOrder &row)
{
	out = {{"id", row.id}, {"label", row.label}, {"icon", row.icon}, {"target", row.target}, {"targets", row.targets}};
}
void from_json(const Json &in, DecisionTeamOrder &row)
{
	const DecisionTeamOrder defaults;
	row.id = in.value("id", defaults.id);
	row.label = in.value("label", defaults.label);
	row.icon = in.value("icon", defaults.icon);
	row.target = in.value("target", defaults.target);
	row.targets = in.value("targets", defaults.targets);
}
void to_json(Json &out, const DecisionNamedLabel &row)
{
	out = {{"id", row.id}, {"label", row.label}};
}
void from_json(const Json &in, DecisionNamedLabel &row)
{
	const DecisionNamedLabel defaults;
	row.id = in.value("id", defaults.id);
	row.label = in.value("label", defaults.label);
}
void to_json(Json &out, const DecisionDutyEffect &row)
{
	out = {{"system", row.system}, {"slot", row.slot}, {"property", row.property}, {"multiplier", row.multiplier}};
}
void from_json(const Json &in, DecisionDutyEffect &row)
{
	const DecisionDutyEffect defaults;
	row.system = in.value("system", defaults.system);
	row.slot = in.value("slot", defaults.slot);
	row.property = in.value("property", defaults.property);
	row.multiplier = in.value("multiplier", defaults.multiplier);
}
void to_json(Json &out, const DecisionDuty &row)
{
	out = {{"id", row.id},
	       {"label", row.label},
	       {"department", row.department},
	       {"location", row.location},
	       {"assignment_reserved", row.assignment_reserved},
	       {"effects", row.effects}};
}
void from_json(const Json &in, DecisionDuty &row)
{
	const DecisionDuty defaults;
	row.id = in.value("id", defaults.id);
	row.label = in.value("label", defaults.label);
	row.department = in.value("department", defaults.department);
	row.location = in.value("location", defaults.location);
	row.assignment_reserved = in.value("assignment_reserved", defaults.assignment_reserved);
	row.effects = in.value("effects", defaults.effects);
}
void to_json(Json &out, const DecisionTraitContribution &row)
{
	out = {{"effect", row.effect},
	       {"name", row.name},
	       {"role", row.role},
	       {"property", row.property},
	       {"delta_permille", row.delta_permille}};
}
void from_json(const Json &in, DecisionTraitContribution &row)
{
	const DecisionTraitContribution defaults;
	row.effect = in.value("effect", defaults.effect);
	row.name = in.value("name", defaults.name);
	row.role = in.value("role", defaults.role);
	row.property = in.value("property", defaults.property);
	row.delta_permille = in.value("delta_permille", defaults.delta_permille);
}
void to_json(Json &out, const DecisionOfficerTrait &row)
{
	out = {{"id", row.id}, {"label", row.label}, {"contributions", row.contributions}};
}
void from_json(const Json &in, DecisionOfficerTrait &row)
{
	const DecisionOfficerTrait defaults;
	row.id = in.value("id", defaults.id);
	row.label = in.value("label", defaults.label);
	row.contributions = in.value("contributions", defaults.contributions);
}
void to_json(Json &out, const DecisionAssignmentRoute &row)
{
	out = {{"duty", row.duty}, {"reachable", row.reachable}, {"eligible", row.eligible}, {"travel", row.travel}};
}
void from_json(const Json &in, DecisionAssignmentRoute &row)
{
	const DecisionAssignmentRoute defaults;
	row.duty = in.value("duty", defaults.duty);
	row.reachable = in.value("reachable", defaults.reachable);
	row.eligible = in.value("eligible", defaults.eligible);
	row.travel = in.value("travel", defaults.travel);
}
void to_json(Json &out, const DecisionOfficer &row)
{
	out = {{"id", row.id},
	       {"name", row.name},
	       {"portrait", row.portrait},
	       {"department", row.department},
	       {"duty", row.duty},
	       {"present", row.present},
	       {"condition", row.condition},
	       {"stabilised", row.stabilised},
	       {"stun_until", row.stun_until},
	       {"facility", row.facility},
	       {"treatment", row.treatment},
	       {"location", row.location},
	       {"remaining", row.remaining},
	       {"destination", row.destination},
	       {"traits", row.traits},
	       {"mobile", row.mobile},
	       {"reserved", row.reserved},
	       {"default_duty", row.default_duty},
	       {"assignment_routes", row.assignment_routes}};
	if (!row.abilities.empty())
		out["abilities"] = row.abilities;
}
void from_json(const Json &in, DecisionOfficer &row)
{
	row.id = in.value("id", "");
	row.name = in.value("name", "");
	row.portrait = in.value("portrait", "");
	row.department = in.value("department", "");
	row.duty = in.value("duty", "");
	row.condition = in.value("condition", "");
	row.facility = in.value("facility", "");
	row.location = in.value("location", "");
	row.destination = in.value("destination", "");
	row.default_duty = in.value("default_duty", "");
	row.present = in.value("present", false);
	row.stabilised = in.value("stabilised", false);
	row.mobile = in.value("mobile", false);
	row.reserved = in.value("reserved", true);
	row.stun_until = in.value("stun_until", 0LL);
	row.treatment = in.value("treatment", 0LL);
	row.remaining = in.value("remaining", 0LL);
	row.traits = in.value("traits", std::vector<DecisionOfficerTrait>{});
	row.assignment_routes = in.value("assignment_routes", std::vector<DecisionAssignmentRoute>{});
	row.abilities = in.value("abilities", std::vector<DecisionNamedLabel>{});
}
void to_json(Json &out, const DecisionPersonnel &row)
{
	out = {{"revision", row.revision},
	       {"manual", row.manual},
	       {"departments", row.departments},
	       {"duties", row.duties},
	       {"officers", row.officers}};
	if (!row.controller.empty())
	{
		out["controller"] = row.controller;
		out["generation"] = row.generation;
	}
}
void from_json(const Json &in, DecisionPersonnel &row)
{
	row.present = !in.is_null();
	if (!row.present)
		return;
	row.revision = in.value("revision", 0ULL);
	row.manual = in.value("manual", false);
	row.controller = in.value("controller", "");
	row.generation = in.value("generation", 0ULL);
	row.departments = in.value("departments", std::vector<DecisionNamedLabel>{});
	row.duties = in.value("duties", std::vector<DecisionDuty>{});
	row.officers = in.value("officers", std::vector<DecisionOfficer>{});
}
void to_json(Json &out, const DecisionTeam &row)
{
	out = {{"id", row.id},
	       {"type", row.type},
	       {"revision", row.revision},
	       {"order", row.order},
	       {"condition", row.condition},
	       {"activity", row.activity},
	       {"reserved", row.reserved},
	       {"location", row.location},
	       {"home", row.home},
	       {"compartments", row.compartments},
	       {"observed_tick", row.observed_tick},
	       {"local_complete", row.local_complete},
	       {"available", row.available},
	       {"work_done", row.work_done},
	       {"departure_open", row.departure_open},
	       {"destination", row.destination},
	       {"orders", row.orders},
	       {"repair", row.repair},
	       {"completed", row.completed},
	       {"work_operation", row.work_operation},
	       {"travel", row.travel},
	       {"work", row.work},
	       {"work_system", row.work_system},
	       {"assignment_authority", row.assignment_authority},
	       {"manual", row.manual},
	       {"patient_control", row.patient_control},
	       {"assigned_compartment", row.assigned_compartment},
	       {"patients", row.patients}};
	if (row.routes_visible)
		out["routes"] = row.routes;
	if (row.facilities_visible)
		out["facilities"] = row.facilities;
}
void from_json(const Json &in, DecisionTeam &row)
{
	row.id = in.value("id", "");
	row.type = in.value("type", "");
	row.order = in.value("order", "");
	row.condition = in.value("condition", "");
	row.activity = in.value("activity", "");
	row.work_operation = in.value("work_operation", "");
	row.work = in.value("work", "");
	row.work_system = in.value("work_system", "");
	row.revision = in.value("revision", 0ULL);
	row.observed_tick = in.value("observed_tick", 0ULL);
	row.location = in.value("location", 0LL);
	row.home = in.value("home", row.location);
	row.destination = in.value("destination", -1LL);
	row.travel = in.value("travel", 0LL);
	row.assigned_compartment = in.value("assigned_compartment", -1LL);
	row.reserved = in.value("reserved", false);
	row.local_complete = in.value("local_complete", false);
	row.available = in.value("available", true);
	row.work_done = in.value("work_done", false);
	row.departure_open = in.value("departure_open", true);
	row.repair = in.value("repair", false);
	row.completed = in.value("completed", false);
	row.assignment_authority = in.value("assignment_authority", false);
	row.manual = in.value("manual", false);
	row.patient_control = in.value("patient_control", false);
	row.compartments = in.value("compartments", std::vector<std::string>{});
	row.orders = in.value("orders", std::vector<DecisionTeamOrder>{});
	row.patients = in.value("patients", std::vector<DecisionPatient>{});
	row.routes_visible = in.contains("routes");
	row.routes = in.value("routes", std::vector<DecisionTeamRoute>{});
	row.facilities_visible = in.contains("facilities");
	row.facilities = in.value("facilities", std::vector<DecisionFacility>{});
}
Json decision_teams_json(const std::vector<DecisionTeam> &rows)
{
	return Json(rows);
}
Json decision_personnel_json(const DecisionPersonnel &row)
{
	return Json(row);
}
} // namespace phoenix
