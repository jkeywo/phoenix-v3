module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.assets;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.content.catalog;

export namespace phoenix
{
struct ShipAudioDefinition
{
	AudioClip ambient, engine;
};
struct ModelPart
{
	std::vector<float> positions, normals, texcoords;
	Bytes albedo, emissive;
	std::array<float, 4> tint{1, 1, 1, 1};
	std::array<float, 3> emission{};
};
struct StaticModel
{
	std::vector<ModelPart> parts;
};
// Render-only navigation aids. Distances are metres; speed is metres/second.
struct DustConfig
{
	bool enabled = true;
	int count = 440;
	float half_width = 30, depth = 100, width = .08f, streak_per_speed = .25f;
	std::array<float, 3> tint{.7f, .8f, 1}, brightness{.7f, .55f, .4f};
	float opacity = .45f;
	// Empty entries select the corresponding bundled V2 texture.
	std::array<std::string, 3> textures{};
	float centre_fade_inner = .12f, centre_fade_outer = .38f, edge_fade = .15f;
};
struct ReferenceGridConfig
{
	float minor_spacing = 10, major_spacing = 50;
	std::array<float, 4> minor_colour{.45f, .62f, .78f, .1f}, major_colour{.55f, .74f, .9f, .2f};
	float opacity = 1, patch_radius = 400, fade_band = 250, fade_exponent = 2.5f, plane_y = -.5f;
	std::optional<float> first_person_plane_y;
	float minor_line_width_px = 1, major_line_width_px = 1.6f;
};
struct SpriteAttachment
{
	float x = 0, y = 0, depth_offset = 0;
	int layer = 0;
};
struct SpriteFrame
{
	std::string name;
	std::array<int, 4> rect{};
	std::array<float, 2> pivot{};
	std::array<float, 2> forward{0, -1};
	bool mirror_x = false;
	float azimuth = 0, depth_scale = 0, depth_offset = 0;
	std::map<std::string, SpriteAttachment> attachments;
};
struct SpriteMaterial
{
	std::string albedo, normal, depth;
	std::vector<SpriteFrame> frames;
	bool emissive = false;
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
	struct Entry
	{
		std::string name;
		std::vector<SpriteFrame> frames;
	};
	std::vector<Entry> entries;
};
AudioClip compile_audio_clip(const Catalog &, const Json &, const std::string &, bool loop = true);
ShipAudioDefinition compile_ship_audio(const Catalog &, const std::string &);
void verify_audio_asset(const Catalog &, const std::string &, const std::string &);
StaticModel compile_model(std::span<const std::uint8_t> bytes);
void verify_model_render(const Json &render, const Catalog &catalog, const std::string &path);
DustConfig compile_dust(const Json &dust, const Catalog &catalog, bool web, const std::string &path);
ReferenceGridConfig compile_reference_grid(const Json &grid, const std::string &path);
Json dust_defaults();
Json reference_grid_defaults();
std::uint32_t sprite_entry_index(const SpriteMaterial &sprite, const std::string &name);
const SpriteFrame &sprite_frame(const SpriteMaterial &sprite, std::uint32_t entry, float bearing);
// Public PNG icon; content-addressed references travel with radar observations.
std::string radar_sprite_reference(const Json &render);
std::string radar_sprite_id(const Bytes &png);
void verify_player_avatar(const Bytes &png);
std::string render_radar_sprite(const Json &render, const Catalog &catalog);
// A small, public symbol selection; no image or hidden entity data is needed.
RadarIcon compile_radar_icon(const Json &value, const std::string &path);
RadarIcon render_radar_icon(const Json &render);
// Verified material contract, independent of GPU/platform types. Calibration
// is in multiples of rendered height, with white toward the viewing camera.
SpriteMaterial compile_sprite(const Json &sprite, const Catalog &catalog, const std::string &path);
// Reuse validated image dimensions while compiling one unchanged catalogue.
class SpriteCompiler
{
	const Catalog &catalog_;
	std::map<std::string, std::array<int, 2>> images_;

public:
	explicit SpriteCompiler(const Catalog &catalog);
	SpriteMaterial compile(const Json &sprite, const std::string &path);
};
void verify_lighting(const Json &lighting, const std::string &path);
// Convert an imported source sidecar to the canonical pack material. Image
// references must already have been imported and resolved by the caller.
Json import_sprite_sidecar(const Json &sidecar, const std::map<std::string, std::string> &images);
void apply_sprite_sidecar(Json &render, const Json &sidecar, const std::map<std::string, std::string> &images,
                          const Catalog &catalog, bool engine_effect = false);
} // namespace phoenix
