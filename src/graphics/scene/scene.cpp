module;
#include <optional>
#include <map>
#include <cstdint>
#include <vector>
#include <memory>
#include <set>
#include <stdexcept>
#include <raylib.h>
#include <rlgl.h>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
module phoenix.graphics.scene;
import phoenix.content.assets;
import phoenix.content.catalog;

import phoenix.graphics.assets;
#include "backends.hpp"
#include "shield_scene.hpp"
namespace phoenix
{
struct SceneRenderer::State
{
	SpriteScene sprites;
	ModelScene models;
	ShieldScene shields;
	EntityId hidden = 0;
	std::map<EntityId, float> shield_radii;
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
	state_->shields.clear();
	state_->shield_radii.clear();
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
	auto result = state_->sprites.instance_statistics();
	result["shields"] = state_->shields.statistics();
	return result;
}
std::set<EntityId> SceneRenderer::prepare(const Catalog &c, std::span<const SceneBody> bodies, Camera3D camera,
                                          Rectangle region, EntityId hidden, double simulation_time,
                                          bool reduced_motion)
{
	// Preparation can initialise GPU resources. Preserve queued surrounding UI first.
	rlDrawRenderBatchActive();
	state_->hidden = hidden;
	return state_->select(c)
	           ? state_->models.prepare(c, bodies, camera, region, hidden, simulation_time, reduced_motion)
	           : state_->sprites.prepare(c, bodies, camera, region, hidden, simulation_time, reduced_motion);
}
void SceneRenderer::draw_prepared(const Catalog &c, std::span<const SceneBody> bodies, Camera3D camera,
                                  Rectangle region, const Json &lighting, double time, SceneDiagnostic diagnostic,
                                  Vector2 size, const SceneEnvironment &environment,
                                  std::optional<unsigned> framebuffer)
{
	rlDrawRenderBatchActive();
	const bool models = state_->select(c);
	state_->shield_radii.clear();
	for (const auto &body : bodies)
		if (body.id != state_->hidden && !body.audio_only && !body.line_style &&
		    (!body.shield_impacts.empty() || state_->shields.active(body.id)))
			state_->shield_radii[body.id] =
			    models ? state_->models.shield_radius(c, body) : state_->sprites.shield_radius(c, body);
	const auto target = framebuffer ? *framebuffer : rlGetActiveFramebuffer();
	state_->shields.update(bodies, state_->hidden, state_->shield_radii);
	rlEnableFramebuffer(target);
	if (state_->select(c))
		state_->models.draw_prepared(c, bodies, camera, region, lighting, time, size, environment, true, framebuffer);
	else
		state_->sprites.draw_prepared(c, bodies, camera, region, lighting, time, diagnostic, size, environment,
		                              framebuffer);
	state_->shields.draw(bodies, camera, region, size, target, state_->shield_radii);
}
void SceneRenderer::draw(const Catalog &c, std::span<const SceneBody> b, Camera3D cam, Rectangle r, const Json &l,
                         double t, SceneDiagnostic d, Vector2 size, const SceneEnvironment &e)
{
	prepare(c, b, cam, r, 0, l.value("environment", Json::object()).value("simulation_time", t), e.reduced_motion);
	draw_prepared(c, b, cam, r, l, t, d, size, e, std::nullopt);
}
} // namespace phoenix
