module;
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.content;
export import phoenix.data;
export import phoenix.contracts;

export namespace phoenix
{
struct Catalog;
struct PackDraft
{
	Json manifest;
	std::map<std::string, Bytes> files;
};
// Loaded packs share immutable decoded content and the exact original archive.
// Editing requires a separate draft; a draft has no transferable archive identity.
class Pack
{
public:
	const Json &manifest() const;
	const std::map<std::string, Bytes> &files() const;
	const std::string &hash() const;
	const Bytes &archive() const;
	std::shared_ptr<const Bytes> archive_owner() const;
	PackDraft draft() const;

private:
	struct Storage;
	std::shared_ptr<const Storage> storage_;
	explicit Pack(std::shared_ptr<const Storage> storage);
	friend Pack read_pack(std::span<const std::uint8_t> bytes);
	friend Pack read_pack(Bytes &&bytes);
};
struct Catalog
{
	std::map<std::string, Json> entities, maps, consoles, trees;
	std::map<std::string, Bytes> assets;
	std::vector<std::pair<std::string, std::string>> ordered_packs;
	bool presentation = false;
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
DustConfig compile_dust(const Json &dust, const Catalog &catalog, bool web, const std::string &path);
ReferenceGridConfig compile_reference_grid(const Json &grid, const std::string &path);
Json dust_defaults();
Json reference_grid_defaults();
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
};
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
void verify_lighting(const Json &lighting, const std::string &path);
// Convert an imported source sidecar to the canonical pack material. Image
// references must already have been imported and resolved by the caller.
Json import_sprite_sidecar(const Json &sidecar, const std::map<std::string, std::string> &images);
void apply_sprite_sidecar(Json &render, const Json &sidecar, const std::map<std::string, std::string> &images,
                          const Catalog &catalog, bool engine_effect = false);
// Throws an actionable exception. ZIP is read into bounded memory, never
// extracted to caller-controlled paths on disk.
Pack read_pack(std::span<const std::uint8_t> bytes);
Pack read_pack(Bytes &&bytes);
Bytes write_pack(const PackDraft &pack);
void verify_pack_draft(const PackDraft &pack);
Catalog resolve_packs(const std::vector<Pack> &ordered);
Pack console_assets(const Catalog &catalog, const std::string &ship);
// Authoring preview only: the final draft is deliberately absent from archive identities.
Catalog resolve_draft(const std::vector<Pack> &libraries, const PackDraft &draft);
void verify_catalog(const Catalog &catalog);
// Public setup hints for the HTML launcher. Admission/setup still verify in C++.
Json launcher_catalog(const Catalog &catalog);
struct ConsoleSpec
{
	std::string id, station, definition;
	SystemMask controls = 0, feeds = 0;
};
// Resolves legacy single-console content and verifies explicit station ownership.
std::vector<ConsoleSpec> resolve_consoles(const Catalog &catalog, const std::string &entity);
struct WidgetBinding
{
	const char *kind;
	const char *binding;
};
std::span<const WidgetBinding> widget_bindings();
Bytes json_bytes(const Json &json);
Json parse_json(std::span<const std::uint8_t> bytes, const std::string &source);
} // namespace phoenix

export namespace phoenix
{
struct TreeState
{
	std::string state;
	std::uint64_t elapsed = 0;
	bool entered = false;
};
// Each state emits bounded operating requests on entry, waits a declared tick
// duration and follows a named transition. Requests still require admission.
void verify_tree(const Json &definition, const std::string &source);
using TreeAction = OperatingAction;
struct TreeOutput
{
	TreeAction action;
	std::int64_t value;
};
struct TreeNode
{
	std::uint64_t ticks;
	std::size_t next, output_begin, output_count;
};
struct TreeProgram
{
	std::size_t initial;
	std::vector<TreeNode> nodes;
	std::vector<TreeOutput> outputs;
	// Names are needed only for save/load compatibility, not tree execution.
	std::vector<std::string> names;
};
struct TreeCursor
{
	static inline constexpr std::size_t unset = static_cast<std::size_t>(-1);
	std::size_t state = unset;
	std::uint64_t elapsed = 0;
};
TreeProgram compile_tree(const Json &definition);
std::span<const TreeOutput> step_tree(const TreeProgram &program, TreeCursor &cursor);
TreeState save_tree(const TreeProgram &program, const TreeCursor &cursor);
TreeCursor load_tree(const TreeProgram &program, const TreeState &saved);
} // namespace phoenix

export namespace phoenix
{
class WorkshopDocument
{
public:
	WorkshopDocument();
	void reset();
	void load_pack(std::span<const std::uint8_t> archive);
	void load_dependency(std::span<const std::uint8_t> archive);
	void rename_pack(const std::string &id);
	Bytes export_pack() const;
	void load_map(std::span<const std::uint8_t> bytes);
	Bytes save_map() const;
	void new_map(const std::string &id);
	void select_map(std::size_t index);
	std::string create_entity(const std::string &parent = {});
	void rename_entity(const std::string &from, const std::string &to);
	void rename_console(const std::string &from, const std::string &to);
	std::uint64_t place(const std::string &definition, std::int64_t x, std::int64_t y);
	void erase(std::uint64_t instance);
	void duplicate(std::uint64_t instance);
	std::string import_image(const std::string &filename, Bytes bytes);
	Catalog verify() const;
	Json &definitions()
	{
		return definitions_;
	}
	const Json &definitions() const
	{
		return definitions_;
	}
	Json &map()
	{
		return definitions_["maps"][map_index_];
	}
	const Json &map() const
	{
		return definitions_["maps"][map_index_];
	}
	Json &manifest()
	{
		return pack_.manifest;
	}
	const PackDraft &pack() const
	{
		return pack_;
	}
	const Catalog &libraries() const
	{
		return library_catalog_;
	}

private:
	PackDraft pack_;
	Json definitions_;
	std::size_t map_index_ = 0;
	std::vector<Pack> libraries_;
	Catalog library_catalog_;
};
} // namespace phoenix
