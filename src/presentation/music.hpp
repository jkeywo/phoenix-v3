#pragma once
#include "alert_audio.hpp"
#include "host_audio.hpp"
namespace phoenix
{
// Local presentation only: no audio state is replicated to other crew clients.
class ServerMusic
{
public:
	~ServerMusic();
	void update(const Application &app);
	void click(const Application &app);

private:
	Music stream_{};
	int track_ = 0;
	std::string track_asset_, siren_asset_;
	Bytes track_bytes_;
	Sound siren_{};
	const void *combat_session_ = nullptr;
	std::uint64_t combat_generation_ = 0;
	const void *alert_session_ = nullptr;
	std::uint64_t alert_generation_ = 0;
	AlertAudioTransition alert_transition_;
	bool attempted_device_ = false, owns_device_ = false;
	Sound click_{};
	Sound voices_[8]{};
	unsigned voice_ = 0, random_ = 1;
	bool attempted_click_ = false;
	struct Loop
	{
		Music stream{};
		Bytes bytes;
		std::string asset;
	};
	struct ShotVoice
	{
		Sound sound{};
		float gain = 1;
		std::uint64_t order = 0;
	};
	std::map<std::string, Loop> loops_;
	std::map<std::string, Sound> shot_assets_;
	ShotVoice shot_voices_[16]{};
	std::uint64_t next_shot_voice_ = 0;
	std::map<std::string, ShipAudioDefinition> ship_audio_;
	std::map<std::string, float> ship_aspects_;
	HostAudioMixer mixer_;
	void clear_host_audio();
	bool host_audio(const Application &, bool);
	bool device();
	void stop();
};
} // namespace phoenix
