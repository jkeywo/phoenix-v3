module;
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.knowledge.decision_psychic;
import phoenix.knowledge.read_models;
namespace phoenix
{
void to_json(Json &out, const PsychicSeatDecision &row)
{
	out = {{"duty", row.duty},
	       {"officer", row.officer},
	       {"power", row.power},
	       {"controller", row.controller},
	       {"enabled_channel", row.enabled_channel},
	       {"target_channel", row.target_channel},
	       {"blocker", row.blocker},
	       {"enabled", row.enabled},
	       {"target", row.target},
	       {"work", row.work},
	       {"ready", row.ready}};
}
void from_json(const Json &in, PsychicSeatDecision &row)
{
	const PsychicSeatDecision defaults;
	row.duty = in.value("duty", defaults.duty);
	row.officer = in.value("officer", defaults.officer);
	row.power = in.value("power", defaults.power);
	row.controller = in.value("controller", defaults.controller);
	row.enabled_channel = in.value("enabled_channel", defaults.enabled_channel);
	row.target_channel = in.value("target_channel", defaults.target_channel);
	row.blocker = in.value("blocker", defaults.blocker);
	row.enabled = in.value("enabled", defaults.enabled);
	row.target = in.value("target", defaults.target);
	row.work = in.value("work", defaults.work);
	row.ready = in.value("ready", defaults.ready);
}
void to_json(Json &out, const PsychicChoirDecision &row)
{
	out = {{"id", row.id}, {"seats", row.seats}, {"emission", row.emission}};
}
void from_json(const Json &in, PsychicChoirDecision &row)
{
	const PsychicChoirDecision defaults;
	row.id = in.value("id", defaults.id);
	row.seats = in.value("seats", defaults.seats);
	row.emission = in.value("emission", defaults.emission);
}
void to_json(Json &out, const PsychicDecisionSnapshot &row)
{
	out = {{"choirs", row.choirs}, {"fading", row.fading}};
}
void from_json(const Json &in, PsychicDecisionSnapshot &row)
{
	const PsychicDecisionSnapshot defaults;
	row.choirs = in.value("choirs", defaults.choirs);
	row.fading = in.value("fading", defaults.fading);
}
void to_json(Json &out, const PsychicFadingDecision &row)
{
	out = {{"body", projection::body_json(row.body)}, {"opacity", row.opacity}};
}
void from_json(const Json &in, PsychicFadingDecision &row)
{
	row.body = projection::read_body(in.at("body"), projection::BodyReadPolicy::SharedDisplay);
	row.opacity = in.at("opacity");
}
Json psychic_decision_json(const PsychicDecisionSnapshot &row)
{
	return Json(row);
}
} // namespace phoenix
