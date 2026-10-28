#pragma once
namespace phoenix
{
class SpriteScene
{
public:
	SpriteScene();
	~SpriteScene();
	SpriteScene(const SpriteScene &) = delete;
	SpriteScene &operator=(const SpriteScene &) = delete;
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
class ModelScene
{
public:
	ModelScene();
	~ModelScene();
	void clear();
	void preload(const Catalog &, const std::string &);
	std::optional<AttachmentPose> attachment(const Catalog &, const SceneBody &, const std::string &, Camera3D);
	void draw(const Catalog &, std::span<const SceneBody>, Camera3D, Rectangle, const Json &, double, Vector2,
	          const SceneEnvironment &, bool background = true, std::optional<unsigned> framebuffer = {});

	std::set<EntityId> prepare(const Catalog &, std::span<const SceneBody>, Camera3D, Rectangle, EntityId hidden = 0,
	                           double simulation_time = 0, bool reduced_motion = false);
	void draw_prepared(const Catalog &, std::span<const SceneBody>, Camera3D, Rectangle, const Json &, double, Vector2,
	                   const SceneEnvironment &, bool background = true, std::optional<unsigned> framebuffer = {});

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
