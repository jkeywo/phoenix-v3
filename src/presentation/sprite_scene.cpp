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
#include <set>
#include <tuple>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.content;
#include "environment.hpp"
#include "sprite_direction.hpp"

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
#include "nebula_shader.hpp"
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
uniform int mirrorX;
uniform int hasDepth;
uniform int perspective;
uniform int instanced;
uniform float simulationTime;
uniform vec3 chunkOrigin;
uniform sampler2D frameMetadata;
#ifdef VERTEX_STAGE
layout(location=8) in vec4 instancePosition;
layout(location=9) in vec4 instanceSpin;
layout(location=10) in vec4 instanceInfo;
flat out vec4 fieldAnchor, fieldCrop, fieldPivotDepth, fieldSpin;
vec4 meta(int n){return texelFetch(frameMetadata,ivec2(n%256,n/256),0);}
void initInstance(){
 if(instanced==0)return;
 fieldAnchor=vec4(chunkOrigin+instancePosition.xyz,instancePosition.w);
 int base=int(instanceInfo.x)*25;
 int count=int(meta(base).x);int selected=0;float best=1000.0;
 float angle=degrees(atan(-(cameraPosition.z-fieldAnchor.z),cameraPosition.x-fieldAnchor.x))-instanceInfo.y;
 for(int i=0;i<8;i++){if(i>=count)break;float az=meta(base+3+i*3).x;float delta=abs(mod(angle-az+540.0,360.0)-180.0);if(delta<best){best=delta;selected=i;}}
 fieldCrop=meta(base+1+selected*3);
 fieldPivotDepth=meta(base+2+selected*3);
 fieldSpin=instanceSpin;
 fieldSpin.z=meta(base+3+selected*3).y;
}
#else
flat in vec4 fieldAnchor, fieldCrop, fieldPivotDepth, fieldSpin;
#endif
vec4 activeCrop(){return instanced==1?fieldCrop:crop;}
vec2 activeSize(){return instanced==1?fieldCrop.zw:frameSize;}
float mirrorSign(){return (instanced==1?fieldSpin.z:float(mirrorX))>.5?-1.0:1.0;}
vec2 sourceUV(vec2 uv){return vec2(mirrorSign()<0.0?1.0-uv.x:uv.x,uv.y);}
vec2 activePivot(){vec2 p=instanced==1?fieldPivotDepth.xy:pivot;if(mirrorSign()<0.0)p.x=activeSize().x-p.x;return p;}
vec3 activeAnchor(){return instanced==1?fieldAnchor.xyz:anchor;}
float activeHeight(){return instanced==1?fieldAnchor.w:height;}
vec3 activeRight(){float a=instanced==1?fieldSpin.x+fieldSpin.y*simulationTime:0.0;return basisRight*cos(a)+basisUp*sin(a);}
vec3 activeUp(){float a=instanced==1?fieldSpin.x+fieldSpin.y*simulationTime:0.0;return basisUp*cos(a)-basisRight*sin(a);}
vec2 sampleUV(vec2 uv){vec4 c=activeCrop();return clamp((c.xy+sourceUV(uv)*c.zw)/atlasSize,(c.xy+.5)/atlasSize,(c.xy+c.zw-.5)/atlasSize);}
float reliefAt(vec2 uv){return (instanced==1?fieldPivotDepth.w:depthOffset)+(hasDepth==1?texture(depthMap,sampleUV(uv)).r-.5:0.0)*(instanced==1?fieldPivotDepth.z:depthScale);}
vec3 surfaceAt(vec2 uv){
  vec2 size=activeSize(), pv=activePivot();float h=activeHeight();
  vec3 plane=activeAnchor()+activeRight()*((uv.x*size.x-pv.x)*h/size.y)
                    -activeUp()*((uv.y*size.y-pv.y)*h/size.y);
  float z=h*reliefAt(uv);
  if(perspective==1) {
    float distance=dot(cameraPosition-plane,basisNear);
    float denominator=(distance<0.0?-1.0:1.0)*max(.01,abs(distance));
    return cameraPosition+(plane-cameraPosition)*(1.0-z/denominator);
  }
  return plane+basisNear*z;
}
)GLSL";
const char *vertex_body = R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 uv;
void main(){initInstance();uv=vertexTexCoord;gl_Position=mvp*vec4(surfaceAt(uv),1.0);}
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
uniform float itemAlpha;
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
  if(burstGraphic==2){
    vec2 q=(uv-.5)*2.0;float r=length(q);float noise=fract(sin(dot(floor(uv*24.0),vec2(12.9898,78.233)))*43758.5453);
    float edge=.72+.12*sin(atan(q.y,q.x)*7.0);if(r>edge)discard;
    finalColor=vec4(burstTint.rgb*(.55+.45*noise),burstTint.a*(1.0-smoothstep(.3,edge,r)));return;
  }
  if(burstGraphic==1){
    vec2 q=(uv-.5)*2.0;float r=length(q);float angle=atan(q.y,q.x);
    float edge=mix(.40,1.0,pow(abs(sin(angle*12.0)),5.0));
    if(r>edge)discard;
    finalColor=vec4(mix(vec3(1.0,.85,.48),burstTint.rgb,smoothstep(.05,.5,r)),burstTint.a);return;
  }
  vec4 base=texture(texture0,sampleUV(uv));base.a*=instanced==1?1.0:itemAlpha;if(base.a<.01)discard;
  if(shadowPass==1){finalColor=packDepth(pixelDepth);return;}
  if(emissive==1||diagnostic==2){finalColor=applyFog(base,cameraPosition,p);return;}
  vec3 n;
  if(hasNormal==1){vec3 raw=texture(normalMap,sampleUV(uv)).rgb*2.0-1.0;n=normalize(activeRight()*raw.x*mirrorSign()+activeUp()*raw.y+basisNear*raw.z);}
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
  vec2 paper=sourceUV(uv)*activeSize()/paperLod;
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
  finalColor=applyFog(vec4(pow(clamp(lit,0.0,1.0),vec3(1.0/2.2)),base.a),cameraPosition,p);
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
	bool colour_visible = true, shadow_visible = true;
	bool effect = false;
	bool flip_y = false;
	float alpha = 1, rotation = 0;
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
const SpriteFrame &select_frame(const SpriteMaterial &material, float angle, std::uint32_t entry = 0)
{
	return sprite_frame(material, entry, angle);
}
float sample_depth(const MaterialData &data, const SpriteFrame &f, float x, float y)
{
	if (data.cpu.size() < 2)
		return 0;
	const auto &im = data.cpu[1];
	const int sx = std::clamp(f.rect[0] + static_cast<int>(x), f.rect[0], f.rect[0] + f.rect[2] - 1),
	          sy = std::clamp(f.rect[1] + static_cast<int>(y), f.rect[1], f.rect[1] + f.rect[3] - 1);
	return GetImageColor(im, sx, sy).r / 255.f - .5f;
}
} // namespace
struct SpriteScene::State
{
	double last_simulation_time = -1, simulation_received = 0;
	NavigationEffects navigation;
	std::map<std::string, MaterialData> materials;
	std::map<std::pair<std::uint64_t, std::string>, float> directions;
	std::map<std::pair<std::uint64_t, std::string>, float> rotations;
	TextureCache textures;
	Shader shader{};
	Shader fog_shader{};
	Material fog_material{};
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
	std::map<std::pair<EntityId, std::string>, Vector3> muzzle_offsets;
	std::vector<LocalLight> lights;
	std::vector<SceneBody> combat_lines;
	struct Smoke
	{
		Vector3 position;
		double started;
	};
	std::vector<Smoke> smoke;
	std::map<std::uint64_t, std::pair<Vector3, double>> trail_heads;
	double trail_clock = -1;
	void update_smoke(std::span<const SceneBody> bodies, double simulation, bool reduced)
	{
		if (reduced || simulation < trail_clock || simulation - trail_clock > .2)
		{
			smoke.clear();
			trail_heads.clear();
			rotations.clear();
		}
		if (reduced || simulation == trail_clock)
			return;
		trail_clock = simulation;
		std::set<std::uint64_t> seen;
		for (const auto &body : bodies)
			if (body.smoke_trail)
			{
				seen.insert(body.id);
				const Vector3 world{body.x / 1000.f, body.visual_height_mm / 1000.f, -body.y / 1000.f};
				if (const auto at = trail_heads.find(body.id);
				    at != trail_heads.end() && simulation - at->second.second <= .15)
				{
					const auto distance = Vector3Distance(at->second.first, world);
					const int count = std::clamp(static_cast<int>(distance / .5f), 1, 12);
					for (int i = 0; i < count && smoke.size() < 4096; ++i)
						smoke.push_back(
						    {Vector3Lerp(at->second.first, world, static_cast<float>(i) / count), simulation});
				}
				trail_heads[body.id] = {world, simulation};
			}
		std::erase_if(trail_heads,
		              [&](const auto &head)
		              {
			              return !seen.contains(head.first);
		              });
		std::erase_if(smoke,
		              [&](const auto &p)
		              {
			              return simulation - p.started > 1.2;
		              });
	}

#include "asteroid_instances.hpp"
	~State()
	{
		clear();
	}
	void clear()
	{
		clear_instances();
		navigation.clear();
		directions.clear();
		rotations.clear();
		smoke.clear();
		trail_heads.clear();
		trail_clock = -1;
		for (auto &[id, data] : materials)
			for (auto im : data.cpu)
				UnloadImage(im);
		materials.clear();
		textures.clear();
		effects.clear();
		items.clear();
		combat_lines.clear();
		lights.clear();
		if (grid.vaoId)
			UnloadMesh(grid);
		grid = {};
		if (shader.id)
			UnloadShader(shader);
		if (fog_shader.id)
			UnloadShader(fog_shader);
		fog_shader = {};
		if (fog_material.maps)
			MemFree(fog_material.maps);
		fog_material = {};
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
		const auto vs = prefix + "#define VERTEX_STAGE\n" + surface_functions + vertex_body,
		           fs = prefix + surface_functions + fog_functions + fragment_body;
		shader = LoadShaderFromMemory(vs.c_str(), fs.c_str());
		if (!shader.id || shader.id == rlGetShaderIdDefault())
		{
			shader = {};
			throw std::runtime_error("Comic sprite shader failed to compile");
		}
		drawMaterial = LoadMaterialDefault();
		drawMaterial.shader = shader;
		fog_shader = LoadShaderFromMemory((prefix + fog_background_vertex).c_str(),
		                                  (prefix + fog_functions + fog_background_fragment).c_str());
		if (!fog_shader.id || fog_shader.id == rlGetShaderIdDefault())
			throw std::runtime_error("Nebula shader failed to compile");
		fog_material = LoadMaterialDefault();
		fog_material.shader = fog_shader;
		fog_shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(fog_shader, "texture0");
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
				data.cpu.push_back(load_asset_image(bytes));
			}
		return materials.emplace(key, std::move(data)).first->second;
	}
	Vector3 surface(const Item &item, float x, float y, float offset = 0) const
	{
		const auto &f = *item.frame;
		Vector3 p = Vector3Add(
		    item.anchor,
		    Vector3Add(Vector3Scale(right, (f.mirror_x ? -1.f : 1.f) * (x - f.pivot[0]) * item.height / f.rect[3]),
		               Vector3Scale(up, (item.flip_y ? 1.f : -1.f) * (y - f.pivot[1]) * item.height / f.rect[3])));
		const float z = item.height * (f.depth_offset + sample_depth(*item.material, f, x, y) * f.depth_scale + offset);
		const float distance = Vector3DotProduct(Vector3Subtract(cameraPosition, p), near);
		// Preserve the sign behind the eye; a positive-only denominator folds
		// rear geometry through the camera into huge foreground triangles.
		const float denominator = std::copysign(std::max(.01f, std::abs(distance)), distance);
		return Vector3Add(p, Vector3Scale(Vector3Subtract(cameraPosition, p), z / denominator));
	}
	void prepare(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera)
	{
		std::erase_if(directions,
		              [&](const auto &entry)
		              {
			              return std::none_of(bodies.begin(), bodies.end(),
			                                  [&](const auto &body)
			                                  {
				                                  return body.id == entry.first.first &&
				                                         (body.definition == entry.first.second ||
				                                          entry.first.second.starts_with(body.definition + "#"));
			                                  });
		              });
		std::erase_if(rotations,
		              [&](const auto &entry)
		              {
			              return std::none_of(bodies.begin(), bodies.end(),
			                                  [&](const auto &body)
			                                  {
				                                  return body.id == entry.first.first &&
				                                         body.definition == entry.first.second;
			                                  });
		              });
		items.clear();
		combat_lines.clear();
		lights.clear();
		rebase = camera.target;
		cameraPosition = Vector3Subtract(camera.position, rebase);
		near = Vector3Normalize(cameraPosition);
		right = Vector3Normalize(Vector3CrossProduct(camera.up, near));
		up = Vector3CrossProduct(near, right);
		muzzle_offsets.clear();
		prepare_instances(catalog, bodies);
		for (const auto &body : bodies)
		{
			if (body.field_instance || (body.definition.empty() && !body.sound.empty()))
				continue;
			if (body.line_style)
			{
				combat_lines.push_back(body);
				if (body.line_style != 2)
					continue;
			}
			const auto &render = catalog.entities.at(body.definition).at("render");
			Vector3 world = {body.x / 1000.f, body.visual_height_mm / 1000.f, -body.y / 1000.f};
			Vector3 p = Vector3Subtract(world, rebase);
			if (const auto muzzle = muzzle_offsets.find({body.attack_source, body.attack_weapon});
			    muzzle != muzzle_offsets.end())
				p = Vector3Add(p, Vector3Scale(muzzle->second, body.muzzle_blend));
			if (Vector3Distance(p, cameraPosition) > rlGetCullDistanceFar() + render.at("scale").get<float>() * 20)
				continue;
			if (!render.contains("sprite"))
			{
				const auto count = render.at("images").size();
				const auto angle = bearing(camera, world, body.heading);
				int index = count == 8 ? static_cast<int>(std::floor((angle + 22.5f) / 45.f)) % 8 : 0;
				if (count == 8)
				{
					auto [prev, added] = directions.try_emplace(std::pair{body.id, body.definition}, index * 45.f);
					if (!added && retain_sprite_direction(angle, prev->second, index * 45.f))
						index = static_cast<int>(prev->second / 45.f);
					prev->second = index * 45.f;
				}
				for (std::size_t i = 0; i < count; ++i)
				{
					const auto key = body.definition + ":legacy:" + std::to_string(i);
					if (materials.contains(key))
						continue;
					MaterialData d;
					const auto asset = render.at("images").at(i).get<std::string>();
					d.albedo = textures.load(asset, catalog.assets.at(asset));
					d.normal = white;
					d.depth = white;
					SpriteFrame f;
					f.rect = {0, 0, d.albedo.width, d.albedo.height};
					f.pivot = {d.albedo.width * .5f, d.albedo.height * .5f};
					d.sprite.emissive = true;
					d.sprite.frames.push_back(f);
					materials.emplace(key, std::move(d));
				}
				auto &d = materials.at(body.definition + ":legacy:" + std::to_string(index));
				items.push_back({&d, &d.sprite.frames.front(), p, render.at("scale").get<float>() * body.visual_scale});
				items.back().alpha = body.alpha;
				continue;
			}
			auto &data = load(body.definition, render.at("sprite"), catalog);
			const float angle = bearing(camera, world, body.heading);
			const auto *selected = &select_frame(data.sprite, angle, body.sprite_entry);
			const auto &frames =
			    data.sprite.entries.empty() ? data.sprite.frames : data.sprite.entries.at(body.sprite_entry).frames;
			if (frames.size() == 8)
			{
				auto [previous, added] = directions.try_emplace(std::pair{body.id, body.definition}, selected->azimuth);
				if (!added && retain_sprite_direction(angle, previous->second, selected->azimuth))
					selected = &select_frame(data.sprite, previous->second, body.sprite_entry);
				previous->second = selected->azimuth;
			}
			const auto &f = *selected;
			Item hull{&data, &f, p, render.at("scale").get<float>() * body.visual_scale};
			hull.alpha = body.alpha;
			if (body.velocity_rotation)
			{
				const Vector3 velocity{body.vx / 1000.f, body.visual_vz / 1000.f, -body.vy / 1000.f};
				const auto dx = Vector3DotProduct(velocity, right), dy = Vector3DotProduct(velocity, up);
				auto &rotation = rotations[std::pair{body.id, body.definition}];
				rotation = velocity_billboard_rotation(dx, dy, f.forward[0], f.forward[1], rotation);
				hull.rotation = rotation;
			}
			if (!body.sound.empty())
			{
				hull.effect = true;
				hull.engine = true;
				hull.shadow_visible = false;
				hull.anchor = Vector3Add(hull.anchor, Vector3Scale(near, 1.f));
			}
			const auto record_muzzles = [&](const Item &item, const std::string &turret)
			{
				for (const auto &muzzle : body.muzzles)
					if (muzzle.turret == turret)
						if (const auto marker = item.frame->attachments.find(muzzle.marker);
						    marker != item.frame->attachments.end())
						{
							const auto &point = marker->second;
							const auto visual = surface(item, point.x, point.y, point.depth_offset);
							const auto physical =
							    Vector3Subtract(Vector3{muzzle.x / 1000.f, 0, -muzzle.y / 1000.f}, rebase);
							muzzle_offsets[{body.id, muzzle.weapon}] = Vector3Subtract(visual, physical);
						}
			};
			record_muzzles(hull, {});
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
				if (ef == effect.sprite.frames.end())
					throw std::runtime_error("Engine sprite has no matching entry frame");
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
			for (const auto &attachment : body.attachments)
			{
				const auto mount = f.attachments.find(attachment.marker);
				if (mount == f.attachments.end())
					continue;
				const auto &turret_render = catalog.entities.at(attachment.definition).at("render");
				auto &turret = load(attachment.definition, turret_render.at("sprite"), catalog);
				const auto turret_angle = bearing(camera, world, attachment.heading);
				const auto *frame = &select_frame(turret.sprite, turret_angle, 0);
				auto [previous, added] =
				    directions.try_emplace(std::pair{body.id, body.definition + "#" + attachment.id}, frame->azimuth);
				if (!added && retain_sprite_direction(turret_angle, previous->second, frame->azimuth))
					frame = &select_frame(turret.sprite, previous->second, 0);
				previous->second = frame->azimuth;
				const auto &point = mount->second;
				Item mounted{&turret,
				             frame,
				             Vector3Add(surface(hull, point.x, point.y, point.depth_offset),
				                        Vector3Scale(near, point.layer * hull.height * .001f)),
				             hull.height * attachment.scale * frame->rect[3] / f.rect[3],
				             false,
				             point.layer};
				mounted.flip_y = attachment.flip_y;
				record_muzzles(mounted, attachment.id);
				(point.layer < 0 ? behind : front).push_back(mounted);
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
		std::stable_partition(items.begin(), items.end(),
		                      [](const auto &item)
		                      {
			                      return !item.effect;
		                      });
	}
	void draw_item(const Item &item, bool shadowPass)
	{
		integer("instanced", 0);
		number("itemAlpha", item.alpha);
		const auto &f = *item.frame;
		auto &m = *item.material;
		vec("anchor", item.anchor);
		const auto rotated_right =
		    Vector3Add(Vector3Scale(right, std::cos(item.rotation)), Vector3Scale(up, std::sin(item.rotation)));
		const auto rotated_up =
		    Vector3Subtract(Vector3Scale(up, std::cos(item.rotation)), Vector3Scale(right, std::sin(item.rotation)));
		vec("basisRight", rotated_right);
		vec("basisUp", item.flip_y ? Vector3Negate(rotated_up) : rotated_up);
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
		integer("mirrorX", f.mirror_x ? 1 : 0);
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
	void fog_uniforms(Shader target, const Json &environment)
	{
		set_scene_fog(target, environment, rebase);
	}
};
SpriteScene::SpriteScene() : state_(std::make_unique<State>())
{
}
SpriteScene::~SpriteScene() = default;
void SpriteScene::clear()
{
	state_->clear();
}
Json SpriteScene::instance_statistics() const
{
	return {{"uploads", state_->instance_uploads},
	        {"records_uploaded", state_->instance_records_uploaded},
	        {"bytes_uploaded", state_->instance_records_uploaded * 12 * sizeof(float)},
	        {"allocations", state_->instance_allocations},
	        {"visible_batches", std::count_if(state_->instance_batches.begin(), state_->instance_batches.end(),
	                                          [](const auto &b)
	                                          {
		                                          return b.second.colour_visible;
	                                          })},
	        {"shadow_coverage", state_->shadow_coverage},
	        {"batches", state_->instance_batches.size()}};
}
SpriteMaterial SpriteScene::prepared_sprite(const Catalog &catalog, const std::string &definition)
{
	return state_->load(definition, catalog.entities.at(definition).at("render").at("sprite"), catalog).sprite;
}
void SpriteScene::preload_environment(const Catalog &catalog, const SceneEnvironment &environment)
{
	state_->init();
	if (!environment.skybox.empty())
		state_->textures.load(environment.skybox, catalog.assets.at(environment.skybox));
	const Camera3D camera{{0, 10, 10}, {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
	state_->navigation.draw(catalog, environment, camera, {}, {0, 0, 1, 1});
}
void SpriteScene::preload(const Catalog &catalog, const std::string &definition)
{
	auto &s = *state_;
	s.init();
	const auto &render = catalog.entities.at(definition).at("render");
	if (render.contains("sprite"))
	{
		s.load(definition, render.at("sprite"), catalog);
		if (render.contains("engines"))
			s.load(definition + ":engines", render.at("engines").at("sprite"), catalog);
	}
	else
		for (const auto &asset : render.at("images"))
		{
			const auto name = asset.get<std::string>();
			s.textures.load(name, catalog.assets.at(name));
		}
}
std::optional<AttachmentPose> SpriteScene::attachment(const Catalog &catalog, const SceneBody &body,
                                                      const std::string &name, Camera3D camera)
{
	const auto &render = catalog.entities.at(body.definition).at("render");
	if (!render.contains("sprite"))
		return {};
	auto &s = *state_;
	s.init();
	s.rebase = camera.target;
	s.cameraPosition = Vector3Subtract(camera.position, s.rebase);
	s.near = Vector3Normalize(s.cameraPosition);
	s.right = Vector3Normalize(Vector3CrossProduct(camera.up, s.near));
	s.up = Vector3CrossProduct(s.near, s.right);
	const Vector3 world{body.x / 1000.f, 0, -body.y / 1000.f};
	auto &material = s.load(body.definition, render.at("sprite"), catalog);
	const auto &frame = select_frame(material.sprite, bearing(camera, world, body.heading), body.sprite_entry);
	if (!frame.attachments.contains(name))
		return {};
	const auto &point = frame.attachments.at(name);
	const Item item{&material, &frame, Vector3Subtract(world, s.rebase), render.at("scale")};
	const auto position = Vector3Add(s.surface(item, point.x, point.y, point.depth_offset), s.rebase);
	const float h = body.heading * DEG2RAD / 1000.f, sign = name.starts_with("engine_") ? -1.f : 1.f;
	return AttachmentPose{position, {sign * std::cos(h), 0, -sign * std::sin(h)}};
}
void SpriteScene::explosion(const ExplosionEffect &effect)
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
void SpriteScene::draw(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
                       const Json &lighting, double time, SceneDiagnostic diagnostic, Vector2 surface_size,
                       const SceneEnvironment &environment)
{
	auto &s = *state_;
	const auto destination = rlGetActiveFramebuffer();
	s.init();
	// Resource creation binds the default framebuffer internally. Preserve the
	// caller's preview target on the first draw as well as subsequent draws.
	rlEnableFramebuffer(destination);
	s.update_smoke(bodies, lighting.value("environment", Json::object()).value("simulation_time", time),
	               environment.reduced_motion);
	s.prepare(catalog, bodies, camera);
	s.cull_instances(camera, region);
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
			if (item.colour_visible)
				centre = Vector3Add(centre, item.anchor);
		const auto receivers = std::count_if(s.items.begin(), s.items.end(),
		                                     [](const auto &item)
		                                     {
			                                     return item.colour_visible;
		                                     });
		if (receivers)
			centre = Vector3Scale(centre, 1.f / receivers);
		for (const auto &item : s.items)
			if (item.colour_visible)
				radius = std::max(radius, Vector3Distance(item.anchor, centre) + s.item_extent(item));
	}
	for (const auto &[key, batch] : s.instance_batches)
		if (batch.colour_visible)
		{
			const auto origin = Vector3Subtract(batch.origin, s.rebase);
			const auto mid = Vector3Add(origin, Vector3Scale(Vector3Add(batch.minimum, batch.maximum), .5f));
			radius =
			    std::max(radius, Vector3Distance(mid, centre) + Vector3Distance(batch.minimum, batch.maximum) * .5f);
		}
	s.shadow_coverage = radius * 2;
	const float horizon = static_cast<float>(rlGetCullDistanceFar());
	const auto lightUp = std::abs(sun.y) > .95f ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
	const auto lightRight = Vector3Normalize(Vector3CrossProduct(lightUp, sun)),
	           lightVertical = Vector3CrossProduct(sun, lightRight);
	float along_min = -radius, along_max = radius;
	auto caster = [&](Vector3 midpoint, float extent)
	{
		const auto delta = Vector3Subtract(midpoint, centre);
		if (Vector3Distance(midpoint, s.cameraPosition) > horizon + extent ||
		    std::abs(Vector3DotProduct(delta, lightRight)) > radius + extent ||
		    std::abs(Vector3DotProduct(delta, lightVertical)) > radius + extent)
			return false;
		const auto along = Vector3DotProduct(delta, sun);
		along_min = std::min(along_min, along - extent);
		along_max = std::max(along_max, along + extent);
		return true;
	};
	for (auto &item : s.items)
		item.shadow_visible = caster(item.anchor, s.item_extent(item));
	for (auto &[key, batch] : s.instance_batches)
	{
		const auto mid = Vector3Add(Vector3Subtract(batch.origin, s.rebase),
		                            Vector3Scale(Vector3Add(batch.minimum, batch.maximum), .5f));
		batch.shadow_visible = caster(mid, Vector3Distance(batch.minimum, batch.maximum) * .5f);
	}
	const float depth_span = along_max - along_min, padding = std::max(1.f, depth_span * .05f),
	            shadow_depth = depth_span + padding * 2;
	const Vector3 lightEye = Vector3Add(centre, Vector3Scale(sun, along_max + padding));
	Camera3D lightCamera{lightEye, centre, lightUp, radius * 2, CAMERA_ORTHOGRAPHIC};
	const auto lightView = MatrixLookAt(lightEye, centre, lightCamera.up),
	           lightProj = MatrixOrtho(-radius, radius, -radius, radius, .1, shadow_depth);
	const auto lightVP = MatrixMultiply(lightView, lightProj);
	const auto framebuffer = rlGetActiveFramebuffer();
	const bool shadows = lighting.value("shadows", true) && (!s.items.empty() || !s.instance_batches.empty());
	const auto clock = lighting.value("environment", Json::object());
	const double simulation = clock.value("simulation_time", time), step = clock.value("simulation_step", 0.0);
	if (simulation != s.last_simulation_time)
	{
		s.simulation_received =
		    (s.last_simulation_time < 0 || simulation < s.last_simulation_time) ? time - step : time;
		s.last_simulation_time = simulation;
	}
	s.number("simulationTime", static_cast<float>(simulation - std::max(0.0, step - (time - s.simulation_received))));
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
			if (item.shadow_visible && !item.engine && !item.material->sprite.emissive)
				s.draw_item(item, true);
		s.draw_instances(true);
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
	s.navigation.background(catalog, environment, camera);
	const auto fog_environment = lighting.value("environment", Json::object());
	s.fog_uniforms(s.shader, fog_environment);
	if (!fog_environment.value("nebulae", Json::array()).empty())
	{
		const int has_skybox = 0;
		SetShaderValue(s.fog_shader, GetShaderLocation(s.fog_shader, "hasSkybox"), &has_skybox, SHADER_UNIFORM_INT);
		if (has_skybox)
		{
			const auto texture = s.textures.load(environment.skybox, catalog.assets.at(environment.skybox));
			SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
			SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
			s.fog_material.maps[MATERIAL_MAP_ALBEDO].texture = texture;
		}
		else
			s.fog_material.maps[MATERIAL_MAP_ALBEDO].texture = s.white;
		s.fog_uniforms(s.fog_shader, fog_environment);
		SetShaderValueMatrix(s.fog_shader, GetShaderLocation(s.fog_shader, "inverseViewProjection"), MatrixInvert(vp));
		rlDisableDepthMask();
		rlDisableBackfaceCulling();
		DrawMesh(s.grid, s.fog_material, MatrixIdentity());
		rlEnableDepthMask();
	}
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
	s.number("shadowBias", get("shadow_bias", .001f) * radius * 6 / shadow_depth);
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
		if (item.colour_visible)
			s.draw_item(item, false);
	s.draw_instances(false);
	for (const auto &line : s.combat_lines)
	{
		auto start =
		    Vector3Subtract(Vector3{line.x / 1000.f, line.visual_height_mm / 1000.f, -line.y / 1000.f}, s.rebase);
		auto end = Vector3Subtract(
		    Vector3{line.line_end_x / 1000.f, line.line_end_z / 1000.f, -line.line_end_y / 1000.f}, s.rebase);
		if (const auto muzzle = s.muzzle_offsets.find({line.attack_source, line.attack_weapon});
		    muzzle != s.muzzle_offsets.end())
		{
			start = Vector3Add(start, Vector3Scale(muzzle->second, line.muzzle_blend));
			end = Vector3Add(end, Vector3Scale(muzzle->second, line.muzzle_tail_blend));
		}
		if (line.line_style == 3)
		{
			if (!environment.reduced_motion)
			{
				const auto segments = std::max(2, static_cast<int>(line.rotation_mdeg / 15000));
				const auto radius = line.visual_scale * .55f;
				for (float latitude : {-.45f, 0.f, .45f})
					for (int i = 0; i < segments; ++i)
					{
						const auto a = (line.heading - line.rotation_mdeg / 2 + line.rotation_mdeg * i / segments) /
						               1000.f * pi / 180;
						const auto b =
						    (line.heading - line.rotation_mdeg / 2 + line.rotation_mdeg * (i + 1) / segments) / 1000.f *
						    pi / 180;
						const Vector3 from{start.x + radius * std::cos(latitude) * std::cos(a),
						                   start.y + radius * std::sin(latitude),
						                   start.z - radius * std::cos(latitude) * std::sin(a)};
						const Vector3 to{start.x + radius * std::cos(latitude) * std::cos(b),
						                 start.y + radius * std::sin(latitude),
						                 start.z - radius * std::cos(latitude) * std::sin(b)};
						DrawLine3D(from, to, Color{95, 205, 255, static_cast<unsigned char>(line.alpha * 180)});
					}
			}
		}
		else if (Vector3Distance(start, end) > .001f)
		{
			DrawCylinderEx(start, end, line.line_style == 1 ? .10f : .035f, line.line_style == 1 ? .10f : .035f, 5,
			               line.line_style == 1 ? Color{255, 95, 65, 220} : Color{255, 220, 125, 255});
			if (line.line_style == 1)
				DrawCylinderEx(start, end, .025f, .025f, 5, Color{255, 245, 210, 255});
		}
	}
	for (const auto &p : s.smoke)
	{
		const float age = static_cast<float>(s.trail_clock - p.started);
		SpriteFrame frame;
		frame.rect = {0, 0, 1, 1};
		frame.pivot = {.5f, .5f};
		MaterialData material;
		material.albedo = s.white;
		material.normal = s.white;
		material.depth = s.white;
		Item puff{&material, &frame, Vector3Subtract(Vector3Add(p.position, {0, age * .15f, 0}), s.rebase),
		          .4f + age * 1.8f, true};
		const Vector4 tint{.28f, .30f, .32f, .6f * (1 - age / 1.2f)};
		SetShaderValue(s.shader, s.loc("burstTint"), &tint, SHADER_UNIFORM_VEC4);
		s.integer("burstGraphic", 2);
		s.draw_item(puff, false);
		s.integer("burstGraphic", 0);
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
	if (diagnostic == SceneDiagnostic::comic || diagnostic == SceneDiagnostic::smooth ||
	    diagnostic == SceneDiagnostic::unlit)
		s.navigation.draw(catalog, environment, camera, s.rebase,
		                  {region.x * sx, (surface_size.y / sy - region.y - region.height) * sy, region.width * sx,
		                   region.height * sy});
	rlEnableBackfaceCulling();
	EndMode3D();
	rlViewport(0, 0, static_cast<int>(surface_size.x), static_cast<int>(surface_size.y));
	EndScissorMode();
}
} // namespace phoenix
