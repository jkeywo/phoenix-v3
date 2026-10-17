module;
#include <raylib.h>
#include <cstdint>
#include <map>
#include <span>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.presentation;
export import phoenix.navigation;

export namespace phoenix
{
// Each content owner keeps a separate cache and clears it before replacing assets.
class TextureCache
{
public:
	TextureCache() = default;
	TextureCache(const TextureCache &) = delete;
	TextureCache &operator=(const TextureCache &) = delete;
	~TextureCache();
	Texture2D load(const std::string &id, std::span<const std::uint8_t> png);
	void clear();

private:
	std::map<std::string, Texture2D> textures_;
};
void open_window();
bool close_requested();
bool graphics_has_keyboard_focus();
void draw_screen(Application &app);
void close_window();
} // namespace phoenix

export namespace phoenix
{
void publish_semantics(const Application &app);
void drain_semantic_actions(Application &app);
void install_native_accessibility(void *window);
void remove_native_accessibility(void *window);
bool probe_native_accessibility(Application &app);
} // namespace phoenix
