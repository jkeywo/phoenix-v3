module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.graphics.audio_policy;

namespace phoenix
{
bool AlertAudioTransition::observe(const void *current, std::uint64_t epoch, const Json &alert, bool enabled)
{
	const bool same = session == current && generation == epoch;
	const auto next = enabled && !alert.empty() ? alert.at("revision").get<std::uint64_t>() : 0;
	const bool entry = same && ready && enabled && !alert.empty() && next != revision;
	session = current;
	generation = epoch;
	revision = next;
	ready = enabled && !alert.empty();
	return entry;
}
std::string condition_music(const Json &alert)
{
	return alert.is_object() ? alert.value("music", std::string{}) : std::string{};
}
void HostAudioMixer::reset()
{
	seen_.clear();
	levels_.clear();
	ready_ = false;
}
AudioMixFrame HostAudioMixer::frame(std::span<const SceneBody> bodies, EntityId own, const ShipAudioDefinition &ship,
                                    const std::set<EntityId> &visible, std::uint64_t tick, bool active, float seconds)
{
	AudioMixFrame result;
	if (!active)
	{
		reset();
		return result;
	}
	seconds = std::clamp(seconds, 0.f, .1f);
	std::set<std::string> current;
	std::map<std::string, SceneAudio> shields;
	for (const auto &body : bodies)
	{
		if (!body.audio_alive)
			continue;
		const auto prefix = std::to_string(body.id) + ":";
		if (body.id == own)
		{
			if (!ship.ambient.asset.empty())
				result.loops[prefix + "ambient"] = ship.ambient;
			if (!ship.engine.asset.empty())
			{
				auto clip = ship.engine;
				const auto key = prefix + "engine";
				const float target = clip.gain * std::clamp(body.forward_thrust, 0, 1000) / 1000.f;
				auto &level = levels_[key];
				level += (target - level) * (1.f - std::exp(-8.f * seconds));
				clip.gain = level;
				result.loops[key] = clip;
			}
		}
		for (const auto &cue : body.audio)
		{
			if (cue.clip.asset.empty())
				continue;
			const auto key = prefix + cue.source + ":" + cue.clip.asset;
			bool fresh = false;
			if (cue.event)
			{
				const auto identity = key + ":" + std::to_string(cue.event);
				current.insert(identity);
				fresh = ready_ && !seen_.contains(identity) && tick >= cue.tick && tick <= cue.tick + 3;
			}
			// Consume unseen off-screen receipts too: turning the camera must
			// never replay an earlier shot or impact.
			if (!visible.contains(body.id))
				continue;
			if (cue.kind == SceneAudioKind::Shot)
			{
				if (fresh)
					result.shots.push_back(cue.clip);
			}
			else if (cue.kind == SceneAudioKind::Loop)
				result.loops[key] = cue.clip;
			else
			{
				auto [at, added] = shields.try_emplace(key, cue);
				auto &aggregate = at->second;
				if (added)
					aggregate.damage = 0;
				aggregate.clip.gain = std::max(aggregate.clip.gain, cue.clip.gain);
				aggregate.peak_gain = std::max(aggregate.peak_gain, cue.peak_gain);
				if (fresh)
					aggregate.damage += cue.damage;
			}
		}
	}
	for (const auto &[key, cue] : shields)
	{
		auto &level = levels_[key];
		// V2 decays normalized intensity, then maps bed -> peak gain.
		level = std::max(0.f, level - cue.decay * seconds);
		if (cue.damage >= cue.threshold)
			level = std::max(level, cue.full_damage <= cue.threshold
			                            ? 1.f
			                            : std::min(1.f, static_cast<float>(cue.damage - cue.threshold) /
			                                                (cue.full_damage - cue.threshold)));
		result.loops[key] = {cue.clip.asset, cue.clip.gain + (cue.peak_gain - cue.clip.gain) * level};
	}
	std::erase_if(levels_,
	              [&](const auto &entry)
	              {
		              return !result.loops.contains(entry.first);
	              });
	seen_ = std::move(current);
	ready_ = true;
	return result;
}
} // namespace phoenix
