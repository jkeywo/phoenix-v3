module;
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>

module phoenix.presentation;
import phoenix.application;
#include "music.hpp"

namespace phoenix
{
void ServerMusic::stop()
{
	if (IsMusicValid(stream_))
	{
		StopMusicStream(stream_);
		UnloadMusicStream(stream_);
	}
	stream_ = {};
	track_ = 0;
}
ServerMusic::~ServerMusic()
{
	stop();
	if (owns_device_)
		CloseAudioDevice();
}
void ServerMusic::update(const Application &app)
{
	const bool playing = app.session && app.session->running();
	const int wanted = !app.can_host || app.screen == Screen::workshop || (app.screen == Screen::gm && !playing) ? 0
	                   : playing                                                                                 ? 2
	                                                                                                             : 1;
	if (!wanted)
	{
		stop();
		return;
	}
	if (!attempted_device_)
	{
		attempted_device_ = true;
		if (!IsAudioDeviceReady())
		{
			InitAudioDevice();
			owns_device_ = IsAudioDeviceReady();
		}
	}
	if (!IsAudioDeviceReady())
		return;
	if (track_ != wanted)
	{
		stop();
		track_ = wanted;
#ifdef __EMSCRIPTEN__
		const std::string directory = "/assets/audio/";
#else
		const std::string directory = std::string(GetApplicationDirectory()) + "assets/audio/";
#endif
		stream_ = LoadMusicStream((directory + (wanted == 1 ? "menu.ogg" : "ambient.ogg")).c_str());
		if (IsMusicValid(stream_))
		{
			stream_.looping = true;
			SetMusicVolume(stream_, std::clamp(app.music_volume, 0, 100) / 100.f);
			PlayMusicStream(stream_);
		}
	}
	if (IsMusicValid(stream_))
	{
		SetMusicVolume(stream_, std::clamp(app.music_volume, 0, 100) / 100.f);
		UpdateMusicStream(stream_);
	}
}
} // namespace phoenix
