module;
#include <raylib.h>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

export module phoenix.graphics.assets;
export import phoenix.data;

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
	Texture2D load(const std::string &id, std::span<const std::uint8_t> png);
	Texture2D load_file(const std::string &path);
	void clear();

private:
	std::map<std::string, std::shared_ptr<Texture2D>> textures_;
};
} // namespace phoenix
