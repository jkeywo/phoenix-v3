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
module phoenix.graphics.assets;
import phoenix.content.image_codec;

namespace phoenix
{
Image load_asset_image(std::span<const std::uint8_t> bytes)
{
	if (!compiled_image(bytes))
		return LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size()));
	const auto decoded = decode_asset_image(bytes);
	Image image{MemAlloc(static_cast<unsigned int>(decoded.rgba.size())), decoded.width, decoded.height, 1,
	            PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	if (!image.data)
		throw std::runtime_error("Image allocation failed");
	std::copy(decoded.rgba.begin(), decoded.rgba.end(), static_cast<std::uint8_t *>(image.data));
	return image;
}
namespace
{
std::map<std::string, std::weak_ptr<Texture2D>> shared_textures;
std::map<std::string, Bytes> bundled_images;
} // namespace
TextureCache::~TextureCache()
{
	clear();
}
void TextureCache::clear()
{
	textures_.clear();
}
Texture2D TextureCache::load(const std::string &id, std::span<const std::uint8_t> bytes)
{
	if (const auto found = textures_.find(id); found != textures_.end())
		return *found->second;
	// Static scene owners can clear after the shared index has been destroyed.
	// Prune only while loading; releasing a cache needs no access to that index.
	std::erase_if(shared_textures,
	              [](const auto &entry)
	              {
		              return entry.second.expired();
	              });
	const auto hash = sha256(bytes);
	auto texture = shared_textures[hash].lock();
	if (!texture)
	{
		const auto image = load_asset_image(bytes);
		if (!image.data)
			throw std::runtime_error(id + ": image cannot decode");
		int maximum = 0;
#ifdef __EMSCRIPTEN__
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
#else
		using GetInteger = void (*)(unsigned, int *);
		if (const auto get = reinterpret_cast<GetInteger>(rlGetProcAddress("glGetIntegerv")))
			get(0x0D33, &maximum);
#endif
		if (maximum > 0 && (image.width > maximum || image.height > maximum))
		{
			UnloadImage(image);
			throw std::runtime_error(id + ": image exceeds this graphics device's texture limit");
		}
		const auto uploaded = LoadTextureFromImage(image);
		UnloadImage(image);
		if (!uploaded.id)
			throw std::runtime_error(id + ": texture upload failed");
		texture = std::shared_ptr<Texture2D>(new Texture2D(uploaded),
		                                     [](Texture2D *value)
		                                     {
			                                     UnloadTexture(*value);
			                                     delete value;
		                                     });
		shared_textures[hash] = texture;
	}
	textures_[id] = texture;
	return *texture;
}
Texture2D TextureCache::load_file(const std::string &path)
{
	if (!bundled_images.contains(path))
		bundled_images[path] = read_file(path);
	return load(path, bundled_images.at(path));
}
} // namespace phoenix
