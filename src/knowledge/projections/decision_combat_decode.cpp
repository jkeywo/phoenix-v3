module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
module phoenix.knowledge.decision_combat;
namespace phoenix
{
namespace
{
template <class T> T decode(const Json &);
template <class T> std::vector<T> rows(const Json &j)
{
	std::vector<T> out;
	for (const auto &row : j)
		out.push_back(decode<T>(row));
	return out;
}
template <> CombatCompartmentDecision decode<CombatCompartmentDecision>(const Json &j)
{
	CombatCompartmentDecision v;
	v.id = j.value("id", v.id);
	v.index = j.value("index", v.index);
	v.deck = j.value("deck", v.deck);
	v.hp = j.value("hp", v.hp);
	v.maximum = j.value("maximum", v.maximum);
	v.disabled = j.value("disabled", v.disabled);
	v.inspected = j.value("inspected", v.inspected);
	v.damaged = j.value("damaged", v.damaged);
	v.x_mm = j.value("x_mm", v.x_mm);
	v.y_mm = j.value("y_mm", v.y_mm);
	v.half_width_mm = j.value("half_width_mm", v.half_width_mm);
	v.half_length_mm = j.value("half_length_mm", v.half_length_mm);
	v.reported_inspection = j.value("reported_inspection", v.reported_inspection);
	v.inspection_tick = j.value("inspection_tick", v.inspection_tick);
	v.inspection_revision = j.value("inspection_revision", v.inspection_revision);
	v.inspection_fault_count = j.value("inspection_fault_count", v.inspection_fault_count);
	v.inspection_hp = j.value("inspection_hp", v.inspection_hp);
	v.inspection_maximum = j.value("inspection_maximum", v.inspection_maximum);
	return v;
}
template <> CombatTeamDecision decode<CombatTeamDecision>(const Json &j)
{
	CombatTeamDecision v;
	v.id = j.value("id", v.id);
	v.assignment_manual = j.value("assignment_manual", v.assignment_manual);
	v.assignment_held = j.value("assignment_held", v.assignment_held);
	v.home = j.value("home", v.home);
	v.observed_tick = j.value("observed_tick", v.observed_tick);
	v.local_complete = j.value("local_complete", v.local_complete);
	v.location = j.value("location", v.location);
	v.destination = j.value("destination", v.destination);
	v.travel = j.value("travel", v.travel);
	v.edge_to = j.value("edge_to", v.edge_to);
	v.edge_total = j.value("edge_total", v.edge_total);
	return v;
}
template <> CombatArcDecision decode<CombatArcDecision>(const Json &j)
{
	CombatArcDecision v;
	v.id = j.value("id", v.id);
	v.kind = j.value("kind", v.kind);
	v.bearing_mdeg = j.value("bearing_mdeg", v.bearing_mdeg);
	v.rest_mdeg = j.value("rest_mdeg", v.rest_mdeg);
	v.arc_mdeg = j.value("arc_mdeg", v.arc_mdeg);
	v.coverage = j.value("coverage", v.coverage);
	v.label = j.value("label", v.label);
	v.family = j.value("family", v.family);
	v.active = j.value("active", v.active);
	v.disabled = j.value("disabled", v.disabled);
	return v;
}
template <> CombatFaultDecision decode<CombatFaultDecision>(const Json &j)
{
	CombatFaultDecision v;
	v.system = j.value("system", v.system);
	v.compartment = j.value("compartment", v.compartment);
	v.compartment_index = j.value("compartment_index", v.compartment_index);
	v.symptom = j.value("symptom", v.symptom);
	v.diagnosed = j.value("diagnosed", v.diagnosed);
	v.diagnosis = j.value("diagnosis", v.diagnosis);
	v.destroyed = j.value("destroyed", v.destroyed);
	v.work = j.value("work", v.work);
	v.required_work = j.value("required_work", v.required_work);
	v.observed_tick = j.value("observed_tick", v.observed_tick);
	v.revision = j.value("revision", v.revision);
	v.source = j.value("source", v.source);
	v.uncertainty = j.value("uncertainty", v.uncertainty);
	v.expires_tick = j.value("expires_tick", v.expires_tick);
	v.evidence_present = j.value("evidence_present", j.contains("observed_tick"));
	return v;
}
template <> CombatMagazineDecision decode<CombatMagazineDecision>(const Json &j)
{
	CombatMagazineDecision v;
	v.id = j.value("id", v.id);
	v.selected_resource = j.value("selected_resource", v.selected_resource);
	v.resource_choices = j.value("resource_choices", v.resource_choices);
	v.resource_labels = j.value("resource_labels", v.resource_labels);
	v.selectable_resources = j.value("selectable_resources", v.selectable_resources);
	v.stock_revision = j.value("stock_revision", v.stock_revision);
	v.manual = j.value("manual", v.manual);
	return v;
}
template <> CombatAmmunitionDecision decode<CombatAmmunitionDecision>(const Json &j)
{
	CombatAmmunitionDecision v;
	v.index = j.value("index", v.index);
	v.id = j.value("id", v.id);
	v.range_mm = j.value("range_mm", v.range_mm);
	v.guidance = j.value("guidance", v.guidance);
	v.quantity = j.value("quantity", v.quantity);
	v.use = j.value("use", v.use);
	v.available = j.value("available", v.available);
	return v;
}
template <> CombatShieldDecision decode<CombatShieldDecision>(const Json &j)
{
	CombatShieldDecision v;
	v.id = j.value("id", v.id);
	v.contribution = j.value("contribution", v.contribution);
	v.effect_id = j.value("effect_id", v.effect_id);
	v.component_id = j.value("component_id", v.component_id);
	v.controllable = j.value("controllable", v.controllable);
	v.strength = j.value("strength", v.strength);
	v.capacity = j.value("capacity", v.capacity);
	v.warmup = j.value("warmup", v.warmup);
	v.warmup_ticks = j.value("warmup_ticks", v.warmup_ticks);
	v.arc_mdeg = j.value("arc_mdeg", v.arc_mdeg);
	v.bearing_mdeg = j.value("bearing_mdeg", v.bearing_mdeg);
	v.layer = j.value("layer", v.layer);
	v.discharging = j.value("discharging", v.discharging);
	return v;
}
template <> SensorProgressDecision decode<SensorProgressDecision>(const Json &j)
{
	SensorProgressDecision v;
	v.capability = j.value("capability", v.capability);
	v.ticks = j.value("ticks", v.ticks);
	v.required = j.value("required", v.required);
	return v;
}
template <> CombatWeaponDecision decode<CombatWeaponDecision>(const Json &j)
{
	CombatWeaponDecision v;
	v.id = j.value("id", v.id);
	v.enabled = j.value("enabled", v.enabled);
	v.durable_payload = j.value("durable_payload", v.durable_payload);
	v.ammunition_free = j.value("ammunition_free", v.ammunition_free);
	v.label = j.value("label", v.label);
	v.magazines = j.value("magazines", v.magazines);
	v.selected = j.value("selected", v.selected);
	v.shots = j.value("shots", v.shots);
	v.loaded = j.value("loaded", v.loaded);
	v.loaded_resource = j.value("loaded_resource", v.loaded_resource);
	v.loaded_quantity = j.value("loaded_quantity", v.loaded_quantity);
	v.loading = j.value("loading", v.loading);
	v.unloading = j.value("unloading", v.unloading);
	v.load_ticks = j.value("load_ticks", v.load_ticks);
	v.unload_ticks = j.value("unload_ticks", v.unload_ticks);
	v.mode = j.value("mode", v.mode);
	v.point_defence = j.value("point_defence", v.point_defence);
	v.intercept = j.value("intercept", v.intercept);
	v.ammunition_options = rows<CombatAmmunitionDecision>(j.value("ammunition_options", Json::array()));
	v.reload_stock_blocked = j.value("reload_stock_blocked", false);
	return v;
}
template <> SensorScannerDecision decode<SensorScannerDecision>(const Json &j)
{
	SensorScannerDecision v;
	v.id = j.value("id", v.id);
	v.target = j.value("target", v.target);
	v.powered = j.value("powered", v.powered);
	v.capabilities = j.value("capabilities", v.capabilities);
	v.bearing_mdeg = j.value("bearing_mdeg", v.bearing_mdeg);
	v.arc_mdeg = j.value("arc_mdeg", v.arc_mdeg);
	v.radar_arc = j.value("radar_arc", v.radar_arc);
	v.range_mm = j.value("range_mm", v.range_mm);
	v.x = j.value("x", v.x);
	v.y = j.value("y", v.y);
	v.blocker = j.value("blocker", v.blocker);
	v.progress = rows<SensorProgressDecision>(j.value("progress", Json::array()));
	return v;
}
} // namespace
CombatDecisionSnapshot combat_decision_from_json(const Json &j)
{
	CombatDecisionSnapshot v;
	v.present = !j.is_null();
	if (!v.present)
		return v;
	v.destroyed = j.value("destroyed", false);
	v.compartments = rows<CombatCompartmentDecision>(j.value("compartments", Json::array()));
	v.teams = rows<CombatTeamDecision>(j.value("teams", Json::array()));
	v.arcs = rows<CombatArcDecision>(j.value("arcs", Json::array()));
	v.faults = rows<CombatFaultDecision>(j.value("faults", Json::array()));
	v.shields = rows<CombatShieldDecision>(j.value("shields", Json::array()));
	v.weapons = rows<CombatWeaponDecision>(j.value("weapons", Json::array()));
	v.magazines = rows<CombatMagazineDecision>(j.value("magazines", Json::array()));

	if (j.contains("result") && !j.at("result").is_null())
		v.result = j.at("result").get<std::string>();
	return v;
}
SensorDecisionSnapshot sensor_decision_from_json(const Json &j)
{
	SensorDecisionSnapshot v;
	v.scanners = rows<SensorScannerDecision>(j.value("scanners", Json::array()));
	for (const auto &row : j.value("contacts", Json::array()))
	{
		SensorContactDecision c;
		c.id = row.at("id").get<EntityId>();
		const auto findings = row.value("findings", Json::object());
		for (const auto &[cap, f] : findings.items())
		{
			auto data = sensor_finding_decision(f.value("data", Json()), f.value("observed", std::uint64_t{}));
			data.live = f.value("live", false);
			data.revision = f.value("revision", std::uint64_t{});
			data.source = f.value("source", std::string{});
			c.findings.emplace(cap, std::move(data));
		}
		v.contacts.push_back(std::move(c));
	}
	return v;
}
} // namespace phoenix
