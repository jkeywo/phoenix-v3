module;
#include <raylib.h>
#include <rlgl.h>
#include "ship_camera.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>
#include <span>
#include <cmath>
#include <chrono>
#include <string>
#include <map>
#include <set>

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
	track_asset_.clear();
	track_bytes_.clear();
}
void ServerMusic::clear_host_audio()
{
	for (auto &[key, loop] : loops_)
		if (IsMusicValid(loop.stream))
			UnloadMusicStream(loop.stream);
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
bool ServerMusic::host_audio(const Application &app, bool active)
{
	const auto own = active ? app.session->focus_body() : std::nullopt;
	auto bodies = active ? app.session->scene() : std::vector<SceneBody>{};
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
	const auto &catalog = app.session->catalog();
	if (!ship_audio_.contains(own->definition))
		ship_audio_[own->definition] = compile_ship_audio(catalog, own->definition);
	const auto &render = catalog.entities.at(own->definition).at("render");
	if (!ship_aspects_.contains(own->definition))
	{
		float value = 1;
		if (render.contains("sprite"))
		{
			const auto sprite = compile_sprite(render.at("sprite"), catalog, own->definition);
			auto frames = [&](const auto &items)
			{
				for (const auto &f : items)
					value = std::max(value, static_cast<float>(f.rect[2]) / f.rect[3]);
			};
			frames(sprite.frames);
			for (const auto &entry : sprite.entries)
				frames(entry.frames);
		}
		else if (render.contains("images"))
			for (const auto &asset : render.at("images"))
			{
				const auto dimensions = verified_image_dimensions(catalog.assets.at(asset.get<std::string>()));
				value = std::max(value, static_cast<float>(dimensions[0]) / dimensions[1]);
			}
		ship_aspects_[own->definition] = value;
	}
	const auto settings = ship_camera_state(app.session->public_board());
	const auto pose = ship_camera_pose(own->x, own->y, own->heading, render.at("scale").get<float>(),
	                                   ship_aspects_.at(own->definition), settings.mode, settings.direction);
	const float aspect = app.viewscreen_region ? app.viewscreen_region->width / app.viewscreen_region->height
	                                           : static_cast<float>(GetScreenWidth()) / std::max(1, GetScreenHeight());
	std::set<EntityId> visible;
	for (auto &body : bodies)
	{
		const float radius = catalog.entities.contains(body.definition)
		                         ? catalog.entities.at(body.definition).at("render").value("scale", 1.f) * 2
		                         : 1;
		// Own-ship actions are audible even when its hull is hidden by the cockpit camera.
		if (body.id == own->id ||
		    ship_camera_visible(pose, body.x, body.y, body.visual_height_mm, radius, aspect, rlGetCullDistanceFar()))
			visible.insert(body.id);
		if (!body.sound.empty())
			body.audio.push_back({SceneAudioKind::Shot, "explosion", {body.sound, 1}, body.id, body.effect_tick});
	}
	auto frame = mixer_.frame(bodies, own->id, ship_audio_.at(own->definition), visible, app.session->ticks(), active,
	                          GetFrameTime());
	// Let a final destruction one-shot finish, while all mission loops stop.
	if (!own_alive)
		frame.loops.clear();
	const float volume = std::clamp(app.sfx_volume, 0, 100) / 100.f;
	std::erase_if(loops_,
	              [&](auto &entry)
	              {
		              if (frame.loops.contains(entry.first))
			              return false;
		              if (IsMusicValid(entry.second.stream))
			              UnloadMusicStream(entry.second.stream);
		              return true;
	              });
	for (const auto &[key, clip] : frame.loops)
	{
		auto [at, added] = loops_.try_emplace(key);
		auto &loop = at->second;
		if (added || loop.asset != clip.asset)
		{
			if (IsMusicValid(loop.stream))
				UnloadMusicStream(loop.stream);
			loop.stream = {};
			loop.asset = clip.asset;
			if (catalog.assets.contains(clip.asset))
			{
				loop.bytes = catalog.assets.at(clip.asset);
				loop.stream = LoadMusicStreamFromMemory(".ogg", loop.bytes.data(), static_cast<int>(loop.bytes.size()));
			}
			if (IsMusicValid(loop.stream))
			{
				TraceLog(LOG_INFO, "HOST AUDIO: loop %s", clip.asset.c_str());
				loop.stream.looping = true;
				SetMusicVolume(loop.stream, clip.gain * volume);
				PlayMusicStream(loop.stream);
			}
			else
				TraceLog(LOG_WARNING, "HOST AUDIO: cannot load loop %s", clip.asset.c_str());
		}
		if (IsMusicValid(loop.stream))
		{
			SetMusicVolume(loop.stream, clip.gain * volume);
			UpdateMusicStream(loop.stream);
		}
	}
	for (auto &voice : shot_voices_)
		if (IsSoundValid(voice.sound))
			SetSoundVolume(voice.sound, volume * voice.gain);
	for (const auto &clip : frame.shots)
	{
		if (!app.sfx_volume || !app.master_volume)
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
ServerMusic::~ServerMusic()
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
	if (combat_session_ != app.session.get() || combat_generation_ != (app.session ? app.session->generation() : 0))
	{
		combat_session_ = app.session.get();
		combat_generation_ = app.session ? app.session->generation() : 0;
		clear_host_audio();
		ship_audio_.clear();
		ship_aspects_.clear();
	}
	const bool mission_audio =
	    host_audio(app, playing && app.can_host && app.screen != Screen::workshop && !app.session->recovery_pending());

	const auto alert = playing ? app.session->public_board().value("alert", Json::object()) : Json::object();
	const bool live_alert = mission_audio && !alert.empty();
	const auto generation = app.session ? app.session->generation() : 0;
	const bool entry = alert_transition_.observe(app.session.get(), generation, alert, live_alert);
	if (!live_alert || alert_session_ != app.session.get() || alert_generation_ != generation)
	{
		if (alert_session_ != app.session.get() || alert_generation_ != generation)
		{
			stop();
			if (IsSoundValid(siren_))
				UnloadSound(siren_);
			siren_ = {};
			siren_asset_.clear();
		}
		alert_session_ = app.session.get();
		alert_generation_ = generation;
		if (IsSoundValid(siren_))
			StopSound(siren_);
	}
	if (live_alert)
	{
		const auto asset = alert.value("siren", std::string{});
		if (asset != siren_asset_)
		{
			if (IsSoundValid(siren_))
				UnloadSound(siren_);
			siren_ = {};
			siren_asset_ = asset;
			if (IsAudioDeviceReady() && app.session->catalog().assets.contains(asset))
			{
				const auto &bytes = app.session->catalog().assets.at(asset);
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
			SetSoundVolume(siren_, std::clamp(app.siren_volume, 0, 100) / 100.f);
			if (entry && app.master_volume && app.siren_volume)
				PlaySound(siren_);
		}
	}
	const int wanted = !app.can_host || app.screen == Screen::workshop || (playing && !mission_audio) ||
	                           (app.screen == Screen::gm && !playing)
	                       ? 0
	                   : playing ? 2
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
		if (!asset.empty() && app.session->catalog().assets.contains(asset))
		{
			track_bytes_ = app.session->catalog().assets.at(asset);
			stream_ = LoadMusicStreamFromMemory(".ogg", track_bytes_.data(), static_cast<int>(track_bytes_.size()));
		}
		else
			stream_ = LoadMusicStream((audio_directory() + (wanted == 1 ? "menu.ogg" : "ambient.ogg")).c_str());
		if (!IsMusicValid(stream_) && !asset.empty())
		{
			TraceLog(LOG_WARNING, "HOST AUDIO: cannot load music %s; using ordinary mission music", asset.c_str());
			stream_ = LoadMusicStream((audio_directory() + "ambient.ogg").c_str());
		}
		if (IsMusicValid(stream_))
		{
			TraceLog(LOG_INFO, "HOST AUDIO: music %s",
			         asset.empty() ? (wanted == 1 ? "menu.ogg" : "ambient.ogg") : asset.c_str());
			stream_.looping = true;
			SetMusicVolume(stream_, music_gain * std::clamp(app.music_volume, 0, 100) / 100.f);
			PlayMusicStream(stream_);
		}
	}
	if (IsMusicValid(stream_))
	{
		SetMusicVolume(stream_, music_gain * std::clamp(app.music_volume, 0, 100) / 100.f);
		UpdateMusicStream(stream_);
	}
}
} // namespace phoenix
