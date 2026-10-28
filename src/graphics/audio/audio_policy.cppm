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
export module phoenix.graphics.audio_policy;
export import phoenix.content.assets;

export namespace phoenix
{
// The audio boundary consumes revisions, never replays historical state.
struct AlertAudioTransition
{
	const void *session = nullptr;
	std::uint64_t generation = 0, revision = 0;
	bool ready = false;
	bool observe(const void *current, std::uint64_t epoch, const Json &alert, bool enabled);
};
std::string condition_music(const Json &alert);
} // namespace phoenix

export namespace phoenix
{
struct AudioMixFrame
{
	std::map<std::string, AudioClip> loops;
	std::vector<AudioClip> shots;
};
// Device-independent production mixer policy. Only the host supplies its local
// visible scene; no cue or gain envelope enters replication or saved gameplay.
class HostAudioMixer
{
	std::set<std::string> seen_;
	std::map<std::string, float> levels_;
	bool ready_ = false;

public:
	void reset();
	AudioMixFrame frame(std::span<const SceneBody> bodies, EntityId own, const ShipAudioDefinition &ship,
	                    const std::set<EntityId> &visible, std::uint64_t tick, bool active, float seconds);
};
} // namespace phoenix
