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
#include <vector>
#include <variant>

export module phoenix.content;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.resources;
export import phoenix.knowledge;
export import phoenix.scenario;
export import phoenix.state_tree;
export import phoenix.gameplay_vocabulary;
export import phoenix.flight;
export import phoenix.conversations;

export namespace phoenix
{
struct Catalog;
struct FactionCatalog
{
	std::map<std::string, std::string> labels;
	std::map<std::pair<std::string, std::string>, std::string> relationships;
	std::string relationship(const std::string &a, const std::string &b) const;
	void validate(const std::string &id) const;
};
FactionCatalog compile_factions(const Json &map);
struct ControllerTreeBindings;
struct ControllerCandidateSpec
{
	std::string id, description, deviation;
	state_tree::Program program;
};
std::vector<ControllerCandidateSpec> compile_controller_candidates(const Json &, const Catalog &,
                                                                   const state_tree::Registry &);
struct ControllerActionSpec
{
	std::string id, label, tree;
	std::vector<std::size_t> inputs;
	state_tree::Program program, availability;
};
std::vector<ControllerActionSpec> compile_controller_actions(const Json &, const Catalog &,
                                                             const state_tree::Registry &);
void expand_combat_policies(Catalog &);
void expand_officer_policies(Catalog &);
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
	AlertId,
	AlertSeverity,
	Stance,
	FtlReady,
	Visitors,
	LocalWorkComplete,
	CraftX,
	CraftY,
	CraftAltitude,
	CraftSpeed,
	CraftTarget,
	CraftNavigate,
	CraftHeading,
	CraftOrder,
	CraftReserveLow,
	CraftArmed,
	CraftTargetPresent,
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
	BearingAvailable,
	BearingMdeg,
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
	std::string ftl_system;
	std::size_t decision_base = 0, action_base = 0, coordination_base = 0;
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
struct BaseConfiguration
{
	std::string id, hash, render_mode = "sprites", interface_style = "current";
	bool operator==(const BaseConfiguration &) const = default;
};
// Parent is the required dependency for rooted packs. Legacy synthetic fixtures
// may still use the old dependency vocabulary outside a launched base tree.
std::vector<std::string> pack_dependencies(const Json &manifest);
BaseConfiguration base_configuration(const Pack &pack);
struct Catalog
{
	std::map<std::string, Json> entities, maps, consoles, trees;
	std::map<std::string, Json> equipment, assemblies, messages, effects;
	Json knowledge = Json::object();
	CompiledKnowledge knowledge_definition;
	std::map<std::string, ResourceNetworkDefinition> resource_definitions;
	std::map<std::string, ScenarioDefinition> scenarios;
	std::map<std::string, state_tree::Program> tree_programs;
	std::map<std::string, ConversationProgram> conversation_programs;
	std::map<std::string, ControllerTreeBindings> controller_bindings;
	std::map<std::string, Bytes> assets;
	std::vector<std::pair<std::string, std::string>> ordered_packs;
	bool presentation = false, selection = false;
	std::string loading_background;
	BaseConfiguration base;
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
StaticModel compile_model(std::span<const std::uint8_t> bytes);
void verify_model_render(const Json &render, const Catalog &catalog, const std::string &path);
ControllerTreeBindings compile_controller_bindings(const Json &, const Catalog &);
void verify_conversation_authoring(const Catalog &catalog);
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
// Fully validates pixels, reusing bounded success receipts for identical bytes.
std::array<int, 2> verified_image_dimensions(std::span<const std::uint8_t> bytes);
Bytes compile_asset_image(std::span<const std::uint8_t> bytes);
Bytes source_asset_image(std::span<const std::uint8_t> bytes);
Bytes source_asset_image(const AssetImage &image);
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
std::string mission_briefing(const Json &map, const std::string &ship);
// Display-only metadata and optional public ship packages, pinned to exact source archives.
Json pack_menu(const std::vector<Pack> &ordered);
Catalog menu_catalog(const std::vector<Json> &menus);
void validate_ship_client(const Pack &pack, const Json &descriptor);
Pack ship_client_assets(const Catalog &catalog, const std::string &ship, const std::string &background = {});
std::map<std::string, Bytes> pack_companions(const std::vector<Pack> &ordered, const std::string &filename);
Json ship_client_descriptor(const Catalog &catalog, const std::string &ship, const std::string &hash,
                            const std::map<std::string, std::string> &urls);
Bytes distribution_zip(const std::map<std::string, Bytes> &files);
void write_companions(const std::string &archive_path, const std::map<std::string, Bytes> &files);
struct ConsoleSpec
{
	std::string id, station, definition;
	SystemMask controls = 0, feeds = 0;
	std::vector<std::string> controlled_systems, feed_systems;
	bool human_seeking = false;
	std::vector<std::string> human_preferences;
	bool shield_feed = false;
};
// Authored personnel is compiled once; runtime assignments never alter these records.
state_tree::Registry team_registry();
struct TeamOrderDefinition
{
	std::string id, label, icon, target, capability;
	std::set<std::string> patient_states{"minor", "severe", "stabilised"};
	state_tree::Program program;
};
struct AwaySlot
{
	std::string id, kind, trait;
	bool required = true;
	std::string team_type, capability;
};
struct SensorDefinition
{
	std::int64_t long_range = 0;
	std::int64_t arc = 360000, bearing = 0;
	std::string turret;
	std::map<std::string, std::int64_t> capabilities;
};
struct SensorFinding
{
	std::string id, text, requires_event, unless_event, event;
	EntityId target = 0;
	std::vector<std::string> capabilities;
	std::int64_t ticks = 1;
};
std::map<std::string, SensorDefinition> compile_sensors(const Catalog &, const std::string &);
std::vector<SensorFinding> compile_sensor_findings(const Catalog &, const Json &);
struct BoardingEncounter
{
	std::string id, compartment, report;
	std::vector<std::string> defenders;
	std::map<std::string, std::int64_t> defences;
	std::int64_t success = 700, team_bonus = 100, defender_penalty = 150, injury = 250, loss = 50,
	             medical_reduction = 150, assessment_ticks = 20, officer_injury = 0, officer_loss = 0;
};
struct BoardingObjective
{
	std::string compartment, kind, system, fault;
	bool prize_crew = true;
};
struct BoardingDefinition
{
	std::string id, title, dialogue, entry;
	bool retain_craft = true;
	state_tree::Program program;
	std::vector<AwaySlot> slots;
	std::map<std::string, std::size_t> compartments;
	std::map<std::string, BoardingEncounter> encounters;
	std::map<std::string, BoardingObjective> objectives;
};
std::vector<BoardingDefinition> compile_boarding(const Catalog &, const std::string &entity);
struct AwayDefinition
{
	std::string id, title, slot, conversation, reveal_event, tree;
	EntityId target = 0, origin = 0;
	bool retain_craft = true;
	std::optional<BoardingDefinition> boarding;
	std::vector<AwaySlot> slots;
	state_tree::Program program;
	std::map<std::string, std::size_t> repair_compartments;
	bool allow_ship_transport = false;
	std::vector<std::string> docking_ports;
	std::string ship_transport_reason;
	std::int64_t deployment_range = 20000, deployment_ticks = 20;
};
state_tree::Registry away_registry(const Json &tree = Json::object());
std::vector<AwayDefinition> compile_away(const Catalog &, const Json &);
struct MaterialDefinition
{
	std::string id;
	std::int64_t volume = 1;
};
struct MaterialStoreDefinition
{
	std::string id, compartment, system, controller;
	std::int64_t capacity = 0;
	bool main = false;
	std::vector<std::string> accepts;
	std::map<std::string, std::int64_t> initial, targets;
};
struct MaterialConsumerDefinition
{
	std::string system, store;
	std::map<std::string, std::map<std::string, std::int64_t>> costs;
};
struct LogisticsDefinition
{
	std::string controller;
	std::int64_t throughput = 20, concurrency = 4, batch_size = 10;
	std::map<std::string, MaterialDefinition> materials;
	std::vector<MaterialStoreDefinition> stores;
	std::vector<MaterialConsumerDefinition> consumers;
};
LogisticsDefinition compile_logistics(const Catalog &, const std::string &);
struct DockPortDefinition
{
	std::string id, controller, compartment, compatibility;
	std::int64_t x = 0, y = 0, z = 0, heading = 0, capture_range = 1000, capture_speed = 500, alignment = 5000,
	             mating_ticks = 20, separation_ticks = 20, throughput = 20, emergency_damage = 0;
	bool auto_accept_allies = false, supply = false, emergency_release = false;
};
std::vector<DockPortDefinition> compile_docking(const Catalog &, const std::string &);
struct TransportDefinition
{
	std::string compartment;
	std::int64_t capacity = 64;
	std::vector<std::string> capabilities;
	std::map<std::string, std::vector<std::string>> capability_systems;
};
struct CraftDefinition
{
	std::string role;
	std::int64_t vertical_speed = 10000, vertical_acceleration = 5000;
	std::int64_t cruise_altitude = 15000, engagement_range = 200000, return_reserve = 200;
};
struct CraftSupplyDefinition
{
	std::string source, destination;
	std::int64_t rate = 0;
	std::size_t source_store = 0;
	std::string material;
	std::map<std::string, std::size_t> destination_stores;
};
struct CraftBayDefinition
{
	std::string compartment, power_store;
	std::int64_t slots = 0, transfer_ticks = 20, docking_range = 10000, docking_speed = 5000;
	std::int64_t flight_spacing = 0;
	std::int64_t charge_rate = 0;
	bool legacy_transport = false;
	std::int64_t transport_range = 0, transport_capacity = 0, arrival_distance = 0, arrival_speed = 0, cruise_speed = 0;
	std::vector<std::string> initial;
	std::vector<CraftSupplyDefinition> ammunition;
};
void expand_transport_fittings(Catalog &);
TransportDefinition compile_transport(const Catalog &, const std::string &);
std::optional<CraftDefinition> compile_craft(const Catalog &, const std::string &);
std::map<std::string, CraftBayDefinition> compile_craft_bays(const Catalog &, const std::string &);
struct PsychicPowerDefinition
{
	std::string id, kind, label, apparent_class = "Unidentified contact";
	std::int64_t strength = 700, energy = 100, recovery_ticks = 100, ghosts = 3;
	std::int64_t offset = 20000, spacing = 8000, speed = 10000, acceleration = 2000, turn_rate = 10000;
	std::int64_t radius = 2000, scan_ticks = 100, fade_ticks = 20;
};
struct ChoirSeatDefinition
{
	std::string duty;
	std::size_t controller = 0, enabled = 0, target = 0;
	std::optional<std::size_t> planner;
	std::size_t planning_enabled = 0, planning_target = 0;
};
struct ChoirDefinition
{
	std::string id, compartment, planning_controller;
	std::set<std::string> powers;
	std::int64_t range = 500000, arc = 90000;
	std::vector<ChoirSeatDefinition> seats;
};
struct PsychicDefinition
{
	std::map<std::string, PsychicPowerDefinition> powers;
	std::map<std::string, ChoirDefinition> choirs;
};
PsychicDefinition compile_psychic(const Catalog &, const std::string &);
struct OfficerTrait
{
	std::string id, label;
	std::map<std::string, std::int64_t> bonuses;
};
struct PersonnelDepartment
{
	std::string id, label, station;
};
struct PersonnelDuty
{
	std::string id, label, department, controller, team, compartment, console, required_trait, choir;
	std::map<std::string, std::vector<std::string>> effects;
};
struct OfficerDefinition
{
	std::string id, name, portrait, department, duty, compartment;
	std::string condition = "healthy", power;
	std::vector<std::string> traits;
	std::map<std::string, std::int64_t> bonuses;
};
struct MedicalFacility
{
	std::string id, compartment;
	std::int64_t capacity = 1, recovery_ticks = 1200;
};
struct CasualtyPolicy
{
	std::int64_t significant_damage = 20, heavy_below_permille = 300, lethal_damage = 80;
	std::int64_t stun = 150, minor = 100, severe = 30, killed = 10;
	bool lethal = false;
};
struct PersonnelDefinition
{
	std::vector<MedicalFacility> facilities;
	CasualtyPolicy casualties;
	std::vector<PersonnelDepartment> departments;
	std::map<std::string, OfficerTrait> traits;
	std::vector<PersonnelDuty> duties;
	std::map<std::string, std::size_t> duty_indices;
	std::vector<OfficerDefinition> officers;
	std::vector<std::string> compartments;
	struct Link
	{
		std::size_t from, to;
		std::int64_t ticks;
	};
	std::vector<Link> links;
};
PersonnelDefinition compile_personnel(const Catalog &, const std::string &entity);
struct BridgeChoice
{
	std::string id, label, description;
};
struct AlertCondition : BridgeChoice
{
	std::string colour = "#000000", siren, music;
	bool glow = false;
};
struct BridgeDefinition
{
	std::vector<AlertCondition> conditions;
	std::size_t initial = 0;
	std::string alert_console, sidebar_default;
};
struct ConsoleBridgeDefinition
{
	std::vector<BridgeChoice> stances;
	std::size_t initial = 0;
	bool command = false, auto_hide = false, viewscreen = false;
	Json compact;
};
BridgeDefinition compile_bridge(const Catalog &, const std::string &entity);
ConsoleBridgeDefinition compile_console_bridge(const Json &, const std::string &path);
// Combat definitions are compiled once. Runtime components never interpret JSON.
struct CompartmentDefinition
{
	std::string id, deck;
	std::int64_t x = 0, y = 0, half_width = 0, half_length = 0, maximum = 0, initial = 0, resistance = 0, recovery = 0;
	std::int64_t deck_weight = 1;
};
struct CompartmentConnection
{
	std::size_t from = 0, to = 0;
	std::int64_t ticks = 1;
};
struct DestructionDefinition
{
	std::string visual, sound;
	std::int64_t chance = 1000, damage = 0, radius = 0, shield_bypass = 0;
	std::int64_t growth_ticks = 1, fade_ticks = 10, size = 10000;
	bool affects_missiles = true;
};
struct ProjectileDefinition
{
	std::string visual;
	std::int64_t speed = 0, lifetime = 0, penetration = 0, damage = 0, deposition = 0, spread = 0;
	std::string guidance = "none", trail;
	bool tracer = false;
	std::int64_t shield_bypass = 0, turn_rate = 0, seeker_range = 0, arc_height = 0, chaos = 0;
	std::int64_t durability = 0, radius = 100, resistance = 1, blast_radius = 0, fuse_radius = 0;
	DestructionDefinition destruction;
};
struct FaultDefinition
{
	std::string id, symptom, diagnosis, effect = "output";
	std::int64_t chance = 0, threshold = 1, severity = 1, effectiveness = 500, repair_work = 100;
	bool destroys = false;
};
struct AmmunitionDefinition
{
	std::int64_t interval = 1, load_ticks = 0;
	ProjectileDefinition projectile;
};
struct ReserveDefinition
{
	std::int64_t capacity = 0, initial = 0;
};
struct WeaponDefinition
{
	AmmunitionDefinition ammunition;
	std::string turret, muzzle_marker, family = "projectile";
	std::int64_t ammo_use = 1, range = 0, beam_damage = 0, standby_energy = 0;
	bool point_defence = false;
	std::vector<std::string> magazines, intercept_classes;
};
struct TurretDefinition
{
	std::string marker, visual;
	std::int64_t traverse = 0, scale_permille = 1000;
	bool flip_y = false;
};
struct MagazineDefinition
{
	AmmunitionDefinition ammunition;
	ReserveDefinition reserve;
	std::string reload = "none";
	std::int64_t refill_ticks = 1, refill_amount = 0, reload_energy = 0;
};
struct TeamDefinition
{
	std::int64_t repair_rate = 0, stack_penalty = 500, treatment_ticks = 400, stabilise_ticks = 200,
	             injured_effectiveness = 500;
	std::string type = "engineering";
	std::set<std::string> capabilities{"repair"};
	std::vector<TeamOrderDefinition> orders;
};
enum class ShieldReactionKind
{
	Damage,
	Apply,
	Remove
};
struct ShieldReactionOperation
{
	ShieldReactionKind kind = ShieldReactionKind::Damage;
	std::string target = "attacker", effect, grant;
	std::int64_t damage = 0;
};
struct ShieldReactionDefinition
{
	std::string payer;
	std::int64_t energy = 0;
	std::vector<ShieldReactionOperation> operations;
};
struct ShieldDefinition
{
	std::vector<ShieldReactionDefinition> reactions;
	std::int64_t pre_damage_permille = 1000, post_damage = 0;
	std::int64_t recharge_modifier = 0, sustain_modifier = 0, discharge_modifier = 0, protection_modifier = 0;
	ReserveDefinition reserve;
	std::int64_t sustain_energy = 0, recharge_energy = 1, recharge_rate = 0, off_recharge_rate = 0;
	std::int64_t warmup_ticks = 1, layer = 0, discharge_rate = 0, discharge_efficiency = 1000;
};
struct ShieldContributionDefinition
{
	std::string name;
	std::int64_t bearing = 0, arc = 360000;
	ShieldDefinition shield;
};
std::vector<ShieldContributionDefinition> compile_shield_effect(const Catalog &, const std::string &);
struct CombatSystemDefinition
{
	std::string shield_effect;
	std::vector<ShieldContributionDefinition> shield_contributions;
	std::string id, compartment;
	OperatingSystem kind = OperatingSystem::Weapon;
	std::int64_t x = 0, y = 0, bearing = 0, arc = 360000;
	std::variant<WeaponDefinition, TurretDefinition, MagazineDefinition, TeamDefinition, ShieldDefinition> details;
	explicit CombatSystemDefinition(OperatingSystem type = OperatingSystem::Weapon) : kind(type)
	{
		switch (type)
		{
		case OperatingSystem::Weapon:
			details = WeaponDefinition{};
			break;
		case OperatingSystem::Turret:
			details = TurretDefinition{};
			break;
		case OperatingSystem::Magazine:
			details = MagazineDefinition{};
			break;
		case OperatingSystem::Team:
			details = TeamDefinition{};
			break;
		case OperatingSystem::Shield:
			details = ShieldDefinition{};
			break;
		default:
			break;
		}
	}
	template <class T> T &get()
	{
		return std::get<T>(details);
	}
	template <class T> const T &get() const
	{
		return std::get<T>(details);
	}
	AmmunitionDefinition &ammunition()
	{
		return kind == OperatingSystem::Magazine ? get<MagazineDefinition>().ammunition
		                                         : get<WeaponDefinition>().ammunition;
	}
	const AmmunitionDefinition &ammunition() const
	{
		return kind == OperatingSystem::Magazine ? get<MagazineDefinition>().ammunition
		                                         : get<WeaponDefinition>().ammunition;
	}
	ReserveDefinition &reserve()
	{
		return kind == OperatingSystem::Shield ? get<ShieldDefinition>().reserve : get<MagazineDefinition>().reserve;
	}
	const ReserveDefinition &reserve() const
	{
		return kind == OperatingSystem::Shield ? get<ShieldDefinition>().reserve : get<MagazineDefinition>().reserve;
	}
};
struct CombatDefinition
{
	std::vector<CompartmentDefinition> compartments;
	std::vector<CompartmentConnection> connections;
	std::vector<CombatSystemDefinition> systems;
	std::map<std::string, std::size_t> compartment_ids, system_ids;
	std::map<std::string, std::string> equipment_compartments;
	std::map<std::string, std::vector<FaultDefinition>> faults;
	std::string repair_console, access_controller;
	std::int64_t collision_scale = 1, collision_threshold = 1000, half_height = 5000;
	DestructionDefinition destruction;
};
std::optional<CombatDefinition> compile_combat(const Catalog &, const std::string &entity);
DestructionDefinition compile_destruction(const Json &, const Catalog &, const std::string &path);
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
struct FtlDefinition
{
	ResourceIndex store = no_resource;
	ResourceAmount charge_rate = 0, charging_loss = 0, ready_loss = 0, off_discharge = 0;
};
std::map<std::string, FtlDefinition> compile_ftl(const Catalog &catalog, const std::string &entity);
std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity);
using SystemObservationSchema = std::map<std::string, state_tree::Type>;
std::map<std::string, SystemObservationSchema> compile_system_observations(const Catalog &, const std::string &entity);
// Reuse already compiled ship metadata when validating several Controller trees.
std::map<std::string, SystemObservationSchema>
compile_system_observations(std::span<const FittedSystemSpec>, const ShipResourcesDefinition *, bool advanced);
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
	std::string policy;
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
	Message,
	Effect
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
	std::map<std::string, Bytes> export_companions(const std::string &filename, PackEncoding encoding,
	                                               bool scenario_only) const;
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
	std::string import_model(const std::string &filename, Bytes bytes);
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
