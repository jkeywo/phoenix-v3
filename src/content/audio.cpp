module;
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
// Private decoder symbols must not collide with raylib raudio.
#define stb_vorbis_close phoenix_verify_stb_vorbis_close
#define stb_vorbis_decode_filename phoenix_verify_stb_vorbis_decode_filename
#define stb_vorbis_decode_frame_pushdata phoenix_verify_stb_vorbis_decode_frame_pushdata
#define stb_vorbis_decode_memory phoenix_verify_stb_vorbis_decode_memory
#define stb_vorbis_flush_pushdata phoenix_verify_stb_vorbis_flush_pushdata
#define stb_vorbis_get_comment phoenix_verify_stb_vorbis_get_comment
#define stb_vorbis_get_error phoenix_verify_stb_vorbis_get_error
#define stb_vorbis_get_file_offset phoenix_verify_stb_vorbis_get_file_offset
#define stb_vorbis_get_frame_float phoenix_verify_stb_vorbis_get_frame_float
#define stb_vorbis_get_frame_short phoenix_verify_stb_vorbis_get_frame_short
#define stb_vorbis_get_frame_short_interleaved phoenix_verify_stb_vorbis_get_frame_short_interleaved
#define stb_vorbis_get_info phoenix_verify_stb_vorbis_get_info
#define stb_vorbis_get_sample_offset phoenix_verify_stb_vorbis_get_sample_offset
#define stb_vorbis_get_samples_float phoenix_verify_stb_vorbis_get_samples_float
#define stb_vorbis_get_samples_float_interleaved phoenix_verify_stb_vorbis_get_samples_float_interleaved
#define stb_vorbis_get_samples_short phoenix_verify_stb_vorbis_get_samples_short
#define stb_vorbis_get_samples_short_interleaved phoenix_verify_stb_vorbis_get_samples_short_interleaved
#define stb_vorbis_open_file phoenix_verify_stb_vorbis_open_file
#define stb_vorbis_open_file_section phoenix_verify_stb_vorbis_open_file_section
#define stb_vorbis_open_filename phoenix_verify_stb_vorbis_open_filename
#define stb_vorbis_open_memory phoenix_verify_stb_vorbis_open_memory
#define stb_vorbis_open_pushdata phoenix_verify_stb_vorbis_open_pushdata
#define stb_vorbis_seek phoenix_verify_stb_vorbis_seek
#define stb_vorbis_seek_frame phoenix_verify_stb_vorbis_seek_frame
#define stb_vorbis_seek_start phoenix_verify_stb_vorbis_seek_start
#define stb_vorbis_stream_length_in_samples phoenix_verify_stb_vorbis_stream_length_in_samples
#define stb_vorbis_stream_length_in_seconds phoenix_verify_stb_vorbis_stream_length_in_seconds
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>
#define DRWAV_API static
#define DRWAV_PRIVATE static
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
module phoenix.content;
namespace phoenix
{
void verify_audio_asset(const Catalog &catalog, const std::string &asset, const std::string &path)
{
	if (!catalog.assets.contains(asset))
		throw std::runtime_error(path + ": unresolved audio " + asset);
	if (!asset.ends_with(".ogg") && !asset.ends_with(".wav"))
		throw std::runtime_error(path + ": unsupported audio format " + asset);
	const auto &bytes = catalog.assets.at(asset);
	const auto hash = asset.substr(asset.find_last_of('.')) + sha256(bytes);
	static std::mutex mutex;
	static std::set<std::string> verified;
	const std::lock_guard lock(mutex);
	if (verified.contains(hash))
		return;
	bool valid = false;
	if (asset.ends_with(".ogg"))
	{
		int error = 0;
		auto *decoder = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
		if (decoder)
		{
			const auto info = stb_vorbis_get_info(decoder);
			const auto length = stb_vorbis_stream_length_in_samples(decoder);
			valid = info.channels > 0 && info.channels <= 2 && info.sample_rate >= 8000 && info.sample_rate <= 192000 &&
			        length > 0 && length <= info.sample_rate * 1200ULL;
			float block[4096];
			unsigned long long decoded = 0;
			if (valid)
				while (const int count = stb_vorbis_get_samples_float_interleaved(decoder, info.channels, block, 4096))
				{
					decoded += count;
					if (decoded > length)
					{
						valid = false;
						break;
					}
				}
			valid = valid && decoded == length && stb_vorbis_get_error(decoder) == VORBIS__no_error;
			stb_vorbis_close(decoder);
		}
	}
	else if (asset.ends_with(".wav"))
	{
		drwav decoder;
		if (drwav_init_memory(&decoder, bytes.data(), bytes.size(), nullptr))
		{
			valid = decoder.channels > 0 && decoder.channels <= 2 && decoder.sampleRate >= 8000 &&
			        decoder.sampleRate <= 192000 && decoder.totalPCMFrameCount > 0 &&
			        decoder.totalPCMFrameCount <= decoder.sampleRate * 1200ULL;
			float block[4096];
			drwav_uint64 decoded = 0;
			if (valid)
				while (const auto count = drwav_read_pcm_frames_f32(&decoder, 4096 / decoder.channels, block))
					decoded += count;
			valid = valid && decoded == decoder.totalPCMFrameCount;
			drwav_uninit(&decoder);
		}
	}
	if (!valid)
		throw std::runtime_error(path + ": invalid/unsupported OGG or WAV recording " + asset);
	if (verified.size() >= 1024)
		verified.clear();
	verified.insert(hash);
}
AudioClip compile_audio_clip(const Catalog &catalog, const Json &value, const std::string &path, bool loop)
{
	if (!value.is_object())
		throw std::runtime_error(path + ": expected audio object");
	for (const auto &[key, unused] : value.items())
		if (key != "asset" && key != "gain")
			throw std::runtime_error(path + ": unsupported audio field " + key);
	AudioClip result{value.value("asset", std::string{}), value.value("gain", 1.f)};
	if (!std::isfinite(result.gain) || result.gain < 0 || result.gain > 1 ||
	    !result.asset.ends_with(loop ? ".ogg" : ".wav"))
		throw std::runtime_error(path + ": expected bounded gain and " + (loop ? "OGG loop" : "WAV one-shot"));
	verify_audio_asset(catalog, result.asset, path);
	return result;
}
ShipAudioDefinition compile_ship_audio(const Catalog &catalog, const std::string &id)
{
	ShipAudioDefinition result;
	const auto &entity = catalog.entities.at(id);
	if (!entity.contains("audio"))
		return result;
	const auto &audio = entity.at("audio");
	if (!audio.is_object())
		throw std::runtime_error(id + ".audio: expected object");
	for (const auto &[key, clip] : audio.items())
	{
		if (key != "ambient" && key != "engine")
			throw std::runtime_error(id + ".audio: unsupported field " + key);
		(key == "ambient" ? result.ambient : result.engine) = compile_audio_clip(catalog, clip, id + ".audio." + key);
	}
	return result;
}
} // namespace phoenix
