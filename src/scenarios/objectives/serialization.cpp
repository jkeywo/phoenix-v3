module;
#include <nlohmann/json.hpp>

module phoenix.scenarios.objectives;

namespace phoenix
{
void to_json(Json &value, const PersonnelPosting &state)
{
	value["target"] = state.target;
	value["status"] = state.status;
}
void from_json(const Json &value, PersonnelPosting &state)
{
	value.at("target").get_to(state.target);
	value.at("status").get_to(state.status);
}
void to_json(Json &value, const MissionTransition &state)
{
	value["id"] = state.id;
	value["tick"] = state.tick;
	value["source"] = state.source;
	value["subject"] = state.subject;
	value["kind"] = state.kind;
	value["operation"] = state.operation;
	value["detail"] = state.detail;
}
void from_json(const Json &value, MissionTransition &state)
{
	value.at("id").get_to(state.id);
	value.at("tick").get_to(state.tick);
	value.at("source").get_to(state.source);
	value.at("subject").get_to(state.subject);
	value.at("kind").get_to(state.kind);
	value.at("operation").get_to(state.operation);
	value.at("detail").get_to(state.detail);
}
void to_json(Json &value, const MissionLedger &state)
{
	value["next"] = state.next;
	value["instance"] = state.instance;
	value["states"] = state.states;
	value["lost"] = state.lost;
	value["transitions"] = state.transitions;
}
void from_json(const Json &value, MissionLedger &state)
{
	value.at("next").get_to(state.next);
	value.at("instance").get_to(state.instance);
	value.at("states").get_to(state.states);
	value.at("lost").get_to(state.lost);
	value.at("transitions").get_to(state.transitions);
}
} // namespace phoenix
