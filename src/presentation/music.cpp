module;
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <string>

module phoenix.presentation;
import phoenix.application;
#include "music.hpp"

namespace phoenix
{
namespace
{
std::string audio_directory()
{
#ifdef __EMSCRIPTEN__
	return "/assets/audio/";
#else
	return std::string(GetApplicationDirectory()) + "assets/audio/";
#endif
}
} // namespace
bool ServerMusic::device()
{
	if (!attempted_device_)
	{
		attempted_device_ = true;
		random_ = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count());
		if (!IsAudioDeviceReady())
		{
			InitAudioDevice();
			owns_device_ = IsAudioDeviceReady();
		}
	}
	return IsAudioDeviceReady();
}
void ServerMusic::click(const Application &app)
{
	if (!device())
		return;
	SetMasterVolume(std::clamp(app.master_volume, 0, 100) / 100.f);
	if (!attempted_click_)
	{
		attempted_click_ = true;
		click_ = LoadSound((audio_directory() + "ui_click.ogg").c_str());
		if (IsSoundValid(click_))
			for (auto &voice : voices_)
				voice = LoadSoundAlias(click_);
	}
	if (!IsSoundValid(click_) || !app.master_volume || !app.sfx_volume)
		return;
	auto &voice = voices_[voice_++ % 8];
	if (!IsSoundValid(voice))
		return;
	// Private cosmetic randomness cannot consume simulation random state.
	random_ = random_ * 1664525u + 1013904223u;
	SetSoundPitch(voice, .94f + .12f * static_cast<float>(random_ / 4294967296.0));
	SetSoundVolume(voice, std::clamp(app.sfx_volume, 0, 100) / 100.f);
	PlaySound(voice);
}
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
	for (auto &voice : voices_)
		if (IsSoundValid(voice))
			UnloadSoundAlias(voice);
	if (IsSoundValid(click_))
		UnloadSound(click_);
	if (owns_device_)
		CloseAudioDevice();
}
void ServerMusic::update(const Application &app)
{
	if (device())
	{
		SetMasterVolume(std::clamp(app.master_volume, 0, 100) / 100.f);
		for (auto &voice : voices_)
			if (IsSoundValid(voice))
				SetSoundVolume(voice, std::clamp(app.sfx_volume, 0, 100) / 100.f);
	}
	const bool playing = app.session && app.session->running();
	const int wanted = !app.can_host || app.screen == Screen::workshop || (app.screen == Screen::gm && !playing) ? 0
	                   : playing                                                                                 ? 2
	                                                                                                             : 1;
	if (!wanted)
	{
		stop();
		return;
	}
	if (!IsAudioDeviceReady())
		return;
	if (track_ != wanted)
	{
		stop();
		track_ = wanted;
		stream_ = LoadMusicStream((audio_directory() + (wanted == 1 ? "menu.ogg" : "ambient.ogg")).c_str());
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
