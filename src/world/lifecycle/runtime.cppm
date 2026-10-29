module;
#include "native_headers.hpp"

export module phoenix.simulation:runtime;
import phoenix.simulation;
export import phoenix.content.assets;
export import phoenix.content.combat;
export import phoenix.content.controllers;
export import phoenix.content.effects;
export import phoenix.content.equipment;
export import phoenix.content.knowledge;
export import phoenix.content.logistics;
export import phoenix.content.operations;
export import phoenix.content.resources;
export import phoenix.content.scenarios;
export import phoenix.content.sensing;
export import phoenix.content.ui;
export import phoenix.content.verification;
export import phoenix.resources;
export import phoenix.conversations;
export import phoenix.gameplay_vocabulary;
export import phoenix.gameplay_scenario;
export import phoenix.knowledge.consoles;
export import phoenix.knowledge.projections;
export import phoenix.knowledge.read_models;
export import phoenix.control.controllers;
export import phoenix.control.views;
export import phoenix.control.templates;
export import phoenix.control.coordination;
export import phoenix.control.protocol;
export import phoenix.control.native_commands;
export import phoenix.control.coordination_facts;
export import phoenix.control.execution;
export import phoenix.control.providers;
export import phoenix.control.ownership;
export import phoenix.control.handover;
export import phoenix.effects.contracts;
export import phoenix.effects.abilities;
export import phoenix.effects.processes;
export import phoenix.effects.rules;
export import phoenix.effects.properties;
export import phoenix.effects.rule_state;
export import phoenix.world.state;
export import phoenix.world.persistence;
export import phoenix.world.assembly;
export import phoenix.world.spatial;
export import phoenix.gameplay.flight;
export import phoenix.gameplay.flight.native;
export import phoenix.gameplay.fitting;
export import phoenix.gameplay.combat.faults;
export import phoenix.gameplay.docking;
export import phoenix.gameplay.craft;
export import phoenix.gameplay.logistics;
export import phoenix.gameplay.ftl;
export import phoenix.gameplay.ftl.coordinator;
export import phoenix.gameplay.combat.state;
export import phoenix.gameplay.directed;
export import phoenix.gameplay.ordnance;
export import phoenix.gameplay.projectiles;
export import phoenix.gameplay.ordnance.contacts;
export import phoenix.gameplay.repairs;
export import phoenix.gameplay.damage;
export import phoenix.gameplay.combat.continuation;
export import phoenix.gameplay.weapons;
export import phoenix.gameplay.personnel.state;
export import phoenix.gameplay.personnel.clinical;
export import phoenix.gameplay.personnel.treatment;
export import phoenix.gameplay.personnel.runtime;
export import phoenix.gameplay.teams.routes;
export import phoenix.resources.equipment;
export import phoenix.effects.events;
export import phoenix.effects.grants;
export import phoenix.effects.transactions;
export import phoenix.effects.native;
export import phoenix.effects.clocks;
export import phoenix.resources.processing;
export import phoenix.gameplay.machinery;
export import phoenix.gameplay.flight.work;
export import phoenix.effects.machinery;
export import phoenix.effects.finite_work;
export import phoenix.effects.process_validation;
export import phoenix.effects.continuation_validation;
export import phoenix.effects.ability_validation;
export import phoenix.effects.bindings;
export import phoenix.effects.funding;
export import phoenix.effects.preparation;
export import phoenix.gameplay.combat.outcomes;
export import phoenix.gameplay.combat.shields;
export import phoenix.gameplay.personnel.outcomes;
export import phoenix.gameplay.away.state;
export import phoenix.gameplay.away.operations;
export import phoenix.gameplay.away.crew;
export import phoenix.gameplay.boarding.runtime;
export import phoenix.scenarios.objectives;
export import phoenix.scenarios.environment.streaming;
export import phoenix.gameplay.psychic.state;
export import phoenix.scenario.interdictions;
export import phoenix.gameplay.psychic.emission;
export import phoenix.gameplay.psychic.interference;
export import phoenix.gameplay.psychic.contributions;
export import phoenix.gameplay.psychic.receptions;
export import phoenix.gameplay.psychic.projection;
export import phoenix.gameplay.sensors.state;
export import phoenix.gameplay.sensors.envelopes;
export import phoenix.gameplay.sensors.acquisition;
export import phoenix.gameplay.sensors.findings;
export import phoenix.gameplay.sensors.validation;
export import phoenix.gameplay.sensors.projection;
export import phoenix.gameplay.sensors.coverage;
export import phoenix.gameplay.sensors.evidence;
export import phoenix.gameplay.away.runtime;
export import phoenix.world.schedule.profiling;
export import phoenix.effects.properties.runtime;
export import phoenix.effects.active_rules;

export namespace phoenix::world_detail
{

inline constexpr std::int64_t rate = World::ticks_per_second;
struct Clock
{
	std::uint64_t tick = 0;
};

struct FactionMembership
{
	std::string id;
};

struct CollisionEntry
{
	EntityId id;
	Pose *pose;
	Motion *motion;
	CollisionBody body;
};
struct ContactEntry
{
	EntityId id;
	flecs::entity entity;
	fixed::Vec position;
};
Bounds collision_bounds(const CollisionBody &body)
{
	// Match the existing conservative circle/rotated-box rejection exactly.
	return Bounds::around(body.position, body.box ? body.half_width + body.half_length : body.radius);
}
BodyView view(EntityId id, const Definition &definition, const Pose &pose, const Motion &motion)
{
	BodyView result{id,
	                definition.name,
	                pose.position.x,
	                pose.position.y,
	                motion.velocity.x,
	                motion.velocity.y,
	                pose.heading,
	                definition.radar_icon,
	                definition.radar_sprite};
	result.altitude = pose.altitude;
	result.vz = motion.vertical_velocity;
	return result;
}
bool admissible(OperatingRequest output, const Engine *engine, const Steering *steering, const Radar *radar)
{
	return operating_permitted(
	    output.action, output.value,
	    {engine ? std::optional{engine->max_speed} : std::nullopt, steering != nullptr, radar != nullptr});
}
void apply(OperatingRequest output, Engine *engine, Steering *steering, Radar *radar)
{
	// Both controllers and admitted external commands use this capability gate.
	if (!admissible(output, engine, steering, radar))
		return;
	switch (output.action)
	{
	case OperatingAction::Speed:
		engine->speed = output.value;
		break;
	case OperatingAction::Heading:
		steering->heading = output.value;
		break;
	case OperatingAction::EngineEnabled:
		engine->enabled = output.value != 0;
		break;
	case OperatingAction::RadarEnabled:
		radar->enabled = output.value != 0;
		break;
	default:
		break;
	}
}

struct Runtime
{

	using Impl = Runtime;

	// Declare the world before its handles so cached queries are destroyed first.
	flecs::world ecs;
	std::vector<std::pair<std::string, std::string>> packs;
	std::map<std::string, CompiledDefinition> definitions;
	std::map<std::string, Json> console_layouts;
	std::map<std::string, ShieldEffectDefinition> shield_templates;
	std::map<std::string, ModifierEffectDefinition> modifier_templates;
	std::map<std::string, EffectGroupDefinition> effect_groups;
	std::map<std::string, AbilityGrantDefinition> ability_grant_templates;
	SharedProcessCatalog process_catalog;
	std::map<std::string, ProcessGrantDefinition> process_grant_templates;
	SharedResourceCatalogue resource_catalogue;
	SharedPropertyCatalog property_catalogue;
	std::uint64_t next_shield_hit = 1;
	std::map<std::string, std::vector<std::string>> camera_widgets;
	std::string map;
	FactionCatalog factions;
	std::map<std::string, std::string> ships;
	std::map<std::string, EntityId> slot_entities;
	// This is a boundary lookup index, never the tick's iteration source.
	std::map<EntityId, flecs::entity> entities;
	ConsoleOwnership ownership;
	struct DisplayPublication
	{
		std::uint64_t claimant = 0, revision = 0;
		ConsoleAddress address;
		std::string widget;
		std::vector<std::int64_t> camera;
	};
	std::map<EntityId, DisplayPublication> displays;
	std::uint64_t display_revision = 0;
	bool explicit_stations = false;
	std::vector<ControllerProposal> controller_pending;
	std::vector<CollisionEntry> collisions;
	std::map<std::string, float> visual_scales;
	std::vector<ContactEntry> contact_rows;
	std::vector<Bounds> collision_boxes, contact_boxes;
	SpatialIndex collision_index, contact_index;
	PropertyApplicationStore property_applications;
	ActiveRuleStore active_rules;
	flecs::query<const StableId, const Definition, const Pose, const Motion> bodies;
	std::unique_ptr<NativeFlightActuators> flight_native;
	flecs::query<const Equipment, SensorRuntime> sensor_query;
	flecs::query<const Equipment, PsychicChoirState> choir_query;

	flecs::query<const Equipment, const ResourceBinding> passive_resource_query;
	flecs::query<const StableId, const GameplayShip> advanced_ships;
	flecs::query<const StableId, const CombatStructure> combat_ships;
	ProjectileStore projectiles;
	flecs::query<CraftState> craft_query;
	flecs::query<const StableId, MaterialInventory> inventory_query;
	flecs::query<const StableId, DockingState> docking_query;
	flecs::query<const StableId, DockConnection> dock_connection_query;
	std::vector<MaterialRequest> material_requests;
	std::vector<MaterialBatch> material_batches;
	std::uint64_t next_material_request = 1, next_material_batch = 1;
	flecs::query<const StableId, VerticalMotion> vertical_query;
	flecs::system integrate, resolve_collisions, index_contacts;
	bool instance_systems = false;
	GameplayKnowledge knowledge;
	std::optional<GameplayScenarioBridge> scenario;
	std::vector<ScenarioRegion> regions;
	std::map<EntityId, std::uint32_t> sprite_choices;
	mutable SensorEnvelopes sensor_envelopes;
	std::uint64_t environment_tick = 0;
	EntityId next_entity = 1;
	std::map<EntityId, fixed::Vec> step_starts;
	using TelemetrySnapshot = ProjectionTelemetry;
	using ContactSnapshot = ProjectionContacts;
	ConsoleDecisionFrame decision_frame;
	mutable ConsolePresentationCache presentation;
	void clear_presentation_snapshots();
	bool gameplay = false;
	std::vector<std::pair<std::uint64_t, OperatingCommand>> meta_pending;
	// Authoritative generation uses integer arithmetic.
	static constexpr EntityId field_identity_base = EntityId{1} << 52;
	std::map<EntityId, FieldRock> field_rocks;
	AsteroidWindowCache field_window;
	std::uint64_t field_seed = 1;
	void load_rock(FieldRock &rock);
	void unload_rock(FieldRock &rock);
	void stream_fields();
	Json save_fields() const;
	void restore_fields(const Json &saved);

	Runtime();
	~Runtime();
	void compile(const Catalog &source, const DirectedCatalog &directed_catalog, SharedPropertyCatalog properties);
	void create(EntityId id, const std::string &definition, const Json &transform);
	const CompiledDefinition &definition(EntityId ship) const;
	NativeOwnerDefinitions native_owner_definitions(EntityId id) const;
	const ConsoleSpec *seat(EntityId ship, const std::string &name) const;
	std::optional<EquipmentRef> instance(flecs::entity ship, const std::string &id) const;
	std::uint64_t console_operator(const ConsoleAddress &address) const;
	ConsoleAddress command_address(std::uint64_t claimant, const OperatingCommand &command) const;
	ConsoleAddress system_owner(EntityId ship, const std::string &system) const;
	std::optional<FlightCommandKind> flight_action(OperatingAction action) const;
	std::optional<ResourceControl> resource_action(OperatingAction action) const;
	bool operate(flecs::entity entity, const std::string &name, OperatingRequest output, bool execute);
	void publish_waypoint(EntityId ship, const ControllerChannelState &state, bool reached);
	void waypoint_arrivals();
	// Ship-owned derived evidence. Controllers only read the published assessment.
	void refresh_navigation_blackboard(EntityId id);
	// Execution endpoints add grant guards to the existing Controller command envelope.
	bool controller_arguments_valid(EntityId ship, std::size_t controller, std::size_t channel, OperatingAction action,
	                                std::int64_t value, std::span<const std::int64_t> arguments,
	                                bool restoring = false) const;
	// Textual selections retain normal Console ownership and generation admission.
	bool selection_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute);
	// Reset only the selected settings; a shared Controller can serve other duties.
	void clear_controller_settings(EntityId ship, std::size_t controller_index,
	                               std::initializer_list<std::size_t> channels);
	// World::Impl methods. Physical mutation is confined to command consumption below.
	std::vector<state_tree::Value>
	controller_observations(EntityId id, const ConsoleControllerRuntime &controller,
	                        const Observation *projected = nullptr,
	                        const GameplayKnowledgeProjection *projected_board = nullptr);
	void controller_output(EntityId id, std::size_t index, const state_tree::Output &output,
	                       std::span<const state_tree::Value> observations,
	                       const state_tree::Program *execution = nullptr);
	std::optional<ControllerProposal> controller_proposal(const ConsoleAddress &address, OperatingCommand command,
	                                                      bool human) const;
	bool controller_command_effect_matches(flecs::entity entity, const ControllerCommandSpec &spec,
	                                       const ControllerProposal &proposal) const;
	bool commit_controller(const ControllerProposal &proposal);
	ControllerCommandChecks controller_command_checks(EntityId id) const;
	void refresh_controllers(EntityId id, bool validate = false);
	std::uint64_t coordinated_generation(const ConsoleAddress &a, const std::string &group) const;
	bool coordinated_impulse(const ConsoleAddress &a, const OperatingCommand &c, bool execute);
	bool controller_meta(const ConsoleAddress &address, const OperatingCommand &command, bool execute);
	void run_console_controllers(flecs::entity entity);
	void consume_controller_commands(flecs::entity entity);
	Json project_controllers(const ConsoleAddress &address, const Observation &projected);

	void restore_controllers(flecs::entity entity, const Json &saved);
	// World::Impl: shared action execution, independent of the selecting policy.
	void controller_decision_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
	                               std::vector<state_tree::Value> &values) const;
	ControllerActionPorts controller_action_ports(EntityId id, std::size_t index,
	                                              std::span<const state_tree::Value> source,
	                                              std::optional<Observation> &fallback);
	void cancel_controller_action(EntityId id, std::size_t index, const std::string &reason,
	                              std::span<const state_tree::Value> source);
	bool select_controller_action(EntityId id, std::size_t index, const std::string &name,
	                              const std::string &invocation, std::span<const state_tree::Value> inputs,
	                              std::span<const state_tree::Value> source, std::uint64_t generation);
	void run_controller_action(EntityId id, std::size_t index, std::span<const state_tree::Value> source);
	bool admit_controller_decision(const ControllerDecisionProposal &proposal,
	                               std::span<const state_tree::Value> observations);
	// World-side delivery/routing adapter. Protocol state operations are independently compiled.
	const OfficerRequest *officer_pending(const ConsoleControllerRuntime &controller) const;
	void restore_controller_coordination(EntityId ship, ConsoleControllerRuntime &controller, const Json &saved);
	KnowledgeId officer_message(EntityId id, ConsoleControllerRuntime &controller, const OfficerRequest &request,
	                            bool reply);
	void process_controller_coordination(EntityId id, std::size_t index);
	void controller_coordination_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
	                                   std::vector<state_tree::Value> &values) const;

	bool controller_coordination_action(EntityId id, std::size_t index, const std::string &name,
	                                    std::span<const state_tree::Value> args);
	// Included in World::Impl. Inference is external; these operations are deterministic.
	ConsoleControllerRuntime *model_controller(const ControllerAddress &address);
	Json model_basis(EntityId id, const ConsoleControllerRuntime &controller);
	Json model_snapshot(const ControllerAddress &address);
	void model_fallback(EntityId id, std::size_t index, const std::string &reason);
	void model_deviation(const ControllerAddress &address, ConsoleControllerRuntime &controller,
	                     const std::string &reason);
	std::int64_t sensor_range(EntityId ship, const std::string &name, bool outer = false) const;
	const SensorEnvelope &sensor_envelope(EntityId ship, const std::string &name, std::int64_t range) const;

	std::map<std::string, std::int64_t> telemetry(EntityId id, std::size_t index) const;
	// Resource identities are projected once, so shared reserves are never counted per fitting.
	Json power_board(const ConsoleAddress &address) const;
	// Append contacts using only the permitted radar feeds supplied by the caller.
	void append_sensor_contacts(const ConsoleAddress &address, Observation &result,
	                            const std::map<EntityId, Json> &evidence,
	                            ContactSnapshot *contact_snapshot = nullptr) const;
	Observation capture_sensor_observation(const ConsoleAddress &address) const;
	std::vector<BodyView> capture_sensor_contacts(const ConsoleAddress &address) const;
	// Human projection and frozen decisions share the same permitted physical feeds.
	// Decisions need combat facts, telemetry, contacts and admission revisions, without
	// constructing presentation-only lists and sensor reach geometry for every Console.
	enum class ObservationDetail
	{
		Presentation,
		Decision,
		Contacts
	};
	Observation capture_console_observation(const ConsoleAddress &address, Json gameplay = Json::object(),
	                                        ObservationDetail detail = ObservationDetail::Presentation,
	                                        TelemetrySnapshot *telemetry_snapshot = nullptr,
	                                        ContactSnapshot *contact_snapshot = nullptr) const;
	Observation capture_decision_observation(const ConsoleAddress &address,
	                                         TelemetrySnapshot *telemetry_snapshot = nullptr,
	                                         ContactSnapshot *contact_snapshot = nullptr) const;
	// Visibility consumers use the identical Console feed/type/contact boundary,
	// without building combat and UI boards just to discard them.
	std::vector<BodyView> capture_console_contacts(const ConsoleAddress &address) const;
	NativeFlightPorts native_flight_ports();
	template <typename... Components>
	std::vector<flecs::entity> ordered_equipment(const flecs::query<Components...> &query, EntityId ship = 0) const
	{
		std::vector<flecs::entity> rows;
		auto iterator = query.iter();
		if (ship)
			iterator.set_var("ship", entities.at(ship));
		iterator.run(
		    [&](flecs::iter &it)
		    {
			    while (it.next())
				    for (auto row : it)
				    {
					    auto entity = it.entity(row);
					    rows.push_back(entity);
				    }
		    });
		std::sort(rows.begin(), rows.end(),
		          [](flecs::entity a, flecs::entity b)
		          {
			          const auto &x = a.get<Equipment>();
			          const auto &y = b.get<Equipment>();
			          return std::pair{a.parent().get<StableId>().id, x.ordinal} <
			                 std::pair{b.parent().get<StableId>().id, y.ordinal};
		          });
		return rows;
	}

	void request_passive_resources(flecs::entity ship);

	std::map<std::pair<EntityId, std::string>, PreparedDirectedOperation> directed_operations;
	void release_directed_operations(flecs::entity ship);
	using ConsoleContacts = std::map<ConsoleAddress, Observation>;
	const Observation &console_detection(const ConsoleAddress &owner, ConsoleContacts &cache) const;
	const std::vector<BodyView> &console_contacts(const ConsoleAddress &owner, ConsoleContacts &cache) const;
	std::optional<DirectedAim> directed_contact(EntityId ship, const std::string &id, EntityId wanted,
	                                            ConsoleContacts &cache, bool allow_bearing = true) const;
	const DirectedDefinition *directed_definition(EntityId ship, const std::string &id) const;
	const DirectedModeDefinition *directed_mode(EntityId ship, const std::string &id) const;
	EntityId directed_target(EntityId ship, const std::string &id) const;
	bool directed_operation(EntityId ship, const std::string &id, DirectedOperationKind kind) const;
	bool directed_compatible(const DirectedDefinition &d, std::size_t mode, std::size_t targeting) const;
	DirectedTargetingKind directed_targeting(EntityId ship, const std::string &id) const;
	EntityId directed_mount_target(flecs::entity ship, flecs::entity mount) const;
	void apply_directed_mode(flecs::entity ship, flecs::entity child, bool restoring = false);
	void initialize_directed(flecs::entity ship);
	bool directed_setting(EntityId ship, const std::string &id, const std::string &suffix, std::int64_t value,
	                      bool execute);
	bool directed_enabled(flecs::entity ship, flecs::entity child, DirectedOperationKind kind) const;
	void request_directed_resources(flecs::entity ship);
	template <class Operation>
	bool execute_directed(flecs::entity ship, flecs::entity child, DirectedOperationKind kind, bool qualified,
	                      Operation &&operation)
	{
		if (!child.has<DirectedRuntime>())
			return false;
		auto &state = child.get_mut<DirectedRuntime>();
		const bool enabled = directed_enabled(ship, child, kind);
		DirectedPower funding;
		if (enabled && qualified)
			if (const auto *power = ship.try_get<ShipResources>(); power && child.has<ResourceBinding>())
			{
				const auto slot = child.get<ResourceBinding>().slot;
				const auto &grant = power->grants.at(slot);
				funding = {true, grant.blocked, power->definition.systems.at(slot).discrete_energy != 0,
				           grant.discrete_paid, static_cast<int>(grant.supply_permille)};
			}
		if (!begin_directed_operation(state, kind, enabled, qualified, funding))
			return false;
		return finish_directed_operation(state, operation());
	}
	Json directed_projection(EntityId ship, const std::string &id) const;
	Json save_directed() const;
	void restore_directed(const Json &rows);
	// Native receipts and apparent-contact ownership are available only after their
	// corresponding state has been restored. Intent itself is parsed exactly once.
	void validate_directed_commitments() const;
	// Native damage owns geometry and allocation;
	// authored hooks supply one frozen quote per reached native subject.
	struct DamageRuleFrame
	{
		std::string source;
		NativeRuleContext context;
		RuleEvaluation evaluation;
		bool disabled = false;
	};
	NativeRuleContext damage_rule_context(const PayloadHitContext &hit, std::int64_t presented, EntityId attacker = 0,
	                                      const Projectile *payload = nullptr,
	                                      const NativeRuleContext *origin = nullptr) const;
	template <class Callback>
	void visit_damage_rule_bindings(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &event,
	                                const ShieldContribution *shield, Callback &&callback,
	                                const std::vector<NativeRuleSource> *sources = nullptr,
	                                const NativeRuleContext *shield_origin = nullptr) const
	{
		const auto local_sources = sources ? std::vector<NativeRuleSource>{} : rule_sources(ship);
		for (const auto &source : sources ? *sources : local_sources)
			visit_rule_event_bindings(source, trigger, event, callback);
		if (shield)
		{
			NativeRuleSource source;
			source.kind = RuleOwnerKind::Shield;
			source.key = "shield:" + std::to_string(ship.get<StableId>().id) + ":" + std::to_string(shield->effect_id) +
			             ":" + shield->definition.name;
			source.owner = event.subject;
			source.context = shield_origin
			                     ? *shield_origin
			                     : native_rule_origin({ship.get<StableId>().id, shield->equipment}, shield->source,
			                                          NativeEffectKind::Shield, shield->effect_id);
			source.eligible = true;
			source.bindings = &shield->definition.shield.rules;
			visit_rule_event_bindings(source, trigger, event, callback);
		}
	}
	std::vector<DamageRuleFrame> freeze_damage_rules(flecs::entity ship, RuleTrigger trigger,
	                                                 const NativeRuleContext &event,
	                                                 const ShieldContribution *shield = nullptr,
	                                                 const std::vector<NativeRuleSource> *sources = nullptr,
	                                                 const NativeRuleContext *shield_origin = nullptr) const;
	static std::int64_t damage_rule_factor(const std::vector<DamageRuleFrame> &frames);
	static EffectGroupInvocation damage_rule_invocation(const DamageRuleFrame &frame);
	std::int64_t apply_pre_damage_frames(std::vector<DamageRuleFrame> &frames, NativeRuleContext &context);
	std::int64_t apply_pre_damage_rules(flecs::entity ship, RuleTrigger trigger, NativeRuleContext &context,
	                                    const std::vector<NativeRuleSource> *sources = nullptr);
	void emit_damage_rules(flecs::entity ship, RuleTrigger trigger, const NativeRuleContext &context,
	                       const ShieldContribution *shield = nullptr,
	                       const std::vector<NativeRuleSource> *sources = nullptr,
	                       const NativeRuleContext *shield_origin = nullptr);
	// Short-lived cosmetic receipts are deliberately absent from checkpoints.
	// Playback baselines on a new world, while continuous cues come from live state.
	std::map<EntityId, std::vector<SceneAudio>> audio_events;
	std::uint64_t next_audio_event = 1;
	void emit_audio(flecs::entity ship, SceneAudio cue);
	void retire_audio();
	void refresh_presentation_audio(bool baseline = false);
	void emit_arrival_audio(EntityId id);
	struct AudioStatus
	{
		std::set<std::uint64_t> delivered;
		std::map<std::string, std::uint64_t> abilities;
		std::uint64_t next_comms = 0, next_warning = 0;
		unsigned power_shortage_ticks = 0;
		bool critical = false;
	};
	std::map<EntityId, AudioStatus> audio_status;
	bool audio_baselined = false;
	bool equipment_destroyed(flecs::entity ship, const std::string &equipment) const;
	bool hard_fault_disabled(flecs::entity ship, const std::string &equipment) const;
	std::int64_t fault_effectiveness(flecs::entity ship, const std::string &equipment) const;
	void damage_faults(flecs::entity ship, std::size_t compartment, std::int64_t damage,
	                   std::vector<std::string> *affected = nullptr, const std::string &only_equipment = {},
	                   const NativeRuleContext *origin = nullptr,
	                   const std::vector<NativeRuleSource> *sources = nullptr);

	std::int64_t shield_property(flecs::entity ship, const ShieldContribution &c, const std::string &property,
	                             std::int64_t base, std::int64_t extra_delta = 0) const;
	std::int64_t shield_property(flecs::entity ship, const ShieldContribution &c, PropertyKey property,
	                             std::int64_t base, std::int64_t extra_delta = 0) const;
	void step_shield_effects();
	void request_shields(flecs::entity ship);
	void actuate_shields(flecs::entity ship);
	bool shield_available(flecs::entity ship, const ShieldContribution &c) const;
	std::int64_t absorb_shields(flecs::entity ship, std::int64_t heading, std::int64_t damage, std::int64_t bypass,
	                            std::int64_t *carry = nullptr, EntityId source = 0, const Projectile *payload = nullptr,
	                            const NativeRuleContext *origin = nullptr,
	                            const std::vector<NativeRuleSource> *sources = nullptr,
	                            std::int64_t *native_inflicted = nullptr);
	// World::Impl combat adapter. All decisions originate at Controllers; mechanisms consume admitted intent.
	std::vector<flecs::entity> projectile_rows() const;
	std::vector<std::pair<EntityId, flecs::entity>> combat_rows() const;
	bool combat_duel = false;
	bool duel_ended() const;
	bool combat_dead(flecs::entity ship) const;
	bool structurally_disabled(flecs::entity ship, const std::string &equipment) const;
	std::int64_t combat_damage(flecs::entity ship, std::size_t compartment, std::int64_t amount,
	                           const Projectile *payload = nullptr, const NativeRuleContext *origin = nullptr,
	                           const std::vector<NativeRuleSource> *sources = nullptr,
	                           std::int64_t *native_inflicted = nullptr);

	bool operate_combat(flecs::entity ship, flecs::entity child, OperatingRequest output, bool execute);
	void combat_telemetry(flecs::entity ship, flecs::entity child, std::map<std::string, std::int64_t> &v) const;
	void apply_structural_disablement();
	WeaponMagazines weapon_magazines(flecs::entity ship, const CombatMechanism &weapon) const;
	const AmmunitionDefinition &magazine_ammunition(const CombatMechanism &magazine, const std::string &resource) const;
	const AmmunitionDefinition &magazine_ammunition(flecs::entity ship, const CombatMechanism &magazine) const;
	const AmmunitionDefinition &weapon_ammunition(flecs::entity ship, const CombatMechanism &weapon,
	                                              std::int64_t magazine = -1) const;
	std::int64_t load_duration(flecs::entity ship, const CombatMechanism &m, std::int64_t magazine = -1) const;
	const ProjectileDefinition &weapon_payload(flecs::entity ship, const CombatMechanism &m) const;
	using WeaponAim = DirectedAim;
	std::optional<WeaponAim> weapon_contact(flecs::entity ship, const CombatMechanism &m, ConsoleContacts &cache) const;
	std::pair<fixed::Vec, std::int64_t> weapon_mount(flecs::entity ship, const CombatMechanism &m) const;
	fixed::Vec weapon_lead(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target,
	                       fixed::Vec origin) const;
	bool weapon_covers(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target, fixed::Vec origin,
	                   std::int64_t orientation, fixed::Vec lead) const;
	std::uint64_t point_defence_target(flecs::entity ship, const CombatSystemDefinition &d,
	                                   const std::vector<BodyView> &contacts) const;
	void prepare_weapons(flecs::entity ship);
	void fire_weapons(flecs::entity ship);
	void prepare_combat(flecs::entity ship);
	void request_combat_resources(flecs::entity ship);
	void actuate_combat(flecs::entity ship);

	void penetrate(Projectile &p, flecs::entity ship, fixed::Vec start, fixed::Vec end);
	std::vector<BlastVisual> blast_visuals;
	std::uint64_t next_blast = 1;
	// Run after shared rule/payload catalogues and native fittings are compiled,
	// before admitting gameplay. Destruction must never discover an oversized
	// launch-capture layout after native damage has already committed.
	void validate_native_payload_layouts() const;
	std::optional<std::int64_t> volume_hit(fixed::Vec a, fixed::Vec b, std::int64_t za, std::int64_t zb,
	                                       CollisionBody target, fixed::Vec target_start, std::int64_t tz_start,
	                                       std::int64_t tz_end, std::int64_t half_height,
	                                       std::int64_t radius = 1) const;
	bool blast_occluded(fixed::Vec origin, std::int64_t z, fixed::Vec end, std::int64_t ez, EntityId ignore) const;
	BlastExposure blast_exposure(fixed::Vec origin, std::int64_t altitude, flecs::entity target) const;
	void emit_blast(fixed::Vec origin, std::int64_t altitude, const DestructionDefinition &d, EntityId source = 0,
	                const std::string &weapon = {}, const std::vector<RuleCapturedFact> *origin_captures = nullptr);
	void emit_native_destroy_rules(flecs::entity object, const RuleActor &actor,
	                               const std::vector<NativeRuleSource> *snapshot = nullptr);
	void destroy_missile(flecs::entity row, bool warhead);
	void resolve_destructions();
	void fire_beam(flecs::entity ship, flecs::entity child, fixed::Vec origin, std::int64_t bearing);
	void step_ordnance();
	std::pair<std::int64_t, std::int64_t> repair_route(flecs::entity ship, std::size_t from, std::size_t to,
	                                                   std::int64_t *total_cost = nullptr) const;

	RepairCandidate repair_candidate(flecs::entity ship, std::size_t compartment, const std::string &equipment = {},
	                                 const std::set<RepairFault> &claimed = {}) const;
	// Shared by ordinary repair orders and away staging. Inspect at each arrival.
	bool advance_repair_team(flecs::entity ship, flecs::entity child);
	void route_repair_team(flecs::entity ship, TeamState &r, std::size_t target);
	// Repair effort is milli-HP per second; compartment work carries sub-tick fractions.
	std::int64_t structural_repair_work(flecs::entity ship, std::vector<flecs::entity> teams, EntityId target,
	                                    std::int64_t compartment = -1, const std::string &work_key = {},
	                                    std::int64_t progress = 0, std::int64_t required = 0);
	void apply_structural_repair(CombatStructure &structure, std::size_t index, std::int64_t work);
	void finish_fault_repair(CombatStructure &structure, const std::string &system, std::size_t index);

	void step_repairs();
	void collision_damage(EntityId a, EntityId b, const CollisionBody &before_a, const CollisionBody &before_b);
	bool repair_team_present(flecs::entity ship, std::size_t compartment) const;
	void project_combat(const ConsoleAddress &address, Observation &result) const;
	Json save_combat(flecs::entity ship) const;
	void restore_combat(flecs::entity ship, const Json &j);
	Json save_projectiles() const;
	void restore_projectiles(const Json &saved);

	Json save_shields(flecs::entity ship) const;
	void restore_shields(flecs::entity ship, const Json &value);
	// Native owners retain stored property values.

	CustomPropertyState *custom_property_state(const CustomPropertyAddress &address) const;

	void validate_custom_property_value(const std::string &id, const CustomPropertyValue &value) const;
	CustomPropertyValue effective_custom_property(const CustomPropertyAddress &address, const std::string &id,
	                                              const CustomPropertyState &state, bool retiring = false) const;
	std::optional<state_tree::Value> read_custom_property(const RuleActor &actor, const std::string &id,
	                                                      bool base = false) const;
	std::vector<CustomPropertyAddress> custom_property_addresses(EntityId id) const;
	// Fractional work belongs to a native execution lifetime. Withdrawal while
	// paused keeps it; permanent source erasure releases it on every actual owner.

	std::set<std::string> retired_custom_property_rates;
	bool custom_property_native_rates_dirty = false;
	void mark_custom_property_rates_dirty();
	void erase_custom_property_rate_source(const std::string &source);
	void prune_custom_property_rates(std::span<const NativeRuleSource> sources);
	void commit_custom_property_state(const CustomPropertyAddress &address, const CustomPropertyState &value);
	const CustomPropertyState &custom_property_declarations(const CustomPropertyAddress &address) const;
	Json save_custom_properties() const;
	void restore_custom_properties(const Json &saved);

	struct CustomPropertyBefore
	{
		CustomPropertyState state;
		std::map<std::string, CustomPropertyValue> effective;
	};
	bool suppress_custom_property_events = false;
	std::size_t custom_property_batch_depth = 0;
	std::map<CustomPropertyAddress, CustomPropertyBefore> custom_property_batch;
	void begin_custom_property_batch();
	void capture_custom_property_owner(const CustomPropertyAddress &address, bool retiring = false);
	void emit_custom_property_change(const CustomPropertyAddress &address, const CustomPropertyBefore &before);
	void finish_custom_property_batch();
	std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment, PropertyKey property) const;
	PropertyTargetKey property_target_key(flecs::entity ship, const std::string &name) const;

	PropertyAggregateValue read_property_aggregate(flecs::entity ship, const PropertyTargetKey &key,
	                                               PropertyKey property, const std::string &custom,
	                                               RuleSourceAvailabilityCache &sources,
	                                               std::map<std::string, bool> &grants, bool retiring = false) const;
	std::int64_t property_delta(flecs::entity ship, const std::string &equipment, PropertyKey property,
	                            const std::string &contribution = {}) const;
	std::int64_t property_delta(flecs::entity ship, const std::string &equipment, const std::string &property,
	                            const std::string &contribution = {}) const;
	std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment,
	                                  const std::string &property) const;
	std::int64_t effective_property(flecs::entity ship, const std::string &equipment, PropertyKey property,
	                                std::int64_t base, std::int64_t extra_delta = 0,
	                                const std::string &contribution = {}) const;
	std::int64_t effective_property(flecs::entity ship, const std::string &equipment, const std::string &property,
	                                std::int64_t base, std::int64_t extra_delta = 0,
	                                const std::string &contribution = {}) const;
	bool modifier_away_target_available(flecs::entity ship, const std::string &equipment) const;
	bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
	                                std::span<const PropertyValue> values) const;
	std::vector<PropertyValue> modifier_values(const ModifierEffectDefinition &definition) const;
	bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
	                                const ModifierEffectDefinition &definition) const;
	std::uint64_t install_modifier_grant(PropertyEffects &owner, const ModifierEffectDefinition &definition,
	                                     const std::string &source, const std::string &equipment, std::int64_t tick,
	                                     bool maintained = false);

	PropertyMutationObserver property_mutation_observer();
	void erase_modifier(flecs::entity effect);
	void commit_property_effects(flecs::entity ship, const PropertyEffects &snapshot);
	void step_property_effects();
	void prune_property_targets();
	Json save_active_modifiers() const;
	void restore_active_modifiers(const Json &saved);
	// Prepare only native component copies; callers commit
	// them with their own receipts/payment after the complete operation succeeds.
	void prepare_effect_application(flecs::entity ship, const EffectApplication &application, ShieldComponent *shields,
	                                PropertyEffects *properties, std::int64_t tick);

	void apply_scenario_effect(const ScenarioEffectRequest &request);
	// Native work and direct outcomes share scalar
	// snapshot capture and completion helpers; preparation never mutates live state.
	NativeStructureOutcome &prepare_structure_outcome(PreparedEffectGroup &quote, EntityId target);
	NativePatientOutcome &prepare_patient_outcome(PreparedEffectGroup &quote, EntityId target, std::size_t patient);
	NativeTeamOutcome &prepare_team_outcome(PreparedEffectGroup &quote, EntityId target, const std::string &team);
	void prepare_native_outcome(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
	                            const EffectOperation &operation);
	bool native_outcome_quote_stale(const PreparedEffectGroup &quote) const;
	void commit_native_outcomes(PreparedEffectGroup &quote);
	// Each port exposes one native policy; prepared
	// copies and opcode dispatch belong to the effects preparation module.
	EffectPreparation effect_preparation();
	// Resolve an explicit store identity across native
	// mechanisms; there is no implicit ship-wide pool or remote supply lookup.
	NamedStock named_stock(flecs::entity ship, const std::string &name,
	                       ResourceNetwork *prepared_network = nullptr) const;

	// This epoch is transient quote ownership, not a gameplay counter or receipt.
	EffectTransactionCommitter effect_transactions;

	std::string effect_group_equipment(const EffectGroupInvocation &frame, const std::string &binding) const;
	flecs::entity effect_group_target(EntityId id, bool ordnance = false) const;
	bool effect_group_equipment_available(EntityId id, const std::string &equipment) const;
	PropertyEffects &prepare_property_owner(PreparedEffectGroup &quote, flecs::entity ship) const;
	std::vector<ActiveRuleEffect> &prepare_active_rule_owner(PreparedEffectGroup &quote) const;
	flecs::entity watch_effect_target(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
	                                  bool outcome = true, bool ordnance = false) const;
	StockStore &prepared_effect_stock(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
	                                  const std::string &name, bool outcome = true);
	CustomPropertyState &prepare_custom_property_owner(PreparedEffectGroup &quote,
	                                                   const CustomPropertyAddress &address);
	RuleActor effect_property_actor(const EffectGroupInvocation &frame, const RuleActor &original,
	                                const std::string &equipment, const std::string &officer,
	                                const std::string &compartment = {}) const;
	std::vector<PreparedEffectCost> resolve_effect_costs(PreparedEffectGroup &quote,
	                                                     const EffectGroupInvocation &frame);
	bool spend_prepared_effect_energy(PreparedEffectGroup &quote, const PreparedEffectCost &cost);
	void prepare_owner_effect_removal(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
	                                  const EffectOperation &operation);
	void stage_effect_costs(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
	                        std::span<const PreparedEffectCost> costs);
	ProcessRuntime &prepare_process_owner(PreparedEffectGroup &quote, const CustomPropertyAddress &address) const;
	std::vector<EffectGroupInvocation> process_removal_frames(PreparedEffectGroup &quote,
	                                                          const EffectGroupInvocation &frame);
	// Expansion belongs to effect preparation even when the caller only quotes
	// fees. Visit before expanding so a prepaid exit can skip its already-paid tree.
	template <typename Visit>
	void expand_process_effect_frames(PreparedEffectGroup &scope, Visit &&visit, const char *capacity_failure)
	{
		if (scope.invocations.size() > rule_instance_limit)
			throw std::runtime_error("Effect invocation batch capacity exhausted");
		scope.invocation_roots.clear();
		for (std::size_t i = 0; i < scope.invocations.size(); ++i)
			scope.invocation_roots.push_back(i);
		for (std::size_t i = 0; i < scope.invocations.size(); ++i)
		{
			scope.failed_invocation = i;
			const auto frame = scope.invocations[i];
			if (!visit(i, frame))
				continue;
			for (auto &stop : process_removal_frames(scope, frame))
			{
				if (scope.invocations.size() >= rule_instance_limit)
					throw std::runtime_error(capacity_failure);
				const auto address = custom_property_address(stop.rule_context->owner);
				if (scope.prepaid_process_stops.contains({address, stop.source}))
				{
					stop.execution.energy = 0;
					stop.execution.costs.clear();
				}
				scope.invocations.push_back(std::move(stop));
				scope.invocation_roots.push_back(scope.invocation_roots.at(i));
			}
		}
	}
	PreparedEffectGroup prepare_effect_groups(std::span<const EffectGroupInvocation> invocations,
	                                          PreparedEffectGroup quote = {});
	PreparedEffectGroup prepare_effect_group(const EffectGroupDefinition &execution, EntityId protected_ship,
	                                         EntityId attacker, const std::string &source,
	                                         const PayloadHitContext *hit_context = nullptr,
	                                         const std::string &original_equipment = {},
	                                         const NativeRuleContext *rule_context = nullptr,
	                                         std::int64_t output_permille = 1000, bool maintained = false);
	bool effect_group_quote_stale(const PreparedEffectGroup &quote) const;
	std::string commit_effect_group(PreparedEffectGroup &quote);
	std::string execute_effect_group(const EffectGroupDefinition &execution, EntityId protected_ship, EntityId attacker,
	                                 const std::string &source, const PayloadHitContext *hit_context = nullptr,
	                                 const std::string &original_equipment = {},
	                                 const NativeRuleContext *rule_context = nullptr,
	                                 std::int64_t output_permille = 1000, bool maintained = false);

	// Native trees are trusted authored execution sources, not Controller commands.
	// Their saved once-task continuation owns delivery; this adapter only executes
	// the common transaction and records its bounded failure receipt.
	void execute_tree_effect(const std::string &group, const std::string &source, const NativeRuleContext &context);

	std::string execute_scenario_effect_group(const ScenarioEffectRequest &request);

	void validate_stock_claim_owners();
	// Processes reuse the shared expression/payment
	// boundary; native mechanisms remain the only owners of physical progress.
	ProcessRuntime *process_runtime(const CustomPropertyAddress &address) const;
	ProcessState *process_state(const CustomPropertyAddress &address, const std::string &id) const;
	const ProcessDefinition &process_definition(const ProcessState &state) const;

	NativeRuleContext process_context(const CustomPropertyAddress &address, const ProcessState &state) const;
	EffectGroupInvocation process_effect_invocation(const CustomPropertyAddress &address, const ProcessState &state,
	                                                const EffectGroupDefinition &group,
	                                                std::int64_t output = 1000) const;
	std::string process_execute_groups(const CustomPropertyAddress &address, const ProcessState &state,
	                                   std::span<const EffectGroupDefinition> groups, std::int64_t output = 1000);
	std::string process_execute_lifecycle(const CustomPropertyAddress &address, const ProcessState &state,
	                                      const std::optional<EffectGroupDefinition> &group);
	const std::vector<ProcessBinding> &process_owner_bindings(const CustomPropertyAddress &address) const;
	ProcessRuntime initial_process_runtime(const CustomPropertyAddress &address) const;
	void initialise_processes(flecs::entity ship);
	std::string validate_process_grant(const CustomPropertyAddress &address, const ProcessGrantDefinition &grant) const;
	std::string install_process_grant(ProcessRuntime &runtime, const ProcessGrantDefinition &grant,
	                                  const std::string &effect, const std::string &source, const std::string &slot,
	                                  std::int64_t tick, const NativeRuleContext &origin) const;

	bool process_retirement_pending = false;
	void commit_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &value);
	void retire_process_dependants();
	bool process_origin_live(EntityId ship, const std::string &source,
	                         const PreparedEffectGroup *prepared = nullptr) const;
	bool process_run_available(const NativeRuleContext &context) const;
	std::string process_exclusion_key(const CustomPropertyAddress &address,
	                                  const AbilityExclusionDefinition &exclusion) const;
	std::string process_conflict(const CustomPropertyAddress &address, const ProcessState &state) const;
	RuleContext process_expression_context(const ProcessDefinition &definition, const NativeRuleContext &context,
	                                       std::optional<std::int64_t> work = {},
	                                       std::optional<std::int64_t> activity = {}) const;
	RuleContext process_expression_context(const CustomPropertyAddress &address, const ProcessState &state,
	                                       std::optional<std::int64_t> work = {},
	                                       std::optional<std::int64_t> activity = {}) const;
	std::string process_ability_conflict(EntityId ship, const AbilityGrantState &ability) const;
	void retire_ship_processes(EntityId ship);
	bool process_owner_terminated(EntityId id) const;
	std::string process_eligibility(const CustomPropertyAddress &address, const ProcessState &state,
	                                const RuleActor *target = nullptr) const;
	std::string process_requirements(const CustomPropertyAddress &address, const ProcessState &state) const;
	void reset_process_run_state(ProcessState &state, bool clear_request) const;
	std::string stop_process(const CustomPropertyAddress &address, const std::string &id, bool clear_request = true);
	bool process_request(const CustomPropertyAddress &address, const std::string &id, bool requested);
	void set_process_paused(const CustomPropertyAddress &address, const std::string &id, const std::string &reason);
	bool admit_process_run(const CustomPropertyAddress &address, const std::string &id);
	std::string begin_finite_native_run(const CustomPropertyAddress &address, const std::string &id,
	                                    const std::string &target, const RuleActor &target_actor, std::int64_t progress,
	                                    std::int64_t required);

	void prepare_processes();
	std::string stage_process_funding_state(const CustomPropertyAddress &, ProcessState &state, std::int64_t idle,
	                                        std::int64_t activity) const;
	std::optional<EffectGroupDefinition> process_funding_effect(const CustomPropertyAddress &,
	                                                            const ProcessState &state, std::int64_t idle,
	                                                            std::int64_t activity) const;
	std::int64_t process_effect_work(const CustomPropertyAddress &, const ProcessState &state, std::int64_t work) const;
	std::int64_t process_next_work_remainder(const CustomPropertyAddress &, const ProcessState &state,
	                                         std::int64_t work) const;
	RuleEvaluation process_work_effects(const CustomPropertyAddress &address, const ProcessState &state,
	                                    std::int64_t work, std::int64_t activity) const;
	void commit_finite_process_completion(const CustomPropertyAddress &address, const std::string &id,
	                                      const std::string &target);
	void commit_process_work_receipt(const CustomPropertyAddress &address, const ProcessState &frozen,
	                                 std::int64_t work, std::int64_t activity);
	void publish_process_work();
	std::optional<std::int64_t> process_completed_work(const RuleActor &actor, const std::string &family) const;
	Json save_processes() const;
	void validate_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &runtime) const;
	void validate_saved_process_state(const Json &state) const;
	void restore_processes(const Json &saved, const std::set<EntityId> &departed_ships);
	// Native adapters keep physical state and commands;
	// these lookups resolve the one authored execution permitted to drive a mechanism.
	std::string process_native_blocker(const CustomPropertyAddress &address, const ProcessState &state,
	                                   const ProcessDefinition &definition) const;
	NativeProcessView native_process_view(EntityId entity, std::string_view kind, const std::string &native) const;
	std::int64_t native_process_rate(EntityId entity, std::string_view kind, const std::string &native) const;
	ProcessMachineryBook process_machinery;
	ProcessMachineryQuote *native_machinery_quote(EntityId entity, std::string_view kind, const std::string &native);
	const ProcessMachineryQuote *native_machinery_quote(EntityId entity, std::string_view kind,
	                                                    const std::string &native) const;
	std::int64_t native_process_paid(EntityId entity, std::string_view kind, const std::string &native,
	                                 std::int64_t paid) const;
	std::int64_t native_process_work_limit(EntityId entity, std::string_view kind, const std::string &native) const;
	void begin_process_machinery_quotes();
	void prepare_process_machinery_controls();
	void record_native_process_work(EntityId entity, std::string_view kind, const std::string &native,
	                                std::int64_t available, std::int64_t completed);

	std::int64_t quote_native_flight_work(flecs::entity child, const SystemResourceGrant *grant,
	                                      bool full = false) const;
	bool capture_process_native_work(const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
	                                 bool stage);
	void reset_surviving_process_native_caps();
	bool cap_process_native_work(bool producers_only = false);

	void capture_process_native_requests();
	void price_process_native_requests();
	// Ability execution shares native grants and the
	// prepared effect transaction; native effects retain their specialist state.

	bool ability_source_exists(EntityId ship, const std::string &source, const std::string &owner) const;
	const AbilityGrantState *ability_execution(EntityId ship, const std::string &grant) const;
	const AbilityDefinition &ability_definition(EntityId ship, const std::string &grant) const;
	std::uint64_t ability_recovery_until(EntityId ship, const AbilityGrantState &g) const;
	const AbilityDefinition &ability_definition(EntityId ship, const AbilityGrantState &grant) const;

	std::string ability_control_owner(EntityId ship, const AbilityControlDefinition &c) const;
	const AbilityControlDefinition *resolve_ability_control(EntityId ship, const AbilityGrantState &g) const;
	bool ability_target_valid(EntityId ship, const AbilityControlDefinition &c, const AbilityDefinition &d,
	                          EntityId target, ConsoleContacts &cache) const;
	std::string ability_blocker(EntityId ship, const AbilityControlDefinition &c, const AbilityGrantState &g,
	                            EntityId target, ConsoleContacts &cache) const;
	std::string ability_apparatus_blocker(EntityId ship, const AbilityGrantState &g) const;
	std::string ability_conflict(EntityId ship, const AbilityGrantState &g) const;
	void initialise_abilities(flecs::entity ship);
	void ability_begin_recovery(EntityId ship, AbilityRuntime &runtime, const AbilityGrantState &g) const;
	std::vector<std::string> ability_recovery_reservations(EntityId ship, const AbilityGrantState &g) const;
	bool ability_recovery_capacity(EntityId ship, const AbilityGrantState &g) const;
	void stop_ability(EntityId ship, AbilityRuntime &runtime, AbilityGrantState &g);
	void reset_ability_endpoint(EntityId ship, const AbilityControlDefinition &c);
	void stop_abilities_for_duty(EntityId ship, const std::string &duty);
	void clear_retired_ability_control(EntityId ship, const AbilityGrantState &removed,
	                                   const AbilityRuntime &remaining);
	void prepare_ability_grant(AbilityRuntime &runtime, EntityId ship, const EffectOperation &operation,
	                           const std::string &parent);
	EffectGroupInvocation ability_effect_invocation(EntityId ship, const AbilityGrantState &g,
	                                                const EffectGroupDefinition &group) const;
	std::string ability_apply_groups(EntityId ship, const AbilityGrantState &g,
	                                 const std::optional<EffectGroupDefinition> &start,
	                                 const std::optional<EffectGroupDefinition> &activation = std::nullopt);
	void ability_request_result(EntityId ship, const AbilityGrantState &g, bool accepted);
	void activate_ability(EntityId ship, const std::string &key, bool activation_committed = false);
	void prepare_abilities(EntityId ship);
	void request_ability_resources(EntityId ship);
	std::optional<std::int64_t> ability_completed_work(const RuleActor &actor, const std::string &family) const;
	void refresh_ability_work(bool advance = true);
	// Control and continuation use the same compiled
	// endpoints and source identities as physical ability execution.
	bool ability_channel_bound(EntityId ship, std::size_t controller, std::size_t channel) const;

	const AbilityGrantState *ability_control_grant(EntityId ship, const AbilityControlDefinition &control) const;

	bool ability_generation_valid(EntityId ship, std::size_t controller, std::size_t channel,
	                              std::span<const std::int64_t> arguments) const;

	bool ability_channel_valid(EntityId ship, std::size_t controller, std::size_t channel, std::int64_t value,
	                           std::uint64_t request_id = 0) const;

	bool ability_command_valid(EntityId ship, std::size_t controller, std::size_t channel,
	                           const OperatingCommand &command) const;

	bool ability_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller,
	                      std::size_t channel, Json &buttons, ConsoleContacts &contacts) const;

	struct AbilityStatus
	{
		std::string phase = "unavailable", blocker = "Ability source unavailable";
		std::uint64_t deadline = 0, recovery = 0;
		std::int64_t preparation = 0, output = 0;
		bool ready = false, blocked = true, requested = false;
	};

	AbilityStatus ability_status(EntityId ship, const AbilityControlDefinition &control, const AbilityGrantState *grant,
	                             ConsoleContacts &contacts) const;

	Json execution_observed_properties(const RuleActor &actor, std::span<const std::string> observations) const;

	Json ability_observed_properties(EntityId ship, const AbilityGrantState &grant) const;

	Json ability_decision_facts(const ConsoleAddress &address, const Observation &permitted) const;

	Json ability_board(const ConsoleAddress &address) const;

	Json save_abilities() const;

	// The effects validator owns continuation
	// invariants; native identities and accepted Controller intent remain here.
	void validate_ability_runtime(EntityId ship, const AbilityRuntime &state, const AbilityRuntime &initial) const;

	void restore_abilities(const Json &saved, const std::set<EntityId> &departed_ships);
	// Process endpoints use existing Console command
	// admission; observations contain only permitted, frozen execution state.
	CustomPropertyAddress process_control_address(EntityId ship, const ProcessControlDefinition &control) const;
	const ProcessState *process_control_grant(EntityId ship, const ProcessControlDefinition &control) const;
	bool process_channel_bound(EntityId ship, std::size_t controller, std::size_t channel) const;
	static bool process_arguments_shape(std::span<const std::int64_t> arguments);
	bool process_generation_valid(EntityId ship, std::size_t controller, std::size_t channel,
	                              std::span<const std::int64_t> arguments) const;
	bool process_channel_valid(EntityId ship, std::size_t controller, std::size_t channel, std::int64_t value) const;
	bool process_command_valid(EntityId ship, std::size_t controller, std::size_t channel,
	                           const OperatingCommand &command) const;
	void consume_process_commands(EntityId ship);
	bool process_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller,
	                      std::size_t channel, Json &buttons) const;
	bool process_control_visible(const ConsoleAddress &address, const ProcessControlDefinition &binding) const;
	bool process_rule_visible(const ConsoleAddress &address, const NativeRuleSource &source) const;

	// A process endpoint permits its own execution state, not unrestricted facts
	// about a remote patient, fault or craft. Reuse the native Console scope before
	// publishing a job target and its progress.
	bool process_job_details_visible(const ConsoleAddress &address, const CustomPropertyAddress &owner,
	                                 const ProcessState &state, const Observation *permitted) const;
	Json process_board(const ConsoleAddress &address, const Observation *permitted = nullptr) const;
	// Native adapters supply addresses and facts; the
	// shared evaluator and prepared transaction own authored rule execution.
	RulesCatalog rule_templates;
	std::map<std::string, std::vector<RuleBinding>> native_effect_rules, region_rule_bindings;
	RuleEventQueue rule_events;

	flecs::entity rule_entity(const RuleActor &actor) const;
	bool rule_actor_available(const RuleActor &actor) const;
	bool rule_actor_eligible(const RuleActor &actor) const;
	bool rule_actor_enabled(const RuleActor &actor) const;
	std::optional<std::int64_t> rule_native_output(const RuleActor &actor) const;
	std::optional<std::int64_t> rule_native_property(flecs::entity entity, const RuleActor &actor,
	                                                 const std::string &property, bool base_view = false) const;
	state_tree::Value read_rule_fact(const RuleFactBinding &fact, const NativeRuleContext &context) const;
	RuleContext freeze_rule_context(const RuleDefinition &rule, const NativeRuleContext &context) const;
	bool rule_binding_matches(const RuleBinding &binding, const NativeRuleContext &context) const;

	bool enqueue_rule(const std::string &rule, const std::string &source, const NativeRuleContext &context,
	                  RuleEventPhase phase, std::int64_t due);
	std::string prepared_damage_failure(const PreparedEffectGroup &quote) const;
	void queue_prepared_damage(const PreparedEffectGroup &quote);
	std::string execute_rule_quote(const RuleEvaluation &evaluation, const NativeRuleContext &context,
	                               const std::string &source, bool maintained = false, std::int64_t output = 1000);
	void step_rule_events(RuleEventPhase phase);
	// Views retain native lifetime/eligibility authority;
	// provenance records only the frozen application context, never a second lifetime.
	using RuleOriginKey = std::tuple<EntityId, bool, std::string>;
	std::map<RuleOriginKey, NativeRuleContext> rule_origins;
	inline static constexpr std::size_t rule_origin_limit = 32768;
	bool native_rule_provenance_required(const std::string &definition) const;

	RuleActor rule_origin_target(const EffectGroupInvocation &frame, const std::string &role) const;
	bool rule_origin_live(const RuleOriginKey &key, const PreparedEffectGroup *prepared = nullptr) const;
	bool native_property_rate_owner_live(const NativeRuleContext &context) const;
	std::string rule_origin_failure(const PreparedEffectGroup &prepared) const;
	void remember_rule_origins(const PreparedEffectGroup &prepared);
	NativeRuleContext native_rule_origin(const RuleActor &owner, const std::string &source,
	                                     NativeEffectKind kind = NativeEffectKind::None,
	                                     std::uint64_t instance = 0) const;
	// Event producers own capture/phase policy; native binding traversal is shared.
	template <class Callback>
	void visit_rule_event_bindings(const NativeRuleSource &source, RuleTrigger trigger, const NativeRuleContext &event,
	                               Callback &&callback) const
	{
		if (!source.eligible || !source.bindings)
			return;
		for (const auto &binding : *source.bindings)
		{
			const auto found = rule_templates.find(binding.rule);
			if (found == rule_templates.end() || found->second.trigger != trigger)
				continue;
			auto context = merge_rule_event_context(source, event);
			if (!rule_binding_matches(binding, context))
				continue;
			context = bind_rule_context(binding, std::move(context));
			callback(rule_key(source.key, binding.id, event.subject), found->second, std::move(context));
		}
	}
	NativeRuleSource active_native_rule_source(const ActiveRuleEffect &effect, bool retiring = false) const;
	std::vector<NativeRuleSource> rule_sources(const RuleActor &actor, bool retiring = false) const;
	std::vector<NativeRuleSource> rule_sources(flecs::entity ship, bool retiring = false) const;
	// Native delivery and duration owners; shared rules own reactions and clocks.
	std::map<std::string, PayloadDefinition> payload_templates;
	std::map<std::string, PeriodicEffectDefinition> periodic_templates;
	// Active condition entities own their clocks and captured origin. Ordered handles
	// are a query-derived cache; stable effect IDs govern execution and persistence.
	const std::vector<flecs::entity> &active_rule_rows() const;
	std::vector<ActiveRuleEffect> active_rule_snapshot() const;
	flecs::entity active_rule_entity(std::uint64_t id) const;
	ActiveRuleMutationObserver active_rule_mutation_observer();
	void erase_active_rule_effect(flecs::entity entity);
	template <typename Predicate> std::size_t erase_active_rule_effects_if(Predicate predicate)
	{
		return active_rules.erase_if(predicate, rule_clocks, active_rule_mutation_observer());
	}
	void commit_active_rule_effects(const std::vector<ActiveRuleEffect> &prepared);
	bool active_rule_effect_exists(const std::vector<ActiveRuleEffect> *prepared, const std::string &source,
	                               EntityId target) const;
	RuleTargetValidity active_rule_target_validity() const;
	bool active_rule_target_available(const ActiveRuleEffect &effect) const;

	std::vector<RuleFactBinding> payload_capture_layout(const std::vector<std::string> &payloads) const;
	std::vector<RuleCapturedFact> capture_payload_rule_facts(EntityId source, const std::string &equipment,
	                                                         const std::vector<std::string> &payloads) const;
	void validate_rule_captures(const std::vector<RuleCapturedFact> &captures,
	                            const std::vector<std::string> &payloads) const;
	static RuleTrigger payload_trigger(PayloadOutcomeKind kind);
	void queue_payload_outcome(const Projectile &projectile, const PayloadHitContext &hit, std::int64_t damage);

	void prepare_active_rule_effect(std::vector<ActiveRuleEffect> &prepared, std::uint64_t &next,
	                                const EffectOperation &operation, const std::string &source,
	                                EntityId original_source, const PayloadHitContext &context,
	                                const std::string &group_source, const std::string &original_equipment,
	                                const NativeRuleContext *origin = nullptr);

	flecs::entity payload_target(const PayloadHitContext &c) const;
	bool payload_context_available(const PayloadHitContext &c, flecs::entity resolved = {}) const;
	std::vector<std::string> capture_directed_payloads(flecs::entity ship, const CombatMechanism &mechanism,
	                                                   const std::string &resource) const;
	// Effects address native damage outcomes; native
	// reserve, compartment, fault and durability owners retain their existing routes.
	std::int64_t damage_native_durability(flecs::entity ship, std::int64_t amount, const PayloadHitContext &hit,
	                                      const NativeRuleContext *origin = nullptr,
	                                      const Projectile *payload = nullptr);
	void apply_payload_damage(flecs::entity ship, std::int64_t amount, const PayloadHitContext &hit,
	                          const NativeRuleContext *origin = nullptr,
	                          std::optional<std::int64_t> incoming_heading = {});
	// These plans live for one resource phase only.
	// Authoritative prepaid work and leases remain on their native rule owners.
	std::map<std::string, RuleUpkeepPlan> rule_upkeep_plans;
	std::unique_ptr<PreparedEffectGroup> rule_upkeep_effects;
	std::unique_ptr<PreparedEffectGroup> process_producer_quote;
	std::map<EntityId, ResourceNetwork> process_producer_networks;
	std::map<std::pair<CustomPropertyAddress, std::string>, std::string> process_exit_failures;
	bool process_funding_rejection_pending = false;

	void clear_rule_upkeep();
	void stage_rule_upkeep(const RuleExecutionState &state, std::span<const EffectCostDefinition> costs,
	                       const std::string &shortage);
	std::string ability_upkeep_key(EntityId ship, const std::string &grant) const;
	void stage_ability_upkeep(EntityId ship, const AbilityGrantState &grant, const AbilityDefinition &definition,
	                          std::span<const EffectCostDefinition> costs);
	std::int64_t ability_upkeep_output(EntityId ship, const std::string &grant) const;
	void stage_process_costs(const CustomPropertyAddress &address, const ProcessState &state,
	                         const ProcessDefinition &definition, bool work, std::int64_t actual_work,
	                         std::int64_t scale);
	void append_process_fee_group(RuleUpkeepPlan &plan, const EffectGroupInvocation &initial);

	void stage_process_upkeep(const CustomPropertyAddress &address, const ProcessState &state,
	                          const ProcessDefinition &definition);
	void stage_process_work(const CustomPropertyAddress &address, const ProcessState &state,
	                        const ProcessDefinition &definition, std::int64_t actual_work, std::int64_t activity);
	std::int64_t process_funding_output(const CustomPropertyAddress &address, const std::string &id, bool work) const;
	bool reject_process_funding(const CustomPropertyAddress &address, const std::string &id, const std::string &reason,
	                            bool work = true);
	void release_suspended_process_plans();
	// Quote and live execution states use the same canonical credit identity and
	// payment result. Callers retain responsibility for commit eligibility.
	// Resolve real addresses before resource allocation;
	// the funding module owns coalescing and canonical work-credit identity.
	void resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &plan,
	                         std::set<std::pair<CustomPropertyAddress, std::string>> *paid_stops = nullptr);

	const ResourceNetwork *process_producer_seed(EntityId id) const;
	void prepare_producer_exit_fees(PreparedEffectGroup &quote, const RuleUpkeepPlan &plan);
	bool prepare_process_producer_funding();
	void request_rule_upkeep();
	bool prepare_rule_upkeep_payments(std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
	                                  bool native_requests_removed = false);
	bool commit_rule_upkeep_stocks();
	// The native allocation still owns its candidate network. Work operations and
	// already quoted inputs join one prepared effect transaction before either can
	// commit. A failed invocation identifies the process to withdraw and requote.
	std::string append_rule_upkeep_effects(std::span<const EffectGroupInvocation> invocations,
	                                       std::size_t &failed_invocation);
	void stage_rule_upkeep_process_receipt(const CustomPropertyAddress &address, const std::string &id,
	                                       std::int64_t actual_work, std::int64_t idle, std::int64_t activity);
	struct RuleProcessWork
	{
		CustomPropertyAddress owner;
		std::string id;
		std::int64_t work = 0, activity = 1000;
	};
	bool prepare_process_funding_effects(std::span<const RuleProcessWork> completed,
	                                     std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations);
	void finish_rule_upkeep();
	// Included inside World::Impl after rule_funding.hpp. Late native jobs use the
	// same prepared transactions and fractional input credits as continuous work.
	NativeProcessView finite_native_driver(EntityId entity, std::string_view kind, const std::string &native,
	                                       const RuleActor *target = nullptr) const;

	std::int64_t stage_finite_native_costs(FiniteNativeWork &quote, RuleUpkeepPlan plan, bool proportional);
	void fail_finite_native_work(const CustomPropertyAddress &owner, const std::string &id, const std::string &reason);
	std::optional<FiniteNativeWork> prepare_finite_native_work(EntityId entity, std::string_view kind,
	                                                           const std::string &native, const std::string &target,
	                                                           const RuleActor &target_actor, std::int64_t progress,
	                                                           std::int64_t required, std::int64_t desired,
	                                                           std::span<const EffectCostDefinition> native_costs = {},
	                                                           bool scale_rate = true);
	bool commit_finite_native_payment(FiniteNativeWork &quote);
	void commit_finite_native_work(const FiniteNativeWork &quote, std::int64_t actual, std::int64_t progress,
	                               std::int64_t required, bool completed);

	void finalise_finite_native_jobs();

	void cancel_finite_native_owner_jobs(EntityId entity, const std::string &native);
	// Derived addresses never retain Flecs pointers;
	// ordinary bodies and native active records own the persisted clocks.
	// The effects store owns derived addresses and
	// frontier rules; native components remain the authoritative clock owners.
	RuleClockStore rule_clocks;
	RuleClockStorage rule_clock_storage();
	RuleClockReadStorage rule_clock_read_storage() const;
	void rebuild_rule_clock_addresses();
	RuleExecutionState *find_rule_execution(const std::string &key);
	const RuleExecutionState *readonly_rule_execution(const std::string &key) const;
	RuleExecutionState *ensure_rule_execution(const NativeRuleSource &source, const RuleBinding &binding,
	                                          const NativeRuleContext &context, bool create);
	void erase_rule_execution(const std::string &key);
	std::size_t rule_clock_capacity(const std::vector<ActiveRuleEffect> *prepared = nullptr) const;
	std::vector<NativeRuleSource> lifecycle_rule_sources() const;
	std::string parent_rule_execution_key(const std::string &source) const;
	bool rule_grant_available(EntityId, const std::string &source,
	                          RuleSourceAvailabilityCache *scratch = nullptr) const;
	bool rule_lease_available(const std::string &source) const;
	std::int64_t rule_source_output(EntityId ship, const std::string &source) const;
	// Native fact capture is the only runtime port
	// needed by authored rule application binding and identity preparation.
	RuleApplications rule_applications() const;
	void capture_rule_custom_targets(const RuleDefinition &rule, const NativeRuleContext &context);
	bool maintained_modifier_exists(const RuleExecutionState &state, const EffectOperation &operation) const;
	void withdraw_rule_grants(RuleExecutionState &state);
	void retire_rule_state(RuleExecutionState &state, bool deactivate);
	void discover_active_rule_clocks();
	void retire_active_rule_effects();
	void prepare_rule_sources();
	void refresh_rule_eligibility();
	void step_rule_clocks();
	// Membership is sampled; instant crossings use
	// the native executed movement segment, including both bands of an annulus.
	void queue_region_rule_event(EntityId id, const std::string &region, RuleTrigger trigger);
	void initialise_rule_membership();
	void step_region_rules();
	// Structural continuation checks belong to
	// effects; these ports validate existing native identity and compatibility.
	RuleContinuationValidator rule_continuation_validator() const;
	// Included inside World::Impl after common rule lifecycle/source adapters.
	static void rule_saved_fields(const Json &value, std::initializer_list<const char *> fields);
	static std::int64_t rule_saved_integer(const Json &value, std::int64_t minimum = 0,
	                                       std::int64_t maximum = 9007199254740991LL);
	static void rule_saved_text(const Json &value, std::size_t maximum = 4096, bool empty = true);
	static void rule_saved_bool(const Json &value);
	static bool saved_rule_key(const std::string &value);
	void validate_saved_rule_actor(const Json &value) const;
	void validate_saved_rule_context(const Json &value) const;
	void validate_saved_work_credit(const Json &credit) const;
	void validate_saved_rule_state(const Json &value, std::int64_t now) const;
	Json save_rules() const;
	void restore_rules(const Json &saved, const Json &conditions, const Json &next_condition);
	// Publish automatic rule status through the same
	// Console feeds and personnel departments as the native source's other data.
	Json rules_board(const ConsoleAddress &address) const;
	ProcessorStores processor_stores(flecs::entity ship, const ProcessorDefinition &definition) const;
	void initialize_processors(flecs::entity ship, const CompiledDefinition &data);
	void prepare_processors(flecs::entity ship);
	void request_processor_resources(flecs::entity ship);
	// A rejected launch can release stock needed by a processor. Retry only work
	// that did not prepare, using its original budget so fractional work is not
	// advanced twice and committed batches are never prepared again.
	void retry_blocked_processors(flecs::entity ship);
	void actuate_processors(flecs::entity ship);
	bool select_processor(flecs::entity ship, const std::string &id, const std::string &recipe, bool execute);
	Json save_processors(flecs::entity ship) const;
	void restore_processors(flecs::entity ship, const Json &saved);
	Json project_processors(const ConsoleAddress &address) const;

	bool quote_process_native_work(const std::vector<std::pair<EntityId, flecs::entity>> &ships,
	                               const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
	                               bool stage);
	void run_advanced_equipment();
	void scenario_effects();
	void scenario_spawns();
	void scenario_publications();
	void refresh_control(EntityId ship, bool transitions = true);
	void prune_displays();
	const Json *display_widget(const ConsoleAddress &address, const std::string &id) const;
	bool display_command(std::uint64_t claimant, const OperatingCommand &command, bool execute);
	// Personnel commands use Station ownership, never role names.
	const PersonnelDuty *personnel_duty(EntityId id, const std::string &duty) const;

	OfficerState personnel_destination(EntityId id, const PersonnelDuty &duty) const;
	bool personnel_owns(const ConsoleAddress &address, const std::string &department) const;
	int station_stars(const ConsoleAddress &address) const;
	bool manual_workload(const ConsoleAddress &address, const std::string &family) const;
	bool assisted_workload(const ConsoleAddress &address, const std::string &family) const;
	bool manual_team_destination(const ConsoleAddress &address) const;
	void advance_personnel(EntityId id);
	std::pair<std::string, std::string> officer_speaker(EntityId id, const std::string &console,
	                                                    const std::string &controller) const;
	bool auto_assign_officer(const ConsoleAddress &address, const std::string &duty_id, bool reassign);
	bool personnel_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute,
	                       bool ai = false);
	PersonnelState personnel_snapshot(EntityId id) const;
	Json personnel_board(const ConsoleAddress &address) const;
	void restore_personnel(EntityId id, const Json &saved);
	// World adapter. Apparatus components own commitments; receiver records own apparent evidence.
	PsychicReceptions ghost_receptions;
	PsychicInterferenceMemory psychic_interference;
	PsychicEmissionMemory weapon_emissions;
	void psychic_weapon_emission(EntityId ship);
	std::int64_t emission(EntityId ship) const;
	const OfficerDefinition *choir_officer(EntityId ship, const std::string &duty) const;
	bool choir_assignment(EntityId ship, const OfficerDefinition &officer, const PersonnelDuty *duty) const;
	const AbilityGrantState *psychic_execution(EntityId ship, const PsychicContribution &contribution) const;
	const PsychicPowerDefinition &contribution_power(EntityId ship, const PsychicContribution &contribution) const;
	void refresh_psychic_work();
	// Native state follows committed ability executions. A funded pause keeps the same
	// activation/shapes; only a new execution can create a new projection identity.
	void sync_psychic_contributions();
	bool psychic_coverage(EntityId ship, const ChoirDefinition &c, const PsychicContribution &s,
	                      EntityId receiver) const;
	std::int64_t compute_concealment(EntityId target) const;
	void refresh_concealment();
	std::int64_t concealment(EntityId target) const;
	void dispel_ghost(const GhostReception &g);
	bool ghost_live(const GhostReception &g) const;
	void refresh_psychic_reception();
	PsychicSource strongest_scrambler(EntityId receiver) const;
	void advance_psychic();
	BodyView ghost_body(const GhostReception &g) const;
	void ghost_damage(EntityId observer, fixed::Vec from, fixed::Vec to, std::int64_t from_z, std::int64_t to_z,
	                  std::int64_t radius = 0);
	Json psychic_board(const ConsoleAddress &address) const;
	Json save_psychic() const;
	void restore_psychic(const Json &saved, const std::set<EntityId> &departed_ships);
	ConsoleAddress enabled_system_owner(EntityId ship, const std::string &system) const;
	// World adapter: authored team orders use the same physical routes, work and personnel as repair/away work.
	const TeamOrderDefinition *team_order_definition(const CombatMechanism &m, const std::string &id) const;
	void personnel_changed(EntityId ship);
	void personnel_casualties(flecs::entity ship, std::size_t compartment, std::int64_t damage);
	bool team_available(const CombatMechanism &m) const;
	// Injury output joins other comparable modifiers. Loss/detention remain hard eligibility.
	std::int64_t team_work(flecs::entity ship, const CombatMechanism &team, PropertyKey property,
	                       std::int64_t base) const;
	ClinicalRoster clinical_roster(EntityId ship) const;
	std::vector<ClinicalPatient> clinical_patients(EntityId ship) const;
	bool patient_eligible(const OfficerState &o) const;
	// Order filters and action restrictions apply before severity/individual priority.
	bool patient_matches(EntityId target, std::size_t patient, const OfficerState &o, const TeamOrderDefinition &order,
	                     const std::string &activity = {}, bool with_party = false) const;
	void step_team_order(const TeamOrderDefinition &order, state_tree::Instance &continuation, std::string &activity,
	                     bool arrived, bool work_done, bool eligible, EntityId origin, const std::string &team,
	                     EntityId target, std::int64_t compartment);
	bool team_target_eligible(EntityId ship, const TeamOrderDefinition &order, std::size_t compartment) const;
	bool team_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false);
	std::optional<std::size_t> triage_patient(EntityId target, std::optional<std::size_t> compartment,
	                                          const TeamOrderDefinition &order, const std::string &activity,
	                                          const std::string &priority,
	                                          const std::set<std::pair<EntityId, std::size_t>> &claimed,
	                                          const std::set<std::size_t> *party = nullptr) const;
	void treat_patient(EntityId origin, const CombatMechanism &team, EntityId target, std::size_t patient,
	                   const std::string &activity);

	bool step_clinical_work(EntityId origin, const CombatMechanism &team, EntityId target, std::size_t compartment,
	                        const TeamOrderDefinition &order, const std::string &activity, const std::string &priority,
	                        std::set<std::pair<EntityId, std::size_t>> &claimed);
	bool clinical_work_done(EntityId target, std::optional<std::size_t> compartment, const TeamOrderDefinition &order,
	                        const std::string &activity) const;
	void step_medical_facilities();

	void step_teams();
	Json team_board(const ConsoleAddress &a) const;

	bool team_map_allowed(const ConsoleAddress &a) const;

	void advance_away_medical(std::size_t index);

	void validate_medical_state() const;

	// This relation depends only on the resolved fit, not officer assignment,
	// condition or Controller ownership. A new World owns a fresh definition cache.
	mutable std::map<std::pair<std::string, std::string>, std::set<std::string>> duty_systems_;
	bool duty_affects_system(EntityId id, const PersonnelDuty &duty, const std::string &system) const;
	// Compartment access is shared by people, teams and physical deliveries.
	std::string compartment_edge_key(std::size_t a, std::size_t b) const;
	std::uint64_t logistics_revision(flecs::entity e) const;
	bool physical_edge_open(EntityId id, std::size_t from, std::size_t to) const;
	template <class Links>
	std::pair<std::int64_t, std::int64_t>
	physical_route(EntityId id, std::size_t count, const Links &links, std::size_t from, std::size_t to,
	               std::int64_t *total = nullptr, bool avoid_disabled = false) const
	{
		const auto *structure = entities.at(id).try_get<CombatStructure>();
		Links available;
		for (const auto &edge : links)
			if (physical_edge_open(id, edge.from, edge.to) &&
			    (!avoid_disabled || !structure ||
			     (!structure->compartments.at(edge.from).disabled && !structure->compartments.at(edge.to).disabled)))
				available.push_back(edge);
		return compartment_route(count, available, from, to, total);
	}
	bool compartment_access_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai);
	// Typed material simulation. Strings/JSON are confined to commands and projections.
	std::vector<std::pair<EntityId, flecs::entity>> inventory_rows() const;
	const MaterialStoreDefinition &material_store(EntityId id, std::size_t store) const;
	bool material_accepts(EntityId id, std::size_t store, const std::string &m) const;
	StockStore &physical_stock(flecs::entity ship, const std::string &key) const;
	bool select_physical_resource(flecs::entity ship, const std::string &key, const std::string &resource,
	                              bool execute);
	StockStore &material_stock(EntityId id, std::size_t store) const;
	std::int64_t material_size(EntityId id, std::size_t store, const std::string &m) const;

	std::int64_t material_quantity(EntityId id, std::size_t store, const std::string &m) const;
	void material_add(EntityId id, std::size_t store, const std::string &m, std::int64_t n);
	std::int64_t material_occupied_volume(EntityId id, std::size_t store) const;
	std::int64_t material_space(EntityId id, std::size_t store, const std::string &m) const;
	std::int64_t material_pending(EntityId id, std::size_t store, const std::string &material) const;
	bool return_material_batch(MaterialBatch &batch, bool execute);
	std::size_t material_compartment(EntityId id, std::size_t store) const;
	bool material_store_working(EntityId id, std::size_t store) const;
	std::pair<std::int64_t, std::int64_t> material_route(EntityId id, std::size_t from, std::size_t to) const;
	bool material_connection(EntityId a, EntityId b) const;
	void request_material(EntityId source, std::size_t from, EntityId destination, std::size_t to, const std::string &m,
	                      std::int64_t n, bool automatic);
	void step_logistics();
	bool material_authority(const ConsoleAddress &a, const OperatingCommand &c, const std::string &owner,
	                        bool ai) const;
	bool logistics_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false);
	std::int64_t parse_material_number(const std::string &s) const;
	Json logistics_board(const ConsoleAddress &a) const;
	void restore_logistics(const Json &saved);

	const MaterialConsumerDefinition *material_consumer(EntityId origin, const std::string &system) const;
	// Native work authoring supplies legacy per-episode inputs; actual payment uses
	// the same prepared transaction as process inputs and native outcomes.
	std::optional<std::vector<EffectCostDefinition>> native_material_work_costs(EntityId origin,
	                                                                            const std::string &system,
	                                                                            const std::string &activity,
	                                                                            bool paid) const;

	std::vector<std::string> material_fields(const std::string &text) const;
	// Docking produces guidance; the owning flight Controller commits ordinary channels.
	DockPropulsionContinuation dock_propulsion;
	std::vector<std::pair<EntityId, flecs::entity>> docking_rows() const;
	std::vector<std::pair<EntityId, flecs::entity>> dock_connections() const;
	bool dock_attached(EntityId id) const;
	EntityId dock_root(EntityId id) const;
	bool dock_busy(EntityId id) const;
	bool dock_port_busy(EntityId id, std::size_t port) const;

	fixed::Vec dock_position(EntityId id, std::size_t port) const;
	bool dock_port_working(EntityId id, std::size_t port) const;
	bool dock_permitted(EntityId ship, std::size_t port, EntityId target, std::size_t remote) const;
	bool dock_known(const ConsoleAddress &a, EntityId target) const;
	bool dock_envelope(EntityId id, const DockConnection &c) const;
	bool dock_ready(EntityId a, EntityId b) const;
	bool away_dock_ready(EntityId ship, const AwayDefinition &mission, bool finishing = false) const;
	void detach_dock(EntityId id, bool emergency);
	void cancel_docking_assist(EntityId id, const std::string &controller, const std::string &action,
	                           std::int64_t value);
	bool docking_command(const ConsoleAddress &a, const OperatingCommand &cmd, bool execute, bool ai = false);
	void prepare_docking();
	void apply_docking_controller(flecs::entity e);
	bool dock_crossing_busy(EntityId a, EntityId b) const;
	void step_docking();
	void sync_docks();
	std::int64_t dock_mass(EntityId root) const;
	void capture_dock_propulsion();
	void adjust_dock_propulsion();
	void dock_collision_response(std::size_t index, const CollisionBody &before);
	Json docking_board(const ConsoleAddress &a) const;
	void restore_docking(const Json &saved);

	std::optional<MaterialGateway> material_gateway(EntityId source, EntityId destination,
	                                                bool finishing = false) const;

	bool dock_chain_contains(EntityId id, EntityId needle) const;
	bool dock_report_available(const ConsoleAddress &a, EntityId target) const;
	// Cargo missions reuse ordinary craft, inventory and docking; no virtual delivery.
	std::vector<CargoDelivery> cargo_deliveries;
	std::uint64_t next_cargo_delivery = 1;
	bool cargo_committed(EntityId craft) const;

	void clear_cargo_claims(StockTransaction &transaction, const CargoDelivery &job) const;
	bool stage_cargo_claims(StockTransaction &transaction, const CargoDelivery &job,
	                        const MaterialBatch *dispatch = nullptr) const;
	void sync_cargo_stock(const CargoDelivery &job);
	std::int64_t material_available(EntityId id, std::size_t store, const std::string &material,
	                                std::uint64_t exempt_cargo = 0) const;
	bool stage_material_dispatch(StockTransaction &transaction, const MaterialRequest &request, std::int64_t quantity,
	                             std::uint64_t batch_id);
	bool cargo_authorisation_console(const ConsoleAddress &a, bool human = false) const;
	bool cargo_authority(const ConsoleAddress &a, const OperatingCommand &c, EntityId craft, bool ai) const;
	bool cargo_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai);
	void step_cargo();
	void validate_cargo() const;

	void request_cargo_return(CargoDelivery &job);
	// Collection and barter extend cargo jobs; all goods travel through MaterialBatch.
	std::optional<CargoDelivery> supply_manifest(std::size_t index, std::int64_t requested = -1) const;
	bool supply_pending(const CargoDelivery &job) const;
	bool supply_transfer(CargoDelivery &job, EntityId source, std::size_t from, EntityId destination, std::size_t to,
	                     const std::string &material, std::int64_t quantity);
	void supply_return(CargoDelivery &job);
	void step_supply_cargo(CargoDelivery &job);
	void advance_supply_mission(std::size_t index);
	// Only published ship objectives are eligible. Private Console/scenario records never enter this list.
	std::map<KnowledgeId, KnowledgeRecord> ship_objectives(EntityId ship) const;
	bool active_objective(const KnowledgeRecord &record) const;
	bool ship_has_ai(EntityId ship) const;
	void reconcile_objective_priority(EntityId ship);
	Json objectives_board(EntityId ship) const;
	// Shipwide assessment exposes no private contacts, telemetry or machinery commands.
	Json navigation_board(EntityId ship) const;
	// All reads use accepted intent, including AI reads.
	bool stance_target(EntityId ship, const std::string &console) const;
	bool bridge_visible(const ConsoleAddress &address) const;
	Json alert_board(EntityId ship) const;
	Json stance_board(const ConsoleAddress &address) const;
	bool bridge_command(const ConsoleAddress &source, const OperatingCommand &command, bool execute);
	void bridge_controls(const ConsoleAddress &address, Json &buttons) const;
	void restore_bridge(EntityId ship, const Json &saved);
	// Private World adapter. Domain state is typed; content is compiled only at load.
	std::map<std::string, std::vector<CommsParticipant>> comms_rosters;
	ConversationRuntime conversations;
	bool comms_controller(EntityId id, const ConsoleControllerSpec &controller) const;
	std::optional<ConsoleAddress> comms_owner(EntityId id) const;
	bool conversation_operator(EntityId id, const ConsoleControllerRuntime &controller) const;
	void register_comms(EntityId id, const std::string &definition);
	void refresh_comms(bool disclose = true);
	bool comms_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute);
	struct ConversationStart
	{
		std::string id, tree, title, start_event;
		struct Binding
		{
			EntityId entity = 0;
			std::string slot, character;
		};
		std::map<std::string, Binding> bindings;
		std::vector<std::string> members, events, cancel_events;
		bool cancelled = false;
		ConversationId conversation = 0;
		ScenarioId cursor = 0;
	};
	std::vector<ConversationStart> conversation_starts;
	ScenarioId conversation_instance = 0, conversation_event_sequence = 0;
	void compile_conversation_starts(const Json &map);
	void conversation_scenarios();
	Json save_conversation_starts() const;
	void restore_conversation_starts(const Json &j);
	// World adapter. All mutable mission state is authoritative and saved.
	AwayRuntime away_operations;
	EntityId away_ship(const AwayDefinition &d) const;
	EntityId away_transport_entity(EntityId ship, const std::string &transport) const;
	std::string canonical_away_transport(EntityId ship, const std::string &transport, const std::string &mission) const;
	bool away_ship_transport_owned(const ConsoleAddress &a) const;
	ConversationId away_conversation(const AwayDefinition &d) const;
	flecs::entity away_equipment(EntityId ship, const std::string &key) const;
	const PersonnelDuty *away_team_duty(EntityId ship, const std::string &key) const;
	std::string away_officer_location(EntityId ship, const std::string &officer) const;
	bool away_reserved(EntityId ship, const std::string &asset) const;
	bool away_transport_requested(EntityId ship, const std::string &system) const;
	bool away_craft_reserved(EntityId craft, const std::string &except = {}) const;
	void return_away_escorts(AwayMission &m);
	bool away_qualified(EntityId ship, const std::string &system) const;
	std::string away_dispatch_system(EntityId ship, const std::string &route = {}) const;
	bool away_dispatch(const ConsoleAddress &a, const std::string &route = {}) const;
	bool away_asset_owned(const ConsoleAddress &a, const std::string &asset) const;
	bool away_has_capability(EntityId ship, const AwayMission &m, const std::string &capability) const;
	bool away_asset_healthy(EntityId ship, const std::string &asset) const;
	bool away_slot_accepts(EntityId ship, const AwaySlot &s, const std::string &asset) const;
	std::size_t away_origin(EntityId ship, const std::string &transport) const;
	bool away_staged(EntityId ship, const AwayMission &m) const;
	std::int64_t away_transfer_ticks(const AwayDefinition &d, const AwayMission &m) const;
	void mark_away_deployed(const AwayDefinition &definition, AwayMission &m);
	void finish_away_recovery(EntityId ship, AwayMission &m);
	std::string boarding_insertion_blocker(std::size_t index, const std::string &transport) const;
	std::string away_blocker(std::size_t index, bool power = true) const;
	void prepare_away_officers(const ConsoleAddress &address, std::size_t index);
	void prepare_away_contribution(const ConsoleAddress &a, std::size_t index);
	bool away_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false,
	                  bool assisted = false);
	void prepare_away_tick_contributions(std::size_t i);
	bool prepare_away_assisted_send(std::size_t i);

	void stage_away(EntityId ship, AwayMission &m);
	void advance_away();
	std::string away_asset_label(EntityId ship, const std::string &asset) const;
	AwayPermittedRow permitted_away_row(std::size_t mission_index, const ConsoleAddress &viewer) const;
	Json away_board(const ConsoleAddress &a) const;
	Json save_away() const;
	void restore_away(const Json &rows);
	// Shared physical ownership and custody effects. No scenario identities belong here.
	std::map<EntityId, std::map<std::string, PersonnelPosting>> personnel_postings;
	bool captured_posting(EntityId ship, const std::string &asset) const;
	void validate_personnel_postings() const;
	void set_operational_owner(EntityId target_id, EntityId origin_id);
	void detained_asset(EntityId ship, const std::string &asset, bool detained);
	void capture_visitors(EntityId target, EntityId captor);
	void free_captives(EntityId target, EntityId owner);
	void claim_away_target(std::size_t index, const std::string &capability);
	// Boarding is an away operation, with target-authored objectives and ordinary located teams.
	bool boarding_visible(EntityId origin, EntityId target) const;
	void refresh_boarding_templates();
	bool boarding_conversation_requested(const std::string &id) const;
	bool boarding_defender_reserved(EntityId target, const std::string &asset) const;
	std::vector<flecs::entity> boarding_party_teams(EntityId origin, const AwayMission &m,
	                                                const std::string &capability) const;
	std::int64_t boarding_roll(EntityId target);
	void boarding_casualty(EntityId ship, flecs::entity team, const BoardingEncounter &e, bool medical);
	void boarding_field_care(EntityId origin, AwayMission &mission, bool can_work);
	bool boarding_defender_available(EntityId ship, flecs::entity team) const;
	void advance_boarding(std::size_t index);
	bool boarding_action(std::size_t index, const std::string &action, const std::string &key);
	void validate_boarding_state(const AwayDefinition &d, const AwayMission &m) const;
	// The ownership effect and resource inhibition use the ordinary ship and Controller runtimes.
	std::map<EntityId, EntityId> captured_by;
	void capture_boarded_ship(std::size_t index, bool prize);
	bool boarding_inhibited(EntityId target, const std::string &system) const;

	bool boarding_contested(std::size_t index, std::size_t location) const;

	EntityId operational_owner(EntityId ship) const;
	// World adapter for ordinary carried entities. Guidance is read by authored Controllers.
	std::int64_t altitude(EntityId id) const;
	std::int64_t craft_flight_band(EntityId id) const;
	std::int64_t distance_3d(EntityId a, EntityId b) const;
	std::vector<std::pair<EntityId, flecs::entity>> craft_rows() const;
	bool craft_dormant(flecs::entity e) const;
	bool bay_working(EntityId ship, const std::string &bay) const;
	bool craft_docking_ready(EntityId id) const;
	bool ftl_preparing(EntityId ship) const;
	bool ftl_recall_started(EntityId ship) const;
	bool craft_link(EntityId craft) const;
	void set_craft_active(flecs::entity e, bool active);
	void initialise_craft(std::vector<EntityId> *created = nullptr);
	void restore_spawned_craft(const Json &saved);
	bool torpedo_launched(flecs::entity e) const;
	void release_away_transport(EntityId ship, AwayMission &m);
	bool craft_available(EntityId id, const std::string &mission = {}) const;
	void craft_return(flecs::entity e);
	void complete_craft_recovery(EntityId craft);
	void request_craft_pickup(EntityId craft);
	void advance_craft_transport(std::size_t index, EntityId craft);
	bool craft_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false);
	void launch_craft(flecs::entity e);
	void prepare_craft();
	void step_vertical();
	std::int64_t craft_service_rate(EntityId home, std::string_view kind, const std::string &native) const;
	// Preview and stage against the same native networks and staged payment stocks.
	// Swapping just the stocks retains native discharge, receiving and thermal state.
	std::int64_t stage_craft_service_energy(FiniteNativeWork &quote, EntityId home, EntityId craft, ResourceIndex from,
	                                        ResourceIndex to, std::int64_t offered, bool stage);
	std::int64_t service_craft_energy(EntityId home, const std::string &bay, EntityId craft, ResourceIndex from,
	                                  ResourceIndex to, std::int64_t offered);
	void validate_craft_service_attribution(EntityId home, EntityId craft, std::size_t from, std::size_t to,
	                                        const MaterialServiceAttribution &service) const;
	bool prepare_craft_service_delivery(const MaterialBatch &batch, std::optional<FiniteNativeWork> &work);
	void service_craft();
	void update_craft_reports();
	Json craft_board(const ConsoleAddress &a) const;
	Json save_craft() const;
	void restore_craft(const Json &saved);
	void validate_craft_transports() const;
	// World::Impl FTL mechanism and per-crew departure boundary.
	DepartureCoordinator departures;
	DepartureCapabilities departure_capabilities(EntityId id) const;
	DepartureAdmissionFacts departure_admission_facts() const;
	bool departure_member_authorised(EntityId leader, EntityId member) const;
	// Older saves could retain the pirate-only remainder after the prize changed owner.
	// Discard only that identifiable obsolete encounter goal, without changing physical state.
	void discard_obsolete_encounter_departures();
	void assign_departure_group(EntityId leader, const std::set<EntityId> &members);
	void prune_departure_groups();
	EntityId departure_leader(EntityId member) const;

	flecs::entity departure_drive(EntityId ship) const;
	bool departed(EntityId id) const;
	ConsoleAddress ftl_owner(EntityId id, const std::string &system) const;
	void retire_departed(EntityId id);
	std::vector<std::string> ftl_left_behind(EntityId id) const;
	std::string ftl_blocker(EntityId id, flecs::entity child) const;
	void prepare_ftl(flecs::entity ship);
	void actuate_ftl(flecs::entity ship);
	// Called after the paid machinery receipts. The finite completion is an event;
	// readiness continues to come from the live reserve and never replaces it.
	void finalise_ftl_processes();
	std::pair<bool, std::int64_t> ftl_local_facts(EntityId id) const;
	Json ftl_board(const ConsoleAddress &a) const;

	void commit_departures();
	// Generic world-fact adapter. No scenario/content identities belong here.
	MissionLedger mission_ledger;
	bool truly_destroyed(EntityId id) const;
	EntityId mission_subject(ScenarioId instance_id, const ScenarioActor &a) const;
	std::string lifecycle(EntityId id) const;
	std::map<std::string, std::string> personnel_dispositions(EntityId ship) const;
	state_tree::Value mission_fact(ScenarioId instance_id, const ScenarioFactBinding &b) const;
	ScenarioFacts mission_snapshot(ScenarioId instance_id) const;
	void mission_transition(std::string kind, EntityId source, EntityId subject, std::string operation = {},
	                        std::string detail = {});
	void mission_change(const std::string &key, const std::string &value, const std::string &kind, EntityId source,
	                    EntityId subject, const std::string &operation = {}, const std::string &detail = {});
	void resolve_lost_parties();
	void update_mission_facts();
	Json authoritative_report(EntityId ship, Json legacy) const;
	void finalise_destroyed_missions();

	void validate_mission_ledger() const;
	// Domain operation driven by an authored state tree. Flight, firing, docking and
	// departure retain their ordinary Controller and physical execution paths.
	InterdictionBook interdiction_book;
	bool interdiction_docking_permission(EntityId ship, EntityId target) const;
	void restore_interdiction_ships(const Json &rows, const Json &bodies);
	void record_interdiction_attack(EntityId source, EntityId target);
	void record_interdiction_insertion(EntityId ship, EntityId target);
	// Encounter goals own the relevant AI Controllers, not human Console slots.
	// Machinery, repairs and defensive fire retain their ordinary policies.
	bool encounter_owns_controller(EntityId ship, const ConsoleControllerSpec &controller) const;
	void release_encounter_navigation(EntityId ship);
	bool encounter_command(EntityId ship, OperatingAction action, std::int64_t value);
	std::optional<fixed::Vec> encounter_contact(EntityId ship, EntityId target) const;
	void encounter_move(EntityId ship, fixed::Vec destination, std::int64_t stop_radius,
	                    std::int64_t cruise_speed = 20000);
	bool encounter_dock_order(EntityId ship, const std::string &action, EntityId target = 0);
	void apply_occupation(EntityId target, const std::vector<std::string> &occupation, bool lethal);
	void restore_repairable_ship(EntityId target);

	void advance_interdictions();
	void charge_encounter_drive(EntityId ship);
	void prepare_encounter_departure(EntityId leader, const std::set<EntityId> &group);
	void apply_interdiction_orders();
	Json save_interdictions() const;
	void restore_interdictions(const Json &rows, const Catalog &content);
	void validate_interdiction_physics() const;
	// World adapter: physical coverage, retained evidence and permitted projection.
	SensorFindings sensor_findings;
	const SensorDefinition &sensor_definition(EntityId ship, const std::string &id) const;
	SensorDefinition sensor_acquisition_definition(EntityId ship, const std::string &id) const;
	bool directed_scan_covers(EntityId ship, const std::string &id, const BodyView &body) const;
	std::int64_t sensor_heading(EntityId ship, const SensorDefinition &d) const;
	fixed::Vec sensor_origin(EntityId ship, const SensorDefinition &d) const;
	// Query-local physical coverage. No cache survives a scan: power, damage, mounts,
	// personnel and modifiers can all change during a tick.
	SensorCoverage sensor_coverage(EntityId ship, const std::string &id, bool include_outer) const;
	bool sensor_covers(const SensorCoverage &coverage, fixed::Vec target, std::int64_t range) const;
	bool sensor_covers(EntityId ship, const std::string &id, fixed::Vec target, bool outer = false) const;
	SensorContact sensor_contact(const SensorCoverage &coverage, fixed::Vec target, std::int64_t distance,
	                             std::int64_t visibility = 1000) const;
	SensorContact sensor_contact(EntityId ship, const std::string &id, fixed::Vec target, std::int64_t distance,
	                             bool include_outer = true, std::int64_t visibility = 1000) const;
	bool sensor_powered(EntityId ship, const std::string &id) const;
	bool sensor_target(EntityId ship, const std::string &id, std::int64_t value, bool execute);
	bool finding_available(const SensorFinding &f) const;
	Json sensor_data(EntityId ship, EntityId target, const std::string &cap) const;
	void publish_sensor(EntityId ship, const std::string &system, EntityId target, const std::string &cap,
	                    const Json &data, std::uint64_t tick);
	// A receiver's apparent bodies use the same sensor admission as physical bodies.
	template <typename Visitor>
	void for_sensor_contacts(EntityId ship, const std::string &id, Visitor visit, bool include_outer = false,
	                         bool focused = false) const
	{
		const auto &shows = definition(ship).radar_shows.at(id);
		const auto coverage = sensor_coverage(ship, id, include_outer);
		const auto origin = coverage.origin;
		auto range = include_outer ? coverage.outer : coverage.normal;
		const auto source_altitude = altitude(ship);
		const auto child = instance(entities.at(ship), id)->entity;
		const auto *mode = directed_mode(ship, id);
		const bool acquiring =
		    focused && mode && directed_enabled(entities.at(ship), child, DirectedOperationKind::Scan);
		const auto focused_range =
		    acquiring ? effective_property(entities.at(ship), id, PropertyKey::sensors, mode->range) : 0;
		if (acquiring)
			range = std::max(range, focused_range);
		if (!range)
			return;
		const auto accept = [&](BodyView body, const auto &tags, bool apparent)
		{
			if (body.id == ship || std::none_of(tags.begin(), tags.end(),
			                                    [&](const auto &tag)
			                                    {
				                                    return std::find(shows.begin(), shows.end(), tag) != shows.end();
			                                    }))
				return;
			if (!apparent)
			{
				const auto &shape = definition(body.id).collider;
				body.radius = shape.box ? 0 : shape.radius;
			}
			const auto visibility = apparent ? 1000 : 1000 - concealment(body.id);
			const auto assessment = assess_sensor_contact(coverage, source_altitude, body, visibility, regions);
			const auto distance = assessment.distance;
			const auto detection = assessment.detection;
			const bool focused_only =
			    detection == SensorContact::None && acquiring && body.id == child.get<DirectedRuntime>().target &&
			    distance <= focused_range * visibility / 1000 && directed_scan_covers(ship, id, body);
			if (detection == SensorContact::None && !focused_only)
				return;
			visit(SensorContactInput{body, apparent, focused_only ? SensorContact::Position : detection, focused_only});
		};
		contact_index.each(Bounds::around(origin, range),
		                   [&](std::size_t row)
		                   {
			                   const auto &c = contact_rows[row];
			                   accept(view(c.id, c.entity.template get<Definition>(), c.entity.template get<Pose>(),
			                               c.entity.template get<Motion>()),
			                          definition(c.id).tags, false);
		                   });
		for (const auto &[key, g] : ghost_receptions.records())
			if (g.observer == ship && ghost_live(g))
				accept(ghost_body(g), std::array<std::string, 1>{"ship"}, true);
	}
	std::int64_t ghost_scan_duration(const GhostReception &g, const SensorDefinition &d) const;
	void publish_track_findings(EntityId ship, const std::string &id, EntityId target, const SensorTrack &track);
	void route_finding(EntityId ship, const std::string &system, const SensorFinding &finding, std::uint64_t tick,
	                   bool announce);
	void record_sensor_dispel(const GhostReception &g);
	void advance_sensors(bool acquire_timed = true);
	// Sensor mounts use only current permitted returns or their last observation.
	std::optional<std::pair<fixed::Vec, std::int64_t>> sensor_aim(EntityId ship, const std::string &id,
	                                                              ConsoleContacts &cache);
	// One permitted evidence view supplies both radar identity and detailed readouts.
	std::map<EntityId, Json> sensor_evidence(const ConsoleAddress &address) const;
	Json sensor_projection(const ConsoleAddress &address, const std::map<EntityId, Json> &evidence) const;
	Json save_sensors() const;

	void restore_sensors(const Json &rows);
	bool meta(std::uint64_t claimant, const OperatingCommand &command, bool execute);
};
} // namespace phoenix::world_detail
