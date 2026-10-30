module;
#include <raylib.h>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

export module phoenix.graphics.assets;
export import phoenix.data;
import phoenix.content.image_codec;

export namespace phoenix
{
Image load_asset_image(std::span<const std::uint8_t> bytes);

// One owner per content lifetime. Handles are shared while their decoded content
// remains identical; the last owner releases the GPU resource on the render thread.
class TextureCache
{
public:
	TextureCache() = default;
	TextureCache(const TextureCache &) = delete;
	TextureCache &operator=(const TextureCache &) = delete;
	~TextureCache();
	Texture2D load(const std::string &id, std::span<const std::uint8_t> png, bool mipmapped = false,
	               bool premultiplied = false);
	Texture2D load_file(const std::string &path, bool premultiplied = false);
	// Poll preparation without waiting. CPU decoding runs off the graphics thread
	// on native/threaded WASM; uploads always run on the caller's graphics thread.
	bool prepare(const std::string &id, std::span<const std::uint8_t> bytes, bool premultiplied = false);
	bool prepare_file(const std::string &path, bool premultiplied = false);
	void clear();

private:
	std::map<std::string, std::shared_ptr<Texture2D>> textures_;
	std::map<std::string, std::shared_ptr<const AssetImage>> pixels_;
	struct Decode;
	std::shared_ptr<Decode> decoding_;
	std::string decoding_key_;
};
} // namespace phoenix
