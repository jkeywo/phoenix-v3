module;
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.presentation;

namespace phoenix
{
namespace
{
constexpr int sample_count = 2048;
constexpr double pi = 3.14159265358979323846;
#ifdef __EMSCRIPTEN__
const std::string sweep_prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const std::string sweep_prefix = "#version 330\n";
#endif
const char *sweep_vertex = R"GLSL(
in vec3 vertexPosition;
uniform mat4 mvp;
uniform sampler2D samples;
uniform int nextIndex;
uniform float now;
uniform float duration;
uniform float radius;
uniform vec2 centre;
out float age;
out float radial;
void main(){
 int logical=int(vertexPosition.x);
 int index=(nextIndex+logical)%2048;
 vec4 record=texelFetch(samples,ivec2(index,0),0);
 age=now-record.z;radial=vertexPosition.y;
 float r=record.y*radius*radial;
 if(record.w<.5 || age>duration || age<0.0)r=0.0;
 vec2 p=centre+vec2(cos(record.x),-sin(record.x))*r;
 gl_Position=mvp*vec4(p,0,1);
}
)GLSL";
const char *sweep_fragment = R"GLSL(
in float age;
in float radial;
uniform float duration;
uniform vec4 colour;
out vec4 finalColor;
void main(){
 if(age<0.0 || age>duration)discard;
 float trail=pow(max(0.0,1.0-age/duration),2.0);
 float leading=1.0-smoothstep(0.0,.08,age);
 float rim=smoothstep(.94,1.0,radial);
 float alpha=trail*(.16+.35*rim)+leading*(.45+.25*rim);
 finalColor=vec4(colour.rgb,colour.a*clamp(alpha,0.0,1.0));
}
)GLSL";
} // namespace
struct RadarSweep::State
{
	Shader shader{};
	Material material{};
	Mesh mesh{};
	Texture2D samples{};
	int next = 0;
	double start = -1, previous = -1, angle = 0;
	std::uint64_t writes = 0;
	std::int64_t maximum = 0;
	~State()
	{
		if (mesh.vaoId)
			UnloadMesh(mesh);
		if (samples.id)
			UnloadTexture(samples);
		if (shader.id)
			UnloadShader(shader);
		if (material.maps)
			MemFree(material.maps);
	}
	void init()
	{
		if (shader.id)
			return;
		shader = LoadShaderFromMemory((sweep_prefix + sweep_vertex).c_str(), (sweep_prefix + sweep_fragment).c_str());
		if (!shader.id || shader.id == rlGetShaderIdDefault())
			throw std::runtime_error("Radar sweep shader failed");
		material = LoadMaterialDefault();
		material.shader = shader;
		mesh.vertexCount = (sample_count - 1) * 3;
		mesh.triangleCount = sample_count - 1;
		mesh.vertices = static_cast<float *>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
		for (int i = 0; i < sample_count - 1; ++i)
		{
			const std::array<float, 9> vertices{static_cast<float>(i),     0, 0, static_cast<float>(i), 1, 0,
			                                    static_cast<float>(i + 1), 1, 0};
			std::copy(vertices.begin(), vertices.end(), mesh.vertices + i * 9);
		}
		UploadMesh(&mesh, false);
		std::vector<float> empty(sample_count * 4, 0);
		samples = {rlLoadTexture(empty.data(), sample_count, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1), sample_count,
		           1, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
		material.maps[MATERIAL_MAP_ALBEDO].texture = samples;
		shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(shader, "samples");
		SetTextureFilter(samples, TEXTURE_FILTER_POINT);
	}
	void reset(double time)
	{
		// A reset changes the epoch; old samples remain invalid until overwritten.
		start = time;
		previous = time;
		next = 0;
		angle = 0;
		std::vector<float> empty(sample_count * 4, 0);
		rlUpdateTexture(samples.id, 0, 0, sample_count, 1, samples.format, empty.data());
	}
};
RadarSweep::RadarSweep() : state_(std::make_unique<State>())
{
}
RadarSweep::~RadarSweep() = default;
Json RadarSweep::statistics() const
{
	return {{"sample_writes", state_->writes}, {"next_index", state_->next}};
}
void RadarSweep::draw(Vector2 centre, float radius, const SystemView &sensor, const Json &settings, double time,
                      bool reduced_motion)
{
	auto &s = *state_;
	if (!settings.value("enabled", true) || sensor.effective_range <= 0 || sensor.maximum <= 0)
	{
		s.previous = -1;
		return;
	}
	s.init();
	const auto speed = settings.value("speed_degrees_s", 60.0);
	const float duration = static_cast<float>(std::min(settings.value("duration_seconds", 3.0), 360.0 / speed));
	const auto tint = settings.value("colour", Json::array({.1, .9, .65, .85}));
	Vector4 colour{tint[0], tint[1], tint[2], tint[3]};
	if (s.previous < 0 || time < s.previous || time - s.previous > .5 || s.maximum != sensor.maximum)
		s.reset(time);
	s.maximum = sensor.maximum;
	if (reduced_motion)
	{
		// Static accessible alternative: show the measured envelope without an animated trail.
		const auto count = sensor.radial_ranges.empty() ? 128u : sensor.radial_ranges.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			auto point = [&](std::size_t j)
			{
				const auto range =
				    sensor.radial_ranges.empty() ? sensor.effective_range : sensor.radial_ranges[j % count];
				const auto a = 2 * pi * (j % count) / count;
				const auto r = radius * range / sensor.maximum;
				return Vector2{centre.x + static_cast<float>(std::cos(a) * r),
				               centre.y - static_cast<float>(std::sin(a) * r)};
			};
			DrawLineV(point(i), point(i + 1), ColorFromNormalized(colour));
		}
		s.previous = time;
		return;
	}
	if (time != s.previous || s.writes == 0)
	{
		s.angle = std::fmod(s.angle + speed * std::max(0.0, time - s.previous), 360.0);
		auto distance = sensor.effective_range;
		if (!sensor.radial_ranges.empty())
		{
			const auto at = s.angle / 360 * sensor.radial_ranges.size();
			const auto lo = static_cast<std::size_t>(at) % sensor.radial_ranges.size();
			// Conservative between adjacent authoritative measurements.
			distance = std::min(sensor.radial_ranges[lo], sensor.radial_ranges[(lo + 1) % sensor.radial_ranges.size()]);
		}
		const std::array<float, 4> sample{static_cast<float>(s.angle * pi / 180),
		                                  static_cast<float>(distance) / sensor.maximum,
		                                  static_cast<float>(time - s.start), 1};
		rlUpdateTexture(s.samples.id, s.next, 0, 1, 1, s.samples.format, sample.data());
		s.next = (s.next + 1) % sample_count;
		++s.writes;
		s.previous = time;
	}
	rlDrawRenderBatchActive();
	auto set = [&](const char *name, const void *value, int type)
	{
		SetShaderValue(s.shader, GetShaderLocation(s.shader, name), value, type);
	};
	const float now = static_cast<float>(time - s.start);
	set("nextIndex", &s.next, SHADER_UNIFORM_INT);
	set("now", &now, SHADER_UNIFORM_FLOAT);
	set("duration", &duration, SHADER_UNIFORM_FLOAT);
	set("radius", &radius, SHADER_UNIFORM_FLOAT);
	set("centre", &centre, SHADER_UNIFORM_VEC2);
	set("colour", &colour, SHADER_UNIFORM_VEC4);
	rlDisableBackfaceCulling();
	DrawMesh(s.mesh, s.material, MatrixIdentity());
	rlEnableBackfaceCulling();
}
} // namespace phoenix
