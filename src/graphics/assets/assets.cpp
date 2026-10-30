module;
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#endif
#include <rlgl.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <chrono>
#include <future>
#include <thread>
#include <functional>
#include <vector>
module phoenix.graphics.assets;
import phoenix.content.image_codec;

namespace phoenix
{
Image load_asset_image(std::span<const std::uint8_t> bytes)
{
	const auto decoded = shared_asset_image(bytes);
	Image image{MemAlloc(static_cast<unsigned int>(decoded->rgba.size())), decoded->width, decoded->height, 1,
	            PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	if (!image.data)
		throw std::runtime_error("Image allocation failed");
	std::copy(decoded->rgba.begin(), decoded->rgba.end(), static_cast<std::uint8_t *>(image.data));
	return image;
}
namespace
{
std::map<std::string, std::weak_ptr<Texture2D>> shared_textures;
std::map<std::string, std::weak_ptr<const AssetImage>> shared_pixels;
std::map<std::string, Bytes> bundled_images;
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
struct DecodeWorkers
{
	struct Worker
	{
		std::thread thread;
		std::future<void> exited;
	};
	std::vector<Worker> workers;
	void start(std::function<void()> task)
	{
		std::erase_if(workers,
		              [](auto &worker)
		              {
			              if (worker.exited.valid() &&
			                  worker.exited.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				              return false;
			              if (worker.thread.joinable())
				              worker.thread.join();
			              return true;
		              });
		// Allocate the registry entry before starting a thread. Cancellation only
		// discards a result; thread ownership remains here until safe to reap.
		auto &worker = workers.emplace_back();
		std::promise<void> exited;
		worker.exited = exited.get_future();
		worker.thread = std::thread(
		    [task = std::move(task), exited = std::move(exited)]() mutable
		    {
			    task();
			    exited.set_value_at_thread_exit();
		    });
	}
	~DecodeWorkers()
	{
		// Only process shutdown waits for unfinished work, never a display cancel.
		for (auto &worker : workers)
			if (worker.thread.joinable())
				worker.thread.join();
	}
};
#endif
} // namespace
TextureCache::~TextureCache()
{
	clear();
}
void TextureCache::clear()
{
	textures_.clear();
	pixels_.clear();
	// The worker owns its input and result. Discarding a superseded request never
	// waits for it and cannot publish its result into a newer content owner.
	decoding_.reset();
	decoding_key_.clear();
}
struct TextureCache::Decode
{
	std::string hash;
	std::shared_future<std::shared_ptr<const AssetImage>> result;
};
namespace
{
std::shared_ptr<const AssetImage> decode_texture(std::span<const std::uint8_t> bytes, bool premultiplied)
{
	const auto decoded = shared_asset_image(bytes);
	if (premultiplied)
	{
		auto image = std::make_shared<AssetImage>(*decoded);
		for (std::size_t i = 0; i < image->rgba.size(); i += 4)
			for (std::size_t channel = 0; channel < 3; ++channel)
				image->rgba[i + channel] =
				    static_cast<std::uint8_t>(image->rgba[i + channel] * image->rgba[i + 3] / 255);
		return image;
	}
	return decoded;
}
std::shared_ptr<Texture2D> upload_image(const AssetImage &decoded, const std::string &id, bool mipmapped)
{
	Image image{const_cast<std::uint8_t *>(decoded.rgba.data()), decoded.width, decoded.height, 1,
	            PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	int maximum = 0;
#ifdef __EMSCRIPTEN__
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
#else
	using GetInteger = void (*)(unsigned, int *);
	if (const auto get = reinterpret_cast<GetInteger>(rlGetProcAddress("glGetIntegerv")))
		get(0x0D33, &maximum);
#endif
	if (maximum > 0 && (image.width > maximum || image.height > maximum))
		throw std::runtime_error(id + ": image exceeds this graphics device's texture limit");
	auto uploaded = LoadTextureFromImage(image);
	if (!uploaded.id)
		throw std::runtime_error(id + ": texture upload failed");
	if (mipmapped)
	{
		GenTextureMipmaps(&uploaded);
		SetTextureFilter(uploaded, TEXTURE_FILTER_TRILINEAR);
		SetTextureWrap(uploaded, TEXTURE_WRAP_CLAMP);
	}
	return std::shared_ptr<Texture2D>(new Texture2D(uploaded),
	                                  [](Texture2D *value)
	                                  {
		                                  UnloadTexture(*value);
		                                  delete value;
	                                  });
}
} // namespace
bool TextureCache::prepare(const std::string &id, std::span<const std::uint8_t> bytes, bool premultiplied)
{
	const auto suffix = std::string(premultiplied ? ":base:ui" : ":base");
	const auto key = id + suffix;
	if (textures_.contains(key))
		return true;
	if (!decoding_ || decoding_key_ != key)
	{
		const auto hash = sha256(bytes) + suffix;
		auto retained_pixels = shared_pixels[hash].lock();
		if (auto texture = shared_textures[hash].lock(); texture && (premultiplied || retained_pixels))
		{
			textures_[key] = std::move(texture);
			if (retained_pixels)
				pixels_[key] = std::move(retained_pixels);
			return true;
		}
		// Consoles and the scene frequently request the same encoded payload in
		// one frame. Share CPU work as well as the eventual graphics resource.
		static std::map<std::string, std::weak_ptr<Decode>> shared_decodes;
		std::erase_if(shared_decodes,
		              [](const auto &entry)
		              {
			              return entry.second.expired();
		              });
		decoding_key_ = key;
		decoding_ = shared_decodes[hash].lock();
		if (decoding_)
			return false;
		auto task = std::make_shared<std::packaged_task<std::shared_ptr<const AssetImage>()>>(
		    [encoded = Bytes(bytes.begin(), bytes.end()), premultiplied]
		    {
			    return decode_texture(encoded, premultiplied);
		    });
		decoding_ = std::make_shared<Decode>(Decode{hash, task->get_future().share()});
		shared_decodes[hash] = decoding_;
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
		static DecodeWorkers workers;
		workers.start(
		    [task, pending = decoding_]
		    {
			    (*task)();
		    });
#else
		// The ordinary WASM build has no threads. Yield before each bounded decode;
		// the next call uploads it, with normal input/rendering between resources.
		(*task)();
#endif
		return false;
	}
	if (decoding_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return false;
	auto pending = std::move(decoding_);
	const auto &decoded = pending->result.get();
	pixels_[key] = decoded;
	shared_pixels[pending->hash] = decoded;
	auto texture = shared_textures[pending->hash].lock();
	if (!texture)
	{
		texture = upload_image(*decoded, id, false);
		shared_textures[pending->hash] = texture;
	}
	textures_[key] = std::move(texture);
	return true;
}
bool TextureCache::prepare_file(const std::string &path, bool premultiplied)
{
	if (!bundled_images.contains(path))
		bundled_images[path] = read_file(path);
	return prepare(path, bundled_images.at(path), premultiplied);
}
Texture2D TextureCache::load(const std::string &id, std::span<const std::uint8_t> bytes, bool mipmapped,
                             bool premultiplied)
{
	const auto suffix = std::string(mipmapped ? ":mipmapped" : ":base") + (premultiplied ? ":ui" : "");
	const auto key = id + suffix;
	if (const auto found = textures_.find(key); found != textures_.end())
		return *found->second;
	std::erase_if(shared_textures,
	              [](const auto &entry)
	              {
		              return entry.second.expired();
	              });
	const auto hash = sha256(bytes) + suffix;
	auto texture = shared_textures[hash].lock();
	if (!texture)
	{
		const auto decoded = decode_texture(bytes, premultiplied);
		pixels_[key] = decoded;
		shared_pixels[hash] = decoded;
		texture = upload_image(*decoded, id, mipmapped);
		shared_textures[hash] = texture;
	}
	textures_[key] = texture;
	return *texture;
}
Texture2D TextureCache::load_file(const std::string &path, bool premultiplied)
{
	if (!bundled_images.contains(path))
		bundled_images[path] = read_file(path);
	return load(path, bundled_images.at(path), false, premultiplied);
}
} // namespace phoenix
