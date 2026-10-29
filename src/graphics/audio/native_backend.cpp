module;
// Private miniaudio implementation. Vorbis uses raylib's pinned decoder exports;
// miniaudio symbols have internal linkage and cannot collide with raudio.c.
#define STB_VORBIS_HEADER_ONLY
#include <external/stb_vorbis.c>
#define MA_API static
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <external/miniaudio.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.graphics.audio_backend;

namespace phoenix::audio_backend
{
namespace
{
bool succeeded(ma_result result, const char *stage)
{
	if (result == MA_SUCCESS)
		return true;
	std::fprintf(stderr, "Native audio %s failed: %s\n", stage, ma_result_description(result));
	return false;
}
struct Context
{
	ma_context value{};
	bool ready = false;
	explicit Context(bool synthetic)
	{
		const ma_backend backend = ma_backend_null;
		ready = ma_context_init(synthetic ? &backend : nullptr, synthetic ? 1 : 0, nullptr, &value) == MA_SUCCESS;
	}
	~Context()
	{
		if (ready)
			ma_context_uninit(&value);
	}
};
struct Output
{
	std::shared_ptr<Context> context;
	ma_engine engine{};
	ma_device device{};
	bool engine_ready = false, device_ready = false;
	std::atomic<bool> available{true};
	~Output()
	{
		if (device_ready)
			ma_device_uninit(&device);
		if (engine_ready)
			ma_engine_uninit(&engine);
	}
	bool open(std::shared_ptr<Context> ctx, const ma_device_id &id)
	{
		context = std::move(ctx);
		auto config = ma_engine_config_init();
		config.noDevice = MA_TRUE;
		config.channels = 2;
		config.sampleRate = 48000;
		engine_ready = succeeded(ma_engine_init(&config, &engine), "engine initialisation");
		if (!engine_ready)
			return false;
		auto device_config = ma_device_config_init(ma_device_type_playback);
		device_config.playback.pDeviceID = &id;
		device_config.playback.format = ma_format_f32;
		device_config.playback.channels = 2;
		device_config.sampleRate = 48000;
		device_config.wasapi.noAutoStreamRouting = MA_TRUE;
		device_config.pUserData = this;
		device_config.dataCallback = [](ma_device *device, void *out, const void *, ma_uint32 count)
		{
			auto *self = static_cast<Output *>(device->pUserData);
			ma_engine_read_pcm_frames(&self->engine, out, count, nullptr);
		};
		device_config.notificationCallback = [](const ma_device_notification *notification)
		{
			if (notification->type == ma_device_notification_type_stopped)
				static_cast<Output *>(notification->pDevice->pUserData)->available = false;
		};
		device_ready = succeeded(ma_device_init(&context->value, &device_config, &device), "device initialisation");
		return device_ready && succeeded(ma_device_start(&device), "device start");
	}
};
std::string identity(ma_backend backend, const ma_device_id &id)
{
	// Encode only the active, stable identifier; never persist union padding or indices.
	std::string value;
	if (backend == ma_backend_wasapi)
	{
		for (const auto c : id.wasapi)
		{
			if (!c)
				break;
			value.push_back(static_cast<char>(c & 255));
			value.push_back(static_cast<char>((c >> 8) & 255));
		}
	}
	else if (backend == ma_backend_alsa)
		value = id.alsa;
	else if (backend == ma_backend_pulseaudio)
		value = id.pulse;
	else if (backend == ma_backend_coreaudio)
		value = id.coreaudio;
	else
		return {}; // Unsupported identities are deliberately not guessed from a name.
	std::string result = "endpoint:" + std::to_string(backend) + ":";
	constexpr char digits[] = "0123456789abcdef";
	for (unsigned char c : value)
	{
		result += digits[c >> 4];
		result += digits[c & 15];
	}
	return result;
}
} // namespace
struct Voice::Data
{
	std::shared_ptr<Output> output;
	std::vector<std::uint8_t> bytes;
	ma_decoder decoder{};
	ma_sound sound{};
	bool decoder_ready = false, sound_ready = false;
	~Data()
	{
		if (sound_ready)
			ma_sound_uninit(&sound);
		if (decoder_ready)
			ma_decoder_uninit(&decoder);
	}
};
void Voice::start(bool loop, float gain, float pitch)
{
	if (!valid())
		return;
	ma_sound_stop(&data->sound);
	ma_sound_seek_to_pcm_frame(&data->sound, 0);
	ma_sound_set_looping(&data->sound, loop);
	ma_sound_set_volume(&data->sound, gain);
	ma_sound_set_pitch(&data->sound, pitch);
	ma_sound_start(&data->sound);
}
void Voice::stop()
{
	if (data && data->sound_ready)
		ma_sound_stop(&data->sound);
}
void Voice::gain(float value)
{
	if (valid())
		ma_sound_set_volume(&data->sound, value);
}
bool Voice::playing() const
{
	return valid() && ma_sound_is_playing(&data->sound);
}
bool Voice::loaded() const
{
	return static_cast<bool>(data);
}
bool Voice::valid() const
{
	return data && data->sound_ready && data->output->available;
}
bool Voice::shares_output(const Voice &other) const
{
	return data && other.data && data->output == other.data->output;
}
struct Outputs::State
{
	std::shared_ptr<Context> context;
	std::vector<std::pair<std::string, std::string>> synthetic;
	std::map<std::string, ma_device_id> devices;
	std::map<std::string, std::shared_ptr<Output>> engines;
	std::string default_output;
};
Outputs::Outputs(std::vector<std::pair<std::string, std::string>> synthetic) : state(std::make_unique<State>())
{
	state->context = std::make_shared<Context>(!synthetic.empty());
	state->synthetic = std::move(synthetic);
}
Outputs::~Outputs() = default;
std::vector<std::pair<std::string, std::string>> Outputs::enumerate()
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!state->context->ready)
		return result;
	if (!state->synthetic.empty())
	{
		int index = 0;
		for (const auto &[id, name] : state->synthetic)
		{
			ma_device_id device{};
			device.nullbackend = index++;
			state->devices[id] = device;
		}
		state->default_output = state->synthetic.front().first;
		return state->synthetic;
	}
	ma_device_info *devices = nullptr;
	ma_uint32 count = 0;
	if (ma_context_get_devices(&state->context->value, &devices, &count, nullptr, nullptr) != MA_SUCCESS)
		return result;
	state->devices.clear();
	state->default_output.clear();
	for (ma_uint32 i = 0; i < count; ++i)
	{
		const auto id = identity(state->context->value.backend, devices[i].id);
		if (id.empty())
			continue;
		state->devices[id] = devices[i].id;
		result.emplace_back(id, devices[i].name);
		if (devices[i].isDefault)
			state->default_output = id;
	}
	std::erase_if(state->engines,
	              [&](const auto &entry)
	              {
		              if (!entry.second)
			              return true;
		              if (state->devices.contains(entry.first) && entry.second->available)
			              return false;
		              entry.second->available = false;
		              ma_engine_set_volume(&entry.second->engine, 0);
		              return true;
	              });
	return result;
}
std::string Outputs::resolve(const std::string &output) const
{
	const auto id = output == "default" ? state->default_output : output;
	return state->devices.contains(id) ? id : std::string{};
}
Voice Outputs::load(const std::string &output, const std::vector<std::uint8_t> &bytes)
{
	Voice voice;
	const auto id = resolve(output);
	if (id.empty() || bytes.empty())
		return voice;
	auto &engine = state->engines[id];
	if (!engine)
	{
		engine = std::make_shared<Output>();
		if (!engine->open(state->context, state->devices.at(id)))
		{
			engine.reset();
			return voice;
		}
	}
	auto data = std::make_shared<Voice::Data>();
	data->output = engine;
	data->bytes = bytes;
	auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
	data->decoder_ready =
	    succeeded(ma_decoder_init_memory(data->bytes.data(), data->bytes.size(), &config, &data->decoder),
	              "decoder initialisation");
	if (!data->decoder_ready)
		return voice;
	data->sound_ready =
	    succeeded(ma_sound_init_from_data_source(&engine->engine, &data->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION,
	                                             nullptr, &data->sound),
	              "voice initialisation");
	if (data->sound_ready)
		voice.data = std::move(data);
	return voice;
}
void Outputs::retain(const std::vector<std::string> &outputs)
{
	std::set<std::string> active;
	for (const auto &output : outputs)
		active.insert(resolve(output));
	std::erase_if(state->engines,
	              [&](const auto &entry)
	              {
		              return !active.contains(entry.first) && (!entry.second || entry.second.use_count() == 1);
	              });
}
} // namespace phoenix::audio_backend
