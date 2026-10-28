module;
#include <raylib.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

export module phoenix.graphics.scene;
export import phoenix.content.assets;
export import phoenix.content.catalog;

export namespace phoenix
{
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
struct SceneFrame
{
	std::optional<BodyView> focus_body;
	std::vector<SceneBody> bodies;
	std::shared_ptr<const std::vector<SceneBody>> shared_bodies;
	std::span<const SceneBody> view() const
	{
		return shared_bodies ? std::span<const SceneBody>(*shared_bodies) : std::span<const SceneBody>(bodies);
	}
	std::set<EntityId> visible;
};
struct SceneEnvironment
{
	std::string skybox;
	DustConfig dust;
	std::optional<ReferenceGridConfig> grid;
	std::optional<BodyView> own;
	bool dust_enabled = true, grid_enabled = true, reduced_motion = false;
};
struct AttachmentPose
{
	Vector3 position{}, direction{};
};

// Runtime and Workshop supply an authorised scene. Sprite/model selection and GPU
// resources remain implementation details of this renderer.
class SceneRenderer
{
public:
	SceneRenderer();
	~SceneRenderer();
	SceneRenderer(const SceneRenderer &) = delete;
	SceneRenderer &operator=(const SceneRenderer &) = delete;
	void clear();
	void preload(const Catalog &, const std::string &definition);
	std::optional<AttachmentPose> attachment(const Catalog &, const SceneBody &, const std::string &, Camera3D);
	void preload_environment(const Catalog &, const SceneEnvironment &);
	SpriteMaterial prepared_sprite(const Catalog &, const std::string &definition);
	void explosion(const ExplosionEffect &effect);
	Json instance_statistics() const;
	void draw(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
	          const Json &lighting, double time, SceneDiagnostic diagnostic = SceneDiagnostic::comic,
	          Vector2 surface_size = {}, const SceneEnvironment &environment = {});
	// Preparation and drawing use the same camera/region until prepare or clear.
	std::set<EntityId> prepare(const Catalog &, std::span<const SceneBody>, Camera3D, Rectangle, EntityId hidden = 0,
	                           double simulation_time = 0, bool reduced_motion = false);
	void draw_prepared(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
	                   const Json &lighting, double time, SceneDiagnostic diagnostic = SceneDiagnostic::comic,
	                   Vector2 surface_size = {}, const SceneEnvironment &environment = {},
	                   std::optional<unsigned> framebuffer = {});

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
