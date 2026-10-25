module;
#include <nlohmann/json.hpp>
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
#include "nebula_shader.hpp"
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
const char *sky_vertex = R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 uv;
void main(){uv=vertexTexCoord;gl_Position=vec4(vertexPosition.xy,1,1);}
)GLSL";
const char *sky_fragment = R"GLSL(
in vec2 uv;
out vec4 finalColor;
uniform sampler2D texture0;
uniform mat4 inverseViewProjection;
uniform int cubeStrip;
uniform int hasSkybox;
vec2 cubeUV(vec3 d){
 vec3 a=abs(d);vec2 q;float face;
 if(a.x>=a.y && a.x>=a.z){
  if(d.x>0.0){q=vec2(-d.z,-d.y)/a.x;face=0.0;}
  else{q=vec2(d.z,-d.y)/a.x;face=1.0;}
 }else if(a.y>=a.z){
  if(d.y>0.0){q=vec2(d.x,d.z)/a.y;face=2.0;}
  else{q=vec2(d.x,-d.z)/a.y;face=3.0;}
 }else{
  if(d.z>0.0){q=vec2(d.x,-d.y)/a.z;face=4.0;}
  else{q=vec2(-d.x,-d.y)/a.z;face=5.0;}
 }
 vec2 halfTexel=vec2(.5/float(textureSize(texture0,0).x));
 q=clamp(q*.5+.5,halfTexel,1.0-halfTexel);
 return vec2(q.x,(q.y+face)/6.0);
}
void main(){
 vec4 a=inverseViewProjection*vec4(uv*2.0-1.0,-1,1);
 vec4 b=inverseViewProjection*vec4(uv*2.0-1.0,1,1);
 vec3 d=normalize(b.xyz/b.w-a.xyz/a.w);
 vec2 skyUV;
 if(cubeStrip==1)skyUV=cubeUV(d);
 else{
  skyUV=vec2(atan(d.z,d.x)/6.28318530718+.5,acos(clamp(d.y,-1.0,1.0))/3.14159265359);
  float pole=.5/float(textureSize(texture0,0).y);
  skyUV.y=clamp(skyUV.y,pole,1.0-pole);
 }
 vec4 fog=fogAlong(a.xyz/a.w,b.xyz/b.w);
 finalColor=hasSkybox==1?vec4(mix(texture(texture0,skyUV).rgb,fog.rgb,fog.a),1):fog;
}
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
void uniform(ShaderLocations &locations, Shader shader, const char *name, const void *value, int type)
{
	SetShaderValue(shader, locations.get(shader, name), value, type);
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
void set_scene_fog(Shader target, const Json &environment, Vector3 rebase, ShaderLocations &locations)
{
	const auto volumes = environment.value("nebulae", Json::array());
	const int count = static_cast<int>(volumes.size());
	if (count > 32)
		throw std::runtime_error("Too many nebula volumes");
	std::array<Vector4, 32> bounds{}, extents{}, colours{};
	for (int i = 0; i < count; ++i)
	{
		const auto &v = volumes[i];
		bounds[i] = {v.at("x"), -v.at("y").get<float>(), v.at("radius"), v.at("inner_radius")};
		extents[i] = {v.at("half_width"), v.at("half_height"), v.at("height"), v.at("shape").get<float>()};
		colours[i] = {v.at("colour")[0], v.at("colour")[1], v.at("colour")[2], v.at("density")};
	}
	SetShaderValue(target, locations.get(target, "fogCount"), &count, SHADER_UNIFORM_INT);
	SetShaderValueV(target, locations.get(target, "fogBounds[0]"), bounds.data(), SHADER_UNIFORM_VEC4, 32);
	SetShaderValueV(target, locations.get(target, "fogExtents[0]"), extents.data(), SHADER_UNIFORM_VEC4, 32);
	SetShaderValueV(target, locations.get(target, "fogColours[0]"), colours.data(), SHADER_UNIFORM_VEC4, 32);
	const auto origin = Vector3Negate(rebase);
	SetShaderValue(target, locations.get(target, "fogOrigin"), &origin, SHADER_UNIFORM_VEC3);
}
NavigationEffects::~NavigationEffects()
{
	clear();
}
void NavigationEffects::clear()
{
	textures_.clear();
	locations_.clear();
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
	if (sky_shader_.id)
		UnloadShader(sky_shader_);
	dust_shader_ = grid_shader_ = sky_shader_ = {};
}
void NavigationEffects::background(const Catalog &catalog, const SceneEnvironment &environment, Camera3D camera,
                                   const Json &fog, Vector3 rebase)
{
	if (environment.skybox.empty() && fog.value("nebulae", Json::array()).empty())
		return;
	if (!sky_shader_.id)
	{
		const auto vs = prefix + sky_vertex, fs = prefix + fog_functions + sky_fragment;
		sky_shader_ = LoadShaderFromMemory(vs.c_str(), fs.c_str());
		if (!sky_shader_.id || sky_shader_.id == rlGetShaderIdDefault())
			throw std::runtime_error("Skybox shader failed to compile");
	}
	Texture2D texture{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	const int has_skybox = !environment.skybox.empty();
	if (has_skybox)
		texture = textures_.load(environment.skybox, catalog.assets.at(environment.skybox));
	SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
	uniform(locations_, sky_shader_, "hasSkybox", &has_skybox, SHADER_UNIFORM_INT);
	set_scene_fog(sky_shader_, fog, rebase, locations_);
	const int strip = texture.height == texture.width * 6;
	SetTextureWrap(texture, strip ? TEXTURE_WRAP_CLAMP : TEXTURE_WRAP_REPEAT);
	uniform(locations_, sky_shader_, "cubeStrip", &strip, SHADER_UNIFORM_INT);
	SetShaderValueMatrix(sky_shader_, locations_.get(sky_shader_, "inverseViewProjection"),
	                     MatrixInvert(MatrixMultiply(GetCameraMatrix(camera), rlGetMatrixProjection())));
	rlDrawRenderBatchActive();
	rlDisableDepthMask();
	rlDisableDepthTest();
	rlDisableBackfaceCulling();
	BeginShaderMode(sky_shader_);
	rlSetTexture(texture.id);
	quad({}, {1, 0, 0}, {0, 1, 0});
	rlDrawRenderBatchActive();
	rlSetTexture(0);
	EndShaderMode();
	rlEnableDepthTest();
	rlEnableDepthMask();
	rlEnableBackfaceCulling();
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
		uniform(locations_, grid_shader_, "phase", &phase, SHADER_UNIFORM_VEC2);
		uniform(locations_, grid_shader_, "centre", &centre, SHADER_UNIFORM_VEC2);
		uniform(locations_, grid_shader_, "spacing", &spacing, SHADER_UNIFORM_VEC2);
		uniform(locations_, grid_shader_, "halfWidth", &widths, SHADER_UNIFORM_VEC2);
		uniform(locations_, grid_shader_, "fadeParams", &fade, SHADER_UNIFORM_VEC4);
		uniform(locations_, grid_shader_, "minorColour", c.minor_colour.data(), SHADER_UNIFORM_VEC4);
		uniform(locations_, grid_shader_, "majorColour", c.major_colour.data(), SHADER_UNIFORM_VEC4);
		uniform(locations_, grid_shader_, "opacity", &c.opacity, SHADER_UNIFORM_FLOAT);
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
		uniform(locations_, dust_shader_, "viewport", &viewport, SHADER_UNIFORM_VEC4);
		uniform(locations_, dust_shader_, "tint", c.tint.data(), SHADER_UNIFORM_VEC3);
		uniform(locations_, dust_shader_, "opacity", &c.opacity, SHADER_UNIFORM_FLOAT);
		uniform(locations_, dust_shader_, "masks", &masks, SHADER_UNIFORM_VEC3);
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
			uniform(locations_, dust_shader_, "brightness", &c.brightness[layer], SHADER_UNIFORM_FLOAT);
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
