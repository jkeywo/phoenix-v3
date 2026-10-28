module;
#include <nlohmann/json.hpp>
#include <stdexcept>

module phoenix.gameplay.away.state;

namespace phoenix
{
void to_json(Json &json, AwayPhase phase)
{
	for (const auto &[value, name] : away_phases)
		if (value == phase)
		{
			json = name;
			return;
		}
	throw std::runtime_error("Invalid away phase");
}
void from_json(const Json &json, AwayPhase &phase)
{
	for (const auto &[value, name] : away_phases)
		if (json == name)
		{
			phase = value;
			return;
		}
	throw std::runtime_error("Invalid saved away phase");
}
bool AwayMission::deployed() const
{
	return (phase == AwayPhase::Deployed || phase == AwayPhase::Inbound) && !aboard_craft;
}
bool AwayMission::absent() const
{
	return deployed() || aboard_craft || phase == AwayPhase::Captured ||
	       (phase == AwayPhase::Lost && !assignments.empty());
}
void to_json(Json &json, const AwayCare &state)
{
	json["location"] = state.location;
	json["edge_to"] = state.edge_to;
	json["travel"] = state.travel;
	json["order"] = state.order;
	json["activity"] = state.activity;
	json["done"] = state.done;
	json["work_done"] = state.work_done;
}
void from_json(const Json &json, AwayCare &state)
{
	json.at("location").get_to(state.location);
	json.at("edge_to").get_to(state.edge_to);
	json.at("travel").get_to(state.travel);
	json.at("order").get_to(state.order);
	json.at("activity").get_to(state.activity);
	json.at("done").get_to(state.done);
	json.at("work_done").get_to(state.work_done);
}
void to_json(Json &json, const AwayMission &state)
{
	json["phase"] = state.phase;
	json["transport"] = state.transport;
	json["checkpoint"] = state.checkpoint;
	json["outcome"] = state.outcome;
	json["work_key"] = state.work_key;
	json["repair_compartment"] = state.repair_compartment;
	json["assignments"] = state.assignments;
	json["accompanying"] = state.accompanying;
	json["revision"] = state.revision;
	json["sequence"] = state.sequence;
	json["transfer"] = state.transfer;
	json["work"] = state.work;
	json["progress"] = state.progress;
	json["recovered"] = state.recovered;
	json["effects"] = state.effects;
	json["ever_deployed"] = state.ever_deployed;
	json["participants"] = state.participants;
	json["care"] = state.care;
	json["boarding"] = state.boarding;
	json["aboard_craft"] = state.aboard_craft;
	json["transport_released"] = state.transport_released;
	json["entry_compartment"] = state.entry_compartment;
	json["flags"] = state.flags;
	json["saved_progress"] = state.saved_progress;
	json["timed_work"] = state.timed_work;
	json["crew_deaths"] = state.crew_deaths;
	json["work_capability"] = state.work_capability;
	json["escorts"] = state.escorts;
	json["authorised_departments"] = state.authorised_departments;
	json["supply_quantity"] = state.supply_quantity;
	json["supply_job"] = state.supply_job;
}
void from_json(const Json &json, AwayMission &state)
{
	const AwayMission defaults;
	state.phase = json.value("phase", defaults.phase);
	state.transport = json.value("transport", defaults.transport);
	state.checkpoint = json.value("checkpoint", defaults.checkpoint);
	state.outcome = json.value("outcome", defaults.outcome);
	state.work_key = json.value("work_key", defaults.work_key);
	state.repair_compartment = json.value("repair_compartment", defaults.repair_compartment);
	state.assignments = json.value("assignments", defaults.assignments);
	state.accompanying = json.value("accompanying", defaults.accompanying);
	state.revision = json.value("revision", defaults.revision);
	state.sequence = json.value("sequence", defaults.sequence);
	state.transfer = json.value("transfer", defaults.transfer);
	state.work = json.value("work", defaults.work);
	state.progress = json.value("progress", defaults.progress);
	state.recovered = json.value("recovered", defaults.recovered);
	state.effects = json.value("effects", defaults.effects);
	state.ever_deployed = json.value("ever_deployed", defaults.ever_deployed);
	state.participants = json.value("participants", defaults.participants);
	state.care = json.value("care", defaults.care);
	state.boarding = json.value("boarding", defaults.boarding);
	state.aboard_craft = json.value("aboard_craft", defaults.aboard_craft);
	state.transport_released = json.value("transport_released", defaults.transport_released);
	state.entry_compartment = json.value("entry_compartment", defaults.entry_compartment);
	state.flags = json.value("flags", defaults.flags);
	state.saved_progress = json.value("saved_progress", defaults.saved_progress);
	state.timed_work = json.value("timed_work", defaults.timed_work);
	state.crew_deaths = json.value("crew_deaths", defaults.crew_deaths);
	state.work_capability = json.value("work_capability", defaults.work_capability);
	state.escorts = json.value("escorts", defaults.escorts);
	state.authorised_departments = json.value("authorised_departments", defaults.authorised_departments);
	state.supply_quantity = json.value("supply_quantity", defaults.supply_quantity);
	state.supply_job = json.value("supply_job", defaults.supply_job);
}
} // namespace phoenix
