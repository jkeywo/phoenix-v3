module;
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
module phoenix.graphics.audio;
import phoenix.graphics.audio_backend;
import phoenix.content.assets;

namespace phoenix
{
struct ServerMusic::State
{
	audio_backend::Outputs outputs;
	double refresh_at = 0;
	std::vector<std::pair<std::string, std::string>> devices;
	std::string output, track_asset, siren_asset;
	const void *session = nullptr;
	std::uint64_t generation = 0;
	HostAudioMixer mixer;
	AlertAudioTransition alert_transition;
	std::map<std::string, ShipAudioDefinition> ship_audio;
	struct Loop
	{
		std::string asset;
		audio_backend::Voice voice;
	};
	std::map<std::string, Loop> loops;
	audio_backend::Voice music, siren;
	struct Shot
	{
		audio_backend::Voice voice;
		float gain = 0;
		std::uint64_t order = 0;
	};
	Shot shots[16];
	std::uint64_t order = 0;
	struct Clicks
	{
		std::string output;
		std::vector<audio_backend::Voice> voices;
		unsigned next = 0;
	};
	std::map<std::string, Clicks> clicks;
	unsigned random = 1;
	void refresh()
	{
		if (GetTime() >= refresh_at)
		{
			refresh_at = GetTime() + .5;
			devices = outputs.enumerate();
		}
	}
	Bytes file(const std::string &name)
	{
		std::ifstream input(std::string(GetApplicationDirectory()) + "assets/audio/" + name, std::ios::binary);
		return Bytes(std::istreambuf_iterator<char>(input), {});
	}
	audio_backend::Voice load(const AudioPresentation &app, const std::string &asset)
	{
		if (!app.catalog || !app.catalog->assets.contains(asset))
			return {};
		auto voice = outputs.load(output, app.catalog->assets.at(asset));
		if (!voice.valid() && !output.empty())
			TraceLog(LOG_WARNING, "AUDIO: cannot play %s on selected output", asset.c_str());
		return voice;
	}
	void clear()
	{
		loops.clear();
		for (auto &shot : shots)
			shot = {};
		music = {};
		siren = {};
		track_asset.clear();
		siren_asset.clear();
		mixer.reset();
	}
	void update(const AudioPresentation &app, const SceneFrame &scene)
	{
		refresh();
		const auto resolved = outputs.resolve(app.output);
		if (output != resolved || (!output.empty() && music.loaded() && !music.valid()) || session != app.session ||
		    generation != app.generation)
		{
			clear();
			ship_audio.clear();
			output = resolved;
			session = app.session;
			generation = app.generation;
		}
		const float master = std::clamp(app.levels.master_volume, 0, 100) / 100.f;
		const float sfx = master * std::clamp(app.levels.sfx_volume, 0, 100) / 100.f;
		for (auto &[id, pool] : clicks)
			for (auto &voice : pool.voices)
				voice.gain(sfx);
		std::erase_if(clicks,
		              [&](const auto &entry)
		              {
			              return std::none_of(entry.second.voices.begin(), entry.second.voices.end(),
			                                  [](const auto &voice)
			                                  {
				                                  return voice.valid();
			                                  }) ||
			                     (!app.screen_outputs.empty() &&
			                      (!app.screen_outputs.contains(entry.first) ||
			                       outputs.resolve(app.screen_outputs.at(entry.first)) != entry.second.output));
		              });
		bool active = app.playing && app.host && !app.workshop && !app.recovering && app.catalog && app.focus_body &&
		              !output.empty();
		bool alive = false;
		AudioMixFrame mix;
		if (active)
		{
			const auto &own = *app.focus_body;
			for (const auto &body : scene.view())
				if (body.id == own.id)
					alive = body.audio_alive;
			if (!ship_audio.contains(own.definition))
				ship_audio[own.definition] = compile_ship_audio(*app.catalog, own.definition);
			mix = mixer.frame(scene.view(), own.id, ship_audio.at(own.definition), scene.visible, app.tick, true,
			                  GetFrameTime());
			if (!alive)
				mix.loops.clear();
		}
		else
			mixer.reset();
		std::erase_if(loops,
		              [&](const auto &entry)
		              {
			              return !mix.loops.contains(entry.first);
		              });
		for (const auto &[key, clip] : mix.loops)
		{
			auto &loop = loops[key];
			if (loop.asset != clip.asset)
			{
				loop.asset = clip.asset;
				loop.voice = load(app, clip.asset);
				loop.voice.start(true, clip.gain * sfx);
			}
			loop.voice.gain(clip.gain * sfx);
		}
		if (!active)
			for (auto &shot : shots)
				shot = {};
		for (auto &shot : shots)
			shot.voice.gain(shot.gain * sfx);
		for (const auto &clip : mix.shots)
		{
			if (!sfx)
				continue;
			auto *selected = &shots[0];
			for (auto &shot : shots)
			{
				if (!shot.voice.playing())
				{
					selected = &shot;
					break;
				}
				if (shot.order < selected->order)
					selected = &shot;
			}
			selected->voice = load(app, clip.asset);
			selected->gain = clip.gain;
			selected->order = ++order;
			selected->voice.start(false, clip.gain * sfx);
		}
		const auto alert = app.playing ? app.alert : Json::object();
		const bool live_alert = active && alive && !alert.empty();
		const bool entry = alert_transition.observe(app.session, app.generation, alert, live_alert);
		if (!live_alert)
			siren.stop();
		else
		{
			const auto asset = alert.value("siren", std::string{});
			if (siren_asset != asset)
			{
				siren_asset = asset;
				siren = load(app, asset);
			}
			const float gain = master * std::clamp(app.levels.siren_volume, 0, 100) / 100.f;
			siren.gain(gain);
			if (entry && gain)
				siren.start(false, gain);
		}
		const int wanted = !app.host || app.workshop || (app.playing && !(active && alive)) || (app.gm && !app.playing)
		                       ? 0
		                   : app.playing ? 2
		                                 : 1;
		if (!wanted || output.empty())
		{
			music = {};
			track_asset.clear();
		}
		else
		{
			const auto asset = wanted == 2 ? condition_music(alert) : std::string{};
			const auto key = asset.empty() ? (wanted == 1 ? "menu.ogg" : "ambient.ogg") : asset;
			const float gain = master * std::clamp(app.levels.music_volume, 0, 100) / 100.f *
			                   (asset.empty() ? 1.f : alert.value("music_gain", 1.f));
			if (track_asset != key)
			{
				track_asset = key;
				music = asset.empty() ? outputs.load(output, file(key)) : load(app, asset);
				if (!music.valid() && !asset.empty())
					music = outputs.load(output, file("ambient.ogg"));
				music.start(true, gain);
			}
			music.gain(gain);
		}
		auto retained = app.active_outputs;
		retained.push_back(app.output);
		outputs.retain(retained);
	}
	void click(const AudioLevels &levels, const std::string &requested, const std::string &screen)
	{
		refresh();
		const auto id = outputs.resolve(requested);
		if (id.empty())
			return;
		auto &pool = clicks[screen];
		if (pool.output != id)
		{
			pool = {};
			pool.output = id;
		}
		if (pool.voices.empty())
		{
			const auto bytes = file("ui_click.ogg");
			for (int i = 0; i < 8; ++i)
				pool.voices.push_back(outputs.load(id, bytes));
		}
		const auto gain = std::clamp(levels.master_volume, 0, 100) * std::clamp(levels.sfx_volume, 0, 100) / 10000.f;
		random = random * 1664525u + 1013904223u;
		if (gain)
			pool.voices[pool.next++ % pool.voices.size()].start(false, gain, .94f + .12f * (random / 4294967296.f));
	}
};
ServerMusic::ServerMusic() : state_(std::make_unique<State>())
{
}
ServerMusic::~ServerMusic() = default;
void ServerMusic::update(const AudioPresentation &app, const SceneFrame &scene)
{
	state_->update(app, scene);
}
void ServerMusic::click(const AudioLevels &levels, const std::string &output, const std::string &screen)
{
	state_->click(levels, output, screen);
}
std::vector<std::pair<std::string, std::string>> ServerMusic::outputs()
{
	state_->refresh();
	return state_->devices;
}
} // namespace phoenix
