#pragma once
#include "alert_audio.hpp"
#include "host_audio.hpp"
namespace phoenix
{
// Own the memory backing a raylib stream until after it is unloaded.
struct AudioStream
{
	Music stream{};
	Bytes bytes;
	std::string asset;
	AudioStream() = default;
	AudioStream(const AudioStream &) = delete;
	AudioStream &operator=(const AudioStream &) = delete;
	~AudioStream();
	void reset();
	bool load(const std::string &, float gain, const Bytes * = nullptr, bool file = false);
	void update(float gain);
};
// Local presentation only: no audio state is replicated to other crew clients.
class ServerMusic
{
public:
	~ServerMusic();
	void update(const Application &app, const SceneFrame &frame);
	void click(const Application &app);

private:
	AudioStream stream_;
	int track_ = 0;
	std::string track_asset_, siren_asset_;
	Sound siren_{};
	const void *session_ = nullptr;
	std::uint64_t generation_ = 0;
	AlertAudioTransition alert_transition_;
	bool attempted_device_ = false, owns_device_ = false;
	Sound click_{};
	Sound voices_[8]{};
	unsigned voice_ = 0, random_ = 1;
	bool attempted_click_ = false;
	struct ShotVoice
	{
		Sound sound{};
		float gain = 1;
		std::uint64_t order = 0;
	};
	std::map<std::string, AudioStream> loops_;
	std::map<std::string, Sound> shot_assets_;
	ShotVoice shot_voices_[16]{};
	std::uint64_t next_shot_voice_ = 0;
	std::map<std::string, ShipAudioDefinition> ship_audio_;
	HostAudioMixer mixer_;
	void clear_host_audio();
	bool host_audio(const Application &, const SceneFrame &, bool);
	bool device();
	void stop();
};
} // namespace phoenix
