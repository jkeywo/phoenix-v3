module;
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.presentation;
#include "environment.hpp"
namespace phoenix
{
namespace
{
#include "nebula_shader.hpp"
#ifdef __EMSCRIPTEN__
const std::string model_prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const std::string model_prefix = "#version 330\n";
#endif
const char *model_vertex = R"GLSL(
in vec3 vertexPosition; in vec3 vertexNormal; in vec2 vertexTexCoord;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec3 world; out vec3 normal; out vec3 localPosition; out vec2 uv;
void main(){localPosition=vertexPosition;world=(matModel*vec4(vertexPosition,1)).xyz;normal=normalize((matNormal*vec4(vertexNormal,0)).xyz);uv=vertexTexCoord;gl_Position=mvp*vec4(vertexPosition,1);}
)GLSL";
const char *model_fragment = R"GLSL(
in vec3 world; in vec3 normal; in vec3 localPosition; in vec2 uv;out vec4 finalColor;
uniform sampler2D texture0;uniform sampler2D emissionMap;uniform sampler2D shadowMap;
uniform vec4 colDiffuse;uniform vec3 emission;uniform vec3 sun;uniform vec3 sunColour;uniform float ambient;uniform float sunIntensity;
uniform mat4 lightVP;uniform int shadowPass;uniform int shadows;uniform int effect;
uniform int lightCount;uniform vec4 lights[16];uniform vec4 cones[16];uniform float time;
uniform vec3 cameraPosition;
uniform vec3 surfaceColour;uniform vec3 hotColour;uniform vec3 cellColour;uniform vec3 haloColour;
vec4 packDepth(float d){vec4 v=fract(d*vec4(16777216.,65536.,256.,1.));v-=v.xxyz*vec4(0.,1./256.,1./256.,1./256.);return v;}
float unpackDepth(vec4 v){return dot(v,vec4(1./16777216.,1./65536.,1./256.,1.));}
float visibility(){if(shadows==0)return 1.;vec4 q=lightVP*vec4(world,1);vec3 s=q.xyz/q.w*.5+.5;if(any(lessThan(s,vec3(0)))||any(greaterThan(s,vec3(1))))return 1.;float v=0.;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)v+=s.z-.001<=unpackDepth(texture(shadowMap,s.xy+vec2(x,y)/2048.))?1.:0.;return v/9.;}
float hash3(vec3 p){return fract(sin(dot(p,vec3(510.1,741.9,445.4)))*43758.5453);}
float stellarNoise(vec3 p){vec3 i=floor(p),f=fract(p);vec3 u=f*f*(3.-2.*f);return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),u.x),mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),u.x),u.y),mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),u.x),mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),u.x),u.y),u.z);}
float fbm(vec3 p){float v=0.,a=.5;for(int i=0;i<4;i++){v+=a*stellarNoise(p);p*=2.03;a*=.52;}return v;}
void main(){
 vec4 base=texture(texture0,uv)*colDiffuse;if(base.a<.15)discard;
 if(shadowPass==1){finalColor=packDepth(gl_FragCoord.z);return;}
 if(effect==1){vec3 n=normalize(localPosition);float cells=fbm(n*4.2+vec3(0,time*.055,time*.035));float shimmer=fbm(n*18.+vec3(time*.12,-time*.08,time*.06));vec3 c=mix(surfaceColour,cellColour,smoothstep(.25,.72,1.-cells)*.55);c=mix(c,hotColour,smoothstep(.62,.92,cells+shimmer*.18));finalColor=applyFog(vec4(c*(1.15+shimmer*.18),1),cameraPosition,world);return;}
 if(effect==2){float d=length(uv*2.-1.);if(d>1.)discard;float corona=pow(max(1.-d,0.),1.7);float core=1.-smoothstep(.08,1.,d);finalColor=vec4(haloColour*(1.+corona*1.2),.55*(.92+.08*sin(time*1.2))*max(core*.32,corona));finalColor=applyFog(finalColor,cameraPosition,world);return;}
 if(effect==3){finalColor=applyFog(vec4(1.,.45,.12,.75),cameraPosition,world);return;}
 vec3 n=normalize(normal);vec3 light=vec3(ambient)+sunColour*max(dot(n,sun),0.)*sunIntensity*visibility();
 for(int i=0;i<16;i++){if(i>=lightCount)break;vec3 delta=lights[i].xyz-world;float d=length(delta);vec3 toward=delta/max(.001,d);float cone=smoothstep(.5,.82,dot(-toward,cones[i].xyz));float power=cone*max(dot(n,toward),0.)*cones[i].w/(1.+d*d/(lights[i].w*lights[i].w));light+=vec3(1.,.23,.035)*power;}
 finalColor=vec4(pow(clamp(pow(base.rgb,vec3(2.2))*light+texture(emissionMap,uv).rgb*emission,0.,1.),vec3(1./2.2)),base.a);
 finalColor=applyFog(finalColor,cameraPosition,world);
}
)GLSL";
struct GpuPart
{
	Mesh mesh{};
	Texture2D albedo{}, emissive{};
	std::array<float, 4> tint{1, 1, 1, 1};
	std::array<float, 3> emission{};
};
struct DrawModel
{
	std::string id;
	Matrix matrix;
	const Json *render;
	const SceneBody *body;
};
Matrix local_matrix(const Json &render)
{
	auto v = [](const Json &j, const char *key, Vector3 fallback)
	{
		if (!j.contains(key))
			return fallback;
		const auto &x = j.at(key);
		return Vector3{x[0], x[1], x[2]};
	};
	if (render.contains("star"))
	{
		const float radius = render.at("scale").get<float>() * .5f;
		return MatrixScale(radius, radius, radius);
	}
	const auto m = render.value("model", Json::object());
	const auto s = v(m, "scale", {1, 1, 1}), r = v(m, "rotation", {}), o = v(m, "offset", {});
	const float height = render.at("scale");
	return MatrixMultiply(MatrixMultiply(MatrixScale(s.x * height, s.y * height, s.z * height),
	                                     MatrixRotateXYZ(Vector3Scale(r, DEG2RAD))),
	                      MatrixTranslate(o.x * height, o.y * height, o.z * height));
}
} // namespace
struct ModelScene::State
{
	bool shadow_pass = false;
	Shader shader{};
	RenderTexture2D shadow{};
	Texture2D white{};
	Mesh sphere{}, quad{}, cone{};
	std::map<std::string, std::vector<GpuPart>> cache;
	NavigationEffects navigation;
	~State()
	{
		clear();
	}
	void clear()
	{
		for (auto &[id, parts] : cache)
			for (auto &p : parts)
			{
				UnloadMesh(p.mesh);
				if (p.albedo.id)
					UnloadTexture(p.albedo);
				if (p.emissive.id)
					UnloadTexture(p.emissive);
			}
		cache.clear();
		navigation.clear();
		if (shader.id)
			UnloadShader(shader);
		if (shadow.id)
			UnloadRenderTexture(shadow);
		if (white.id)
			UnloadTexture(white);
		if (sphere.vaoId)
			UnloadMesh(sphere);
		if (quad.vaoId)
			UnloadMesh(quad);
		if (cone.vaoId)
			UnloadMesh(cone);
		shader = {};
		shadow = {};
		white = {};
		sphere = {};
		quad = {};
		cone = {};
	}
	int loc(const char *name)
	{
		return GetShaderLocation(shader, name);
	}
	void integer(const char *name, int n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_INT);
	}
	void number(const char *name, float n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_FLOAT);
	}
	void vec(const char *name, Vector3 n)
	{
		SetShaderValue(shader, loc(name), &n, SHADER_UNIFORM_VEC3);
	}
	void init()
	{
		if (shader.id)
			return;
		const auto framebuffer = rlGetActiveFramebuffer();
		shader = LoadShaderFromMemory((model_prefix + model_vertex).c_str(),
		                              (model_prefix + fog_functions + model_fragment).c_str());
		if (!IsShaderValid(shader) || shader.id == rlGetShaderIdDefault())
			throw std::runtime_error("Model shader unavailable");
		shader.locs[SHADER_LOC_MAP_ALBEDO] = loc("texture0");
		shader.locs[SHADER_LOC_MAP_EMISSION] = loc("emissionMap");
		shader.locs[SHADER_LOC_MAP_ROUGHNESS] = loc("shadowMap");
		auto image = GenImageColor(1, 1, WHITE);
		white = LoadTextureFromImage(image);
		UnloadImage(image);
		shadow = LoadRenderTexture(2048, 2048);
		SetTextureFilter(shadow.texture, TEXTURE_FILTER_POINT);
		sphere = GenMeshSphere(1, 32, 64);
		quad = GenMeshPlane(2, 2, 1, 1);
		cone = GenMeshCone(.15f, 1, 12);
		rlEnableFramebuffer(framebuffer);
	}
	void load(const Catalog &catalog, const Json &render)
	{
		if (!render.contains("model"))
			return;
		const auto id = render.at("model").at("asset").get<std::string>();
		const auto key = sha256(catalog.assets.at(id));
		if (cache.contains(key))
			return;
		const auto model = compile_model(catalog.assets.at(id));
		std::vector<GpuPart> parts;
		auto texture = [](const Bytes &bytes)
		{
			if (bytes.empty())
				return Texture2D{};
			auto image = load_asset_image(bytes);
			auto t = LoadTextureFromImage(image);
			UnloadImage(image);
			SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
			return t;
		};
		for (const auto &cpu : model.parts)
		{
			GpuPart p;
			auto alloc = [](const std::vector<float> &v)
			{
				auto *p = static_cast<float *>(MemAlloc(static_cast<unsigned>(v.size() * sizeof(float))));
				std::memcpy(p, v.data(), v.size() * sizeof(float));
				return p;
			};
			p.mesh.vertexCount = static_cast<int>(cpu.positions.size() / 3);
			p.mesh.triangleCount = p.mesh.vertexCount / 3;
			p.mesh.vertices = alloc(cpu.positions);
			p.mesh.normals = alloc(cpu.normals);
			p.mesh.texcoords = alloc(cpu.texcoords);
			UploadMesh(&p.mesh, false);
			p.albedo = texture(cpu.albedo);
			p.emissive = texture(cpu.emissive);
			p.tint = cpu.tint;
			p.emission = cpu.emission;
			parts.push_back(p);
		}
		cache.emplace(key, std::move(parts));
	}
	void mesh(Mesh mesh, Matrix matrix, Texture2D albedo, Texture2D emission, std::array<float, 4> tint,
	          std::array<float, 3> glow)
	{
		auto material = LoadMaterialDefault();
		material.shader = shader;
		material.maps[MATERIAL_MAP_ALBEDO].texture = albedo.id ? albedo : white;
		material.maps[MATERIAL_MAP_EMISSION].texture = emission.id ? emission : white;
		material.maps[MATERIAL_MAP_ROUGHNESS].texture = shadow_pass ? white : shadow.texture;
		material.maps[MATERIAL_MAP_ALBEDO].color = {static_cast<unsigned char>(std::clamp(tint[0], 0.f, 1.f) * 255),
		                                            static_cast<unsigned char>(std::clamp(tint[1], 0.f, 1.f) * 255),
		                                            static_cast<unsigned char>(std::clamp(tint[2], 0.f, 1.f) * 255),
		                                            static_cast<unsigned char>(std::clamp(tint[3], 0.f, 1.f) * 255)};
		vec("emission", {glow[0], glow[1], glow[2]});
		DrawMesh(mesh, material, matrix);
		MemFree(material.maps);
	}
	void body(const Catalog &catalog, const DrawModel &item, bool shadowPass, double time)
	{
		const auto &r = *item.render;
		if (r.contains("star"))
		{
			if (shadowPass)
				return;
			const auto &star = r.at("star");
			auto colour = [&](const char *key, Vector3 fallback)
			{
				if (star.contains(key))
				{
					auto &c = star.at(key);
					return Vector3{c[0], c[1], c[2]};
				}
				return fallback;
			};
			integer("effect", 1);
			number("time", static_cast<float>(time) * star.value("animation_speed", 1.f));
			vec("surfaceColour", colour("surface_colour", {1, .72f, .12f}));
			vec("hotColour", colour("hot_colour", {1, .96f, .65f}));
			vec("cellColour", colour("cell_colour", {.95f, .32f, .04f}));
			mesh(sphere, item.matrix, {}, {}, {1, 1, 1, 1}, {});
			return;
		}
		integer("effect", 0);
		const auto ref = r.at("model").at("asset").get<std::string>();
		for (auto &p : cache.at(sha256(catalog.assets.at(ref))))
			mesh(p.mesh, item.matrix, p.albedo, p.emissive, p.tint, p.emission);
	}
};
ModelScene::ModelScene() : state_(std::make_unique<State>())
{
}
ModelScene::~ModelScene() = default;
void ModelScene::clear()
{
	state_->clear();
}
void ModelScene::preload(const Catalog &c, const std::string &id)
{
	if (c.entities.at(id).at("render").value("contact_only", false))
		return;
	state_->init();
	state_->load(c, c.entities.at(id).at("render"));
}
std::optional<AttachmentPose> ModelScene::attachment(const Catalog &catalog, const SceneBody &body,
                                                     const std::string &name, Camera3D)
{
	const auto &r = catalog.entities.at(body.definition).at("render");
	if (!r.contains("attachments") || !r.at("attachments").contains(name))
		return {};
	const auto &a = r.at("attachments").at(name);
	const auto &p = a.at("position"), &d = a.at("direction");
	auto transform =
	    MatrixMultiply(local_matrix(r), MatrixMultiply(MatrixRotateY(body.heading * DEG2RAD / 1000.f - PI / 2),
	                                                   MatrixTranslate(body.x / 1000.f, 0, -body.y / 1000.f)));
	const auto root = Vector3Transform({p[0], p[1], p[2]}, transform),
	           tip = Vector3Transform({p[0].get<float>() + d[0].get<float>(), p[1].get<float>() + d[1].get<float>(),
	                                   p[2].get<float>() + d[2].get<float>()},
	                                  transform);
	return AttachmentPose{root, Vector3Normalize(Vector3Subtract(tip, root))};
}
void ModelScene::draw(const Catalog &catalog, std::span<const SceneBody> bodies, Camera3D camera, Rectangle region,
                      const Json &lighting, double time, Vector2 size, const SceneEnvironment &environment)
{
	auto &s = *state_;
	s.init();
	const auto framebuffer = rlGetActiveFramebuffer();
	const auto rebase = camera.target;
	camera.position = Vector3Subtract(camera.position, rebase);
	camera.target = {};
	std::vector<DrawModel> items;
	std::array<Vector4, 16> lights{}, cones{};
	int lightCount = 0;
	float radius = 20;
	for (const auto &body : bodies)
	{
		const auto &r = catalog.entities.at(body.definition).at("render");
		s.load(catalog, r);
		const Vector3 pos{body.x / 1000.f - rebase.x, -rebase.y, -body.y / 1000.f - rebase.z};
		if (Vector3Distance(pos, camera.position) > 10000)
			continue;
		const auto heading = MatrixRotateY(body.heading * DEG2RAD / 1000.f - PI / 2);
		const auto world = MatrixMultiply(heading, MatrixTranslate(pos.x, pos.y, pos.z));
		const float scale = r.at("scale");
		auto local = local_matrix(r);
		auto transform = MatrixMultiply(local, world);
		items.push_back({body.definition, transform, &r, &body});
		radius = std::max(radius, Vector3Length(pos) + scale * 2);
		const auto attachments = r.value("attachments", Json::object());
		if (body.forward_thrust > 0)
			for (const auto &[name, a] : attachments.items())
				if (name.starts_with("engine_") && lightCount < 16)
				{
					const auto &p = a.at("position"), &d = a.at("direction");
					auto root = Vector3Transform({p[0], p[1], p[2]}, transform);
					auto tip =
					    Vector3Transform({p[0].get<float>() + d[0].get<float>(), p[1].get<float>() + d[1].get<float>(),
					                      p[2].get<float>() + d[2].get<float>()},
					                     transform);
					auto dir = Vector3Normalize(Vector3Subtract(tip, root));
					lights[lightCount] = {root.x, root.y, root.z, scale * 5};
					cones[lightCount] = {dir.x, dir.y, dir.z, body.forward_thrust / 1000.f * 3};
					++lightCount;
				}
	}
	auto vector = [&](const char *key, Vector3 fallback)
	{
		if (!lighting.contains(key))
			return fallback;
		const auto &v = lighting.at(key);
		return Vector3{v[0], v[1], v[2]};
	};
	const auto sun = Vector3Normalize(vector("sun_direction", {-.6f, .6f, -.5f}));
	radius = std::min(radius, 5000.f);
	Camera3D lightCamera{Vector3Scale(sun, radius * 3),
	                     {},
	                     std::abs(sun.y) > .95f ? Vector3{0, 0, 1} : Vector3{0, 1, 0},
	                     radius * 2,
	                     CAMERA_ORTHOGRAPHIC};
	const auto lightProj = MatrixOrtho(-radius, radius, -radius, radius, .1, radius * 6),
	           lightVP = MatrixMultiply(MatrixLookAt(lightCamera.position, {}, lightCamera.up), lightProj);
	const bool shadows = lighting.value("shadows", true);
	if (shadows)
	{
		s.shadow_pass = true;
		rlDrawRenderBatchActive();
		rlDisableScissorTest();
		rlEnableFramebuffer(s.shadow.id);
		rlViewport(0, 0, 2048, 2048);
		BeginMode3D(lightCamera);
		rlSetMatrixProjection(lightProj);
		ClearBackground(WHITE);
		s.integer("shadowPass", 1);
		rlDisableBackfaceCulling();
		for (const auto &item : items)
			s.body(catalog, item, true, time);
		rlEnableBackfaceCulling();
		EndMode3D();
		rlDrawRenderBatchActive();
		rlEnableFramebuffer(framebuffer);
	}
	if (size.x <= 0)
		size = {static_cast<float>(GetRenderWidth()), static_cast<float>(GetRenderHeight())};
	const float sx = framebuffer ? 1.f : size.x / GetScreenWidth(), sy = framebuffer ? 1.f : size.y / GetScreenHeight();
	Rectangle physical{region.x * sx, size.y - (region.y + region.height) * sy, region.width * sx, region.height * sy};
	rlEnableScissorTest();
	rlScissor(static_cast<int>(physical.x), static_cast<int>(physical.y), static_cast<int>(physical.width),
	          static_cast<int>(physical.height));
	rlViewport(static_cast<int>(physical.x), static_cast<int>(physical.y), static_cast<int>(physical.width),
	           static_cast<int>(physical.height));
	BeginMode3D(camera);
	const float aspect = region.width / region.height;
	rlSetMatrixProjection(
	    camera.projection == CAMERA_PERSPECTIVE
	        ? MatrixPerspective(camera.fovy * DEG2RAD, aspect, rlGetCullDistanceNear(), rlGetCullDistanceFar())
	        : MatrixOrtho(-camera.fovy * aspect * .5, camera.fovy * aspect * .5, -camera.fovy * .5, camera.fovy * .5,
	                      rlGetCullDistanceNear(), rlGetCullDistanceFar()));
	const auto fog = lighting.value("environment", Json::object());
	s.navigation.background(catalog, environment, camera, fog, rebase);
	set_scene_fog(s.shader, fog, rebase);
	s.vec("cameraPosition", camera.position);
	s.shadow_pass = false;
	s.integer("shadowPass", 0);
	s.integer("shadows", shadows ? 1 : 0);
	s.vec("sun", sun);
	s.vec("sunColour", vector("sun_color", {1, .95f, .85f}));
	s.number("ambient", lighting.value("ambient", .25f));
	s.number("sunIntensity", lighting.value("sun_intensity", 1.f));
	SetShaderValueMatrix(s.shader, s.loc("lightVP"), lightVP);
	s.integer("lightCount", lightCount);
	SetShaderValueV(s.shader, s.loc("lights[0]"), lights.data(), SHADER_UNIFORM_VEC4, 16);
	SetShaderValueV(s.shader, s.loc("cones[0]"), cones.data(), SHADER_UNIFORM_VEC4, 16);
	rlDisableBackfaceCulling();
	for (const auto &item : items)
		s.body(catalog, item, false, time);
	// Transparent procedural corona and exhaust follow opaque depth testing.
	rlDisableDepthMask();
	for (const auto &item : items)
		if (item.render->contains("star"))
		{
			auto pos = Vector3Transform({}, item.matrix);
			const auto &star = item.render->at("star");
			float r = item.render->at("scale").get<float>() * .5f * star.value("halo_radius_multiplier", 2.4f);
			auto facing = QuaternionToMatrix(
			    QuaternionFromVector3ToVector3({0, 1, 0}, Vector3Normalize(Vector3Subtract(camera.position, pos))));
			auto matrix =
			    MatrixMultiply(MatrixMultiply(MatrixScale(r, r, r), facing), MatrixTranslate(pos.x, pos.y, pos.z));
			s.integer("effect", 2);
			const auto c = star.value("halo_colour", std::array<float, 3>{1, .78f, .18f});
			s.vec("haloColour", {c[0], c[1], c[2]});
			s.mesh(s.quad, matrix, {}, {}, {1, 1, 1, 1}, {});
		}
	s.integer("effect", 3);
	for (int i = 0; i < lightCount; ++i)
	{
		auto root = lights[i];
		auto dir = cones[i];
		const float length = std::max(.1f, dir.w) * root.w * .08f;
		auto facing = QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, {dir.x, dir.y, dir.z}));
		auto matrix = MatrixMultiply(MatrixMultiply(MatrixScale(length * .3f, length, length * .3f), facing),
		                             MatrixTranslate(root.x, root.y, root.z));
		s.mesh(s.cone, matrix, {}, {}, {1, 1, 1, 1}, {});
	}
	rlEnableDepthMask();
	rlEnableBackfaceCulling();
	s.navigation.draw(catalog, environment, camera, rebase, physical);
	EndMode3D();
	rlViewport(0, 0, static_cast<int>(size.x), static_cast<int>(size.y));
	EndScissorMode();
}
} // namespace phoenix
