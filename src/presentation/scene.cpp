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
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.content;

namespace phoenix
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
#ifdef __EMSCRIPTEN__
const std::string prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const std::string prefix = "#version 330\n";
#endif
const char *surface_functions = R"GLSL(
uniform sampler2D texture0;
uniform sampler2D depthMap;
uniform vec4 crop;
uniform vec2 atlasSize;
uniform vec2 frameSize;
uniform vec2 pivot;
uniform vec3 anchor;
uniform vec3 basisRight;
uniform vec3 basisUp;
uniform vec3 basisNear;
uniform vec3 cameraPosition;
uniform float height;
uniform float depthScale;
uniform float depthOffset;
uniform int hasDepth;
uniform int perspective;
vec2 sampleUV(vec2 uv){return clamp((crop.xy+uv*crop.zw)/atlasSize,(crop.xy+.5)/atlasSize,(crop.xy+crop.zw-.5)/atlasSize);}
float reliefAt(vec2 uv){return depthOffset+(hasDepth==1?texture(depthMap,sampleUV(uv)).r-.5:0.0)*depthScale;}
vec3 surfaceAt(vec2 uv){
  vec3 plane=anchor+basisRight*((uv.x*frameSize.x-pivot.x)*height/frameSize.y)
                    -basisUp*((uv.y*frameSize.y-pivot.y)*height/frameSize.y);
  float z=height*reliefAt(uv);
  if(perspective==1) return cameraPosition+(plane-cameraPosition)*(1.0-z/max(.01,dot(cameraPosition-plane,basisNear)));
  return plane+basisNear*z;
}
)GLSL";
const char *vertex_body = R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 uv;
void main(){uv=vertexTexCoord;gl_Position=mvp*vec4(surfaceAt(uv),1.0);}
)GLSL";
const char *fragment_body = R"GLSL(
in vec2 uv;
out vec4 finalColor;
uniform sampler2D normalMap;
uniform sampler2D shadowMap;
uniform mat4 viewProjection;
uniform mat4 lightProjection;
uniform int shadowPass;
uniform int useShadows;
uniform int hasNormal;
uniform int emissive;
uniform int diagnostic;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float ambient;
uniform float spacing;
uniform float thickness;
uniform float ink;
uniform float paperLod;
uniform float shadowBias;
uniform float shadowSoftness;
uniform int lightCount;
uniform vec4 lights[16];
uniform vec4 cones[16];
uniform vec4 lightColors[16];
uniform int burstGraphic;
uniform vec4 burstTint;
vec4 packDepth(float d){vec4 v=fract(d*vec4(16777216.0,65536.0,256.0,1.0));v-=v.xxyz*vec4(0.0,1.0/256.0,1.0/256.0,1.0/256.0);return v;}
float unpackDepth(vec4 v){return dot(v,vec4(1.0/16777216.0,1.0/65536.0,1.0/256.0,1.0));}
float visibility(vec3 p){
  if(useShadows==0)return 1.0;
  vec4 q=lightProjection*vec4(p,1.0);vec3 s=q.xyz/q.w*.5+.5;
  if(any(lessThan(s,vec3(0)))||any(greaterThan(s,vec3(1))))return 1.0;
  float result=0.0;
  for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
    float z=unpackDepth(texture(shadowMap,s.xy+vec2(x,y)*shadowSoftness/2048.0));
    result+=s.z-shadowBias<=z?1.0:0.0;
  }return result/9.0;
}
float stroke(vec2 p,float angle,float gap,float width,float seed){
  vec2 d=vec2(cos(angle),sin(angle));float along=dot(p,vec2(-d.y,d.x));float across=dot(p,d);
  across+=.30*sin(along*.16+seed)+.16*sin(along*.57+seed*3.0);
  float band=abs(fract(across/gap)-.5)*gap;float aa=max(fwidth(across)*.7,.4);
  return 1.0-smoothstep(width*thickness-aa,width*thickness+aa,band);
}
void main(){
  vec3 p=surfaceAt(uv);vec4 clip=viewProjection*vec4(p,1.0);
  float pixelDepth=clip.z/clip.w*.5+.5;
  if(pixelDepth<=0.0||pixelDepth>=1.0)discard;
  gl_FragDepth=pixelDepth;
  if(burstGraphic==1){
    vec2 q=(uv-.5)*2.0;float r=length(q);float angle=atan(q.y,q.x);
    float edge=mix(.40,1.0,pow(abs(sin(angle*12.0)),5.0));
    if(r>edge)discard;
    finalColor=vec4(mix(vec3(1.0,.85,.48),burstTint.rgb,smoothstep(.05,.5,r)),burstTint.a);return;
  }
  vec4 base=texture(texture0,sampleUV(uv));if(base.a<.15)discard;
  if(shadowPass==1){finalColor=packDepth(pixelDepth);return;}
  if(emissive==1||diagnostic==2){finalColor=base;return;}
  vec3 n;
  if(hasNormal==1){vec3 raw=texture(normalMap,sampleUV(uv)).rgb*2.0-1.0;n=normalize(basisRight*raw.x+basisUp*raw.y+basisNear*raw.z);}
  else {n=normalize(cross(dFdx(p),dFdy(p)));if(dot(n,basisNear)<0.0)n=-n;}
  if(diagnostic==3){finalColor=vec4(n*.5+.5,1);return;}
  if(diagnostic==4){finalColor=vec4(vec3(clamp(reliefAt(uv)+.5,0.0,1.0)),1);return;}
  float vis=visibility(p);
  if(diagnostic==5){finalColor=vec4(vec3(vis),1);return;}
  float direct=max(dot(n,sunDirection),0.0)*vis*sunIntensity;
  float localPower=0.0;vec3 localColor=vec3(0);
  for(int i=0;i<16;i++){
    if(i>=lightCount)break;
    vec3 toward=lights[i].xyz-p;float d2=max(dot(toward,toward),.000001);toward*=inversesqrt(d2);
    float cone=cones[i].w<0.0?1.0:smoothstep(.5,.819152,dot(-toward,cones[i].xyz));
    float power=cone*max(dot(n,toward),0.0)*lightColors[i].w/(1.0+d2/(lights[i].w*lights[i].w));
    localPower+=power;localColor+=power*lightColors[i].rgb;
  }
  float dark=1.0-clamp(ambient+direct*.95+localPower,0.0,1.0);
  // Stable frame-local ink coordinates, independent of viewport resolution.
  vec2 paper=uv*frameSize/paperLod;
  float hatch=max(stroke(paper,.74,spacing,.66,2.0)*smoothstep(.10,.38,dark),
      max(stroke(paper,-.78,spacing*1.12,.66,8.0)*smoothstep(.37,.68,dark),
          stroke(paper,1.52,spacing*.83,.62,4.0)*smoothstep(.66,.89,dark)));
  hatch=mix(hatch,1.0,smoothstep(.86,1.0,dark)*.8)*ink;
  vec3 pigment=pow(max(base.rgb,vec3(0)),vec3(2.2));float wash=1.0-exp(-localPower*.9);vec3 lit;
  if(diagnostic==1)lit=pigment*((vec3(ambient*.7)+direct*sunColor)*(1.0-.7*wash)+localColor*1.6);
  else {
    lit=pigment*(.40+.60*clamp(ambient+direct*.95,0.0,1.0))*(1.0-.7*wash);
    lit*=mix(vec3(1),vec3(.012,.018,.012),hatch);
    lit+=pigment*localColor*1.6*(1.0-hatch);
  }
  finalColor=vec4(pow(clamp(lit,0.0,1.0),vec3(1.0/2.2)),base.a);
}
)GLSL";
struct MaterialData
{
	SpriteMaterial sprite;
	std::vector<Image> cpu;
	Texture2D albedo{}, normal{}, depth{};
};
struct Item
{
	MaterialData *material;
	const SpriteFrame *frame;
	Vector3 anchor;
	float height;
	bool engine = false;
	int layer = 0;
};
struct LocalLight
{
	Vector4 position, cone, color;
};
float bearing(Camera3D camera, Vector3 p, std::int64_t heading)
{
	return std::fmod(
	    std::atan2(-(camera.position.z - p.z), camera.position.x - p.x) * 180 / pi - heading / 1000.f + 720, 360.f);
}
const SpriteFrame &select_frame(const SpriteMaterial &material, float angle)
{
	return *std::min_element(material.frames.begin(), material.frames.end(),
	                         [&](const auto &a, const auto &b)
	                         {
		                         const auto difference = [&](float v)
		                         {
			                         return std::abs(std::remainder(v - angle, 360.f));
		                         };
		                         return difference(a.azimuth) < difference(b.azimuth);
	                         });
}
float sample_depth(const MaterialData &data, const SpriteFrame &f, float x, float y)
{
	if (data.cpu.size() < 2)
		return 0;
	const auto &im = data.cpu[1];
	const int sx = std::clamp(f.rect[0] + static_cast<int>(x), 0, im.width - 1),
	          sy = std::clamp(f.rect[1] + static_cast<int>(y), 0, im.height - 1);
	return GetImageColor(im, sx, sy).r / 255.f - .5f;
}
} // namespace
struct SceneRenderer::State
{
	std::map<std::string, MaterialData> materials;
	TextureCache textures;
	Shader shader{};
	Material drawMaterial{};
	Mesh grid{};
	RenderTexture2D shadow{};
	Texture2D white{};
	std::vector<ExplosionEffect> effects;
	std::map<std::string, int> locations;
	Vector3 right{}, up{}, near{}, cameraPosition{};
	float focal_pixels = 1;
	bool perspective_draw = true;
	Vector3 rebase{};
	std::vector<Item> items;
	std::vector<LocalLight> lights;
	~State()
	{
		clear();
	}
	void clear()
	{
		for (auto &[id, data] : materials)
			for (auto im : data.cpu)
				UnloadImage(im);
		materials.clear();
		textures.clear();
		effects.clear();
		items.clear();
		lights.clear();
		if (grid.vaoId)
			UnloadMesh(grid);
		grid = {};
		if (shader.id)
			UnloadShader(shader);
		shader = {};
		if (drawMaterial.maps)
			MemFree(drawMaterial.maps);
		drawMaterial = {};
		if (shadow.id)
			UnloadRenderTexture(shadow);
		shadow = {};
		if (white.id)
			UnloadTexture(white);
		white = {};
		locations.clear();
	}
	int loc(const std::string &name)
	{
		auto [it, added] = locations.try_emplace(name, -1);
		if (added)
			it->second = GetShaderLocation(shader, name.c_str());
		return it->second;
	}
	void number(const char *name, float n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_FLOAT);
	}
	void integer(const char *name, int n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_INT);
	}
	void vec(const char *name, Vector3 n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_VEC3);
	}
	void init()
	{
		if (shader.id)
			return;
		const auto vs = prefix + surface_functions + vertex_body, fs = prefix + surface_functions + fragment_body;
		shader = LoadShaderFromMemory(vs.c_str(), fs.c_str());
		if (!shader.id || shader.id == rlGetShaderIdDefault())
		{
			shader = {};
			throw std::runtime_error("Comic sprite shader failed to compile");
		}
		drawMaterial = LoadMaterialDefault();
		drawMaterial.shader = shader;
		shader.locs[SHADER_LOC_MAP_ALBEDO] = loc("texture0");
		shader.locs[SHADER_LOC_MAP_METALNESS] = loc("depthMap");
		shader.locs[SHADER_LOC_MAP_NORMAL] = loc("normalMap");
		shader.locs[SHADER_LOC_MAP_ROUGHNESS] = loc("shadowMap");
		constexpr int side = 129;
		grid.vertexCount = side * side;
		grid.triangleCount = (side - 1) * (side - 1) * 2;
		grid.vertices = static_cast<float *>(MemAlloc(grid.vertexCount * 3 * sizeof(float)));
		grid.texcoords = static_cast<float *>(MemAlloc(grid.vertexCount * 2 * sizeof(float)));
		grid.indices = static_cast<unsigned short *>(MemAlloc(grid.triangleCount * 3 * sizeof(unsigned short)));
		for (int y = 0; y < side; y++)
			for (int x = 0; x < side; x++)
			{
				const int i = y * side + x;
				grid.vertices[i * 3] = x / 128.f;
				grid.vertices[i * 3 + 1] = y / 128.f;
				grid.vertices[i * 3 + 2] = 0;
				grid.texcoords[i * 2] = x / 128.f;
				grid.texcoords[i * 2 + 1] = y / 128.f;
			}
		int at = 0;
		for (int y = 0; y < side - 1; y++)
			for (int x = 0; x < side - 1; x++)
			{
				const unsigned short i = static_cast<unsigned short>(y * side + x);
				for (auto n : {i, static_cast<unsigned short>(i + side), static_cast<unsigned short>(i + 1),
				               static_cast<unsigned short>(i + 1), static_cast<unsigned short>(i + side),
				               static_cast<unsigned short>(i + side + 1)})
					grid.indices[at++] = n;
			}
		UploadMesh(&grid, false);
		shadow = LoadRenderTexture(2048, 2048);
		SetTextureFilter(shadow.texture, TEXTURE_FILTER_POINT);
		auto im = GenImageColor(1, 1, WHITE);
		white = LoadTextureFromImage(im);
		UnloadImage(im);
		if (!shadow.id || !white.id || !grid.vaoId)
			throw std::runtime_error("Comic renderer resources unavailable");
	}
	MaterialData &load(const std::string &key, const Json &source, const Catalog &catalog)
	{
		auto found = materials.find(key);
		if (found != materials.end())
			return found->second;
		MaterialData data;
		data.sprite = compile_sprite(source, catalog, key);
		auto texture = [&](const std::string &asset)
		{
			return textures.load(asset, catalog.assets.at(asset));
		};
		data.albedo = texture(data.sprite.albedo);
		data.normal = data.sprite.normal.empty() ? white : texture(data.sprite.normal);
		data.depth = data.sprite.depth.empty() ? white : texture(data.sprite.depth);
		for (const auto texture : {data.albedo, data.normal, data.depth})
			SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
		for (const auto &asset : {data.sprite.albedo, data.sprite.depth})
			if (!asset.empty())
			{
				const auto &bytes = catalog.assets.at(asset);
				data.cpu.push_back(LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size())));
			}
		return materials.emplace(key, std::move(data)).first->second;
	}
	Vector3 surface(const Item &item, float x, float y, float offset = 0) const
	{
		const auto &f = *item.frame;
		Vector3 p = Vector3Add(item.anchor, Vector3Add(Vector3Scale(right, (x - f.pivot[0]) * item.height / f.rect[3]),
		                                               Vector3Scale(up, -(y - f.pivot[1]) * item.height / f.rect[3])));
		const float z = item.height * (f.depth_offset + sample_depth(*item.material, f, x, y) * f.depth_scale + offset);
		return Vector3Add(
		    p, Vector3Scale(Vector3Subtract(cameraPosition, p),
		                    z / std::max(.01f, Vector3DotProduct(Vector3Subtract(cameraPosition, p), near))));
	}
	void prepare(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera)
	{
		items.clear();
		lights.clear();
		rebase = camera.target;
		cameraPosition = Vector3Subtract(camera.position, rebase);
		near = Vector3Normalize(cameraPosition);
		right = Vector3Normalize(Vector3CrossProduct(camera.up, near));
		up = Vector3CrossProduct(near, right);
		for (const auto &body : bodies)
		{
			const auto &render = catalog.entities.at(body.definition).at("render");
			Vector3 world = {body.x / 1000.f, 0, -body.y / 1000.f};
			Vector3 p = Vector3Subtract(world, rebase);
			if (Vector3Length(p) > Vector3Length(cameraPosition) * 4 + render.at("scale").get<float>() * 20)
				continue;
			if (!render.contains("sprite"))
				continue;
			auto &data = load(body.definition, render.at("sprite"), catalog);
			const auto &f = select_frame(data.sprite, bearing(camera, world, body.heading));
			Item hull{&data, &f, p, render.at("scale").get<float>()};
			std::vector<Item> behind, front;
			if (body.forward_thrust > 0 && render.contains("engines"))
			{
				const auto &config = render.at("engines");
				auto &effect = load(body.definition + ":engines", config.at("sprite"), catalog);
				const auto ef = std::find_if(effect.sprite.frames.begin(), effect.sprite.frames.end(),
				                             [&](const auto &v)
				                             {
					                             return v.name == f.name;
				                             });
				const float thrust = std::clamp(body.forward_thrust / 1000.f, 0.f, 1.f);
				const float heading = body.heading * pi / 180000;
				Vector3 direction = {-std::cos(heading), 0, std::sin(heading)};
				for (const auto &[name, attachment] : f.attachments)
					if (name.starts_with("engine_"))
					{
						Vector3 root = surface(hull, attachment.x, attachment.y, attachment.depth_offset);
						const float bias = attachment.layer * hull.height * .001f;
						Item plume{&effect,
						           &*ef,
						           Vector3Add(root, Vector3Scale(near, bias)),
						           hull.height * config.at("scale").get<float>() * (.3f + .7f * thrust),
						           true,
						           attachment.layer};
						(attachment.layer < 0 ? behind : front).push_back(plume);
						lights.push_back({{root.x, root.y, root.z, config.at("range").get<float>()},
						                  {direction.x, direction.y, direction.z, 1},
						                  {1, .23f, .035f, config.at("power").get<float>() * thrust}});
					}
			}
			const auto order = [](const Item &a, const Item &b)
			{
				return a.layer < b.layer;
			};
			std::stable_sort(behind.begin(), behind.end(), order);
			std::stable_sort(front.begin(), front.end(), order);
			items.insert(items.end(), behind.begin(), behind.end());
			items.push_back(hull);
			items.insert(items.end(), front.begin(), front.end());
		}
	}
	void draw_item(const Item &item, bool shadowPass)
	{
		const auto &f = *item.frame;
		auto &m = *item.material;
		vec("anchor", item.anchor);
		vec("basisRight", right);
		vec("basisUp", up);
		vec("basisNear", near);
		vec("cameraPosition", cameraPosition);
		number("height", item.height);
		if (!shadowPass)
		{
			const float distance = std::max(.1f, Vector3DotProduct(Vector3Subtract(cameraPosition, item.anchor), near));
			const float pixels = item.height * focal_pixels / (perspective_draw ? distance : 1.f);
			const float ratio = std::max(1.f, f.rect[3] / std::max(1.f, pixels));
			// A power-of-two, object-wide LOD retains a stationary ink pattern
			// under changing light while avoiding subpixel hatch aliasing.
			number("paperLod", std::exp2(std::floor(std::log2(ratio))));
		}
		number("depthScale", f.depth_scale);
		number("depthOffset", f.depth_offset);
		integer("hasDepth", m.sprite.depth.empty() ? 0 : 1);
		integer("hasNormal", m.sprite.normal.empty() ? 0 : 1);
		integer("emissive", m.sprite.emissive || item.engine ? 1 : 0);
		const Vector4 rect{static_cast<float>(f.rect[0]), static_cast<float>(f.rect[1]), static_cast<float>(f.rect[2]),
		                   static_cast<float>(f.rect[3])};
		const Vector2 size{static_cast<float>(m.albedo.width), static_cast<float>(m.albedo.height)},
		    frameSize{rect.z, rect.w};
		SetShaderValue(shader, loc("crop"), &rect, SHADER_UNIFORM_VEC4);
		SetShaderValue(shader, loc("atlasSize"), &size, SHADER_UNIFORM_VEC2);
		SetShaderValue(shader, loc("frameSize"), &frameSize, SHADER_UNIFORM_VEC2);
		SetShaderValue(shader, loc("pivot"), f.pivot.data(), SHADER_UNIFORM_VEC2);
		drawMaterial.maps[MATERIAL_MAP_ALBEDO].texture = m.albedo;
		drawMaterial.maps[MATERIAL_MAP_METALNESS].texture = m.depth;
		drawMaterial.maps[MATERIAL_MAP_NORMAL].texture = m.normal;
		drawMaterial.maps[MATERIAL_MAP_ROUGHNESS].texture = shadowPass ? white : shadow.texture;
		if (item.engine)
			rlDisableDepthMask();
		DrawMesh(grid, drawMaterial, MatrixIdentity());
		if (item.engine)
			rlEnableDepthMask();
	}
};
SceneRenderer::SceneRenderer() : state_(std::make_unique<State>())
{
}
SceneRenderer::~SceneRenderer() = default;
void SceneRenderer::clear()
{
	state_->clear();
}
void SceneRenderer::explosion(const ExplosionEffect &effect)
{
	if (!std::isfinite(effect.started) || !std::isfinite(effect.duration) || effect.duration <= 0 ||
	    effect.duration > 60 || !std::isfinite(effect.intensity) || effect.intensity < 0 || effect.intensity > 16 ||
	    !std::isfinite(effect.range) || effect.range <= 0 || effect.range > 1000000 ||
	    !std::isfinite(effect.position.x) || !std::isfinite(effect.position.y) || !std::isfinite(effect.position.z))
		throw std::runtime_error("Invalid explosion presentation event");
	for (float c : effect.color)
		if (!std::isfinite(c) || c < 0 || c > 1)
			throw std::runtime_error("Invalid explosion colour");
	if (state_->effects.size() >= 16)
		state_->effects.erase(state_->effects.begin());
	state_->effects.push_back(effect);
}
void SceneRenderer::draw(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
                         const Json &lighting, double time, SceneDiagnostic diagnostic, Vector2 surface_size)
{
	auto &s = *state_;
	const auto destination = rlGetActiveFramebuffer();
	s.init();
	// Resource creation binds the default framebuffer internally. Preserve the
	// caller's preview target on the first draw as well as subsequent draws.
	rlEnableFramebuffer(destination);
	s.prepare(catalog, bodies, camera);
	const auto get = [&](const char *key, float fallback)
	{
		return lighting.value(key, fallback);
	};
	const auto vector = [&](const char *key, Vector3 fallback)
	{
		if (!lighting.contains(key))
			return fallback;
		const auto &v = lighting.at(key);
		return Vector3{v[0], v[1], v[2]};
	};
	const Vector3 sun = Vector3Normalize(vector("sun_direction", {-.6f, .6f, -.5f}));
	float radius = 10;
	Vector3 centre{};
	if (!s.items.empty())
	{
		for (const auto &item : s.items)
			centre = Vector3Add(centre, item.anchor);
		centre = Vector3Scale(centre, 1.f / s.items.size());
		for (const auto &item : s.items)
			radius =
			    std::max(radius, Vector3Distance(item.anchor, centre) + item.height * (2 + item.frame->depth_scale));
	}
	const Vector3 lightEye = Vector3Add(centre, Vector3Scale(sun, radius * 3));
	Camera3D lightCamera{lightEye, centre, std::abs(sun.y) > .95f ? Vector3{0, 0, 1} : Vector3{0, 1, 0}, radius * 2,
	                     CAMERA_ORTHOGRAPHIC};
	const auto lightView = MatrixLookAt(lightEye, centre, lightCamera.up),
	           lightProj = MatrixOrtho(-radius, radius, -radius, radius, .1, radius * 6);
	const auto lightVP = MatrixMultiply(lightView, lightProj);
	const auto framebuffer = rlGetActiveFramebuffer();
	const bool shadows = lighting.value("shadows", true) && !s.items.empty();
	if (shadows)
	{
		rlDrawRenderBatchActive();
		rlDisableScissorTest();
		rlEnableFramebuffer(s.shadow.id);
		rlViewport(0, 0, 2048, 2048);
		BeginMode3D(lightCamera);
		rlSetMatrixProjection(lightProj);
		ClearBackground(WHITE);
		s.integer("shadowPass", 1);
		s.integer("burstGraphic", 0);
		s.integer("perspective", camera.projection == CAMERA_PERSPECTIVE ? 1 : 0);
		SetShaderValueMatrix(s.shader, s.loc("viewProjection"), lightVP);
		rlDisableBackfaceCulling();
		rlDisableColorBlend();
		for (const auto &item : s.items)
			if (!item.engine && !item.material->sprite.emissive)
				s.draw_item(item, true);
		rlEnableColorBlend();
		rlEnableBackfaceCulling();
		EndMode3D();
		rlDrawRenderBatchActive();
		rlEnableFramebuffer(framebuffer);
	}
	if (surface_size.x <= 0)
		surface_size = {static_cast<float>(GetRenderWidth()), static_cast<float>(GetRenderHeight())};
	const float sx = framebuffer ? 1.f : surface_size.x / GetScreenWidth(),
	            sy = framebuffer ? 1.f : surface_size.y / GetScreenHeight();
	rlDrawRenderBatchActive();
	rlEnableScissorTest();
	rlScissor(static_cast<int>(region.x * sx), static_cast<int>((surface_size.y / sy - region.y - region.height) * sy),
	          static_cast<int>(region.width * sx), static_cast<int>(region.height * sy));
	rlViewport(static_cast<int>(region.x * sx), static_cast<int>((surface_size.y / sy - region.y - region.height) * sy),
	           static_cast<int>(region.width * sx), static_cast<int>(region.height * sy));
	camera.position = Vector3Subtract(camera.position, s.rebase);
	camera.target = Vector3Subtract(camera.target, s.rebase);
	BeginMode3D(camera);
	const float aspect = region.width / std::max(1.f, region.height);
	const auto projection =
	    camera.projection == CAMERA_PERSPECTIVE
	        ? MatrixPerspective(camera.fovy * pi / 180, aspect, rlGetCullDistanceNear(), rlGetCullDistanceFar())
	        : MatrixOrtho(-camera.fovy * aspect * .5, camera.fovy * aspect * .5, -camera.fovy * .5, camera.fovy * .5,
	                      rlGetCullDistanceNear(), rlGetCullDistanceFar());
	rlSetMatrixProjection(projection);
	const auto vp = MatrixMultiply(MatrixLookAt(camera.position, camera.target, camera.up), projection);
	s.integer("shadowPass", 0);
	s.integer("burstGraphic", 0);
	s.integer("useShadows", shadows ? 1 : 0);
	s.integer("perspective", camera.projection == CAMERA_PERSPECTIVE ? 1 : 0);
	s.integer("diagnostic", static_cast<int>(diagnostic));
	s.vec("sunDirection", sun);
	s.vec("sunColor", vector("sun_color", {1, .89f, .7f}));
	s.number("ambient", get("ambient", .14f));
	s.number("sunIntensity", get("sun_intensity", 1));
	s.number("spacing", get("hatch_spacing", 6));
	s.number("thickness", get("hatch_thickness", 2));
	s.number("ink", get("ink_strength", .9f));
	s.number("shadowBias", get("shadow_bias", .001f));
	s.number("shadowSoftness", get("shadow_softness", 1));
	s.perspective_draw = camera.projection == CAMERA_PERSPECTIVE;
	s.focal_pixels = region.height * sy / (s.perspective_draw ? 2 * std::tan(camera.fovy * pi / 360) : camera.fovy);
	SetShaderValueMatrix(s.shader, s.loc("viewProjection"), vp);
	SetShaderValueMatrix(s.shader, s.loc("lightProjection"), lightVP);
	std::erase_if(s.effects,
	              [&](const auto &e)
	              {
		              return time >= e.started + e.duration;
	              });
	for (const auto &e : s.effects)
		if (time >= e.started)
		{
			const float age = static_cast<float>(time - e.started), progress = age / e.duration;
			const float power = 2.8f * std::min(1.f, age / .1f) *
			                    std::pow(std::max(0.f, 1.f - std::max(0.f, progress - .15f) / .85f), 1.35f);
			const auto p = Vector3Subtract(e.position, s.rebase);
			s.lights.push_back(
			    {{p.x, p.y, p.z, e.range}, {0, 0, 0, -1}, {e.color[0], e.color[1], e.color[2], power * e.intensity}});
		}
	std::stable_sort(s.lights.begin(), s.lights.end(),
	                 [](const auto &a, const auto &b)
	                 {
		                 return a.position.x * a.position.x + a.position.y * a.position.y +
		                            a.position.z * a.position.z <
		                        b.position.x * b.position.x + b.position.y * b.position.y + b.position.z * b.position.z;
	                 });
	const int count = std::min(16, static_cast<int>(s.lights.size()));
	s.integer("lightCount", count);
	std::array<Vector4, 16> positions{}, cones{}, colors{};
	for (int i = 0; i < count; i++)
	{
		positions[i] = s.lights[i].position;
		cones[i] = s.lights[i].cone;
		colors[i] = s.lights[i].color;
	}
	SetShaderValueV(s.shader, s.loc("lights[0]"), positions.data(), SHADER_UNIFORM_VEC4, 16);
	SetShaderValueV(s.shader, s.loc("cones[0]"), cones.data(), SHADER_UNIFORM_VEC4, 16);
	SetShaderValueV(s.shader, s.loc("lightColors[0]"), colors.data(), SHADER_UNIFORM_VEC4, 16);
	rlDisableBackfaceCulling();
	for (const auto &item : s.items)
		s.draw_item(item, false);
	// Legacy billboards use their original path and share the same depth buffer.
	for (const auto &body : bodies)
	{
		const auto &render = catalog.entities.at(body.definition).at("render");
		if (render.contains("sprite"))
			continue;
		const Vector3 p = Vector3Subtract({body.x / 1000.f, 0, -body.y / 1000.f}, s.rebase);
		int frame = render.at("mode") == "eight"
		                ? static_cast<int>(std::floor((bearing(camera, p, body.heading) + 22.5f) / 45.f)) % 8
		                : 0;
		const auto asset = render.at("images").at(frame).get<std::string>();
		DrawBillboard(camera, s.textures.load(asset, catalog.assets.at(asset)), p, render.at("scale").get<float>(),
		              WHITE);
	}
	// The event interface is intentionally not wired to gameplay or a UI action.
	for (const auto &e : s.effects)
		if (time >= e.started)
		{
			const float progress = static_cast<float>((time - e.started) / e.duration),
			            alpha = std::pow(1 - progress, 1.35f);
			SpriteFrame f;
			f.rect = {0, 0, 1, 1};
			f.pivot = {.5f, .5f};
			MaterialData graphic;
			graphic.albedo = s.white;
			graphic.normal = s.white;
			graphic.depth = s.white;
			Item item{&graphic, &f, Vector3Subtract(e.position, s.rebase), e.range * (.08f + .3f * std::sqrt(progress)),
			          true};
			const Vector4 tint{e.color[0], e.color[1], e.color[2], alpha};
			SetShaderValue(s.shader, s.loc("burstTint"), &tint, SHADER_UNIFORM_VEC4);
			s.integer("burstGraphic", 1);
			s.draw_item(item, false);
			s.integer("burstGraphic", 0);
		}
	rlEnableBackfaceCulling();
	EndMode3D();
	rlViewport(0, 0, static_cast<int>(surface_size.x), static_cast<int>(surface_size.y));
	EndScissorMode();
}
} // namespace phoenix
