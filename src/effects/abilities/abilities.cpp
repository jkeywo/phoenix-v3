module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.abilities;

namespace phoenix
{
AbilityRuntime initial_ability_runtime(std::span<const AbilityGrantSeed> seeds)
{
	AbilityRuntime runtime;
	for (const auto &seed : seeds)
	{
		AbilityGrantState grant;
		grant.source = seed.source;
		grant.owner = seed.owner;
		grant.definition = seed.definition;
		grant.id = ability_recovery_key(grant);
		grant.generation = runtime.next_generation++;
		runtime.grants.emplace(grant.id, std::move(grant));
	}
	return runtime;
}

bool ability_running(const AbilityGrantState &g)
{
	return g.phase == "preparing" || g.phase == "active" || g.phase == "paused";
}

std::string ability_recovery_key(const AbilityGrantState &g)
{
	return g.source + ":" + g.owner + ":" + g.definition;
}

std::string ability_exclusion_key(const AbilityGrantState &g, const AbilityExclusionDefinition &e)
{
	return e.scope + ":" + (e.scope == "apparatus" ? g.apparatus : g.source + ":" + g.owner) + ":" + e.id;
}

std::uint64_t ability_recovery_until(const AbilityRuntime &runtime, const AbilityGrantState &g,
                                     const AbilityDefinition &d)
{
	const auto &ledger = runtime.recovery;
	const auto at = ledger.find(ability_recovery_key(g));
	auto until = at == ledger.end() ? 0 : at->second;
	if (!d.apparatus_recovery_key.empty() && !g.apparatus.empty())
	{
		const auto shared = ledger.find("apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key);
		if (shared != ledger.end())
			until = std::max(until, shared->second);
	}
	return until;
}

void ability_begin_recovery(AbilityRuntime &runtime, const AbilityGrantState &g, const AbilityDefinition &d,
                            std::uint64_t tick)
{
	if (d.recovery_ticks)
		runtime.recovery[ability_recovery_key(g)] =
		    std::max(runtime.recovery[ability_recovery_key(g)], tick + d.recovery_ticks);
	if (d.apparatus_recovery_ticks && !g.apparatus.empty())
	{
		const auto key = "apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key;
		runtime.recovery[key] = std::max(runtime.recovery[key], tick + d.apparatus_recovery_ticks);
	}
}

std::vector<std::string> ability_recovery_reservations(const AbilityGrantState &g, const AbilityDefinition &d)
{
	std::vector<std::string> keys;
	if (d.recovery_ticks || d.interruption_recovery_ticks)
		keys.push_back(ability_recovery_key(g));
	if (d.apparatus_recovery_ticks && !g.apparatus.empty())
		keys.push_back("apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key);
	return keys;
}

bool ability_recovery_capacity(const AbilityRuntime &runtime, const AbilityGrantState &g, const AbilityDefinition &d,
                               std::uint64_t tick)
{
	(void)effect_expiry(tick, std::max({d.recovery_ticks, d.interruption_recovery_ticks, d.apparatus_recovery_ticks}));
	const auto &ledger = runtime.recovery;
	auto size = ledger.size();
	for (const auto &key : ability_recovery_reservations(g, d))
		if (!ledger.contains(key))
			++size;
	return size <= ability_recovery_limit;
}

void install_ability_grant(AbilityRuntime &runtime, const std::string &source, const std::string &owner,
                           const std::string &parent, const std::string &effect,
                           const AbilityGrantDefinition &definition, std::int64_t tick)
{
	const auto id = "temporary:" + parent + ":" + source + ":" + owner;
	const auto old = runtime.grants.find(id);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	if (old != runtime.grants.end())
	{
		if (old->second.definition != definition.ability || old->second.grant_effect != effect)
			throw std::runtime_error("Ability grant identity changed definition");
		old->second.expiry = refresh_effect_expiry(old->second.expiry, expiry);
		return;
	}
	if (runtime.grants.size() >= ability_grant_limit || runtime.next_generation >= ability_generation_limit)
		throw std::runtime_error("Ability grant capacity exhausted");
	AbilityGrantState grant;
	grant.id = id;
	grant.source = source;
	grant.owner = owner;
	grant.parent = parent;
	grant.grant_effect = effect;
	grant.definition = definition.ability;
	grant.expiry = expiry;
	grant.generation = runtime.next_generation++;
	runtime.grants.emplace(id, std::move(grant));
}

void to_json(Json &out, const AbilityGrantState &value)
{
	out["id"] = value.id;
	out["definition"] = value.definition;
	out["source"] = value.source;
	out["owner"] = value.owner;
	out["parent"] = value.parent;
	out["grant_effect"] = value.grant_effect;
	out["control"] = value.control;
	out["apparatus"] = value.apparatus;
	out["phase"] = value.phase;
	out["resume_phase"] = value.resume_phase;
	out["target"] = value.target;
	out["generation"] = value.generation;
	out["activation"] = value.activation;
	out["request_id"] = value.request_id;
	out["expiry"] = value.expiry;
	out["progress"] = value.progress;
	out["aim_x"] = value.aim_x;
	out["aim_y"] = value.aim_y;
	out["credits"] = value.credits;
	out["funding_permille"] = value.funding_permille;
	out["retained_request"] = value.retained_request;
}
void from_json(const Json &in, AbilityGrantState &value)
{
	in.at("id").get_to(value.id);
	in.at("definition").get_to(value.definition);
	in.at("source").get_to(value.source);
	in.at("owner").get_to(value.owner);
	in.at("parent").get_to(value.parent);
	in.at("grant_effect").get_to(value.grant_effect);
	in.at("control").get_to(value.control);
	in.at("apparatus").get_to(value.apparatus);
	in.at("phase").get_to(value.phase);
	in.at("resume_phase").get_to(value.resume_phase);
	in.at("target").get_to(value.target);
	in.at("generation").get_to(value.generation);
	in.at("activation").get_to(value.activation);
	in.at("request_id").get_to(value.request_id);
	in.at("expiry").get_to(value.expiry);
	in.at("progress").get_to(value.progress);
	in.at("aim_x").get_to(value.aim_x);
	in.at("aim_y").get_to(value.aim_y);
	in.at("credits").get_to(value.credits);
	in.at("funding_permille").get_to(value.funding_permille);
	in.at("retained_request").get_to(value.retained_request);
}

void to_json(Json &out, const AbilityRuntime &value)
{
	out["grants"] = value.grants;
	out["recovery"] = value.recovery;
	out["next_generation"] = value.next_generation;
	out["next_activation"] = value.next_activation;
	out["work_tick"] = value.work_tick;
	out["work"] = value.work;
}
void from_json(const Json &in, AbilityRuntime &value)
{
	in.at("grants").get_to(value.grants);
	in.at("recovery").get_to(value.recovery);
	in.at("next_generation").get_to(value.next_generation);
	in.at("next_activation").get_to(value.next_activation);
	in.at("work_tick").get_to(value.work_tick);
	in.at("work").get_to(value.work);
}

} // namespace phoenix
