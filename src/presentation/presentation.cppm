module;
#include <raylib.h>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <array>

export module phoenix.presentation;
export import phoenix.navigation;

export namespace phoenix
{
// Normalised top-down geometry shared by raylib radars and ImGui tool maps.
// Heading zero points right; positive headings turn towards screen up.
struct RadarGlyph
{
	std::array<std::array<Vector2, 2>, 32> lines{};
	std::size_t count = 0;
	auto begin() const
	{
		return lines.begin();
	}
	auto end() const
	{
		return lines.begin() + count;
	}
};
RadarGlyph radar_icon_strokes(RadarIcon icon, Vector2 centre, float radius, float heading);
void draw_radar_icon(RadarIcon icon, Vector2 centre, float radius, float heading, Color colour);
enum class SceneDiagnostic
{
	comic,
	smooth,
	unlit,
	normals,
	depth,
	shadows
};
struct ExplosionEffect
{
	Vector3 position{};
	std::array<float, 3> color{1.f, .23f, .035f};
	float intensity = 1.5f, range = 30.f, duration = 4.f;
	double started = 0;
};
struct SceneEnvironment
{
	std::string skybox;
	DustConfig dust;
	std::optional<ReferenceGridConfig> grid;
	std::optional<BodyView> own;
	bool dust_enabled = true, grid_enabled = true, reduced_motion = false;
};
// Shared runtime/Workshop renderer. Caller supplies only an authorised scene.
// The colour pass draws directly into the current framebuffer and region.
class SceneRenderer
{
public:
	SceneRenderer();
	~SceneRenderer();
	SceneRenderer(const SceneRenderer &) = delete;
	SceneRenderer &operator=(const SceneRenderer &) = delete;
	void clear();
	void preload(const Catalog &, const std::string &definition);
	void preload_environment(const Catalog &, const SceneEnvironment &);
	SpriteMaterial prepared_sprite(const Catalog &, const std::string &definition);
	void explosion(const ExplosionEffect &effect);
	Json instance_statistics() const;
	void draw(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
	          const Json &lighting, double time, SceneDiagnostic diagnostic = SceneDiagnostic::comic,
	          Vector2 surface_size = {}, const SceneEnvironment &environment = {});

private:
	struct State;
	std::unique_ptr<State> state_;
};
// Each instance owns a wrapped GPU sample history for one radar widget.
class RadarSweep
{
public:
	RadarSweep();
	~RadarSweep();
	RadarSweep(const RadarSweep &) = delete;
	RadarSweep &operator=(const RadarSweep &) = delete;
	void draw(Vector2 centre, float radius, const SystemView &sensor, const Json &settings, double time,
	          bool reduced_motion = false);
	Json statistics() const;

private:
	struct State;
	std::unique_ptr<State> state_;
};
// Each content owner keeps a separate cache and clears it before replacing assets.
Image load_asset_image(std::span<const std::uint8_t> bytes);
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
void draw_radar_body(TextureCache &textures, const Catalog &catalog, const BodyView &body, Vector2 centre, float radius,
                     Color colour);
bool probe_menu_keyboard(Application &app);
bool probe_console_on_screen(ConsoleId route, const std::string &widget);
bool probe_console_helm(Application &app, ConsoleId route, bool verify = false);
bool probe_console_manual(ConsoleId route, unsigned stage);
bool probe_console_report(ConsoleId route);
bool probe_console_readouts(ConsoleId route);
bool probe_console_engineering(ConsoleId route, unsigned stage);
void open_window();
bool close_requested();
bool graphics_has_keyboard_focus();
void draw_screen(Application &app);
void close_window();
// Authoring-only sample model. This preview never acquires gameplay authority.
Texture2D draw_console_preview(const Json &layout, int width, int height, Vector2 pointer, bool pressed, bool released,
                               float wheel, bool pseudo);
} // namespace phoenix

export namespace phoenix
{
void publish_semantics(const Application &app);
void drain_semantic_actions(Application &app);
void install_native_accessibility(void *window);
void remove_native_accessibility(void *window);
bool probe_native_accessibility(Application &app);
} // namespace phoenix
