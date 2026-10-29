module;
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
export module phoenix.simulation:presentation_audio_state;
import phoenix.contracts;
export namespace phoenix::world_detail
{
// Private to the simulation module. Cosmetic receipts never enter checkpoints.
class PresentationAudioState
{
public:
	struct Status
	{
		std::set<std::uint64_t> delivered;
		std::map<std::string, std::uint64_t> abilities;
		std::uint64_t next_comms = 0, next_warning = 0;
		unsigned power_shortage_ticks = 0;
		bool critical = false;
	};
	void emit(EntityId ship, SceneAudio cue, std::uint64_t tick);
	void retire(std::uint64_t tick);
	const std::map<EntityId, std::vector<SceneAudio>> &events() const
	{
		return events_;
	}
	Status &status(EntityId ship)
	{
		return status_[ship];
	}
	bool baselined() const
	{
		return baselined_;
	}
	void mark_baselined()
	{
		baselined_ = true;
	}

private:
	std::map<EntityId, std::vector<SceneAudio>> events_;
	std::map<EntityId, Status> status_;
	std::uint64_t next_event_ = 1;
	bool baselined_ = false;
};
} // namespace phoenix::world_detail
