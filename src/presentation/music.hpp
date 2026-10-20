#pragma once
#include "alert_audio.hpp"
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
	std::map<std::string, Sound> combat_sounds_;
	std::set<std::uint64_t> combat_seen_;
	const void *combat_session_ = nullptr;
	std::uint64_t combat_generation_ = 0;
	bool combat_initialised_ = false;
	const void *alert_session_ = nullptr;
	std::uint64_t alert_generation_ = 0;
	AlertAudioTransition alert_transition_;
	bool attempted_device_ = false, owns_device_ = false;
	Sound click_{};
	Sound voices_[8]{};
	unsigned voice_ = 0, random_ = 1;
	bool attempted_click_ = false;
	bool device();
	void stop();
};
} // namespace phoenix
