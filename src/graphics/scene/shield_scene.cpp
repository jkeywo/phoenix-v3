module;
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "scene_visibility.hpp"
module phoenix.graphics.scene;
#include "shield_scene.hpp"
namespace phoenix
{
namespace
{
// All artistic tuning lives here. Damage is in the simulation's ordinary damage units.
constexpr float reference_damage = 30, cooling_seconds = 2, minimum_radius = .16f, maximum_radius = .85f,
                reference_radius = .48f, hex_cells = 48, line_width = .035f;
constexpr std::size_t maximum_active = 128;
#ifdef __EMSCRIPTEN__
const std::string prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const std::string prefix = "#version 330\n";
#endif
const char *heat_fragment = R"GLSL(
in vec2 fragTexCoord; out vec4 finalColor;
uniform sampler2D texture0;
uniform vec3 impact; uniform float radius; uniform float energy; uniform float cooling;
const float pi=3.141592653589793;
void main(){
 // Render textures use bottom-left UVs. The atlas has latitude increasing upward.
 vec2 uv=fragTexCoord;
 float lon=(uv.x-.5)*2.*pi, lat=(uv.y-.5)*pi;
 vec3 n=vec3(cos(lat)*cos(lon),sin(lat),cos(lat)*sin(lon));
 float angle=acos(clamp(dot(n,impact),-1.,1.));
 float stamp=energy*(1.-smoothstep(0.,radius,angle));
 float h=clamp(texture(texture0,uv).r-cooling+stamp,0.,1.);
 finalColor=vec4(h,0,0,1);
})GLSL";
const char *shell_vertex = R"GLSL(
in vec3 vertexPosition; uniform mat4 mvp; out vec3 local;
void main(){local=vertexPosition;gl_Position=mvp*vec4(vertexPosition,1);}
)GLSL";
const char *shell_fragment = R"GLSL(
in vec3 local; out vec4 finalColor;
uniform sampler2D texture0; uniform float cells; uniform float lineWidth;
const float pi=3.141592653589793;
void main(){
 vec3 n=normalize(local);
 vec2 uv=vec2(atan(n.z,n.x)/(2.*pi)+.5,asin(clamp(n.y,-1.,1.))/pi+.5);
 float heat=texture(texture0,uv).r;if(heat<.004)discard;
 // Periodic hex lattice, with an integer number of columns across the longitude seam.
 vec2 p=vec2(uv.x*cells*1.7320508,uv.y*cells*.5*1.5);
 vec2 s=vec2(1.7320508,3.);
 vec2 a=mod(p,s)-s*.5,b=mod(p-s*.5,s)-s*.5;
 vec2 q=dot(a,a)<dot(b,b)?a:b;
 float edge=.8660254-max(abs(q.x),dot(abs(q),vec2(.5,.8660254)));
 float aa=max(fwidth(edge),.001);
 float ink=1.-smoothstep(lineWidth-aa,lineWidth+aa,edge);
 if(ink<.01)discard;
 // Fade distant/subpixel lattice instead of producing a solid shimmering surface.
 vec2 dx=dFdx(uv),dy=dFdy(uv);dx.x-=round(dx.x);dy.x-=round(dy.x);
 vec2 scale=vec2(cells*1.7320508,cells*.75);
 float lod=1.-smoothstep(.35,1.2,max(length(dx*scale),length(dy*scale)));
 vec3 c=heat<.35?mix(vec3(1,.025,0),vec3(1,.25,0),heat/.35):
        heat<.7?mix(vec3(1,.25,0),vec3(1,1,.08),(heat-.35)/.35):
        mix(vec3(1,1,.08),vec3(1),(heat-.7)/.3);
 finalColor=vec4(c,ink*lod*smoothstep(0.,.18,heat)*.9);
})GLSL";
} // namespace
struct ShieldScene::State
{
	struct Heat
	{
		std::array<RenderTexture2D, 2> textures{};
		int front = 0;
		double time = 0, last_hit = -10, cooling_remainder = 0;
		std::uint64_t last_event = 0;
	};
	std::map<EntityId, Heat> heat;
	Shader update{}, shell{};
	Mesh sphere{};
	Material material{};
	std::uint64_t stamps = 0, passes = 0;
	static void release(Heat &h)
	{
		for (auto &t : h.textures)
			if (t.id)
				UnloadRenderTexture(t);
	}
	void init()
	{
		if (update.id)
			return;
		update = LoadShaderFromMemory(nullptr, (prefix + heat_fragment).c_str());
		shell = LoadShaderFromMemory((prefix + shell_vertex).c_str(), (prefix + shell_fragment).c_str());
		if (!update.id || !shell.id || update.id == rlGetShaderIdDefault() || shell.id == rlGetShaderIdDefault())
			throw std::runtime_error("Shield shader compilation failed");
		material = LoadMaterialDefault();
		material.shader = shell;
		sphere = GenMeshSphere(1, 48, 96);
		SetShaderValue(shell, GetShaderLocation(shell, "cells"), &hex_cells, SHADER_UNIFORM_FLOAT);
		SetShaderValue(shell, GetShaderLocation(shell, "lineWidth"), &line_width, SHADER_UNIFORM_FLOAT);
	}
	void pass(Heat &h, Vector3 impact, float radius, float energy, float cooling)
	{
		const int back = 1 - h.front;
		BeginTextureMode(h.textures[back]);
		// The shader writes alpha=1, so ordinary alpha blending replaces every texel.
		BeginBlendMode(BLEND_ALPHA);
		BeginShaderMode(update);
		SetShaderValue(update, GetShaderLocation(update, "impact"), &impact, SHADER_UNIFORM_VEC3);
		SetShaderValue(update, GetShaderLocation(update, "radius"), &radius, SHADER_UNIFORM_FLOAT);
		SetShaderValue(update, GetShaderLocation(update, "energy"), &energy, SHADER_UNIFORM_FLOAT);
		SetShaderValue(update, GetShaderLocation(update, "cooling"), &cooling, SHADER_UNIFORM_FLOAT);
		// Negative height preserves the heat atlas orientation across ping-pong passes.
		DrawTextureRec(h.textures[h.front].texture, {0, 0, 256, -128}, {}, WHITE);
		EndShaderMode();
		EndBlendMode();
		EndTextureMode();
		h.front = back;
		++passes;
	}
};
ShieldScene::ShieldScene() : state_(std::make_unique<State>())
{
}
ShieldScene::~ShieldScene()
{
	clear();
}
void ShieldScene::clear()
{
	auto &s = *state_;
	for (auto &[id, h] : s.heat)
		State::release(h);
	s.heat.clear();
	if (s.sphere.vaoId)
		UnloadMesh(s.sphere);
	if (s.material.maps)
	{
		// Maps borrow the heat texture and shader; their owners release those separately.
		MemFree(s.material.maps);
	}
	if (s.shell.id)
		UnloadShader(s.shell);
	if (s.update.id)
		UnloadShader(s.update);
	s.sphere = {};
	s.material = {};
	s.shell = {};
	s.update = {};
	s.stamps = s.passes = 0;
}
bool ShieldScene::active(EntityId id) const
{
	return state_->heat.contains(id);
}
Json ShieldScene::statistics() const
{
	return {{"active", state_->heat.size()}, {"stamps", state_->stamps}, {"passes", state_->passes}};
}
void ShieldScene::update(std::span<const SceneBody> bodies, EntityId hidden, const std::map<EntityId, float> &radii)
{
	auto &s = *state_;
	if (s.heat.empty() && std::none_of(bodies.begin(), bodies.end(),
	                                   [](const auto &body)
	                                   {
		                                   return !body.shield_impacts.empty();
	                                   }))
		return;
	std::set<EntityId> present;
	for (const auto &body : bodies)
	{
		if (body.id == hidden || body.audio_only || body.line_style)
			continue;
		auto at = s.heat.find(body.id);
		if (at != s.heat.end() && body.shield_time < at->second.time)
		{
			State::release(at->second);
			s.heat.erase(at);
			at = s.heat.end();
		}
		const bool fresh = std::any_of(body.shield_impacts.begin(), body.shield_impacts.end(),
		                               [&](const auto &hit)
		                               {
			                               const double age = body.shield_time - hit.time;
			                               return hit.absorbed > 0 && age >= 0 && age < cooling_seconds &&
			                                      (at == s.heat.end() || hit.event > at->second.last_event);
		                               });
		if (at == s.heat.end())
		{
			if (!fresh || s.heat.size() >= maximum_active)
				continue;
			s.init();
			auto &h = s.heat[body.id];
			h.time = body.shield_time;
			for (const auto &hit : body.shield_impacts)
				if (hit.absorbed > 0 && hit.time <= body.shield_time && body.shield_time - hit.time < cooling_seconds)
					h.time = std::min(h.time, hit.time);
			for (auto &t : h.textures)
			{
				t = LoadRenderTexture(256, 128);
				SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
				SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
				BeginTextureMode(t);
				ClearBackground(BLACK);
				EndTextureMode();
			}
			at = s.heat.find(body.id);
		}
		auto &h = at->second;
		present.insert(body.id);
		// Replay newly observed hits in time order before cooling to the current snapshot.
		// Cooling each stamp independently would lose the heat of older overlapping hits.
		const auto cool_to = [&](double time)
		{
			const double amount = std::max(0., time - h.time) / cooling_seconds * 255 + h.cooling_remainder;
			const auto steps = std::floor(amount + 1e-7);
			h.cooling_remainder = amount - steps;
			if (steps > 0)
				s.pass(h, {1, 0, 0}, 1, 0, static_cast<float>(steps / 255));
			h.time = std::max(h.time, time);
		};
		for (const auto &hit : body.shield_impacts)
		{
			if (hit.event <= h.last_event)
				continue;
			h.last_event = hit.event;
			const double age = body.shield_time - hit.time;
			if (hit.absorbed <= 0 || age < 0 || age >= cooling_seconds)
				continue;
			cool_to(hit.time);
			Vector3 direction{hit.direction[0], hit.direction[1], hit.direction[2]};
			if (hit.has_path && radii.contains(body.id))
			{
				const Vector3 p{hit.path_point[0], hit.path_point[1], hit.path_point[2]};
				const auto side = Vector3Subtract(p, Vector3Scale(direction, Vector3DotProduct(p, direction)));
				const float r = radii.at(body.id);
				direction = Vector3Normalize(Vector3Add(
				    side, Vector3Scale(direction, std::sqrt(std::max(0.f, r * r - Vector3LengthSqr(side))))));
			}
			const float strength = static_cast<float>(hit.absorbed) / reference_damage;
			const float radius = std::clamp(reference_radius * std::sqrt(strength), minimum_radius, maximum_radius);
			const float energy = std::max(0.f, std::clamp(strength, .3f, 1.f) -
			                                       static_cast<float>((h.time - hit.time) / cooling_seconds));
			s.pass(h, direction, radius, energy, 0);
			h.last_hit = hit.time;
			++s.stamps;
		}
		cool_to(body.shield_time);
		if (body.shield_time - h.last_hit >= cooling_seconds)
		{
			State::release(h);
			s.heat.erase(at);
		}
	}
	std::erase_if(s.heat,
	              [&](auto &entry)
	              {
		              if (present.contains(entry.first))
			              return false;
		              State::release(entry.second);
		              return true;
	              });
}
void ShieldScene::draw(std::span<const SceneBody> bodies, Camera3D camera, Rectangle region, Vector2 size,
                       unsigned framebuffer, const std::map<EntityId, float> &radii)
{
	auto &s = *state_;
	if (s.heat.empty())
		return;
	const auto rebase = camera.target;
	camera.position = Vector3Subtract(camera.position, rebase);
	camera.target = {};
	if (size.x <= 0)
		size = {static_cast<float>(GetRenderWidth()), static_cast<float>(GetRenderHeight())};
	const float sx = framebuffer ? 1.f : size.x / GetScreenWidth(), sy = framebuffer ? 1.f : size.y / GetScreenHeight();
	const Rectangle physical{region.x * sx, size.y - (region.y + region.height) * sy, region.width * sx,
	                         region.height * sy};
	rlDrawRenderBatchActive();
	rlEnableFramebuffer(framebuffer);
	rlEnableScissorTest();
	rlScissor(static_cast<int>(physical.x), static_cast<int>(physical.y), static_cast<int>(physical.width),
	          static_cast<int>(physical.height));
	rlViewport(static_cast<int>(physical.x), static_cast<int>(physical.y), static_cast<int>(physical.width),
	           static_cast<int>(physical.height));
	BeginMode3D(camera);
	rlSetMatrixProjection(scene_camera_projection(camera, region));
	rlDisableDepthMask();
	rlEnableBackfaceCulling();
	for (const auto &body : bodies)
	{
		const auto at = s.heat.find(body.id);
		if (at == s.heat.end() || !radii.contains(body.id))
			continue;
		const float r = radii.at(body.id);
		const Vector3 p{body.x / 1000.f - rebase.x, body.visual_height_mm / 1000.f - rebase.y,
		                -body.y / 1000.f - rebase.z};
		auto transform =
		    MatrixMultiply(MatrixMultiply(MatrixScale(r, r, r), MatrixRotateY(body.heading * DEG2RAD / 1000.f)),
		                   MatrixTranslate(p.x, p.y, p.z));
		s.material.maps[MATERIAL_MAP_DIFFUSE].texture = at->second.textures[at->second.front].texture;
		DrawMesh(s.sphere, s.material, transform);
	}
	rlEnableDepthMask();
	EndMode3D();
	rlViewport(0, 0, static_cast<int>(size.x), static_cast<int>(size.y));
	EndScissorMode();
}
} // namespace phoenix
