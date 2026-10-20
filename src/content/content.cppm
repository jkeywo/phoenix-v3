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
export import phoenix.resources;
export import phoenix.knowledge;
export import phoenix.scenario;
export import phoenix.state_tree;
export import phoenix.gameplay_vocabulary;
export import phoenix.flight;

export namespace phoenix
{
struct Catalog;
struct ControllerTreeBindings;
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
// Resolution only; a root restricts traversal to its dependency closure.
void inherit_definitions(std::map<std::string, Json> &, const std::string &kind, const std::string &root = {});
Json resolve_tree_definition(const Json &, const Catalog &);
void resolve_equipment(Catalog &catalog);
bool uses_advanced_machinery(const Catalog &, const std::string &entity);
void verify_gameplay_fitting(const Catalog &, const std::string &);
std::vector<GameplayTreeRecordBinding> gameplay_record_bindings(const Json &, const Catalog &);
struct GameplayTreeSummaryBinding
{
	std::string name, channel;
};
std::vector<GameplayTreeSummaryBinding> gameplay_summary_bindings(const Json &, const Catalog &);
state_tree::Registry gameplay_registry(const Json &, const Catalog &, state_tree::Context);
struct ControllerTelemetryBinding
{
	std::string name, system, field;
	state_tree::Type type = state_tree::Type::Integer;
};
bool supported_system_observation(const std::string &);
state_tree::Type system_observation_type(const std::string &);
std::vector<ControllerTelemetryBinding> controller_telemetry_bindings(const Json &, const Catalog &);
struct ControllerCommandReadBinding
{
	std::string name, controller, channel;
	state_tree::Type type = state_tree::Type::Integer;
};
std::vector<ControllerCommandReadBinding> controller_command_reads(const Json &, const Catalog &);
struct ControllerContactBinding
{
	std::string name, controller, channel;
};
std::vector<ControllerContactBinding> controller_contact_bindings(const Json &, const Catalog &);
std::vector<std::string> controller_message_templates(const Json &, const Catalog &);
std::vector<GameplayTreeRecordBinding> controller_message_reads(const Json &, const Catalog &);
enum class ControllerOwnField
{
	Entity,
	Console,
	Tick,
	X,
	Y,
	Vx,
	Vy,
	Heading,
	Blocked,
	LeftHeading,
	LateralAvoidance,
	LateralSafe,
	Count
};
enum class ControllerRecordField
{
	Available,
	ObservedTick,
	ReceivedTick,
	Source,
	Uncertainty,
	Kind,
	Withdrawn,
	Stale,
	Revision,
	Subject,
	Count
};
enum class ControllerCommandField
{
	Accepted,
	Revision,
	Consumed,
	RequestId,
	Pending,
	Count
};
enum class ControllerInboxField
{
	Available,
	Id,
	Source,
	SourceConsole,
	Schema,
	Subject,
	ObservedTick,
	DeliveredTick,
	Uncertainty,
	Kind,
	Urgency,
	RequestKey,
	Count
};
enum class ControllerContactField
{
	Available,
	Id,
	X,
	Y,
	Vx,
	Vy,
	Heading,
	Speed,
	ObservedTick,
	Uncertainty,
	Count
};
enum class ControllerResultField
{
	Available,
	Success,
	Id,
	Publication,
	Message,
	Reason,
	Count
};
template <typename Field> struct ControllerDestinations
{
	std::array<std::size_t, static_cast<std::size_t>(Field::Count)> values{};
	std::size_t &operator[](Field field)
	{
		return values.at(static_cast<std::size_t>(field));
	}
	std::size_t operator[](Field field) const
	{
		return values.at(static_cast<std::size_t>(field));
	}
};
template <typename Source> struct ControllerRead
{
	Source source;
	std::size_t destination = 0;
};
struct ControllerRecordRead : ControllerRead<GameplayTreeRecordBinding>
{
	ControllerDestinations<ControllerRecordField> metadata;
};
struct ControllerContactRead
{
	ControllerContactBinding source;
	ControllerDestinations<ControllerContactField> fields;
};
struct ControllerCommandBinding
{
	std::string channel;
	std::size_t destination = 0;
	std::vector<std::size_t> arguments;
	ControllerDestinations<ControllerCommandField> metadata;
};
// Derived together with the registry; never persisted. Sources are resolved to
// entity-local indices by simulation setup, destinations are never reconstructed.
struct ControllerTreeBindings
{
	state_tree::Registry registry;
	ControllerDestinations<ControllerOwnField> own;
	ControllerDestinations<ControllerInboxField> inbox;
	ControllerDestinations<ControllerResultField> result;
	std::vector<ControllerCommandBinding> commands;
	std::vector<ControllerRecordRead> records;
	std::vector<ControllerRead<ControllerTelemetryBinding>> telemetry;
	std::vector<ControllerRead<ControllerCommandReadBinding>> command_reads;
	std::vector<ControllerRead<GameplayTreeRecordBinding>> messages;
	std::vector<ControllerContactRead> contacts;
};
struct Catalog
{
	std::map<std::string, Json> entities, maps, consoles, trees;
	std::map<std::string, Json> equipment, assemblies, messages;
	Json knowledge = Json::object();
	CompiledKnowledge knowledge_definition;
	std::map<std::string, ResourceNetworkDefinition> resource_definitions;
	std::map<std::string, ScenarioDefinition> scenarios;
	std::map<std::string, state_tree::Program> tree_programs;
	std::map<std::string, ControllerTreeBindings> controller_bindings;
	std::map<std::string, Bytes> assets;
	std::vector<std::pair<std::string, std::string>> ordered_packs;
	bool presentation = false, selection = false;
	std::string loading_background;
};
ControllerTreeBindings compile_controller_bindings(const Json &, const Catalog &);
void verify_scenario_authoring(const Catalog &catalog, const Json &map);
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
enum class PackEncoding
{
	Zip,
	Dat
};
Bytes write_pack(const PackDraft &pack, PackEncoding encoding = PackEncoding::Zip);
Pack read_pack_path(const std::string &path);
struct AssetImage
{
	int width = 0, height = 0;
	Bytes rgba;
};
bool compiled_image(std::span<const std::uint8_t> bytes);
AssetImage decode_asset_image(std::span<const std::uint8_t> bytes);
Bytes compile_asset_image(std::span<const std::uint8_t> bytes);
Bytes source_asset_image(std::span<const std::uint8_t> bytes);
void verify_pack_draft(const PackDraft &pack);
Catalog resolve_packs(const std::vector<Pack> &ordered);
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::string &console = {},
                    const std::string &background = {});
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::vector<std::string> &consoles,
                    const std::string &background = {});
// Resolved references, including future spawn content; no production-content assumptions.
std::vector<std::string> scenario_definitions(const Catalog &catalog, const std::string &map);
std::vector<std::string> scenario_assets(const Catalog &catalog, const std::string &map);
// Authoring preview only: the final draft is deliberately absent from archive identities.
Catalog resolve_draft(const std::vector<Pack> &libraries, const PackDraft &draft);
Catalog prepared_catalog(const Catalog &catalog);
void verify_catalog(const Catalog &catalog);
// Public setup hints for the HTML launcher. Admission/setup still verify in C++.
Json launcher_catalog(const Catalog &catalog);
struct ConsoleSpec
{
	std::string id, station, definition;
	SystemMask controls = 0, feeds = 0;
	std::vector<std::string> controlled_systems, feed_systems;
	bool human_seeking = false;
	std::vector<std::string> human_preferences;
};
struct FittedSystemSpec
{
	std::string id;
	OperatingSystem kind;
	std::int64_t maximum = 0, response = 0;
	std::string controller;
};
struct SystemResourceBinding
{
	std::string id;
	OperatingSystem kind = OperatingSystem::Engine;
	bool store_modes = false;
	ResourceIndex store = no_resource, generator = no_resource, group = no_resource, conduit = no_resource,
	              cooling = no_resource;
	ResourceAmount energy_per_second = 0, discrete_energy = 0;
};
struct ShipResourcesDefinition
{
	ResourceNetworkDefinition network;
	std::vector<SystemResourceBinding> systems;
	std::string signature;
};
std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity);
using SystemObservationSchema = std::map<std::string, state_tree::Type>;
std::map<std::string, SystemObservationSchema> compile_system_observations(const Catalog &, const std::string &entity);
struct ControllerCommandSpec
{
	std::string id;
	OperatingAction action = OperatingAction::Speed;
	OperatingAction physical_input = OperatingAction::Speed;
	KnowledgeType type = KnowledgeType::Integer;
	CommandSemantics semantics = CommandSemantics::Setting;
	std::int64_t initial = 0, minimum = 0, maximum = 0;
	std::vector<std::size_t> consumers;
	std::vector<std::int64_t> initial_arguments;
	bool initial_heading = false;
	std::int64_t arrival_radius = 5000;
	std::string label_key;
	std::map<std::int64_t, std::string> choices;
};
struct ConsoleControllerSpec
{
	std::string id, console, tree;
	std::vector<ControllerCommandSpec> commands;
};
struct ConsoleComplexitySpec
{
	std::string id;
	// Profile allocation when a human is available; absence makes every entry AI.
	std::vector<bool> human;
};
struct ControllableLayout
{
	std::vector<ConsoleControllerSpec> controllers;
	std::vector<ConsoleComplexitySpec> profiles;
	std::size_t default_profile = 0;
	bool converted = false;
};
// Compiles explicit layout version 1 or converts unambiguous old ownership at the boundary.
ControllableLayout compile_controllable_layout(const Catalog &, const std::string &entity);
// Boundary conversion only. Unsupported decision/ownership mappings fail before runtime creation.
Catalog convert_controller_content(const Catalog &);
std::vector<FittedSystemSpec> resolve_systems(const Catalog &catalog, const std::string &entity);
// Includes standard capability channels and the fitted controller's inherited declarations.
std::vector<std::string> gameplay_summary_channels(const Catalog &, const std::string &entity,
                                                   const std::string &system);
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
struct OperatingRequest
{
	OperatingAction action;
	std::int64_t value;
	std::vector<std::int64_t> arguments;
};
} // namespace phoenix

export namespace phoenix
{
enum class WorkshopDefinitionKind
{
	Entity,
	Map,
	Console,
	Tree,
	Equipment,
	Assembly,
	Message
};
class WorkshopDocument
{
	friend class WorkshopHistory;

public:
	WorkshopDocument();
	void reset();
	void load_pack(std::span<const std::uint8_t> archive);
	void load_dependency(std::span<const std::uint8_t> archive);
	void rename_pack(const std::string &id);
	Bytes export_pack(PackEncoding encoding = PackEncoding::Zip, bool scenario_only = false) const;
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
	// Resolves reusable equipment and controller namespaces without requiring finished ownership/presets.
	Catalog preview_equipment() const;
	Json preview_tree(const Json &tree) const;
	Json preview_tree(const std::string &id) const;
	std::vector<std::string> definition_choices(WorkshopDefinitionKind kind) const;
	Json preview_definition(WorkshopDefinitionKind kind, const std::string &id) const;
	Json preview_knowledge() const;
	state_tree::Registry preview_controller_registry(const Json &tree) const;
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
	std::vector<Bytes> dependency_archives() const;

private:
	PackDraft pack_;
	Json definitions_;
	std::size_t map_index_ = 0;
	std::vector<Pack> libraries_;
	Catalog library_catalog_;
};
// Complete draft history, including imported assets and dependency closure. Selection is not an edit.
class WorkshopHistory
{
public:
	void reset(const WorkshopDocument &);
	void checkpoint(const WorkshopDocument &);
	bool can_undo() const
	{
		return past_.size() > 1;
	}
	bool can_redo() const
	{
		return !future_.empty();
	}
	bool undo(WorkshopDocument &);
	bool redo(WorkshopDocument &);

private:
	struct Snapshot
	{
		Json definitions, manifest;
		std::size_t map_index, bytes, resource_bytes;
		std::shared_ptr<const WorkshopDocument> resources;
	};
	std::vector<Snapshot> past_, future_;
	static bool same_resources(const WorkshopDocument &, const WorkshopDocument &);
	static void restore(WorkshopDocument &, const Snapshot &);
	Snapshot snapshot(const WorkshopDocument &) const;
	static void trim(std::vector<Snapshot> &);
};

} // namespace phoenix

#include "gameplay_knowledge.hpp"
