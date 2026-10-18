module;
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>
module phoenix.presentation;
#include "environment.hpp"
namespace phoenix
{
namespace
{
#ifdef __EMSCRIPTEN__
const std::string prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const std::string prefix = "#version 330\n";
#endif
const char *vertex = R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
out vec3 position;
out vec4 colour;
void main(){uv=vertexTexCoord;position=vertexPosition;colour=vertexColor;gl_Position=mvp*vec4(vertexPosition,1.0);}
)GLSL";
const char *dust_fragment = R"GLSL(
in vec2 uv;
in vec4 colour;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 viewport;
uniform vec3 tint;
uniform float brightness;
uniform float opacity;
uniform vec3 masks;
void main(){
  vec2 ndc=((gl_FragCoord.xy-viewport.xy)/viewport.zw-.5)*2.0;
  float centre=smoothstep(masks.x,masks.y,length(ndc));
  float edge=masks.z>0.0?1.0-smoothstep(1.0-masks.z,1.0,max(abs(ndc.x),abs(ndc.y))):1.0;
  float alpha=texture(texture0,uv).a*opacity*centre*edge;
  if(alpha<=.001)discard;
  finalColor=vec4(tint*brightness,alpha);
}
)GLSL";
const char *grid_fragment = R"GLSL(
in vec2 uv;
in vec3 position;
out vec4 finalColor;
uniform vec2 phase;
uniform vec2 centre;
uniform vec4 minorColour;
uniform vec4 majorColour;
uniform vec2 spacing;
uniform vec2 halfWidth;
uniform vec4 fadeParams;
uniform float opacity;
float lattice(vec2 world,vec2 derivative,float stepSize,float width){
  vec2 cells=world/stepSize;
  vec2 distance=abs(cells-floor(cells+.5))*stepSize;
  vec2 coverage=clamp(1.0-distance/max(derivative,vec2(.000001))/width,0.0,1.0);
  return max(coverage.x,coverage.y);
}
void main(){
  vec2 world=position.xz+phase;
  vec2 derivative=fwidth(position.xz);
  float minor=lattice(world,derivative,spacing.x,halfWidth.x);
  float major=lattice(world,derivative,spacing.y,halfWidth.y);
  float distance=length(position.xz-centre);
  float t=clamp((distance-fadeParams.x)/fadeParams.y,0.0,1.0);
  float fade=pow(1.0-t*t*(3.0-2.0*t),fadeParams.z);
  float alpha=max(minor*minorColour.a,major*majorColour.a)*opacity*fade;
  if(alpha<=.001)discard;
  finalColor=vec4(mix(minorColour.rgb,majorColour.rgb,major),alpha);
}
)GLSL";
Shader shader(const char *fragment)
{
	const auto vs = prefix + vertex, fs = prefix + fragment;
	auto result = LoadShaderFromMemory(vs.c_str(), fs.c_str());
	if (!result.id || result.id == rlGetShaderIdDefault())
		throw std::runtime_error("Navigation aid shader failed to compile");
	return result;
}
void uniform(Shader shader, const char *name, const void *value, int type)
{
	SetShaderValue(shader, GetShaderLocation(shader, name), value, type);
}
void quad(Vector3 p, Vector3 x, Vector3 y)
{
	rlBegin(RL_QUADS);
	rlColor4ub(255, 255, 255, 255);
	const auto point = [&](float u, float v)
	{
		const auto vertex = Vector3Add(p, Vector3Add(Vector3Scale(x, u), Vector3Scale(y, v)));
		rlTexCoord2f((u + 1) * .5f, (v + 1) * .5f);
		rlVertex3f(vertex.x, vertex.y, vertex.z);
	};
	point(-1, -1);
	point(1, -1);
	point(1, 1);
	point(-1, 1);
	rlEnd();
}
double wrap(double value, double half)
{
	const double period = half * 2;
	return value - std::floor((value + half) / period) * period;
}
} // namespace
NavigationEffects::~NavigationEffects()
{
	clear();
}
void NavigationEffects::clear()
{
	textures_.clear();
	for (auto &texture : defaults_)
	{
		if (texture.id)
			UnloadTexture(texture);
		texture = {};
	}
	if (dust_shader_.id)
		UnloadShader(dust_shader_);
	if (grid_shader_.id)
		UnloadShader(grid_shader_);
	dust_shader_ = grid_shader_ = {};
}
void NavigationEffects::draw(const Catalog &catalog, const SceneEnvironment &environment, Camera3D camera,
                             Vector3 rebase, Rectangle region)
{
	if (!environment.own || environment.reduced_motion)
		return;
	const auto &own = *environment.own;
	rlDrawRenderBatchActive();
	rlDisableDepthMask();
	rlDisableBackfaceCulling();
	if (environment.grid && environment.grid_enabled)
	{
		const auto &c = *environment.grid;
		if (!grid_shader_.id)
			grid_shader_ = shader(grid_fragment);
		const Vector3 p{static_cast<float>(own.x / 1000.0 - rebase.x), c.plane_y - rebase.y,
		                static_cast<float>(-own.y / 1000.0 - rebase.z)};
		const Vector2 phase{static_cast<float>(std::fmod(static_cast<double>(rebase.x), c.major_spacing)),
		                    static_cast<float>(std::fmod(static_cast<double>(rebase.z), c.major_spacing))};
		const Vector2 centre{p.x, p.z}, spacing{c.minor_spacing, c.major_spacing},
		    widths{c.minor_line_width_px * .5f, c.major_line_width_px * .5f};
		const Vector4 fade{c.patch_radius - c.fade_band, std::max(.0001f, c.fade_band), c.fade_exponent, 0};
		uniform(grid_shader_, "phase", &phase, SHADER_UNIFORM_VEC2);
		uniform(grid_shader_, "centre", &centre, SHADER_UNIFORM_VEC2);
		uniform(grid_shader_, "spacing", &spacing, SHADER_UNIFORM_VEC2);
		uniform(grid_shader_, "halfWidth", &widths, SHADER_UNIFORM_VEC2);
		uniform(grid_shader_, "fadeParams", &fade, SHADER_UNIFORM_VEC4);
		uniform(grid_shader_, "minorColour", c.minor_colour.data(), SHADER_UNIFORM_VEC4);
		uniform(grid_shader_, "majorColour", c.major_colour.data(), SHADER_UNIFORM_VEC4);
		uniform(grid_shader_, "opacity", &c.opacity, SHADER_UNIFORM_FLOAT);
		BeginShaderMode(grid_shader_);
		quad(p, {c.patch_radius, 0, 0}, {0, 0, c.patch_radius});
		EndShaderMode();
	}
	const auto &c = environment.dust;
	if (environment.dust_enabled && c.enabled && c.count > 0 && c.width > 0 && c.opacity > 0)
	{
		if (!dust_shader_.id)
			dust_shader_ = shader(dust_fragment);
		std::array<Texture2D, 3> textures;
		const char *names[] = {"space_mote_streak_head.png", "space_mote_streak_soft.png",
		                       "space_mote_compact_core.png"};
		for (int layer = 0; layer < 3; ++layer)
		{
			if (c.textures[layer].empty())
			{
				if (!defaults_[layer].id)
				{
#ifdef __EMSCRIPTEN__
					const std::string root = "/assets/pfx/";
#else
					const std::string root = std::string(GetApplicationDirectory()) + "assets/pfx/";
#endif
					defaults_[layer] = LoadTexture((root + names[layer]).c_str());
					if (!defaults_[layer].id)
						throw std::runtime_error("Bundled dust texture unavailable");
					SetTextureFilter(defaults_[layer], TEXTURE_FILTER_BILINEAR);
				}
				textures[layer] = defaults_[layer];
			}
			else
			{
				textures[layer] = textures_.load(c.textures[layer], catalog.assets.at(c.textures[layer]));
				SetTextureFilter(textures[layer], TEXTURE_FILTER_BILINEAR);
			}
		}
		const Vector4 viewport{region.x, region.y, region.width, region.height};
		const Vector3 masks{c.centre_fade_inner, c.centre_fade_outer, c.edge_fade};
		uniform(dust_shader_, "viewport", &viewport, SHADER_UNIFORM_VEC4);
		uniform(dust_shader_, "tint", c.tint.data(), SHADER_UNIFORM_VEC3);
		uniform(dust_shader_, "opacity", &c.opacity, SHADER_UNIFORM_FLOAT);
		uniform(dust_shader_, "masks", &masks, SHADER_UNIFORM_VEC3);
		const auto forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
		const auto right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
		const auto up = Vector3CrossProduct(right, forward);
		const Vector3 velocity{own.vx / 1000.f, 0, -own.vy / 1000.f};
		const float speed = Vector3Length(velocity);
		const Vector3 localVelocity{-Vector3DotProduct(velocity, right), -Vector3DotProduct(velocity, up),
		                            Vector3DotProduct(velocity, forward)};
		const double hx = c.half_width, hz = c.depth * .5;
		// Absolute lattice anchors, wrapped using double precision before rebasing.
		const double cx = static_cast<double>(camera.position.x) + rebase.x,
		             cy = static_cast<double>(camera.position.y) + rebase.y,
		             cz = static_cast<double>(camera.position.z) + rebase.z;
		BeginShaderMode(dust_shader_);
		for (int layer = 0; layer < 3; ++layer)
		{
			rlDrawRenderBatchActive();
			uniform(dust_shader_, "brightness", &c.brightness[layer], SHADER_UNIFORM_FLOAT);
			rlSetTexture(textures[layer].id);
			for (int i = layer; i < c.count; i += 3)
			{
				const auto fraction = [i](double seed)
				{
					return i * seed - std::floor(i * seed);
				};
				const Vector3 relative{static_cast<float>(wrap(fraction(.7548777) * hx * 2 - cx, hx)),
				                       static_cast<float>(wrap(fraction(.5698403) * hx * 2 - cy, hx)),
				                       static_cast<float>(wrap(fraction(.438289) * hz * 2 - cz, hz))};
				const float z = Vector3DotProduct(relative, forward);
				if (z <= .01f)
					continue;
				const Vector3 local{Vector3DotProduct(relative, right), Vector3DotProduct(relative, up), -z};
				Vector2 travel{localVelocity.x * z + local.x * localVelocity.z,
				               localVelocity.y * z + local.y * localVelocity.z};
				if (Vector2LengthSqr(travel) < .000001f)
					travel = {1, 0};
				travel = Vector2Normalize(travel);
				const auto longAxis = Vector3Add(Vector3Scale(right, travel.x), Vector3Scale(up, travel.y));
				const auto shortAxis = Vector3Add(Vector3Scale(right, -travel.y), Vector3Scale(up, travel.x));
				const float halfWidth = c.width * (1 + layer * .5f) * .5f;
				quad(Vector3Add(camera.position, relative),
				     Vector3Scale(longAxis, halfWidth * (1 + speed * c.streak_per_speed)),
				     Vector3Scale(shortAxis, halfWidth));
			}
			rlDrawRenderBatchActive();
		}
		rlSetTexture(0);
		EndShaderMode();
	}
	rlDrawRenderBatchActive();
	rlEnableDepthMask();
	rlEnableBackfaceCulling();
}
} // namespace phoenix
