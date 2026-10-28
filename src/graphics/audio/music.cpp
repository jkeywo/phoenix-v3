module;
#include <cstdint>
#include <optional>
#include <raylib.h>
#include <rlgl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>
#include <span>
#include <cmath>
#include <chrono>
#include <string>
#include <map>
#include <memory>
#include <set>

module phoenix.graphics.audio;
import phoenix.content.assets;

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
struct ServerMusic::State
{
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
	bool host_audio(const AudioPresentation &, const SceneFrame &, bool);
	bool device();
	void stop();

	~State();
	void update(const AudioPresentation &, const SceneFrame &);
	void click(const AudioLevels &);
};

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
AudioStream::~AudioStream()
{
	reset();
}
void AudioStream::reset()
{
	if (IsMusicValid(stream))
		UnloadMusicStream(stream);
	stream = {};
	bytes.clear();
	asset.clear();
}
bool AudioStream::load(const std::string &id, float gain, const Bytes *source, bool file)
{
	reset();
	asset = id; // Cache failures too; do not retry every frame.
	if (file)
		stream = LoadMusicStream(id.c_str());
	else if (source)
	{
		bytes = *source;
		stream = LoadMusicStreamFromMemory(".ogg", bytes.data(), static_cast<int>(bytes.size()));
	}
	if (!IsMusicValid(stream))
	{
		TraceLog(LOG_WARNING, "HOST AUDIO: cannot load stream %s", id.c_str());
		return false;
	}
	TraceLog(LOG_INFO, "HOST AUDIO: stream %s", id.c_str());
	stream.looping = true;
	SetMusicVolume(stream, gain);
	PlayMusicStream(stream);
	return true;
}
void AudioStream::update(float gain)
{
	if (IsMusicValid(stream))
	{
		SetMusicVolume(stream, gain);
		UpdateMusicStream(stream);
	}
}
bool ServerMusic::State::device()
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
void ServerMusic::State::click(const AudioLevels &app)
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
void ServerMusic::State::stop()
{
	stream_.reset();
	track_ = 0;
	track_asset_.clear();
}
void ServerMusic::State::clear_host_audio()
{
	loops_.clear();
	for (auto &voice : shot_voices_)
	{
		if (IsSoundValid(voice.sound))
			UnloadSoundAlias(voice.sound);
		voice = {};
	}
	for (auto &[asset, sound] : shot_assets_)
		if (IsSoundValid(sound))
			UnloadSound(sound);
	shot_assets_.clear();
	mixer_.reset();
}
bool ServerMusic::State::host_audio(const AudioPresentation &app, const SceneFrame &scene, bool active)
{
	const auto own = active ? app.focus_body : std::nullopt;
	const auto bodies = scene.view();
	const auto own_body = own ? std::find_if(bodies.begin(), bodies.end(),
	                                         [&](const auto &b)
	                                         {
		                                         return b.id == own->id;
	                                         })
	                          : bodies.end();
	active = active && own && IsAudioDeviceReady();
	if (!active)
	{
		clear_host_audio();
		return false;
	}
	const bool own_alive = own_body != bodies.end() && own_body->audio_alive;
	const auto &catalog = (*app.catalog);
	if (!ship_audio_.contains(own->definition))
		ship_audio_[own->definition] = compile_ship_audio(catalog, own->definition);
	auto frame =
	    mixer_.frame(bodies, own->id, ship_audio_.at(own->definition), scene.visible, app.tick, active, GetFrameTime());
	// Let a final destruction one-shot finish, while all mission loops stop.
	if (!own_alive)
		frame.loops.clear();
	const float volume = std::clamp(app.levels.sfx_volume, 0, 100) / 100.f;
	std::erase_if(loops_,
	              [&](const auto &entry)
	              {
		              return !frame.loops.contains(entry.first);
	              });
	for (const auto &[key, clip] : frame.loops)
	{
		auto &loop = loops_[key];
		if (loop.asset != clip.asset)
			loop.load(clip.asset, clip.gain * volume,
			          catalog.assets.contains(clip.asset) ? &catalog.assets.at(clip.asset) : nullptr);
		loop.update(clip.gain * volume);
	}
	for (auto &voice : shot_voices_)
		if (IsSoundValid(voice.sound))
			SetSoundVolume(voice.sound, volume * voice.gain);
	for (const auto &clip : frame.shots)
	{
		if (!app.levels.sfx_volume || !app.levels.master_volume)
			continue;
		if (!shot_assets_.contains(clip.asset))
		{
			Sound sound{};
			if (catalog.assets.contains(clip.asset))
			{
				const auto &bytes = catalog.assets.at(clip.asset);
				auto wave = LoadWaveFromMemory(".wav", bytes.data(), static_cast<int>(bytes.size()));
				if (IsWaveValid(wave))
				{
					sound = LoadSoundFromWave(wave);
					UnloadWave(wave);
				}
			}
			shot_assets_[clip.asset] = sound;
			if (IsSoundValid(sound))
				TraceLog(LOG_INFO, "HOST AUDIO: sample %s", clip.asset.c_str());
			if (!IsSoundValid(sound))
				TraceLog(LOG_WARNING, "HOST AUDIO: cannot load shot %s", clip.asset.c_str());
		}
		if (!IsSoundValid(shot_assets_.at(clip.asset)))
			continue;
		auto *selected = &shot_voices_[0];
		for (auto &candidate : shot_voices_)
		{
			if (!IsSoundValid(candidate.sound) || !IsSoundPlaying(candidate.sound))
			{
				selected = &candidate;
				break;
			}
			if (candidate.order < selected->order)
				selected = &candidate;
		}
		auto &voice = *selected;
		if (IsSoundValid(voice.sound))
			UnloadSoundAlias(voice.sound);
		voice.sound = LoadSoundAlias(shot_assets_.at(clip.asset));
		voice.gain = clip.gain;
		voice.order = ++next_shot_voice_;
		if (IsSoundValid(voice.sound))
		{
			SetSoundVolume(voice.sound, clip.gain * volume);
			PlaySound(voice.sound);
		}
	}
	return own_alive;
}
ServerMusic::State::~State()
{
	clear_host_audio();
	stop();
	for (auto &voice : voices_)
		if (IsSoundValid(voice))
			UnloadSoundAlias(voice);
	if (IsSoundValid(click_))
		UnloadSound(click_);
	if (IsSoundValid(siren_))
		UnloadSound(siren_);
	if (owns_device_)
		CloseAudioDevice();
}
void ServerMusic::State::update(const AudioPresentation &app, const SceneFrame &frame)
{
	if (device())
	{
		SetMasterVolume(std::clamp(app.levels.master_volume, 0, 100) / 100.f);
		for (auto &voice : voices_)
			if (IsSoundValid(voice))
				SetSoundVolume(voice, std::clamp(app.levels.sfx_volume, 0, 100) / 100.f);
	}
	const bool playing = app.playing && app.catalog;
	if (session_ != app.session || generation_ != app.generation)
	{
		session_ = app.session;
		generation_ = app.generation;
		clear_host_audio();
		ship_audio_.clear();
		stop();
		if (IsSoundValid(siren_))
			UnloadSound(siren_);
		siren_ = {};
		siren_asset_.clear();
	}
	const bool mission_audio = host_audio(app, frame, playing && app.host && !app.workshop && !app.recovering);

	const auto alert = playing ? app.alert : Json::object();
	const bool live_alert = mission_audio && !alert.empty();
	const auto generation = app.generation;
	const bool entry = alert_transition_.observe(app.session, generation, alert, live_alert);
	if (!live_alert && IsSoundValid(siren_))
		StopSound(siren_);
	if (live_alert)
	{
		const auto asset = alert.value("siren", std::string{});
		if (asset != siren_asset_)
		{
			if (IsSoundValid(siren_))
				UnloadSound(siren_);
			siren_ = {};
			siren_asset_ = asset;
			if (IsAudioDeviceReady() && (*app.catalog).assets.contains(asset))
			{
				const auto &bytes = (*app.catalog).assets.at(asset);
				auto wave = LoadWaveFromMemory(".ogg", bytes.data(), static_cast<int>(bytes.size()));
				if (IsWaveValid(wave))
				{
					siren_ = LoadSoundFromWave(wave);
					UnloadWave(wave);
				}
			}
		}
		if (IsSoundValid(siren_))
		{
			SetSoundVolume(siren_, std::clamp(app.levels.siren_volume, 0, 100) / 100.f);
			if (entry && app.levels.master_volume && app.levels.siren_volume)
				PlaySound(siren_);
		}
	}
	const int wanted = !app.host || app.workshop || (playing && !mission_audio) || (app.gm && !playing) ? 0
	                   : playing                                                                        ? 2
	                                                                                                    : 1;
	if (!wanted)
	{
		stop();
		return;
	}
	if (!IsAudioDeviceReady())
		return;
	const auto asset = wanted == 2 ? condition_music(alert) : std::string{};
	const float music_gain = asset.empty() ? 1.f : alert.value("music_gain", 1.f);
	if (track_ != wanted || track_asset_ != asset)
	{
		stop();
		track_ = wanted;
		track_asset_ = asset;
		const float gain = music_gain * std::clamp(app.levels.music_volume, 0, 100) / 100.f;
		if (asset.empty())
			stream_.load(audio_directory() + (wanted == 1 ? "menu.ogg" : "ambient.ogg"), gain, nullptr, true);
		else if (!stream_.load(asset, gain,
		                       (*app.catalog).assets.contains(asset) ? &(*app.catalog).assets.at(asset) : nullptr))
			stream_.load(audio_directory() + "ambient.ogg", gain, nullptr, true);
	}
	stream_.update(music_gain * std::clamp(app.levels.music_volume, 0, 100) / 100.f);
}
ServerMusic::ServerMusic() : state_(std::make_unique<State>())
{
}
ServerMusic::~ServerMusic() = default;
void ServerMusic::update(const AudioPresentation &presentation, const SceneFrame &frame)
{
	state_->update(presentation, frame);
}
void ServerMusic::click(const AudioLevels &levels)
{
	state_->click(levels);
}
} // namespace phoenix
