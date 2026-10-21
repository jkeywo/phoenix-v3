module;
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module phoenix.scenario;
export import phoenix.data;
export import phoenix.contracts;

export namespace phoenix
{
using ScenarioId = std::uint64_t;
using ScenarioTick = std::uint64_t;
using ScenarioParameter = std::variant<std::int64_t, bool, std::string>;
inline constexpr std::int64_t scenario_coordinate_limit = 1000000000;
enum class ScenarioRegionShape
{
	Circle,
	Rectangle,
	Annulus
};
enum class ObjectiveStatus
{
	Pending,
	Active,
	Completed,
	Failed,
	Cancelled
};
enum class ScenarioAudience
{
	Ship,
	Consoles
};
enum class ScenarioActorKind
{
	Entity,
	ShipSlot,
	Handle
};
enum class ScenarioActionKind
{
	Spawn,
	Objective,
	Event,
	Publish,
	Finish,
	ObjectiveStatus,
	ObjectivePublication,
	StartSituation
};
enum class ScenarioActionStatus
{
	Pending,
	Succeeded,
	Failed
};
struct ScenarioPoint
{
	std::int64_t x = 0, y = 0;
	bool operator==(const ScenarioPoint &) const = default;
};
struct ScenarioRegion
{
	std::string id;
	ScenarioRegionShape shape = ScenarioRegionShape::Circle;
	ScenarioPoint centre;
	std::int64_t radius = 0, half_width = 0, half_height = 0;
	std::int64_t inner_radius = 0;
	struct Nebula
	{
		std::array<float, 3> colour{.5f, .3f, .8f};
		// Millionths per metre and millionths respectively; authoritative
		// sampling never depends on floating point arithmetic.
		std::int64_t density = 0, sensor_factor = 1000000, height = 100000;
	};
	std::optional<Nebula> nebula;
	Json display = Json::object();
	struct Asteroids
	{
		struct Type
		{
			std::string definition, entry;
			std::int64_t weight = 1000;
		};
		std::vector<Type> types, decorative_types;
		std::uint64_t seed = 1;
		std::int64_t spacing = 25000, density = 400, weight = 1000;
		std::int64_t jitter = 10000, clustering = 500, noise_cells = 8;
		std::int64_t decorative_height = 20000, decorative_fill = 250;
		std::int64_t spin_min = 1000, spin_max = 10000;
		std::int64_t visual_distance = 250000, unload_margin = 50000;
	};
	std::optional<Asteroids> asteroids;
};
struct ScenarioActor
{
	ScenarioActorKind kind = ScenarioActorKind::Entity;
	EntityId entity = 0;
	std::string name;
	bool operator==(const ScenarioActor &) const = default;
};
struct ScenarioRecipients
{
	ScenarioAudience audience = ScenarioAudience::Ship;
	ScenarioActor ship;
	std::vector<std::string> consoles;
	bool operator==(const ScenarioRecipients &) const = default;
};
struct ScenarioSpawnPlacement
{
	std::string definition;
	ScenarioPoint position;
	std::int64_t heading = 0;
};
struct ScenarioSpawnGroup
{
	std::string id;
	std::vector<ScenarioSpawnPlacement> entities;
};
struct ScenarioObjectiveDefinition
{
	std::string id, title_key, description_key;
	std::string competition;
	std::uint32_t region = 0;
	ScenarioActor actor;
	ScenarioRecipients recipients;
};
struct ScenarioObjectiveSequence
{
	std::string id;
	std::vector<std::string> objectives;
	bool repeat = false;
};
struct ScenarioDefinition
{
	std::vector<ScenarioRegion> regions;
	std::map<std::string, std::uint32_t> region_ids;
	std::map<std::string, ScenarioPoint> spawn_points;
	std::map<std::string, ScenarioSpawnGroup> spawn_groups;
	std::map<std::string, ScenarioObjectiveDefinition> objectives;
	std::map<std::string, ScenarioObjectiveSequence> objective_sequences;
	std::vector<std::string> entity_definitions, ship_slots, events, publications;
	std::map<std::string, ScenarioParameter> parameters;
	std::uint32_t spawn_per_tick = 64, spawned_population = 1024, maximum_instances = 128;
	std::uint32_t maximum_actions = 65536, maximum_objectives = 4096, maximum_events = 16384;
	std::string signature;
};
// map JSON is compiled once at the production load boundary. Regions can be
// top-level; scenario-specific definitions live under map.scenario.
ScenarioDefinition compile_scenario(const Json &map, std::span<const std::string> entity_definitions,
                                    std::span<const std::string> ship_slots = {});
bool scenario_contains(const ScenarioRegion &region, ScenarioPoint point);
bool scenario_crosses(const ScenarioRegion &region, ScenarioPoint from, ScenarioPoint to);
std::int64_t scenario_path_length(const ScenarioRegion &region, ScenarioPoint from, ScenarioPoint to);
bool scenario_sensor_visible(std::span<const ScenarioRegion> regions, ScenarioPoint from, ScenarioPoint to,
                             std::int64_t range);
struct GeneratedAsteroid
{
	SceneBody scene{};
	std::string entry;
	bool decorative = false;
};
std::uint64_t asteroid_hash(std::uint64_t value);
std::uint64_t asteroid_map_seed(std::string_view name);
std::vector<GeneratedAsteroid> generate_asteroid_cell(std::span<const ScenarioRegion> regions, std::uint64_t seed,
                                                      std::int64_t x, std::int64_t y);
Json region_markers(std::span<const ScenarioRegion> regions);
Json region_environment(std::span<const ScenarioRegion> regions, double simulation_time);
struct ScenarioActionToken
{
	ScenarioId instance = 0;
	std::uint64_t activation = 0;
	std::uint32_t task = 0;
	std::vector<std::uint32_t> call_path;
	std::vector<std::uint64_t> call_activations;
	bool operator<(const ScenarioActionToken &other) const;
};
struct ScenarioActionResult
{
	ScenarioActionKind kind = ScenarioActionKind::Spawn;
	ScenarioActionStatus status = ScenarioActionStatus::Pending;
	ScenarioId value = 0;
	std::string reason, fingerprint;
	std::string handle;
	std::vector<EntityId> entities;
};
struct ScenarioSpawnRequest
{
	ScenarioId id = 0, instance = 0;
	std::string handle;
	std::vector<ScenarioSpawnPlacement> entities;
	ScenarioActionToken action;
};
struct ScenarioObjective
{
	ScenarioId id = 0, instance = 0;
	std::string definition, handle;
	std::string sequence;
	std::string outcome;
	std::uint64_t cycle = 0;
	std::uint32_t sequence_index = 0;
	ScenarioActor actor;
	ScenarioRecipients recipients;
	ObjectiveStatus status = ObjectiveStatus::Pending;
	ScenarioTick created_tick = 0, completed_tick = 0;
	EntityId completed_actor = 0;
	// Publication and completion credit are explicit acknowledged effects.
	bool completion_published = false, completion_credited = false;
};
struct ScenarioEvent
{
	ScenarioId id = 0, source_instance = 0;
	std::string name;
	std::map<std::string, ScenarioParameter> payload;
	ScenarioTick tick = 0;
};
struct ScenarioPublication
{
	ScenarioId id = 0, instance = 0, objective = 0;
	std::string record;
	ScenarioRecipients recipients;
	ObjectiveStatus status = ObjectiveStatus::Pending;
	std::map<std::string, ScenarioParameter> fields;
	ScenarioTick tick = 0;
};
struct ScenarioMotionSegment
{
	EntityId entity = 0;
	ScenarioPoint from, to;
};
struct ScenarioInstance
{
	ScenarioId id = 0;
	std::string name;
	std::map<std::string, ScenarioParameter> parameters;
	std::map<std::string, std::vector<EntityId>> entity_handles;
	std::map<std::string, ScenarioId> objective_handles;
	std::map<std::string, ScenarioTick> timers;
	ScenarioTick started_tick = 0;
	bool finished = false, succeeded = false;
	std::string finish_reason;
};
class ScenarioRuntime
{
public:
	explicit ScenarioRuntime(ScenarioDefinition definition = {});
	const ScenarioDefinition &definition() const;
	ScenarioId create_instance(std::string name, std::map<std::string, ScenarioParameter> parameters = {});
	const ScenarioActionResult &start_situation(ScenarioActionToken token, std::string configuration, std::string name,
	                                            std::map<std::string, ScenarioParameter> parameters = {});
	const std::map<ScenarioId, ScenarioInstance> &instances() const;
	const std::map<ScenarioId, ScenarioObjective> &objectives() const;
	const std::map<ScenarioId, std::string> &spawned_entities() const;
	const ScenarioActionResult *result(ScenarioActionToken token) const;
	const ScenarioActionResult &spawn_entity(ScenarioActionToken token, std::string definition, ScenarioPoint position,
	                                         std::int64_t heading, std::string handle);
	const ScenarioActionResult &spawn_group(ScenarioActionToken token, std::string group, ScenarioPoint offset,
	                                        std::string handle);
	std::vector<ScenarioSpawnRequest> pending_spawns() const;
	// Caller commits a whole request through production construction, then confirms
	// actual IDs. Empty IDs plus a reason records a failed construction. Never
	// acknowledge partial construction; caller's world operation must be atomic.
	void confirm_spawn(ScenarioId request, std::span<const EntityId> entities, std::string failure = {});
	const ScenarioActionResult &add_objective(ScenarioActionToken token, std::string definition, std::string handle,
	                                          std::optional<ScenarioActor> actor = {}, bool activate = true,
	                                          std::string sequence = {});
	const ScenarioActionResult &set_objective_status(ScenarioActionToken token, ScenarioId objective,
	                                                 ObjectiveStatus status);
	const ScenarioActionResult &publish_objective(ScenarioActionToken token, ScenarioId objective);
	std::optional<ObjectiveStatus> objective_status(ScenarioId objective) const;
	std::optional<ScenarioId> objective_handle(ScenarioId instance, std::string_view handle) const;
	const ScenarioActionResult &emit(ScenarioActionToken token, std::string name,
	                                 std::map<std::string, ScenarioParameter> payload = {});
	std::vector<ScenarioEvent> events(std::string_view name, ScenarioId after = 0) const;
	const ScenarioActionResult &publish(ScenarioActionToken token, std::string record, ScenarioRecipients recipients,
	                                    std::map<std::string, ScenarioParameter> fields = {});
	const ScenarioActionResult &finish(ScenarioActionToken token, bool success, std::string reason = {});
	void start_timer(ScenarioId instance, std::string name);
	bool elapsed(ScenarioId instance, std::string_view timer, ScenarioTick duration) const;
	bool actor_in_region(ScenarioId instance, const ScenarioActor &actor, std::string_view region) const;
	std::optional<EntityId> resolve_actor(ScenarioId instance, const ScenarioActor &actor) const;
	// begin_tick sets the committed world view before scenario actions. evaluate
	// receives all actually executed segments, including zero-length stationary ones.
	void begin_tick(ScenarioTick tick, std::map<std::string, EntityId> slots,
	                std::map<EntityId, ScenarioPoint> positions);
	void evaluate(std::span<const ScenarioMotionSegment> movement);
	void withdraw_actor(EntityId entity);
	bool mission_complete() const;
	std::vector<ScenarioPublication> pending_publications() const;
	void acknowledge_publication(ScenarioId publication);
	void acknowledge_credit(ScenarioId objective);
	Json save() const;
	void restore(const Json &state);

private:
	ScenarioDefinition definition_;
	ScenarioId next_id_ = 1;
	ScenarioTick tick_ = 0;
	std::uint32_t spawned_this_tick_ = 0;
	std::map<ScenarioId, ScenarioInstance> instances_;
	std::map<ScenarioId, ScenarioObjective> objectives_;
	std::map<ScenarioId, std::string> spawned_;
	std::map<ScenarioId, ScenarioSpawnRequest> requests_;
	std::map<ScenarioActionToken, ScenarioActionResult> actions_;
	std::vector<ScenarioEvent> events_;
	std::vector<ScenarioPublication> publications_;
	std::map<std::string, EntityId> slots_;
	std::map<EntityId, ScenarioPoint> positions_;
	ScenarioInstance &instance(ScenarioId id);
	EntityId resolve(ScenarioId instance, const ScenarioActor &actor) const;
	void validate_actor(const ScenarioActor &actor) const;
	void validate_recipients(const ScenarioRecipients &recipients) const;
	ScenarioId allocate();
	ScenarioActionResult &admit(ScenarioActionToken token, ScenarioActionKind kind, std::string fingerprint);
	const ScenarioActionResult &spawn(ScenarioActionToken token, std::vector<ScenarioSpawnPlacement> entities,
	                                  std::string handle, std::string fingerprint);
	void objective_publication(const ScenarioObjective &objective);
};
} // namespace phoenix
