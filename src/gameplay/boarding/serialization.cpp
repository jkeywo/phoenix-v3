module;
#include <nlohmann/json.hpp>

module phoenix.gameplay.boarding.state;

namespace phoenix
{
void to_json(Json &json, const BoardingState &state)
{
	json["location"] = state.location;
	json["destination"] = state.destination;
	json["edge_to"] = state.edge_to;
	json["travel"] = state.travel;
	json["assessment"] = state.assessment;
	json["chance"] = state.chance;
	json["defenders"] = state.defenders;
	json["encounter"] = state.encounter;
	json["encounter_done"] = state.encounter_done;
	json["encounter_success"] = state.encounter_success;
	json["assessed"] = state.assessed;
	json["reported"] = state.reported;
	json["captured"] = state.captured;
	json["prize_crew"] = state.prize_crew;
	json["outcomes"] = state.outcomes;
	json["neutralised"] = state.neutralised;
	json["factors"] = state.factors;
	json["secured"] = state.secured;
	json["assessment_message"] = state.assessment_message;
}
void from_json(const Json &json, BoardingState &state)
{
	json.at("location").get_to(state.location);
	json.at("destination").get_to(state.destination);
	json.at("edge_to").get_to(state.edge_to);
	json.at("travel").get_to(state.travel);
	json.at("assessment").get_to(state.assessment);
	json.at("chance").get_to(state.chance);
	json.at("defenders").get_to(state.defenders);
	json.at("encounter").get_to(state.encounter);
	json.at("encounter_done").get_to(state.encounter_done);
	json.at("encounter_success").get_to(state.encounter_success);
	json.at("assessed").get_to(state.assessed);
	json.at("reported").get_to(state.reported);
	json.at("captured").get_to(state.captured);
	json.at("prize_crew").get_to(state.prize_crew);
	json.at("outcomes").get_to(state.outcomes);
	json.at("neutralised").get_to(state.neutralised);
	json.at("factors").get_to(state.factors);
	json.at("secured").get_to(state.secured);
	json.at("assessment_message").get_to(state.assessment_message);
}
} // namespace phoenix
