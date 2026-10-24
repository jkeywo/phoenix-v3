module;
#include <memory>
#include <set>
#include <stdexcept>
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
module phoenix.presentation;
namespace phoenix
{
struct SceneRenderer::State
{
	SpriteScene sprites;
	ModelScene models;
	std::string mode;
	bool select(const Catalog &catalog)
	{
		const auto next = catalog.base.render_mode;
		if (!mode.empty() && mode != next)
			throw std::runtime_error("Render mode is fixed by the launch base");
		mode = next;
		return mode == "models";
	}
};
SceneRenderer::SceneRenderer() : state_(std::make_unique<State>())
{
}
SceneRenderer::~SceneRenderer() = default;
void SceneRenderer::clear()
{
	state_->sprites.clear();
	state_->models.clear();
}
void SceneRenderer::preload(const Catalog &c, const std::string &id)
{
	if (state_->select(c))
		state_->models.preload(c, id);
	else
		state_->sprites.preload(c, id);
}
void SceneRenderer::preload_environment(const Catalog &c, const SceneEnvironment &e)
{
	if (!state_->select(c))
		state_->sprites.preload_environment(c, e);
}
SpriteMaterial SceneRenderer::prepared_sprite(const Catalog &c, const std::string &id)
{
	if (state_->select(c))
		throw std::runtime_error("Model base has no sprite material");
	return state_->sprites.prepared_sprite(c, id);
}
std::optional<AttachmentPose> SceneRenderer::attachment(const Catalog &c, const SceneBody &body,
                                                        const std::string &name, Camera3D camera)
{
	return state_->select(c) ? state_->models.attachment(c, body, name, camera)
	                         : state_->sprites.attachment(c, body, name, camera);
}
void SceneRenderer::explosion(const ExplosionEffect &e)
{
	state_->sprites.explosion(e);
}
Json SceneRenderer::instance_statistics() const
{
	return state_->sprites.instance_statistics();
}
std::set<EntityId> SceneRenderer::prepare(const Catalog &c, std::span<const SceneBody> bodies, Camera3D camera,
                                          Rectangle region, EntityId hidden, double simulation_time,
                                          bool reduced_motion)
{
	return state_->select(c)
	           ? state_->models.prepare(c, bodies, camera, region, hidden, simulation_time, reduced_motion)
	           : state_->sprites.prepare(c, bodies, camera, region, hidden, simulation_time, reduced_motion);
}
void SceneRenderer::draw_prepared(const Catalog &c, std::span<const SceneBody> bodies, Camera3D camera,
                                  Rectangle region, const Json &lighting, double time, SceneDiagnostic diagnostic,
                                  Vector2 size, const SceneEnvironment &environment)
{
	if (state_->select(c))
		state_->models.draw_prepared(c, bodies, camera, region, lighting, time, size, environment);
	else
		state_->sprites.draw_prepared(c, bodies, camera, region, lighting, time, diagnostic, size, environment);
}
void SceneRenderer::draw(const Catalog &c, std::span<const SceneBody> b, Camera3D cam, Rectangle r, const Json &l,
                         double t, SceneDiagnostic d, Vector2 size, const SceneEnvironment &e)
{
	if (state_->select(c))
		state_->models.draw(c, b, cam, r, l, t, size, e);
	else
		state_->sprites.draw(c, b, cam, r, l, t, d, size, e);
}
} // namespace phoenix
