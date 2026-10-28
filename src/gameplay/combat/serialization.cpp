module;
#include <nlohmann/json.hpp>

module phoenix.gameplay.combat.state;

namespace phoenix
{
void to_json(Json &value, const FaultState &state)
{
	value["active"] = state.active;
	value["diagnosed"] = state.diagnosed;
	value["work"] = state.work;
}
void from_json(const Json &value, FaultState &state)
{
	value.at("active").get_to(state.active);
	value.at("diagnosed").get_to(state.diagnosed);
	value.at("work").get_to(state.work);
}
void to_json(Json &value, const WeaponState &state)
{
	value["cooldown"] = state.cooldown;
	value["shots"] = state.shots;
	value["ready"] = state.ready;
	value["selected_magazine"] = state.selected_magazine;
	value["loaded_magazine"] = state.loaded_magazine;
	value["loading"] = state.loading;
	value["unloading"] = state.unloading;
	value["loaded_resource"] = state.loaded_resource;
	value["loaded_quantity"] = state.loaded_quantity;
	value["beam_remainder"] = state.beam_remainder;
	value["beam_tick"] = state.beam_tick;
	value["bypass_remainder"] = state.bypass_remainder;
	value["beam_end"] = state.beam_end;
	value["intercept"] = state.intercept;
}
void from_json(const Json &value, WeaponState &state)
{
	value.at("cooldown").get_to(state.cooldown);
	value.at("shots").get_to(state.shots);
	value.at("ready").get_to(state.ready);
	value.at("selected_magazine").get_to(state.selected_magazine);
	value.at("loaded_magazine").get_to(state.loaded_magazine);
	value.at("loading").get_to(state.loading);
	value.at("unloading").get_to(state.unloading);
	value.at("loaded_resource").get_to(state.loaded_resource);
	value.at("loaded_quantity").get_to(state.loaded_quantity);
	value.at("beam_remainder").get_to(state.beam_remainder);
	value.at("beam_tick").get_to(state.beam_tick);
	value.at("bypass_remainder").get_to(state.bypass_remainder);
	value.at("beam_end").get_to(state.beam_end);
	value.at("intercept").get_to(state.intercept);
}
void to_json(Json &value, const TurretState &state)
{
	value["heading"] = state.heading;
	value["turn_remainder"] = state.turn_remainder;
}
void from_json(const Json &value, TurretState &state)
{
	value.at("heading").get_to(state.heading);
	value.at("turn_remainder").get_to(state.turn_remainder);
}
void to_json(Json &value, const MagazineState &state)
{
	value["stock"] = state.stock;
}
void from_json(const Json &value, MagazineState &state)
{
	value.at("stock").get_to(state.stock);
}
void to_json(Json &value, const TeamState &state)
{
	value["location"] = state.location;
	value["destination"] = state.destination;
	value["edge_to"] = state.edge_to;
	value["travel"] = state.travel;
	value["edge_total"] = state.edge_total;
	value["repair_system"] = state.repair_system;
	value["repair_auto"] = state.repair_auto;
	value["revision"] = state.revision;
	value["order"] = state.order;
	value["activity"] = state.activity;
	value["patient"] = state.patient;
	value["work_done"] = state.work_done;
	value["condition"] = state.condition;
	value["treatment"] = state.treatment;
	value["detained"] = state.detained;
	value["compartment_assignment"] = state.compartment_assignment;
	value["medical_compartment"] = state.medical_compartment;
}
void from_json(const Json &value, TeamState &state)
{
	const TeamState defaults;
	state.location = value.value("location", defaults.location);
	state.destination = value.value("destination", defaults.destination);
	state.edge_to = value.value("edge_to", defaults.edge_to);
	state.travel = value.value("travel", defaults.travel);
	state.edge_total = value.value("edge_total", defaults.edge_total);
	state.repair_system = value.value("repair_system", defaults.repair_system);
	state.repair_auto = value.value("repair_auto", defaults.repair_auto);
	state.revision = value.value("revision", defaults.revision);
	state.order = value.value("order", defaults.order);
	state.activity = value.value("activity", defaults.activity);
	state.patient = value.value("patient", defaults.patient);
	state.work_done = value.value("work_done", defaults.work_done);
	state.condition = value.value("condition", defaults.condition);
	state.treatment = value.value("treatment", defaults.treatment);
	state.detained = value.value("detained", defaults.detained);
	state.compartment_assignment = value.value("compartment_assignment", defaults.compartment_assignment);
	state.medical_compartment = value.value("medical_compartment", defaults.medical_compartment);
}
void to_json(Json &value, const ShieldState &state)
{
	value["strength"] = state.strength;
	value["warmup"] = state.warmup;
	value["recharge_remainder"] = state.recharge_remainder;
	value["discharge_remainder"] = state.discharge_remainder;
	value["discharge_credit"] = state.discharge_credit;
	value["discharge"] = state.discharge;
}
void from_json(const Json &value, ShieldState &state)
{
	value.at("strength").get_to(state.strength);
	value.at("warmup").get_to(state.warmup);
	value.at("recharge_remainder").get_to(state.recharge_remainder);
	value.at("discharge_remainder").get_to(state.discharge_remainder);
	value.at("discharge_credit").get_to(state.discharge_credit);
	value.at("discharge").get_to(state.discharge);
}
void to_json(Json &value, const Destructible &state)
{
	value["definition"] = state.definition;
	value["durability"] = state.durability;
	value["destroyed_tick"] = state.destroyed_tick;
	value["processed"] = state.processed;
	value["rule_notified"] = state.rule_notified;
}
void from_json(const Json &value, Destructible &state)
{
	value.at("definition").get_to(state.definition);
	value.at("durability").get_to(state.durability);
	value.at("destroyed_tick").get_to(state.destroyed_tick);
	value.at("processed").get_to(state.processed);
	value.at("rule_notified").get_to(state.rule_notified);
}
void to_json(Json &value, const BlastVisual &state)
{
	value["id"] = state.id;
	value["position"] = state.position;
	value["altitude"] = state.altitude;
	value["tick"] = state.tick;
	value["definition"] = state.definition;
	value["kind"] = state.kind;
	value["bearing"] = state.bearing;
	value["arc"] = state.arc;
}
void from_json(const Json &value, BlastVisual &state)
{
	value.at("id").get_to(state.id);
	value.at("position").get_to(state.position);
	value.at("altitude").get_to(state.altitude);
	value.at("tick").get_to(state.tick);
	value.at("definition").get_to(state.definition);
	value.at("kind").get_to(state.kind);
	value.at("bearing").get_to(state.bearing);
	value.at("arc").get_to(state.arc);
}
} // namespace phoenix
