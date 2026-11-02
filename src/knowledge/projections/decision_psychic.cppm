module;
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.knowledge.decision_psychic;
export import phoenix.contracts;
export namespace phoenix
{
struct PsychicSeatDecision
{
	std::string duty, officer, power, controller, enabled_channel, target_channel, blocker;
	bool enabled = false;
	std::int64_t target = 0, work = 0, ready = 0;
};
struct PsychicChoirDecision
{
	std::string id;
	std::vector<PsychicSeatDecision> seats;
	std::int64_t emission = 0;
};
struct PsychicFadingDecision
{
	BodyView body;
	std::int64_t opacity = 0;
};
struct PsychicDecisionSnapshot
{
	std::vector<PsychicChoirDecision> choirs;
	std::vector<PsychicFadingDecision> fading;
};
void to_json(Json &, const PsychicSeatDecision &);
void from_json(const Json &, PsychicSeatDecision &);
void to_json(Json &, const PsychicChoirDecision &);
void from_json(const Json &, PsychicChoirDecision &);
void to_json(Json &, const PsychicFadingDecision &);
void from_json(const Json &, PsychicFadingDecision &);
void to_json(Json &, const PsychicDecisionSnapshot &);
void from_json(const Json &, PsychicDecisionSnapshot &);
Json psychic_decision_json(const PsychicDecisionSnapshot &);
} // namespace phoenix
