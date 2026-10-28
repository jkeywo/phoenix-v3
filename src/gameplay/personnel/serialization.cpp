module;
#include <nlohmann/json.hpp>

module phoenix.gameplay.personnel.state;

namespace phoenix
{
void to_json(Json &value, const OfficerState &state)
{
	value["duty"] = state.duty;
	value["location"] = state.location;
	value["edge_to"] = state.edge_to;
	value["remaining"] = state.remaining;
	value["present"] = state.present;
	value["condition"] = state.condition;
	value["facility"] = state.facility;
	value["escort"] = state.escort;
	value["stabilised"] = state.stabilised;
	value["stun_until"] = state.stun_until;
	value["treatment"] = state.treatment;
	value["recovery"] = state.recovery;
	value["casualty_tick"] = state.casualty_tick;
	value["detained"] = state.detained;
}
void from_json(const Json &value, OfficerState &state)
{
	const OfficerState defaults;
	state.duty = value.value("duty", defaults.duty);
	state.location = value.value("location", defaults.location);
	state.edge_to = value.value("edge_to", defaults.edge_to);
	state.remaining = value.value("remaining", defaults.remaining);
	state.present = value.value("present", defaults.present);
	state.condition = value.value("condition", defaults.condition);
	state.facility = value.value("facility", defaults.facility);
	state.escort = value.value("escort", defaults.escort);
	state.stabilised = value.value("stabilised", defaults.stabilised);
	state.stun_until = value.value("stun_until", defaults.stun_until);
	state.treatment = value.value("treatment", defaults.treatment);
	state.recovery = value.value("recovery", defaults.recovery);
	state.casualty_tick = value.value("casualty_tick", defaults.casualty_tick);
	state.detained = value.value("detained", defaults.detained);
}

void to_json(Json &value, const PersonnelState &state)
{
	value["revision"] = state.revision;
	value["officers"] = state.officers;
}
void from_json(const Json &value, PersonnelState &state)
{
	value.at("revision").get_to(state.revision);
	value.at("officers").get_to(state.officers);
}

} // namespace phoenix
