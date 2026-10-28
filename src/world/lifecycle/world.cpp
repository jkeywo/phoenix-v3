module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import phoenix.content.assets;
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.effects;
import phoenix.content.equipment;
import phoenix.content.knowledge;
import phoenix.content.logistics;
import phoenix.content.operations;
import phoenix.content.resources;
import phoenix.content.scenarios;
import phoenix.content.sensing;
import phoenix.content.ui;
import phoenix.content.verification;
import phoenix.resources;
import phoenix.conversations;
import phoenix.gameplay_vocabulary;
import phoenix.gameplay_scenario;
import phoenix.knowledge.consoles;
import phoenix.knowledge.projections;
import phoenix.knowledge.read_models;
import phoenix.control.controllers;
import phoenix.control.views;
import phoenix.control.templates;
import phoenix.control.coordination;
import phoenix.control.protocol;
import phoenix.control.coordination_facts;
import phoenix.control.execution;
import phoenix.control.providers;
import phoenix.control.ownership;
import phoenix.control.handover;
import phoenix.effects.contracts;
import phoenix.effects.abilities;
import phoenix.effects.processes;
import phoenix.effects.rules;
import phoenix.effects.properties;
import phoenix.effects.rule_state;
import phoenix.world.state;
import phoenix.world.persistence;
import phoenix.world.assembly;
import phoenix.world.spatial;
import phoenix.gameplay.flight;
import phoenix.gameplay.fitting;
import phoenix.gameplay.combat.faults;
import phoenix.gameplay.docking;
import phoenix.gameplay.craft;
import phoenix.gameplay.logistics;
import phoenix.gameplay.ftl;
import phoenix.gameplay.ftl.coordinator;
import phoenix.gameplay.combat.state;
import phoenix.gameplay.directed;
import phoenix.gameplay.ordnance;
import phoenix.gameplay.combat.continuation;
import phoenix.gameplay.weapons;
import phoenix.gameplay.personnel.state;
import phoenix.gameplay.personnel.clinical;
import phoenix.gameplay.personnel.runtime;
import phoenix.gameplay.teams.routes;
import phoenix.resources.equipment;
import phoenix.effects.events;
import phoenix.effects.grants;
import phoenix.effects.transactions;
import phoenix.effects.native;
import phoenix.effects.clocks;
import phoenix.resources.processing;
import phoenix.gameplay.machinery;
import phoenix.gameplay.flight.work;
import phoenix.effects.machinery;
import phoenix.effects.finite_work;
import phoenix.effects.process_validation;
import phoenix.effects.continuation_validation;
import phoenix.effects.ability_validation;
import phoenix.effects.bindings;
import phoenix.effects.funding;
import phoenix.effects.preparation;
import phoenix.gameplay.combat.outcomes;
import phoenix.gameplay.combat.shields;
import phoenix.gameplay.personnel.outcomes;
import phoenix.gameplay.away.state;
import phoenix.gameplay.away.operations;
import phoenix.gameplay.away.crew;
import phoenix.gameplay.boarding.runtime;
import phoenix.scenarios.objectives;
import phoenix.scenarios.environment.streaming;
import phoenix.gameplay.psychic.state;
import phoenix.gameplay.psychic.receptions;
import phoenix.gameplay.psychic.projection;
import phoenix.gameplay.sensors.state;
import phoenix.gameplay.sensors.envelopes;
import phoenix.gameplay.sensors.acquisition;
import phoenix.gameplay.sensors.findings;
import phoenix.gameplay.sensors.validation;
import phoenix.gameplay.sensors.projection;
import phoenix.gameplay.sensors.coverage;
import phoenix.gameplay.sensors.evidence;

namespace phoenix
{
static_assert(World::ticks_per_second == simulation_tick_hz);
static_assert(World::ticks_per_second == resource_tick_hz);
#include "../schedule/tick_profile.hpp"
namespace
{
constexpr std::int64_t rate = World::ticks_per_second;
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
} // namespace
struct World::Impl
{
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
	flecs::query<const ActiveEffectIdentity, const EffectTarget, const EffectLifetime, const ModifierValues>
	    modifier_query;
	flecs::query<const StableId, const PropertyEffectOwner> property_owners;
	flecs::query<const ActiveRuleEffect> active_rule_query;
	std::map<std::uint64_t, flecs::entity> active_rule_index;
	mutable std::vector<flecs::entity> active_rule_order;
	mutable bool active_rule_order_dirty = true;
	flecs::query<const StableId, const Definition, const Pose, const Motion> bodies;
	flecs::query<const Equipment, Steering> steering_query;
	flecs::query<const Equipment, SensorRuntime> sensor_query;
	flecs::query<const Equipment, PsychicChoirState> choir_query;
	flecs::query<const Equipment, Engine> engine_query;
	flecs::query<const Equipment, FlightRuntime> flight_query;
	flecs::query<const Equipment> advanced_equipment_query;
	flecs::query<const Equipment, const ResourceBinding> passive_resource_query;
	flecs::query<const StableId, const GameplayShip> advanced_ships;
	flecs::query<const StableId, const CombatStructure> combat_ships;
	flecs::query<const Projectile> projectile_query;
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
	void clear_presentation_snapshots()
	{
		presentation.clear();
	}
	bool gameplay = false;
	std::vector<std::pair<std::uint64_t, OperatingCommand>> meta_pending;
#include "../../scenarios/environment/asteroid_fields.hpp"

	Impl()
	{
		ecs.set<Clock>({0});
		modifier_query = ecs.query_builder<const ActiveEffectIdentity, const EffectTarget, const EffectLifetime,
		                                   const ModifierValues>()
		                     .cached()
		                     .build();
		property_owners = ecs.query_builder<const StableId, const PropertyEffectOwner>()
		                      .with(flecs::Disabled)
		                      .optional()
		                      .cached()
		                      .build();
		active_rule_query = ecs.query_builder<const ActiveRuleEffect>().cached().build();
		bodies = ecs.query_builder<const StableId, const Definition, const Pose, const Motion>().cached().build();
		steering_query = ecs.query_builder<const Equipment, Steering>()
		                     .with<BasicEquipment>()
		                     .with(flecs::ChildOf)
		                     .second("$ship")
		                     .cached()
		                     .build();
		engine_query = ecs.query_builder<const Equipment, Engine>()
		                   .with<BasicEquipment>()
		                   .with(flecs::ChildOf)
		                   .second("$ship")
		                   .cached()
		                   .build();
		flight_query =
		    ecs.query_builder<const Equipment, FlightRuntime>().with(flecs::ChildOf).second("$ship").cached().build();
		advanced_equipment_query = ecs.query_builder<const Equipment>()
		                               .with<AdvancedEquipment>()
		                               .with(flecs::ChildOf)
		                               .second("$ship")
		                               .cached()
		                               .build();
		choir_query =
		    ecs.query_builder<const Equipment, PsychicChoirState>().with(flecs::ChildOf, "$ship").cached().build();
		sensor_query = ecs.query_builder<const Equipment, SensorRuntime>().with(flecs::ChildOf, "$ship").build();
		passive_resource_query = ecs.query_builder<const Equipment, const ResourceBinding>()
		                             .without<FlightRuntime>()
		                             .with(flecs::ChildOf)
		                             .second("$ship")
		                             .cached()
		                             .build();
		advanced_ships = ecs.query_builder<const StableId, const GameplayShip>().cached().build();
		combat_ships = ecs.query_builder<const StableId, const CombatStructure>().cached().build();
		projectile_query = ecs.query_builder<const Projectile>().cached().build();
		docking_query =
		    ecs.query_builder<const StableId, DockingState>().with(flecs::Disabled).optional().cached().build();
		dock_connection_query = ecs.query_builder<const StableId, DockConnection>().cached().build();
		inventory_query =
		    ecs.query_builder<const StableId, MaterialInventory>().with(flecs::Disabled).optional().cached().build();
		craft_query = ecs.query_builder<CraftState>().with(flecs::Disabled).optional().cached().build();
		vertical_query = ecs.query_builder<const StableId, VerticalMotion>().cached().build();
		integrate = ecs.system<Pose, Motion>("Integrate")
		                .kind(0)
		                .with<Movable>()
		                .each(
		                    [this](flecs::entity e, Pose &pose, Motion &motion)
		                    {
			                    if (dock_attached(e.get<StableId>().id))
				                    return;
			                    integrate_motion(pose, motion);
		                    });
		resolve_collisions =
		    ecs.system<const StableId, Pose, Motion, const Collider>("Collisions")
		        .kind(0)
		        .run(
		            [this](flecs::iter &it)
		            {
			            collisions.clear();
			            while (it.next())
			            {
				            auto ids = it.field<const StableId>(0);
				            auto poses = it.field<Pose>(1);
				            auto motions = it.field<Motion>(2);
				            auto shapes = it.field<const Collider>(3);
				            for (auto row : it)
					            if (!combat_dead(it.entity(row)))
						            collisions.push_back({ids[row].id,
						                                  &poses[row],
						                                  &motions[row],
						                                  {poses[row], motions[row], shapes[row]}});
			            }
			            std::sort(collisions.begin(), collisions.end(),
			                      [](const auto &a, const auto &b)
			                      {
				                      return a.id < b.id;
			                      });
			            for (auto &row : collisions)
				            row.body.mass = dock_mass(dock_root(row.id));
			            collision_boxes.clear();
			            for (const auto &entry : collisions)
				            collision_boxes.push_back(collision_bounds(entry.body));
			            collision_index.rebuild(collision_boxes);
			            struct SweepPair
			            {
				            std::size_t a, b;
				            std::int64_t time;
			            };
			            std::vector<SweepPair> swept;
			            for (std::size_t a = 0; a < collisions.size(); ++a)
			            {
				            auto box = collision_bounds(collisions[a].body);
				            const auto start = step_starts.find(collisions[a].id);
				            if (start != step_starts.end())
				            {
					            auto body = collisions[a].body;
					            body.position = start->second;
					            const auto initial = collision_bounds(body);
					            box.min_x = std::min(box.min_x, initial.min_x);
					            box.min_y = std::min(box.min_y, initial.min_y);
					            box.max_x = std::max(box.max_x, initial.max_x);
					            box.max_y = std::max(box.max_y, initial.max_y);
				            }
				            collision_boxes[a] = box;
			            }
			            collision_index.rebuild(collision_boxes);
			            for (std::size_t a = 0; a < collisions.size(); ++a)
				            for (auto b = collision_index.first(collision_boxes[a], a + 1); b != SpatialIndex::none;
				                 b = collision_index.first(collision_boxes[a], b + 1))
					            if (dock_root(collisions[a].id) != dock_root(collisions[b].id) &&
					                step_starts.contains(collisions[a].id) && step_starts.contains(collisions[b].id))
						            if (auto time = sweep_contact_time(collisions[a].body, collisions[b].body,
						                                               step_starts.at(collisions[a].id),
						                                               step_starts.at(collisions[b].id), true))
							            swept.push_back({a, b, *time});
			            std::sort(swept.begin(), swept.end(),
			                      [](const auto &a, const auto &b)
			                      {
				                      return a.time < b.time ||
				                             (a.time == b.time && std::pair{a.a, a.b} < std::pair{b.a, b.b});
			                      });
			            for (const auto &pair : swept)
			            {
				            const auto before_a = collisions[pair.a].body, before_b = collisions[pair.b].body;
				            if (sweep_collide(collisions[pair.a].body, collisions[pair.b].body,
				                              step_starts.at(collisions[pair.a].id),
				                              step_starts.at(collisions[pair.b].id), true))
				            {
					            dock_collision_response(pair.a, before_a);
					            dock_collision_response(pair.b, before_b);
					            collision_damage(collisions[pair.a].id, collisions[pair.b].id, before_a, before_b);
					            cancel_ship_impulse(collisions[pair.a].id);
					            cancel_ship_impulse(collisions[pair.b].id);
				            }
			            }
			            for (std::size_t i = 0; i < collisions.size(); ++i)
				            collision_boxes[i] = collision_bounds(collisions[i].body);
			            collision_index.rebuild(collision_boxes);
			            // Re-query after each pair: correction can create a later contact.
			            // Already visited pairs stay visited until the next pass, exactly
			            // as in the original stable-ID nested loop.
			            for (int pass = 0; pass < 4; ++pass)
				            for (std::size_t a = 0; a < collisions.size(); ++a)
				            {
					            auto begin = a + 1;
					            for (;;)
					            {
						            auto b = collision_index.first(collision_bounds(collisions[a].body), begin);
						            if (b == SpatialIndex::none)
							            break;
						            if (dock_root(collisions[a].id) == dock_root(collisions[b].id))
						            {
							            begin = b + 1;
							            continue;
						            }
						            const auto impact_a = collisions[a].body, impact_b = collisions[b].body;
						            const auto before_a = collisions[a].body.position,
						                       before_b = collisions[b].body.position;
						            collide(collisions[a].body, collisions[b].body);
						            dock_collision_response(a, impact_a);
						            dock_collision_response(b, impact_b);
						            if (before_a.x != collisions[a].body.position.x ||
						                before_a.y != collisions[a].body.position.y ||
						                before_b.x != collisions[b].body.position.x ||
						                before_b.y != collisions[b].body.position.y)
						            {
							            collision_damage(collisions[a].id, collisions[b].id, impact_a, impact_b);
							            cancel_ship_impulse(collisions[a].id);
							            cancel_ship_impulse(collisions[b].id);
						            }
						            for (std::size_t row = 0; row < collisions.size(); ++row)
							            if (dock_root(collisions[row].id) == dock_root(collisions[a].id) ||
							                dock_root(collisions[row].id) == dock_root(collisions[b].id))
								            collision_index.update(row, collision_bounds(collisions[row].body));
						            collision_index.update(a, collision_bounds(collisions[a].body));
						            collision_index.update(b, collision_bounds(collisions[b].body));
						            begin = b + 1;
					            }
				            }
			            // No structural changes occur in this phase; pointers never escape it.
			            for (auto &entry : collisions)
			            {
				            if (combat_dead(entities.at(entry.id)))
					            entry.body.velocity = {};
				            entry.pose->position = entry.body.position;
				            entry.pose->altitude = entry.body.altitude;
				            if (auto *v = entities.at(entry.id).try_get_mut<VerticalMotion>())
				            {
					            v->altitude = entry.body.altitude;
					            v->velocity = entry.body.vertical_velocity;
				            }
				            entry.motion->vertical_velocity = entry.body.vertical_velocity;
				            entry.motion->velocity = entry.body.velocity;
			            }
			            collisions.clear();
		            });
		index_contacts =
		    ecs.system<const StableId, const Pose>("IndexContacts")
		        .kind(0)
		        .run(
		            [this](flecs::iter &it)
		            {
			            contact_rows.clear();
			            contact_boxes.clear();
			            while (it.next())
			            {
				            auto ids = it.field<const StableId>(0);
				            auto poses = it.field<const Pose>(1);
				            for (auto row : it)
				            {
					            if (combat_dead(it.entity(row)))
						            continue;
					            contact_rows.push_back({ids[row].id, it.entity(row), poses[row].position});
				            }
			            }
			            // Spatial ties must not inherit Flecs table order: attaching or
			            // retiring effects can move bodies between tables after restore.
			            std::sort(contact_rows.begin(), contact_rows.end(),
			                      [](const auto &a, const auto &b)
			                      {
				                      return a.id < b.id;
			                      });
			            for (const auto &contact : contact_rows)
			            {
				            const auto &shape = definition(contact.id).collider;
				            contact_boxes.push_back(Bounds::around(
				                contact.position, shape.box ? shape.half_width + shape.half_length : shape.radius));
			            }
			            contact_index.rebuild(contact_boxes);
		            });
	}
	void compile(const Catalog &source, const DirectedCatalog &directed_catalog, SharedPropertyCatalog properties)
	{
		property_catalogue = std::move(properties);
		const auto content = convert_controller_content(source, directed_catalog, property_catalogue);
		const auto ability_catalog = compile_ability_catalog(content, property_catalogue);
		process_catalog = compile_process_catalog(content, property_catalogue);
		rule_templates = compile_rule_catalog(content, property_catalogue);
		native_effect_rules = compile_effect_rules(content, rule_templates);
		console_layouts = content.consoles;
		resource_catalogue = share_resource_catalogue(compile_resource_catalog(content));
		for (const auto &[id, effect] : content.effects)
			if (effect.at("kind") == "modifier")
				modifier_templates.emplace(id, compile_modifier_effect(content, id, property_catalogue));
			else if (effect.at("kind") == "operations")
				effect_groups.emplace(id, compile_effect_group(content, id, property_catalogue));
			else if (effect.at("kind") == "shield")
				shield_templates.emplace(id, compile_shield_effect(content, id, rule_templates));
			else if (effect.at("kind") == "payload")
				payload_templates.emplace(id, compile_payload(content, id, rule_templates));
			else if (effect.at("kind") == "periodic")
				periodic_templates.emplace(id, compile_periodic_effect(content, id, rule_templates));
			else if (effect.at("kind") == "process_grant")
				process_grant_templates.emplace(id, compile_process_grant(content, id));
			else if (effect.at("kind") == "ability_grant")
				ability_grant_templates.emplace(id, compile_ability_grant(content, id));
		packs = content.ordered_packs;
		camera_widgets.clear();
		for (const auto &[name, layout] : content.consoles)
		{
			auto &kinds = camera_widgets[name];
			for (const auto &widget : layout.at("widgets"))
				if (const auto kind = widget.at("kind").get<std::string>();
				    (kind == "camera_mode" || kind == "camera_direction") &&
				    std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
					kinds.push_back(kind);
		}
		knowledge.configure(content, property_catalogue);
		gameplay = true;
		for (const auto &[name, definition] : content.entities)
		{
			auto assembly =
			    compile_world_entity(source, content, name, directed_catalog, rule_templates, property_catalogue,
			                         ability_catalog, process_catalog, resource_catalogue);
			visual_scales[name] = assembly.visual_scale;
			if (!assembly.participants.empty())
				comms_rosters[name] = std::move(assembly.participants);
			definitions.emplace(name, std::move(assembly.definition));
		}
	}
	void create(EntityId id, const std::string &definition, const Json &transform)
	{
		const auto &data = definitions.at(definition);
		const auto faction = transform.value("faction", data.faction);
		factions.validate(faction);
		auto choice = data.sprite_entry;
		if (transform.contains("sprite_entry") && !transform.at("sprite_entry").get<std::string>().empty())
		{
			const auto name = transform.at("sprite_entry").get<std::string>();
			const auto found = std::find(data.sprite_entries.begin(), data.sprite_entries.end(), name);
			if (found == data.sprite_entries.end())
				throw std::runtime_error("Unknown placement sprite entry " + name);
			choice = static_cast<std::uint32_t>(found - data.sprite_entries.begin());
		}
		sprite_choices[id] = choice;
		instance_systems |= data.fitted.has_value();
		explicit_stations |= data.explicit_stations;
		Pose pose{{transform.at("x_mm"), transform.at("y_mm")}, transform.at("heading_mdeg")};
		auto entity = ecs.entity()
		                  .set<StableId>({id})
		                  .set<FactionMembership>({faction})
		                  .set<Definition>({definition, data.radar_icon, data.radar_sprite})
		                  .set<Pose>(pose)
		                  .set<Motion>({})
		                  .set<Collider>(data.collider)
		                  .set<CustomPropertyState>(data.properties);
		// Before the boundary index owns the ship, exceptions must also dispose
		// of any equipment children already constructed through ChildOf.
		struct Construction
		{
			flecs::entity entity;
			bool indexed = false;
			std::map<EntityId, flecs::entity> *registry = nullptr;
			EntityId id = 0;
			~Construction()
			{
				if (!indexed)
				{
					if (registry)
						registry->erase(id);
					entity.destruct();
				}
			}
		} construction{entity};
		if (data.destruction || data.durability)
			entity.set<Destructible>({data.destruction.value_or(DestructionDefinition{}), data.durability});
		if (data.combat)
		{
			CombatStructure structure;
			structure.definition = *data.combat;
			structure.random = id * 7919 + 1;
			for (const auto &[equipment, faults] : data.combat->faults)
				structure.faults[equipment].resize(faults.size());
			for (const auto &c : data.combat->compartments)
				structure.compartments.push_back({c.initial, 0, c.initial == 0});
			if (std::all_of(structure.compartments.begin(), structure.compartments.end(),
			                [](const auto &c)
			                {
				                return c.hp == 0;
			                }))
				entity.get_mut<Destructible>().destroyed_tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
			entity.set<CombatStructure>(std::move(structure));
			entity.set<ShieldComponent>({});
		}
		if (!data.docking.empty())
		{
			DockingState state;
			for (const auto &p : data.docking)
				state.ports.push_back({p.auto_accept_allies, p.supply, {}});
			entity.set<DockingState>(std::move(state));
		}
		ShipStockStores stock_stores;
		if (data.combat)
			for (const auto &system : data.combat->systems)
				if (system.kind == OperatingSystem::Magazine)
					stock_stores.stores.emplace("system:" + system.id,
					                            StockStore(system.get<MagazineDefinition>().stock, resource_catalogue));
		if (!data.logistics.stores.empty())
		{
			MaterialInventory inventory;
			for (const auto &store : data.logistics.stores)
			{
				const bool magazine =
				    data.combat && data.combat->system_ids.contains(store.system) &&
				    data.combat->systems.at(data.combat->system_ids.at(store.system)).kind == OperatingSystem::Magazine;
				const auto key = magazine ? "system:" + store.system : "logistics:" + store.id;
				if (!magazine)
					stock_stores.stores.emplace(key, StockStore(store.stock, resource_catalogue));
				inventory.stores.push_back({key, store.targets});
			}
			entity.set<MaterialInventory>(std::move(inventory));
		}
		if (!stock_stores.stores.empty())
			entity.set<ShipStockStores>(std::move(stock_stores));
		if (data.collider.mass)
			entity.add<Movable>();
		if (data.fitted)
			entity.add<NamedEquipment>();
		if (data.gameplay)
		{
			entity.add<GameplayShip>();
			if (data.gameplay->power)
				entity.emplace<ShipResources>(*data.gameplay->power, std::uint64_t{1});
		}
		const auto &templates = data.fitted->values;
		EquipmentIndex index;
		for (std::size_t i = 0; i < templates.size(); ++i)
		{
			const auto &v = templates[i];
			auto child =
			    ecs.entity().child_of(entity).set<Equipment>({v.id, v.kind, i, v.maximum, v.advanced, v.enabled});
			child.set<CustomPropertyState>(data.equipment_properties.at(v.id));
			index.values.push_back(child);
			index.by_id.emplace(v.id, child);
			if (data.combat && data.combat->system_ids.contains(v.id))
			{
				CombatMechanism mechanism;
				mechanism.definition = data.combat->systems.at(data.combat->system_ids.at(v.id));
				switch (mechanism.definition.kind)
				{
				case OperatingSystem::Weapon:
					mechanism.state = WeaponState{};
					break;
				case OperatingSystem::Turret:
					mechanism.state = TurretState{mechanism.definition.bearing, 0};
					break;
				case OperatingSystem::Magazine:
					mechanism.state = MagazineState{"system:" + mechanism.definition.id};
					break;
				case OperatingSystem::Shield:
					mechanism.state = ShieldBindingState{};
					break;
				case OperatingSystem::Team:
					mechanism.state = TeamState{
					    static_cast<std::int64_t>(data.combat->compartment_ids.at(mechanism.definition.compartment))};
					break;
				default:
					throw std::runtime_error("Unsupported combat mechanism");
				}
				child.set<CombatMechanism>(std::move(mechanism));
			}
			if (v.engine)
				child.set<Engine>(*v.engine);
			if (v.steering)
			{
				auto turn = *v.steering;
				turn.heading = pose.heading;
				child.set<Steering>(turn);
			}
			if (v.radar)
				child.set<Radar>(*v.radar).set<SensorRuntime>({});
			else if (data.sensors.contains(v.id))
			{
				std::int64_t range = 0;
				for (const auto &mode : data.directed.at(v.id).modes)
					if (mode.operation == DirectedOperationKind::Scan)
						range = std::max(range, mode.range);
				child.set<Radar>({range, v.enabled}).set<SensorRuntime>({});
			}
			if (!data.gameplay)
			{
				child.add<BasicEquipment>();
				continue;
			}
			child.add<AdvancedEquipment>().set<OperatingHeat>({});
			if (data.ftl.contains(v.id))
				child.set<FtlReserve>({data.ftl.at(v.id), 0});
			const auto &g = data.gameplay->values[i];
			if (g.flight)
			{
				auto state = initial_flight(*g.flight, pose, Motion{});
				state.enabled = v.enabled;
				child.set<FlightRuntime>({*g.flight, state});
			}
			if (auto power = entity.try_get_mut<ShipResources>())
			{
				child.set<ResourceBinding>({i});
				if (!v.enabled)
					control_resource(*power, i,
					                 v.kind == OperatingSystem::Coolant ? ResourceControl::CoolingIsolated
					                                                    : ResourceControl::Enabled,
					                 v.kind == OperatingSystem::Coolant ? 1 : 0);
			}
		}
		entity.set<EquipmentIndex>(std::move(index));
		initialize_processors(entity, data);
		entity.set<PropertyEffectOwner>({});
		if (entity.has<ShieldComponent>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::Shield)
					install_shield_grant(entity.get_mut<ShieldComponent>(), m->definition.shield_definition,
					                     m->definition.id, m->definition.id, m->definition.shield_effect);
		if (!entities.emplace(id, entity).second)
			throw std::runtime_error("duplicate entity identity");
		construction.registry = &entities;
		construction.id = id;
		initialize_directed(entity);
		BridgeBlackboard board;
		board.condition = data.bridge.initial;
		for (const auto &[console, spec] : data.console_bridge)
			if (!spec.stances.empty())
			{
				board.stances.emplace(console, spec.initial);
				board.epochs.emplace(console, 0);
			}
		entity.set<BridgeBlackboard>(std::move(board));
		entity.set<PersonnelState>(initial_personnel(data.personnel));
		for (std::size_t i = 0; i < data.personnel.officers.size(); ++i)
			if (const auto at = data.officer_properties.find(data.personnel.officers[i].id);
			    at != data.officer_properties.end())
				entity.get_mut<PersonnelState>().officers[i].properties = at->second;
		if (auto *structure = entity.try_get_mut<CombatStructure>())
			for (std::size_t i = 0; i < structure->compartments.size(); ++i)
				if (const auto at = data.compartment_properties.find(structure->definition.compartments[i].id);
				    at != data.compartment_properties.end())
					structure->compartments[i].properties = at->second;
		initialise_abilities(entity);
		initialise_processes(entity);
		for (const auto &[choir, d] : data.psychic.choirs)
		{
			PsychicChoirState state;
			instance(entity, choir)->entity.set<PsychicChoirState>(state);
		}
		if (data.craft)
			entity.set<VerticalMotion>({}).set<CraftGuidance>({});
		if (data.layout)
		{
			EntityControls controls;
			controls.layout = *data.layout;
			for (const auto &console : data.consoles)
				controls.profiles.emplace(console.id, data.layout->default_profiles.contains(console.id)
				                                          ? data.layout->default_profiles.at(console.id)
				                                          : data.layout->default_profile);
			controls.controllers = data.controller_templates;
			for (auto &controller : controls.controllers)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					const auto &command = controller.definition.commands[i];
					if (command.initial_heading)
					{
						if (pose.heading < command.minimum || pose.heading > command.maximum)
							throw std::runtime_error("Spawn heading is outside Controller command bounds");
						controller.commands[i].value = pose.heading;
					}
				}
			entity.set<EntityControls>(std::move(controls));
		}
		if (data.layout)
			for (const auto fitted = fittings(entity); const auto &system : fitted->values)
			{
				const auto values = telemetry(id, system.ordinal);
				knowledge.verify_reporter_fields(definition, system.id, values);
				for (const auto &controller : data.controller_templates)
					if (controller.brain)
						for (const auto &read : controller.brain->telemetry_reads)
							if (const auto &binding = read.source;
							    binding.system == system.id && !values.contains(binding.field))
								throw std::runtime_error("Controller observation field unavailable on fitted System " +
								                         system.id + ": " + binding.field);
			}
		ShipParticipation participation;
		participation.sidebar_default = data.bridge.sidebar_default;
		for (const auto &seat : data.consoles)
			participation.consoles.push_back(
			    {seat.id, seat.station, seat.human_seeking,
			     data.console_bridge.contains(seat.id) && data.console_bridge.at(seat.id).viewscreen,
			     seat.human_preferences});
		ownership.configure_ship(id, std::move(participation));
		construction.indexed = true;
		next_entity = std::max(next_entity, id + 1);
		knowledge.register_ship(id, definition, ecs.get<Clock>().tick);
		register_comms(id, definition);
		if (!data.consoles.empty())
			knowledge.set_camera(id, {}, ecs.get<Clock>().tick);
		if (data.layout)
			consume_controller_commands(entity);
	}
	const CompiledDefinition &definition(EntityId ship) const
	{
		return definitions.at(entities.at(ship).get<Definition>().name);
	}
	NativeOwnerDefinitions native_owner_definitions(EntityId id) const
	{
		const auto &data = definition(id);
		return {data.personnel, data.logistics, data.abilities, data.processes, data.psychic, data.tags};
	}
	const ConsoleSpec *seat(EntityId ship, const std::string &name) const
	{
		const auto &seats = definition(ship).consoles;
		for (const auto &seat : seats)
			if (seat.id == name || (name.empty() && seats.size() == 1))
				return &seat;
		return nullptr;
	}
	std::optional<EquipmentRef> instance(flecs::entity ship, const std::string &id) const
	{
		const auto &index = ship.get<EquipmentIndex>().by_id;
		const auto found = index.find(id);
		return found == index.end() ? std::optional<EquipmentRef>{} : equipment_ref(found->second);
	}
	std::uint64_t console_operator(const ConsoleAddress &address) const
	{
		return ownership.operator_for(address);
	}
	ConsoleAddress command_address(std::uint64_t claimant, const OperatingCommand &command) const
	{
		auto address = ownership.claims().at(claimant);
		if (!command.console.empty())
			address.console = command.console;
		return address;
	}
	ConsoleAddress system_owner(EntityId ship, const std::string &system) const
	{
		if (definition(ship).layout)
			return {ship, definition(ship).observation_owners.at(system)};
		for (const auto &c : definition(ship).consoles)
			if (std::find(c.controlled_systems.begin(), c.controlled_systems.end(), system) !=
			    c.controlled_systems.end())
				return {ship, c.id};
		throw std::runtime_error("System has no Console owner");
	}
	std::optional<FlightCommandKind> flight_action(OperatingAction action) const
	{
		switch (action)
		{
		case OperatingAction::Speed:
			return FlightCommandKind::Speed;
		case OperatingAction::Heading:
			return FlightCommandKind::Heading;
		case OperatingAction::EngineEnabled:
		case OperatingAction::SystemEnabled:
			return FlightCommandKind::Enabled;
		case OperatingAction::Assisted:
			return FlightCommandKind::Assisted;
		case OperatingAction::Longitudinal:
			return FlightCommandKind::Longitudinal;
		case OperatingAction::Lateral:
			return FlightCommandKind::Lateral;
		case OperatingAction::ManualSteering:
			return FlightCommandKind::Steering;
		case OperatingAction::Boost:
			return FlightCommandKind::Boost;
		case OperatingAction::Impulse:
			return FlightCommandKind::Impulse;
		case OperatingAction::MatchSpeed:
			return FlightCommandKind::MatchSpeed;
		case OperatingAction::SelectContact:
			return FlightCommandKind::SelectContact;
		case OperatingAction::Destination:
			return FlightCommandKind::Destination;
		case OperatingAction::FollowContact:
			return FlightCommandKind::FollowContact;
		default:
			return {};
		}
	}
	std::optional<ResourceControl> resource_action(OperatingAction action) const
	{
		switch (action)
		{
		case OperatingAction::SystemEnabled:
			return ResourceControl::Enabled;
		case OperatingAction::GeneratorOutput:
			return ResourceControl::GeneratorOutput;
		case OperatingAction::ChargeLimit:
			return ResourceControl::ChargeLimit;
		case OperatingAction::DischargeLimit:
			return ResourceControl::DischargeLimit;
		case OperatingAction::GroupAllocation:
			return ResourceControl::GroupBudget;
		case OperatingAction::LinkIsolation:
			return ResourceControl::ConduitIsolated;
		case OperatingAction::CoolingAllocation:
			return ResourceControl::CoolingAllocation;
		case OperatingAction::ThermalOverride:
			return ResourceControl::ThermalOverride;
		case OperatingAction::Overdrive:
			return ResourceControl::Overdrive;
		default:
			return {};
		}
	}
	bool operate(flecs::entity entity, const std::string &name, OperatingRequest output, bool execute)
	{
		if (output.action == OperatingAction::DirectedOperatingMode ||
		    output.action == OperatingAction::DirectedTargetingMode || output.action == OperatingAction::DirectedTarget)
			return output.arguments.empty() &&
			       directed_setting(entity.get<StableId>().id, name,
			                        output.action == OperatingAction::DirectedOperatingMode   ? "operating_mode"
			                        : output.action == OperatingAction::DirectedTargetingMode ? "targeting_mode"
			                                                                                  : "target",
			                        output.value, execute);
		if (output.action == OperatingAction::Altitude)
		{
			const auto equipment = instance(entity, name);
			if (!entity.has<VerticalMotion>() || !equipment || equipment->kind != OperatingSystem::Engine ||
			    !output.arguments.empty() || output.value < -1000000 || output.value > 1000000)
				return false;
			if (execute)
				entity.get_mut<VerticalMotion>().desired = output.value;
			return true;
		}
		if (output.action == OperatingAction::SensorTarget)
			return output.arguments.empty() && sensor_target(entity.get<StableId>().id, name, output.value, execute);
		if (auto target = instance(entity, name);
		    target && (target->kind == OperatingSystem::Comms || target->kind == OperatingSystem::Displacement ||
		               target->kind == OperatingSystem::Dispatch || target->kind == OperatingSystem::Ftl ||
		               target->kind == OperatingSystem::ShuttleBay || target->kind == OperatingSystem::Psychic))
		{
			if (output.action != OperatingAction::SystemEnabled || output.value < 0 || output.value > 1 ||
			    !output.arguments.empty())
				return false;
			if (execute)
				target->enabled = output.value != 0;
			return true;
		}
		if (entity.has<CombatStructure>())
			if (auto target = instance(entity, name); target && target->entity.has<CombatMechanism>())
				return operate_combat(entity, target->entity, output, execute);
		auto fitted = fittings(entity);
		auto systems = gameplay_systems(entity);
		if (!fitted)
			return false;
		if (!systems)
		{
			const auto target = instance(entity, name);
			if (!target || !output.arguments.empty())
				return false;
			if (output.action == OperatingAction::SystemEnabled)
			{
				if (target->kind == OperatingSystem::Engine)
					output.action = OperatingAction::EngineEnabled;
				else if (target->kind == OperatingSystem::Radar)
					output.action = OperatingAction::RadarEnabled;
				else
					return false;
			}
			if (!admissible(output, target->engine, target->steering, target->radar))
				return false;
			if (execute)
			{
				apply(output, target->engine, target->steering, target->radar);
				if (target->engine)
					target->enabled = target->engine->enabled;
				if (target->radar)
					target->enabled = target->radar->enabled;
			}
			return true;
		}
		std::size_t index = 0;
		for (; index < fitted->values.size(); ++index)
			if (fitted->values[index].id == name)
				break;
		if (index == fitted->values.size() || !operating_value_valid(operating_action(output.action), output.value) ||
		    !operating_arguments_valid(output.action, output.value, output.arguments))
			return false;
		if (output.action == OperatingAction::FollowContact && output.value &&
		    static_cast<EntityId>(output.arguments[0]) == entity.get<StableId>().id)
			return false;
		auto system = systems->values[index];
		if (system.flight)
			if (auto kind = flight_action(output.action))
			{
				const auto &definition = system.flight->definition;
				if (*kind == FlightCommandKind::Impulse && output.value &&
				    system.flight->state.impulse == ImpulsePhase::Idle && definition.impulse_minimum_reserve)
				{
					const auto *resources = entity.try_get<ShipResources>();
					if (!resources)
						return false;
					const auto store = resources->definition.systems[index].store;
					if (store == no_resource || !resources->network.stores()[store].enabled ||
					    resources->network.stores()[store].thermal_stop ||
					    resources->network.stores()[store].available() < definition.impulse_minimum_reserve)
						return false;
				}
				auto candidate = system.flight->state;
				const auto result =
				    apply_flight(system.flight->definition, candidate, {*kind, output.value, output.arguments},
				                 entity.get<Pose>(), entity.get<Motion>());
				if (result.accepted && execute)
				{
					system.flight->state = std::move(candidate);
					if (output.action == OperatingAction::SystemEnabled ||
					    output.action == OperatingAction::EngineEnabled)
						fitted->values[index].enabled = system.flight->state.enabled;
				}
				return result.accepted;
			}
		if (fitted->values[index].radar &&
		    (output.action == OperatingAction::RadarEnabled || output.action == OperatingAction::SystemEnabled))
		{
			if (execute)
			{
				fitted->values[index].radar->enabled = output.value != 0;
				fitted->values[index].enabled = output.value != 0;
			}
			return output.value == 0 || output.value == 1;
		}
		if (output.action == OperatingAction::SystemEnabled && fitted->values[index].kind == OperatingSystem::Coolant)
		{
			if (output.value != 0 && output.value != 1)
				return false;
			if (execute)
			{
				fitted->values[index].enabled = output.value != 0;
				if (systems->power)
					control_resource(*systems->power, fitted->values[index].entity.get<ResourceBinding>().slot,
					                 ResourceControl::CoolingIsolated, output.value ? 0 : 1);
			}
			return true;
		}
		if (systems->power)
			if (auto kind = resource_action(output.action))
			{
				const auto accepted =
				    execute
				        ? control_resource(*systems->power, fitted->values[index].entity.get<ResourceBinding>().slot,
				                           *kind, output.value)
				        : can_control_resource(*systems->power,
				                               fitted->values[index].entity.get<ResourceBinding>().slot, *kind,
				                               output.value);
				if (accepted && execute && output.action == OperatingAction::SystemEnabled)
					fitted->values[index].enabled = output.value != 0;
				return accepted;
			}
		if (output.action == OperatingAction::RadarEnabled && fitted->values[index].radar)
		{
			if (execute)
				fitted->values[index].radar->enabled = output.value != 0;
			return true;
		}
		return false;
	}
	void publish_waypoint(EntityId ship, const ControllerChannelState &state, bool reached)
	{
		const auto &schema = knowledge.definition();
		const auto key = schema.schema("phoenix.waypoint");
		KnowledgeRecordInput record;
		record.schema = key;
		record.producer = schema.role("phoenix.scenario");
		record.subject = ship;
		record.provenance = {ship, ecs.get<Clock>().tick, state.revision * 2 + (reached ? 1 : 0), 0,
		                     EvidenceKind::Fact};
		record.fields = {{schema.field(key, "active"), state.value != 0},
		                 {schema.field(key, "reached"), reached},
		                 {schema.field(key, "x"), state.arguments.at(0)},
		                 {schema.field(key, "y"), state.arguments.at(1)},
		                 {schema.field(key, "revision"), static_cast<std::int64_t>(state.revision)}};
		knowledge.publish_ship(ship, record, ecs.get<Clock>().tick);
	}
	void waypoint_arrivals()
	{
		for (const auto &[id, entity] : entities)
			if (entity.has<EntityControls>())
				for (const auto &controller : entity.get<EntityControls>().controllers)
					for (std::size_t i = 0; i < controller.commands.size(); ++i)
					{
						const auto &state = controller.commands[i];

						if (controller.definition.commands[i].action != OperatingAction::Waypoint || !state.value)
							continue;
						const auto board = knowledge.project_json({id, controller.definition.console});
						bool reached = false;
						for (const auto &record : board.at("shared"))
							if (record.at("schema") == "phoenix.waypoint")
								reached = record.at("fields").value("reached", false);
						const auto from = step_starts.at(id), to = entity.get<Pose>().position;
						ScenarioRegion region;
						region.centre = {state.arguments[0], state.arguments[1]};
						region.radius = controller.definition.commands[i].arrival_radius;
						if (!reached && scenario_crosses(region, {from.x, from.y}, {to.x, to.y}))
							publish_waypoint(id, state, true);
					}
	}
#include "../../gameplay/flight/navigation_assessment.hpp"
#include "../../control/execution/controller_execution.hpp"
#include "../../control/execution/controller_actions.hpp"
#include "../../control/coordination/controller_coordination.hpp"
#include "../../control/providers/controller_models.hpp"
	std::int64_t sensor_range(EntityId ship, const std::string &name, bool outer = false) const
	{
		const auto entity = entities.at(ship);
		const auto sensor = instance(entity, name);
		if (!sensor || !sensor->radar || !sensor->enabled || !sensor->radar->enabled ||
		    structurally_disabled(entity, name))
			return 0;
		auto range =
		    outer ? std::max(sensor->radar->range, sensor_definition(ship, name).long_range) : sensor->radar->range;
		if (sensor->kind == OperatingSystem::Weapon && directed_definition(ship, name))
		{
			const auto *mode = directed_mode(ship, name);
			if (!mode || mode->operation != DirectedOperationKind::Scan)
				return 0;
			range = mode->range;
		}
		range = effective_property(entity, name, "sensors", range);
		if (auto systems = gameplay_systems(entity); systems && systems->power)
		{
			const auto index = sensor->entity.get<ResourceBinding>().slot;
			const auto &power = *systems->power;
			const auto &binding = power.definition.systems[index];
			if (binding.store != no_resource)
			{
				const auto &store = power.network.stores()[binding.store];
				if (!store.enabled || (store.thermal_stop && !store.thermal_override))
					return 0;
			}
			range = range * power.grants[index].supply_permille / 1000;
		}
		return range;
	}
	const SensorEnvelope &sensor_envelope(EntityId ship, const std::string &name, std::int64_t range) const
	{
		return sensor_envelopes.get(ship, name, entities.at(ship).get<Pose>().position, range, regions);
	}

	std::map<std::string, std::int64_t> telemetry(EntityId id, std::size_t index) const
	{
		auto entity = entities.at(id);
		const auto fitted = (*fittings(entity)).values[index];
		std::map<std::string, std::int64_t> values{{"enabled", fitted.enabled}, {"requested", 0}};
		if (fitted.engine)
		{
			values["requested"] = fitted.engine->speed;
			values["enabled"] = fitted.engine->enabled;
		}
		if (fitted.steering)
			values["requested"] = fitted.steering->heading;
		if (fitted.radar)
			values["enabled"] = fitted.radar->enabled;
		if (auto systems = gameplay_systems(entity))
		{
			const auto system = systems->values[index];
			if (system.flight)
			{
				const auto &f = system.flight->state;
				values["requested"] =
				    system.flight->definition.kind == FlightKind::Steering ? f.desired_heading : f.desired_speed;
				values["desired_speed"] = f.desired_speed;
				values["desired_heading"] = f.desired_heading;
				values["enabled"] = f.enabled;
				values["minimum_speed"] = -system.flight->definition.reverse_speed;
				values["assisted"] = f.assisted;
				values["longitudinal_thrust"] = f.longitudinal;
				values["lateral_thrust"] = f.lateral;
				values["manual_steering"] = f.steering;
				values["boost"] = f.boost;
				values["impulse"] = f.impulse != ImpulsePhase::Idle;
				values["impulse_phase"] = static_cast<unsigned>(f.impulse);
				values["impulse_progress"] = f.charge_milliticks;
				values["impulse_charge_spent"] = f.charge_spent;
				values["impulse_charge_total"] = system.flight->definition.impulse_charge_ticks * 1000;
				values["impulse_reason"] = f.impulse_reason;
				values["effective_boost"] = f.effective_boost;
				values["lateral_direction"] = system.flight->definition.lateral_direction;
				values["lateral_acceleration"] = system.flight->definition.lateral_acceleration;
				values["lateral_maximum_speed"] = system.flight->definition.lateral_speed;
				values["impulse_ready"] = 0;
				if (const auto *resources = entity.try_get<ShipResources>())
				{
					const auto store = resources->definition.systems[index].store;
					values["impulse_ready"] = f.enabled && store != no_resource &&
					                          resources->network.stores()[store].enabled &&
					                          !resources->network.stores()[store].thermal_stop &&
					                          resources->network.stores()[store].available() >=
					                              system.flight->definition.impulse_minimum_reserve;
				}
				values["match_speed"] = f.matching;
				values["selected_contact"] = f.selected_contact;
				values["matched_contact"] = f.matched_contact;
				values["match_stale"] = f.match_stale;
				values["navigating"] = f.navigating;
				values["destination_x"] = f.destination.x;
				values["destination_y"] = f.destination.y;
				values["following"] = f.following;
				values["follow_stale"] = f.follow_stale;
				values["follow_distance"] = f.follow_distance;
				values["follow_speed_limit"] = f.follow_speed_limit;
				values["matched_speed"] = f.matched_speed;
				values["actual_longitudinal"] = f.actual_longitudinal;
				values["actual_lateral"] = f.actual_lateral;
				values["actual_steering"] = f.actual_steering;
			}
			if (systems->power)
			{
				const auto &p = *systems->power;
				const auto &binding = p.definition.systems[fitted.entity.get<ResourceBinding>().slot];
				const auto &g = p.grants[fitted.entity.get<ResourceBinding>().slot];
				values["supply_permille"] = g.supply_permille;
				values["used_power"] = (g.paid + g.discrete) * rate;
				values["reserved_power"] = g.requested * rate;
				values["supply_limited"] = g.supply_permille < 1000;
				if (binding.store != no_resource)
				{
					const auto &d = p.definition.network.stores[binding.store];
					const auto &v = p.network.stores()[binding.store];
					values["enabled"] =
					    values.at("enabled") && v.enabled && (!system.flight || system.flight->state.enabled);
					const auto &t = p.network.telemetry()[binding.store];
					values["stored_energy"] = v.energy();
					values["energy_capacity"] = d.capacity;
					values["incoming_power"] = t.accepted_incoming * rate;
					values["heat"] = v.heat;
					values["heat_capacity"] = d.stop_heat;
					values["stress"] = v.stress;
					values["thermal_stopped"] = v.thermal_stop;
					values["thermal_override"] = v.thermal_override;
					values["overdrive"] = v.overdrive;
					values["charge_limit"] = v.charge_limit;
					values["discharge_limit"] = v.discharge_limit;
					values["passive_cooling"] = d.passive_cooling;
					values["actual_cooling"] = t.heat_removed * rate;
					const auto forecast = p.network.forecast(binding.store);
					values["depletion_ticks"] = forecast.depletion_ticks;
					values["thermal_stop_ticks"] = forecast.thermal_stop_ticks;
					values["full_ticks"] = forecast.full_ticks;
					values["thermal_recovery_ticks"] = forecast.thermal_recovery_ticks;
				}
				if (binding.generator != no_resource)
				{
					const auto &v = p.network.generators()[binding.generator];
					values["requested"] = v.commanded_rate;
					values["generator_output"] = v.commanded_rate;
					values["actual_output"] = v.actual_rate;
					const auto &generator = p.definition.network.generators[binding.generator];
					const auto &load = p.network.telemetry()[generator.store];
					// Previous committed load, including exported power and storage losses.
					values["demand_output"] =
					    std::min(generator.maximum_rate, (load.outgoing + load.consumed + load.lost) * rate);
				}
				if (binding.group != no_resource)
				{
					const auto &v = p.network.groups()[binding.group];
					values["requested"] = v.budget_rate;
					values["group_allocation"] = v.budget_rate;
					values["flow_cut"] = v.cut;
				}
				if (binding.conduit != no_resource)
					values["link_isolated"] = p.network.links()[binding.conduit].isolated;
				if (binding.cooling != no_resource)
				{
					const auto &v = p.network.cooling_links()[binding.cooling];
					values["requested"] = v.allocation_rate;
					values["cooling_allocation"] = v.allocation_rate;
					const auto source = p.definition.network.cooling_links[binding.cooling].source;
					const auto forecast = p.network.coolant_forecast(source);
					values["coolant_depletion_ticks"] = forecast.depletion_ticks;
					values["actual_coolant_used"] = forecast.observed_coolant_used * rate;
					values["coolant_stock"] =
					    p.network.coolant_sources()[p.definition.network.cooling_links[binding.cooling].source]
					        .remaining();
				}
			}
		}
		combat_telemetry(entity, fitted.entity, values);
		return values;
	}
#include "../../resources/equipment/power_projection.hpp"
#include "../../knowledge/projections/console_observations.hpp"
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
	Steering effective_basic_steering(flecs::entity child) const
	{
		auto effective = child.get<Steering>();
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		effective.rate = effective_property(ship, equipment, "turn", effective.rate);
		effective.rate = apply_property_delta(
		    effective.rate, native_process_rate(ship.get<StableId>().id, "propulsion", equipment) - 1000,
		    resource_limit);
		return effective;
	}
	Engine effective_basic_engine(flecs::entity child) const
	{
		auto effective = child.get<Engine>();
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		effective.enabled &= !structurally_disabled(ship, equipment);
		effective.acceleration = effective_property(ship, equipment, "propulsion", effective.acceleration);
		effective.max_speed = effective_property(ship, equipment, "speed", effective.max_speed);
		effective.speed = effective.speed == child.get<Engine>().max_speed
		                      ? effective.max_speed
		                      : std::min(effective.speed, effective.max_speed);
		effective.acceleration = apply_property_delta(
		    effective.acceleration, native_process_rate(ship.get<StableId>().id, "propulsion", equipment) - 1000,
		    resource_limit);
		return effective;
	}
	void run_basic_steering()
	{
		for (auto child : ordered_equipment(steering_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id) &&
			                                !structurally_disabled(ship, child.get<Equipment>().id))
			{
				auto &native = child.get_mut<Steering>();
				const auto *quote =
				    native_machinery_quote(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id);
				auto effective =
				    quote && quote->basic_steering ? *quote->basic_steering : effective_basic_steering(child);
				const auto paid =
				    native_process_paid(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id, 1000);
				const bool active = native.heading != ship.get<Pose>().heading;
				step_basic_steering(ship.get_mut<Pose>(), effective, paid);
				native.remainder = effective.remainder;
				record_native_process_work(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id,
				                           active ? 1000 : 0, active ? paid : 0);
			}
	}
	void run_basic_engines()
	{
		for (auto child : ordered_equipment(engine_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id))
			{
				auto &native = child.get_mut<Engine>();
				const auto *quote =
				    native_machinery_quote(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id);
				auto effective = quote && quote->basic_engine ? *quote->basic_engine : effective_basic_engine(child);
				const auto &equipment = child.get<Equipment>().id;
				step_basic_engine(ship.get<Pose>(), ship.get_mut<Motion>(), effective,
				                  native_process_paid(ship.get<StableId>().id, "propulsion", equipment, 1000));
				native.remainder = effective.remainder;
				native.damping_remainder = effective.damping_remainder;
				native.forward_output = effective.forward_output;
				record_native_process_work(ship.get<StableId>().id, "propulsion", equipment, 1000,
				                           effective.forward_output);
			}
	}
	struct FlightRestrictions
	{
		bool impulse = false;
		int steering_limit = 1000;
	};
	void cancel_ship_impulse(EntityId id)
	{
		for (auto child : ordered_equipment(flight_query, id))
		{
			auto &state = child.get_mut<FlightRuntime>().state;
			if (state.impulse != ImpulsePhase::Idle)
				cancel_impulse(state, 5);
			state.external_impulse = false;
		}
	}
	FlightRestrictions flight_restrictions(EntityId id)
	{
		FlightRestrictions result;
		for (auto child : ordered_equipment(flight_query, id))
		{
			const auto &flight = child.get<FlightRuntime>();
			if (flight.state.impulse != ImpulsePhase::Idle)
			{
				result.impulse = true;
				if (flight.state.impulse == ImpulsePhase::Active)
					result.steering_limit =
					    std::min(result.steering_limit, static_cast<int>(flight.definition.impulse_steering_permille));
			}
		}
		return result;
	}
	FlightDefinition effective_flight_definition(flecs::entity child, FlightRestrictions restrictions,
	                                             std::int64_t speed = 0, std::int64_t acceleration = 0,
	                                             std::int64_t turn = 0, std::int64_t paid = 1000) const
	{
		const auto &flight = child.get<FlightRuntime>();
		auto definition = flight.definition;
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		// Legacy self-contained boost/impulse processes keep their own funding and
		// controls, but their output joins the same sum as external target bindings.
		if (definition.targets.empty())
		{
			if (flight.state.impulse == ImpulsePhase::Active)
			{
				speed += (definition.impulse_speed_multiplier - 1000) * paid / 1000;
				acceleration += (definition.impulse_acceleration_multiplier - 1000) * paid / 1000;
			}
			else if (flight.state.boost && !restrictions.impulse)
			{
				speed += (definition.boost_speed_multiplier - 1000) * paid / 1000;
				acceleration += (definition.boost_acceleration_multiplier - 1000) * paid / 1000;
			}
		}
		definition.boost_speed_multiplier = definition.boost_acceleration_multiplier = 1000;
		definition.impulse_speed_multiplier = definition.impulse_acceleration_multiplier = 1000;
		definition.maximum_speed = effective_property(ship, equipment, "speed", definition.maximum_speed, speed);
		definition.acceleration =
		    effective_property(ship, equipment, "propulsion", definition.acceleration, acceleration);
		definition.lateral_acceleration =
		    effective_property(ship, equipment, "propulsion", definition.lateral_acceleration, acceleration);
		definition.turn_rate = effective_property(ship, equipment, "turn", definition.turn_rate, turn);
		const auto process_rate = native_process_rate(ship.get<StableId>().id, "propulsion", equipment);
		definition.acceleration = apply_property_delta(definition.acceleration, process_rate - 1000, resource_limit);
		definition.lateral_acceleration =
		    apply_property_delta(definition.lateral_acceleration, process_rate - 1000, resource_limit);
		definition.turn_rate = apply_property_delta(definition.turn_rate, process_rate - 1000, resource_limit);
		return definition;
	}
	FlightState effective_flight_state(flecs::entity child, const FlightDefinition &definition,
	                                   FlightRestrictions restrictions) const
	{
		const auto &flight = child.get<FlightRuntime>();
		auto state = flight.state;
		state.steering_limit_permille = restrictions.steering_limit;
		if (structurally_disabled(child.parent(), child.get<Equipment>().id))
			state.enabled = false;
		if (restrictions.impulse)
			state.boost = false;
		state.desired_speed = state.desired_speed == flight.definition.maximum_speed
		                          ? definition.maximum_speed
		                          : std::min(state.desired_speed, definition.maximum_speed);
		return state;
	}
	std::int64_t lateral_velocity_intent(flecs::entity ship) const
	{
		std::int64_t desired = 0;
		for (const auto child : ship.get<EquipmentIndex>().values)
			if (const auto *side = child.try_get<FlightRuntime>(); side && side->definition.kind == FlightKind::Lateral)
			{
				const auto effort = side->state.lateral;
				if (side->definition.lateral_direction && effort * side->definition.lateral_direction < 0)
					continue;
				const auto value = side->definition.lateral_speed * effort / 1000;
				if (std::abs(value) > std::abs(desired))
					desired = value;
			}
		return desired;
	}
	void include_vertical_demand(flecs::entity child, bool enabled)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		const auto ship = child.parent();
		if (const auto *vertical = ship.try_get<VerticalMotion>();
		    vertical && enabled && flight.definition.kind == FlightKind::Engine &&
		    (vertical->desired != vertical->altitude || vertical->velocity) &&
		    native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id))
		{
			flight.demand.power_rate = std::max(flight.demand.power_rate, flight.definition.power_rate);
			flight.demand.effort_permille = 1000;
		}
	}
	void request_flight_resources(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		const auto tick = ecs.get<Clock>().tick;
		const auto &pose = ship.get<Pose>();
		const auto &motion = ship.get<Motion>();
		const auto lateral = lateral_velocity_intent(ship);
		for (auto child : ordered_equipment(flight_query, id))
		{
			auto &flight = child.get_mut<FlightRuntime>();
			flight.state.steering_limit_permille = restrictions.steering_limit;
			const auto owner = system_owner(id, child.get<Equipment>().id);
			// Actuator guidance consumes the same frozen, permitted contact feed as its Console.
			const auto &observation = decision_frame.observations().at(owner);
			std::optional<FlightContactObservation> observed;
			for (const auto &contact : observation.contacts)
				if (contact.id == flight.state.selected_contact)
				{
					observed = FlightContactObservation{contact.id,
					                                    fixed::length({contact.vx, contact.vy}),
					                                    tick,
					                                    0,
					                                    {contact.x, contact.y},
					                                    {contact.vx, contact.vy},
					                                    contact.heading};
					break;
				}
			observe_flight(flight.definition, flight.state, tick, observed);
			guide_follow(flight.definition, flight.state, pose, motion, observed);
			const auto effective = effective_flight_definition(child, restrictions);
			const auto requested = effective_flight_state(child, effective, restrictions);
			flight.demand = flight_demand(effective, requested, pose, motion, lateral);
			if (!native_process_rate(id, "propulsion", child.get<Equipment>().id))
				flight.demand = {};
			include_vertical_demand(child, requested.enabled);
			if (auto power = ship.try_get_mut<ShipResources>())
			{
				const bool charging = flight.state.impulse == ImpulsePhase::Charging &&
				                      flight.definition.impulse_charge_energy &&
				                      native_process_rate(id, "propulsion", child.get<Equipment>().id);
				const auto energy = charging ? quote_flight_charge(flight.definition, flight.state).energy : 0;
				request_system_resource(*power, child.get<ResourceBinding>().slot, 0, energy > 0,
				                        (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
				                            ? 0
				                            : flight.demand.power_rate,
				                        energy);
			}
		}
		for (auto child : ordered_equipment(passive_resource_query, id))
		{
			const auto &fit = child.get<Equipment>();
			const auto radar = child.try_get<Radar>();
			request_system_resource(
			    ship.get_mut<ShipResources>(), child.get<ResourceBinding>().slot,
			    fit.enabled && !structurally_disabled(ship, fit.id) && (!radar || radar->enabled) &&
			            fit.kind != OperatingSystem::Psychic &&
			            (fit.kind != OperatingSystem::Coolant ||
			             (child.get<ResourceBinding>().slot < ship.get<ShipResources>().definition.systems.size() &&
			              native_process_rate(id, "cooling",
			                                  ship.get<ShipResources>()
			                                      .network.definition()
			                                      .cooling_links
			                                      .at(ship.get<ShipResources>()
			                                              .definition.systems[child.get<ResourceBinding>().slot]
			                                              .cooling)
			                                      .id))) &&
			            ((fit.kind != OperatingSystem::Displacement &&
			              !(fit.kind == OperatingSystem::ShuttleBay &&
			                definition(id).craft_bays.at(fit.id).legacy_transport)) ||
			             away_transport_requested(id, fit.id))
			        ? 1000
			        : 0);
		}
	}
	void reserve_paid_modifiers(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		const auto fittings = ordered_equipment(flight_query, id);
		auto *power = ship.try_get_mut<ShipResources>();
		// Reserve full target duty while a modifier is requested. The preview and
		// committed allocation use identical requests, even if a paid bonus changes
		// guidance effort. Only the committed tick debits energy or advances cooling.
		for (auto source : fittings)
		{
			const auto &flight = source.get<FlightRuntime>();
			if (!flight.state.enabled || !native_process_rate(id, "propulsion", source.get<Equipment>().id) ||
			    !(flight.state.impulse == ImpulsePhase::Active || (flight.state.boost && !restrictions.impulse)))
				continue;
			for (const auto &binding : flight.definition.targets)
			{
				auto target = ship.get<EquipmentIndex>().values.at(binding.index);
				if (power && target.has<FlightRuntime>() && target.get<FlightRuntime>().state.enabled)
				{
					const auto slot = target.get<ResourceBinding>().slot;
					reserve_system_resource_rate(*power, slot, target.get<FlightRuntime>().definition.power_rate);
				}
			}
		}
	}
	void prepare_paid_modifiers(flecs::entity ship, FlightRestrictions restrictions,
	                            const ShipResourceAllocation *allocation)
	{
		const auto id = ship.get<StableId>().id;
		const auto fittings = ordered_equipment(flight_query, id);
		for (auto child : fittings)
		{
			auto &flight = child.get_mut<FlightRuntime>();
			flight.state.external_impulse = false;
			flight.state.effective_boost = 0;
			if (flight.state.impulse == ImpulsePhase::Active)
			{
				if (!flight.state.enabled)
					cancel_impulse(flight.state, 4);
				else if (allocation && allocation->grants()[child.get<ResourceBinding>().slot].supply_permille < 1000)
					cancel_impulse(flight.state,
					               allocation->grants()[child.get<ResourceBinding>().slot].blocked ? 4 : 3);
				for (const auto &binding : flight.definition.targets)
				{
					const auto target = ship.get<EquipmentIndex>().values.at(binding.index);
					if (!target.get<FlightRuntime>().state.enabled)
						cancel_impulse(flight.state, 4);
					else if (allocation &&
					         allocation->grants()[target.get<ResourceBinding>().slot].supply_permille < 1000)
						cancel_impulse(flight.state, 3);
				}
			}
		}
		restrictions = flight_restrictions(id);
		const auto lateral = lateral_velocity_intent(ship);
		for (auto target : fittings)
		{
			auto &flight = target.get_mut<FlightRuntime>();
			auto definition = flight.definition;
			std::int64_t speed = 0, acceleration = 0, turn = 0;
			for (auto source : fittings)
			{
				auto &modifier = source.get_mut<FlightRuntime>();
				const bool impulse = modifier.state.impulse == ImpulsePhase::Active;
				if (!modifier.state.enabled || structurally_disabled(ship, source.get<Equipment>().id) ||
				    !(impulse || (modifier.state.boost && !restrictions.impulse)))
					continue;
				const auto paid = native_process_paid(
				    id, "propulsion", source.get<Equipment>().id,
				    allocation ? allocation->grants()[source.get<ResourceBinding>().slot].supply_permille : 1000);
				for (const auto &binding : modifier.definition.targets)
					if (binding.index == target.get<Equipment>().ordinal)
					{
						const auto bonus = (binding.multiplier - 1000) * paid / 1000;
						auto &value = binding.property == FlightProperty::Speed          ? speed
						              : binding.property == FlightProperty::Acceleration ? acceleration
						                                                                 : turn;
						value += bonus;
						if (!impulse)
							modifier.state.effective_boost = paid;
						if (impulse && definition.kind == FlightKind::Engine)
							flight.state.external_impulse = true;
					}
			}
			const auto paid = native_process_paid(
			    id, "propulsion", target.get<Equipment>().id,
			    allocation ? allocation->grants()[target.get<ResourceBinding>().slot].supply_permille : 1000);
			definition = effective_flight_definition(target, restrictions, speed, acceleration, turn, paid);
			const auto state = effective_flight_state(target, definition, restrictions);
			flight.demand = flight_demand(definition, state, ship.get<Pose>(), ship.get<Motion>(), lateral);
			include_vertical_demand(target, state.enabled);
		}
	}
	void actuate_flight(flecs::entity child, FlightRestrictions restrictions)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		auto ship = child.parent();
		auto power = ship.try_get_mut<ShipResources>();
		const auto slot = power ? child.get<ResourceBinding>().slot : 0;
		int paid =
		    static_cast<int>(native_process_paid(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id,
		                                         power ? power->grants[slot].supply_permille : 1000));
		const auto charge_before = flight.state.charge_milliticks;
		if (flight.state.impulse == ImpulsePhase::Charging && flight.definition.impulse_charge_energy)
		{
			const auto &grant = power->grants[slot];
			const auto due = quote_flight_charge(flight.definition, flight.state).energy;
			if ((!due || grant.discrete_paid) && !grant.blocked &&
			    native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) &&
			    native_process_work_limit(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) >= 1000)
			{
				flight.state.charge_spent += due;
				flight.state.charge_milliticks += 1000;
				flight.state.impulse_reason = 0;
				if (flight.state.charge_milliticks == flight.definition.impulse_charge_ticks * 1000)
					flight.state.impulse = ImpulsePhase::Active;
			}
			else
				flight.state.impulse_reason = 2;
			paid = (!due || grant.discrete_paid) && !grant.blocked &&
			               native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) &&
			               native_process_work_limit(ship.get<StableId>().id, "propulsion",
			                                         child.get<Equipment>().id) >= 1000
			           ? 1000
			           : 0;
		}
		if (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
			paid = 0;
		const bool structural_stop =
		    dock_attached(ship.get<StableId>().id) || structurally_disabled(ship, child.get<Equipment>().id);
		const bool accepted_enabled = flight.state.enabled;
		if (structural_stop)
			flight.state.enabled = false;
		const bool requested_boost = flight.state.boost;
		if (restrictions.impulse)
			flight.state.boost = false;
		const auto execution =
		    step_flight(flight.definition, flight.state, ship.get_mut<Pose>(), ship.get_mut<Motion>(), flight.demand,
		                paid, power ? power->grants[slot].overdrive_permille : 1000,
		                power ? power->grants[slot].overdrive_heat_permille : 1000);
		if (power && execution.heat_rate)
		{
			const auto store = power->definition.systems[slot].store;
			if (store != no_resource)
			{
				auto &remainder = child.get_mut<OperatingHeat>().remainder;
				remainder += execution.heat_rate;
				power->network.apply_operating_heat(store, remainder / rate);
				remainder %= rate;
			}
		}
		flight.state.enabled = accepted_enabled;
		flight.state.boost = requested_boost;
		record_native_process_work(
		    ship.get<StableId>().id, "propulsion", child.get<Equipment>().id, flight.demand.effort_permille,
		    execution.effort_milliticks + std::max<std::int64_t>(0, flight.state.charge_milliticks - charge_before));
		if (auto engine = child.try_get_mut<Engine>())
		{
			engine->speed = flight.state.desired_speed;
			engine->enabled = flight.state.enabled;
			engine->forward_output = std::max(0, execution.forward_permille);
		}
		if (auto steering = child.try_get_mut<Steering>())
			steering->heading = flight.state.desired_heading;
	}
	void run_equipment_actuation(EntityId id, FlightRestrictions restrictions)
	{
		// Merge the flight and telemetry work in authored order. Sampling immediately
		// after each actuator preserves telemetry when several fittings heat one store.
		const auto actuators = ordered_equipment(flight_query, id);
		std::size_t next = 0;
		for (auto child : ordered_equipment(advanced_equipment_query, id))
		{
			if (next < actuators.size() && actuators[next] == child)
			{
				actuate_flight(child, restrictions);
				++next;
			}
			const auto &fit = child.get<Equipment>();
			knowledge.ingest_telemetry(id, fit.id, telemetry(id, fit.ordinal), ecs.get<Clock>().tick);
		}
	}

#include "../../gameplay/combat/directed.hpp"
#include "../../gameplay/combat/combat_execution.hpp"
#include "../../effects/properties/custom_properties.hpp"
#include "../../effects/properties/property_effects.hpp"
#include "../../effects/transactions/effect_application.hpp"
#include "../../effects/transactions/store_effects.hpp"
#include "../../effects/processes/processes.hpp"
#include "../../effects/processes/process_machinery.hpp"
#include "../../effects/abilities/abilities.hpp"
#include "../../effects/abilities/ability_interface.hpp"
#include "../../effects/processes/process_interface.hpp"
#include "../../effects/rules/rules.hpp"
#include "../../effects/rules/rule_sources.hpp"
#include "../../effects/lifecycle/payload_effects.hpp"
#include "../../effects/transactions/effect_damage.hpp"
#include "../../effects/rules/rule_funding.hpp"
#include "../../effects/processes/finite_native_work.hpp"
#include "../../effects/rules/rule_lifecycle.hpp"
#include "../../effects/rules/region_rules.hpp"
#include "../../effects/rules/rule_persistence.hpp"
#include "../../effects/rules/rule_interface.hpp"
#include "../../resources/equipment/processors.hpp"
	void quote_basic_process_work()
	{
		for (auto &quote : process_machinery.rows())
		{
			quote.basic_available = quote.basic_full = 0;
			quote.basic_engine.reset();
			quote.basic_steering.reset();
		}
		BasicFlightWorkBook candidates;
		for (auto child : ordered_equipment(steering_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id) &&
			                                !structurally_disabled(ship, child.get<Equipment>().id))
			{
				auto effective = effective_basic_steering(child);
				const auto id = ship.get<StableId>().id;
				const auto &equipment = child.get<Equipment>().id;
				const auto paid = native_process_paid(id, "propulsion", equipment, 1000);
				const auto work = candidates.steering(id, ship.get<Pose>(), effective, paid);
				if (auto *quote = native_machinery_quote(id, "propulsion", equipment))
				{
					quote->basic_full = work.full;
					quote->basic_available = work.available;
					quote->basic_steering = work.frozen;
				}
			}
		for (auto child : ordered_equipment(engine_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id))
			{
				auto full_engine = effective_basic_engine(child);
				auto actual_engine = effective_basic_engine(child);
				const auto id = ship.get<StableId>().id;
				const auto &equipment = child.get<Equipment>().id;
				const auto paid = native_process_paid(id, "propulsion", equipment, 1000);
				const auto work =
				    candidates.engine(id, ship.get<Pose>(), ship.get<Motion>(), full_engine, actual_engine, paid);
				if (auto *quote = native_machinery_quote(id, "propulsion", equipment))
				{
					quote->basic_engine = work.frozen;
					quote->basic_full = std::max<std::int64_t>(quote->basic_full, work.full);
					quote->basic_available = std::max<std::int64_t>(quote->basic_available, work.available);
				}
			}
	}

	bool quote_process_native_work(const std::vector<std::pair<EntityId, flecs::entity>> &ships,
	                               const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
	                               bool stage)
	{
		std::vector<std::tuple<flecs::entity, FlightState, FlightDemand>> snapshots;
		for (const auto &[id, ship] : ships)
			for (auto child : ordered_equipment(flight_query, id))
			{
				const auto &flight = child.get<FlightRuntime>();
				snapshots.emplace_back(child, flight.state, flight.demand);
			}
		try
		{
			for (const auto &[id, ship] : ships)
			{
				const auto found = allocations.find(id);
				prepare_paid_modifiers(ship, flight_restrictions(id),
				                       found != allocations.end() && found->second ? &*found->second : nullptr);
			}
			const auto changed = capture_process_native_work(allocations, stage);
			for (auto &[child, state, demand] : snapshots)
			{
				auto &flight = child.get_mut<FlightRuntime>();
				flight.state = state;
				flight.demand = demand;
			}
			return changed;
		}
		catch (...)
		{
			for (auto &[child, state, demand] : snapshots)
			{
				auto &flight = child.get_mut<FlightRuntime>();
				flight.state = state;
				flight.demand = demand;
			}
			throw;
		}
	}
	void run_advanced_equipment()
	{
		for (const auto &[id, ship] : entities)
			if (ship.has<EntityControls>())
				consume_process_commands(id);
		prepare_processes();
		begin_process_machinery_quotes();
		std::vector<std::pair<EntityId, flecs::entity>> ships;
		advanced_ships.each(
		    [&](flecs::entity entity, const StableId &id, const GameplayShip &)
		    {
			    ships.emplace_back(id.id, entity);
		    });
		std::sort(ships.begin(), ships.end(),
		          [](const auto &a, const auto &b)
		          {
			          return a.first < b.first;
		          });
		for (const auto &[id, ship] : ships)
		{
			if (auto *power = ship.try_get_mut<ShipResources>())
				clear_named_requests(*power);
			prepare_abilities(id);
			const auto restrictions = flight_restrictions(id);
			prepare_combat(ship);
			prepare_processors(ship);
			request_flight_resources(ship, restrictions);
			request_combat_resources(ship);
			request_directed_resources(ship);
			request_ability_resources(id);
			if (!definition(id).ftl.empty())
				prepare_ftl(ship);
			reserve_paid_modifiers(ship, restrictions);
			request_processor_resources(ship);
		}
		std::map<EntityId, std::optional<ShipResourceAllocation>> allocations;
		capture_process_native_requests();
		const auto allocate_native = [&](bool producers)
		{
			allocations.clear();
			for (const auto &[id, ship] : ships)
				if (auto *power = ship.try_get_mut<ShipResources>())
				{
					std::vector<std::size_t> unavailable;
					for (auto child : ship.get<EquipmentIndex>().values)
						if (structurally_disabled(ship, child.get<Equipment>().id) ||
						    boarding_inhibited(id, child.get<Equipment>().id))
							unavailable.push_back(child.get<ResourceBinding>().slot);
					allocations[id].emplace(*power, unavailable, producers ? process_producer_seed(id) : nullptr);
				}
		};
		prepare_process_machinery_controls();
		allocate_native(false);
		quote_process_native_work(ships, allocations, true);
		std::vector<RuleProcessWork> process_work;
		for (std::size_t attempt = 0;; ++attempt)
		{
			if (attempt > 4096)
				throw std::runtime_error("Native process allocation did not settle within supported bounds");
			prepare_process_machinery_controls();
			if (prepare_process_producer_funding())
				continue;
			if (cap_process_native_work(true))
			{
				price_process_native_requests();
				continue;
			}
			request_rule_upkeep();
			allocate_native(true);
			bool removed = false;
			for (const auto &[id, ship] : ships)
			{
				auto &allocation = allocations[id];
				auto *power = ship.try_get_mut<ShipResources>();
				if (!allocation)
					continue;
				for (auto child : ship.get<EquipmentIndex>().values)
				{
					const auto &equipment = child.get<Equipment>();
					if (!child.has<DirectedRuntime>() ||
					    !directed_operation(id, equipment.id, DirectedOperationKind::Launch))
						continue;
					const auto slot = child.get<ResourceBinding>().slot;
					const auto &grant = allocation->grants().at(slot);
					if (power->requests.at(slot).discrete &&
					    (grant.blocked || grant.supply_permille < 1000 ||
					     (power->definition.systems.at(slot).discrete_energy && !grant.discrete_paid)))
					{
						child.get_mut<CombatMechanism>().get<WeaponState>().ready = false;
						auto &state = child.get_mut<DirectedRuntime>();
						state.ready = false;
						state.blocker = "Insufficient operating power";
						auto &request = power->requests.at(slot);
						request.discrete = false;
						request.discrete_amount = 0;
						request.extra_rate -= directed_mode(id, equipment.id)->energy_per_second;
						auto &operation = directed_operations.at({id, equipment.id});
						if (operation.stock_claim)
						{
							const auto magazine = instance(ship, operation.magazine)->entity;
							auto &stock =
							    physical_stock(ship, magazine.get<CombatMechanism>().get<MagazineState>().stock);
							stock.release_stock("directed-launch:" + equipment.id);
							operation.stock_claim = false;
						}
						removed = true;
					}
				}
			}
			if (removed)
			{
				// A withdrawn launch releases real stock. Retry before any quote
				// restaging can continue this loop and lose the withdrawal edge.
				for (const auto &[id, ship] : ships)
					retry_blocked_processors(ship);
				reset_surviving_process_native_caps();
				price_process_native_requests();
			}
			removed = prepare_rule_upkeep_payments(allocations, removed) || removed;
			if (cap_process_native_work())
			{
				price_process_native_requests();
				continue;
			}
			if (quote_process_native_work(ships, allocations, true))
				continue;
			process_work.clear();
			for (const auto &quote : process_machinery.rows())
				process_work.push_back({quote.owner, quote.frozen.id, quote.work, quote.activity});
			if (prepare_process_funding_effects(process_work, allocations))
			{
				cap_process_native_work();
				price_process_native_requests();
				continue;
			}
			if (!removed)
				break;
		}
		for (const auto &[id, ship] : ships)
		{
			auto &allocation = allocations[id];
			prepare_paid_modifiers(ship, flight_restrictions(id), allocation ? &*allocation : nullptr);
		}
		// Freeze this paid operating frame before its work effects change modifiers.
		if (!commit_rule_upkeep_stocks())
			throw std::logic_error("rules: prepared upkeep changed before commit");
		for (auto &[id, allocation] : allocations)
			if (allocation)
				allocation->commit();
		finish_rule_upkeep();
		run_basic_steering();
		run_basic_engines();
		for (const auto &[id, ship] : ships)
		{
			const auto restrictions = flight_restrictions(id);
			actuate_processors(ship);
			actuate_combat(ship);
			if (!definition(id).ftl.empty())
				actuate_ftl(ship);
			run_equipment_actuation(id, restrictions);
		}
		refresh_ability_work();
		sync_psychic_contributions();
		for (const auto &quote : process_machinery.rows())
			commit_process_work_receipt(quote.owner, quote.frozen, quote.work, quote.activity);
		publish_process_work();
		finalise_ftl_processes();
	}
	void scenario_effects()
	{
		if (!scenario)
			return;
		for (const auto &request : scenario->pending_effects())
		{
			std::string failure;
			try
			{
				if (request.kind == ScenarioEffectKind::Group)
					failure = execute_scenario_effect_group(request);
				else
					apply_scenario_effect(request);
			}
			catch (const std::exception &error)
			{
				failure = error.what();
			}
			scenario->confirm_effect(request.token, std::move(failure));
		}
	}
	void scenario_spawns()
	{
		if (!scenario)
			return;
		for (const auto &request : scenario->pending_spawns())
		{
			if (next_entity < 1 || next_entity > 1000000000ULL || request.entities.size() > 1000000001ULL - next_entity)
			{
				scenario->confirm_spawn(request.id, {}, "entity identity space exhausted");
				continue;
			}
			const auto allocator = next_entity;
			const auto previous_instances = instance_systems;
			const auto previous_stations = explicit_stations;
			auto previous = knowledge;
			const auto previous_participants = conversations.participants;
			std::vector<EntityId> ids;
			try
			{
				for (const auto &p : request.entities)
					if (!definitions.contains(p.definition))
						throw std::runtime_error("Uncompiled dynamic entity");
				for (const auto &p : request.entities)
				{
					const auto id = next_entity++;
					ids.push_back(id);
					create(id, p.definition,
					       {{"x_mm", p.position.x}, {"y_mm", p.position.y}, {"heading_mdeg", p.heading}});
				}
				const auto parents = ids;
				initialise_craft(&ids);
				scenario->confirm_spawn(request.id, parents);
			}
			catch (const std::exception &error)
			{
				for (const auto id : ids)
				{
					if (entities.contains(id))
					{
						entities.at(id).destruct();
						entities.erase(id);
					}
				}
				knowledge = std::move(previous);
				conversations.participants = previous_participants;
				next_entity = allocator;
				instance_systems = previous_instances;
				explicit_stations = previous_stations;
				scenario->confirm_spawn(request.id, {}, error.what());
			}
		}
	}
	void scenario_publications()
	{
		if (!scenario)
			return;
		auto &runtime = scenario->runtime();
		const auto &definition = runtime.definition();
		for (const auto &publication : runtime.pending_publications())
		{
			const auto ship = runtime.resolve_actor(publication.instance, publication.recipients.ship);
			if (!ship || !entities.contains(*ship))
				continue;
			if (publication.objective)
			{
				const auto &objective = runtime.objectives().at(publication.objective);
				const auto &spec = definition.objectives.at(objective.definition);
				const ScenarioRegion region = spec.condition ? ScenarioRegion{} : definition.regions.at(spec.region);
				const auto &schema = knowledge.definition();
				const auto key = schema.schema("phoenix.objective");
				KnowledgeRecordInput value;
				value.schema = key;
				value.producer = schema.role("phoenix.scenario");
				value.subject = objective.id;
				value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Fact};
				const char *names[] = {"pending", "active", "completed", "failed", "cancelled"};
				value.fields = {
				    {schema.field(key, "id"), static_cast<std::int64_t>(objective.id)},
				    {schema.field(key, "status"), std::string(names[static_cast<unsigned>(publication.status)])},
				    {schema.field(key, "title"), spec.title_key},
				    {schema.field(key, "description"), spec.description_key},
				    {schema.field(key, "outcome"), objective.outcome},
				    {schema.field(key, "radius"), region.radius},
				    {schema.field(key, "region"), region.id},
				    {schema.field(key, "credited"),
				     objective.completion_credited || publication.status == ObjectiveStatus::Completed},
				    {schema.field(key, "destination_x"), region.centre.x},
				    {schema.field(key, "destination_y"), region.centre.y}};
				if (spec.condition)
					for (const auto *field : {"radius", "region", "destination_x", "destination_y"})
						value.fields.erase(schema.field(key, field));
				if (publication.recipients.audience == ScenarioAudience::Ship)
					knowledge.publish_ship(*ship, value, publication.tick);
				else
					for (const auto &console : publication.recipients.consoles)
						knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
				if (objective.status == ObjectiveStatus::Completed)
					runtime.acknowledge_credit(objective.id);
			}
			// Custom scenario publications are routed only to their declared recipients.
			else
			{
				try
				{
					const auto &schema = knowledge.definition();
					const auto key = schema.schema(publication.record);
					const auto &contract = schema.schemas.at(key - 1);
					KnowledgeRecordInput value;
					value.schema = key;
					value.producer = schema.role("phoenix.scenario");
					value.subject = *ship;
					value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Claim};
					for (const auto &[name, field] : publication.fields)
						value.fields[schema.field(key, name)] = field;
					if (publication.recipients.audience == ScenarioAudience::Ship)
						knowledge.publish_ship(*ship, value, publication.tick);
					else
						for (const auto &console : publication.recipients.consoles)
							knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
				}
				catch (const std::exception &)
				{
					continue;
				}
			}
			runtime.acknowledge_publication(publication.id);
		}
	}
	void refresh_control(EntityId ship, bool transitions = true)
	{
		refresh_controllers(ship, !transitions);
	}
	void prune_displays()
	{
		std::erase_if(displays,
		              [&](const auto &entry)
		              {
			              const auto &d = entry.second;
			              return !ownership.claims().contains(d.claimant) ||
			                     (ownership.claims().at(d.claimant) != d.address &&
			                      console_operator(d.address) != d.claimant);
		              });
	}
	const Json *display_widget(const ConsoleAddress &address, const std::string &id) const
	{
		const auto spec = seat(address.ship, address.console);
		if (!spec || !console_layouts.contains(spec->definition))
			return nullptr;
		for (const auto &w : console_layouts.at(spec->definition).at("widgets"))
			if (w.at("id") == id &&
			    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
				return &w;
		return nullptr;
	}
	bool display_command(std::uint64_t claimant, const OperatingCommand &command, bool execute)
	{
		if (command.action != "display.toggle" && command.action != "display.update")
			return false;
		const auto address = command_address(claimant, command);
		if (!ownership.claims().contains(claimant) || command.target != ownership.claims().at(claimant).ship ||
		    (address != ownership.claims().at(claimant) && console_operator(address) != claimant) ||
		    !command.controller.empty() || !command.channel.empty() || command.request_id || command.value ||
		    !display_widget(address, command.system) || command.arguments.size() != 3 || command.arguments[2] < 1)
			return false;
		const auto widget = display_widget(address, command.system);
		if (widget->at("kind") == "radar")
		{
			const auto view = capture_console_observation(address);
			if (!select_system(view.systems, OperatingSystem::Radar, widget->value("system", std::string{}), false))
				return false;
		}
		for (auto arg : command.arguments)
			if (arg < -1000000000 || arg > 1000000000)
				return false;
		const auto found = displays.find(address.ship);
		const bool same = found != displays.end() && found->second.claimant == claimant &&
		                  found->second.address == address && found->second.widget == command.system;
		if (command.action == "display.update")
		{
			if (!same || command.generation != found->second.revision)
				return false;
			if (execute)
				found->second.camera = command.arguments;
		}
		else
		{
			if (command.generation || display_revision >= 9007199254740991ULL)
				return false;
			if (execute)
			{
				++display_revision;
				if (same)
					displays.erase(address.ship);
				else
					displays[address.ship] = {claimant, display_revision, address, command.system, command.arguments};
			}
		}
		return true;
	}
#include "../../gameplay/personnel/personnel.hpp"
#include "../../gameplay/psychic/psychic.hpp"
#include "../../gameplay/teams/teams.hpp"
#include "../../gameplay/teams/compartment_access.hpp"
#include "../../gameplay/logistics/logistics.hpp"
#include "../../gameplay/docking/docking.hpp"
#include "../../gameplay/logistics/cargo.hpp"
#include "../../resources/equipment/supply.hpp"
#include "../../control/coordination/bridge.hpp"
#include "../../gameplay/conversations/conversations.hpp"
#include "../../gameplay/away/away.hpp"
#include "../../gameplay/boarding/ownership.hpp"
#include "../../gameplay/boarding/boarding.hpp"
#include "../../gameplay/craft/craft.hpp"
#include "../../gameplay/ftl/ftl.hpp"
#include "../../scenarios/objectives/missions.hpp"
#include "../../gameplay/psychic/interdictions.hpp"
#include "../../gameplay/sensors/sensors.hpp"
	bool meta(std::uint64_t claimant, const OperatingCommand &command, bool execute)
	{
		if (!ownership.claims().contains(claimant) || command.target != ownership.claims().at(claimant).ship)
			return false;
		const auto address = command_address(claimant, command);
		if (address != ownership.claims().at(claimant) && console_operator(address) != claimant)
			return false;
		const auto &console = *seat(address.ship, address.console);
		const auto &spec = definition(address.ship);
		if (departed(address.ship) && command.action != "camera_mode" && command.action != "camera_direction")
			return false;
		if (command.action.starts_with("ftl."))
			return ftl_command(address, command, execute);
		if (command.action == "flight.impulse")
			return coordinated_impulse(address, command, execute);
		if (command.action.starts_with("away."))
			return away_command(address, command, execute);
		if (command.action.starts_with("dock."))
			return docking_command(address, command, execute);
		if (command.action == "magazine.select" || command.action == "processor.select" ||
		    command.action == "store.select")
			return selection_command(address, command, execute);
		if (command.action.starts_with("logistics."))
			return logistics_command(address, command, execute);
		if (command.action.starts_with("team."))
			return team_command(address, command, execute);
		if (command.action.starts_with("craft."))
			return craft_command(address, command, execute);
		if (command.action.starts_with("personnel."))
			return personnel_command(address, command, execute);
		if (command.action == "alert.set" || command.action == "stance.set" || command.action == "objective.priority")
			return bridge_command(address, command, execute);
		if (command.action.starts_with("comms."))
			return comms_command(address, command, execute);
		if (command.action.starts_with("display."))
			return display_command(claimant, command, execute);
		if (command.action == "camera_mode" || command.action == "camera_direction")
		{
			if (!command.system.empty() || !command.controller.empty() || !command.channel.empty() ||
			    command.generation || command.request_id || !command.arguments.empty() || command.value < 0 ||
			    command.value > (command.action == "camera_mode" ? 1 : 7))
				return false;
			const auto &kinds = camera_widgets.at(console.definition);
			if (std::find(kinds.begin(), kinds.end(), command.action) == kinds.end())
				return false;
			if (execute)
			{
				auto camera = ship_camera_state(knowledge.public_board(address.ship));
				if (command.action == "camera_mode")
					camera.mode = static_cast<int>(command.value);
				else
					camera.direction = static_cast<int>(command.value);
				knowledge.set_camera(address.ship, camera, ecs.get<Clock>().tick);
			}
			return true;
		}
		if (entities.at(address.ship).has<EntityControls>() && !command.action.starts_with("message.") &&
		    !command.action.starts_with("publish."))
			return controller_meta(address, command, execute);
		if (command.action.starts_with("publish."))
		{
			if (!command.system.empty() || !command.controller.empty() || !command.channel.empty() ||
			    command.generation || command.request_id || command.value)
				return false;
			const auto name = command.action.substr(8);
			if (std::find(spec.publications.begin(), spec.publications.end(), name) == spec.publications.end())
				return false;
			try
			{
				if (!execute)
				{
					knowledge.activate_publication(address, name, ecs.get<Clock>().tick, false);
					return true;
				}
				auto candidate = knowledge;
				candidate.activate_publication(address, name, ecs.get<Clock>().tick);
				if (execute)
					knowledge = std::move(candidate);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
		if (command.action.starts_with("message."))
		{
			const bool controller_sender = !command.controller.empty();
			if (!controller_sender)
				return false;
			const auto &sender = controller_sender ? command.controller : command.system;
			if (controller_sender)
			{
				if (!entities.at(address.ship).has<EntityControls>() || !command.system.empty())
					return false;
				const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
				if (std::none_of(controllers.begin(), controllers.end(),
				                 [&](const auto &controller)
				                 {
					                 return controller.definition.console == address.console &&
					                        controller.definition.id == sender && controller.human &&
					                        controller.generation == command.generation;
				                 }))
					return false;
			}
			const auto name = command.action.substr(8);
			if (std::find(spec.message_templates.begin(), spec.message_templates.end(), name) ==
			    spec.message_templates.end())
				return false;
			try
			{
				const auto &message = knowledge.message_definition(name);
				KnowledgeRecordInput payload;
				payload.schema = message.schema;
				payload.producer = knowledge.message_producer(address, sender, message.schema, ControlOrigin::Human,
				                                              controller_sender);
				payload.subject = address.ship;
				payload.provenance = {controller_sender
				                          ? knowledge.controller_id({address.ship, address.console, sender})
				                          : knowledge.system_id(address.ship, sender),
				                      ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0, EvidenceKind::Intent};
				GameplayKnowledgeMessageContext context;
				context.amount = command.value;
				context.flag = command.value != 0;
				context.detail = std::to_string(command.value);
				const auto bound = evaluate_gameplay_message(message, context);
				payload.fields = bound.fields;
				if (!execute)
				{
					knowledge.admit_template(address, sender, name, payload, ControlOrigin::Human,
					                         ecs.get<Clock>().tick, bound.parameters, false, controller_sender);
					return true;
				}
				// Only actual commands need a transactional copy.
				auto candidate = knowledge;
				candidate.admit_template(address, sender, name, payload, ControlOrigin::Human, ecs.get<Clock>().tick,
				                         bound.parameters, true, controller_sender, command.speaker_name,
				                         command.speaker_avatar);
				if (execute)
					knowledge = std::move(candidate);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
		return false;
	}
};
World::World() : impl_(std::make_unique<Impl>())
{
}
World::~World() = default;
void World::load(const Catalog &source, const std::string &map, const std::map<std::string, std::string> &ships)
{
	load_impl(source, map, ships, false);
}
void World::load_impl(const Catalog &source, const std::string &map, const std::map<std::string, std::string> &ships,
                      bool restoring)
{
	const auto properties = compile_property_catalog(source);
	const auto content = prepared_catalog(source, properties);
	if (content.presentation)
		throw std::runtime_error("Console presentation assets cannot create an authoritative world");
	if (!content.maps.contains(map))
		throw std::runtime_error("Unknown map: " + map);
	const auto directed_catalog = compile_directed_catalog(content);
	auto candidate = std::make_unique<Impl>();
	candidate->compile(content, directed_catalog, properties);
	candidate->map = map;
	candidate->ships = ships;
	const auto &data = content.maps.at(map);
	candidate->factions = compile_factions(data);
	candidate->combat_duel = data.value("combat_duel", false);
	std::vector<std::string> entity_names;
	for (const auto &[name, definition] : content.entities)
		entity_names.push_back(name);
	std::vector<std::string> slot_names;
	for (const auto &slot : data.at("ship_slots"))
		slot_names.push_back(slot.at("id"));
	candidate->regions = compile_scenario(data, entity_names, slot_names).regions;
	candidate->region_rule_bindings = compile_region_rules(content, data, candidate->rule_templates);
	candidate->validate_native_payload_layouts();
	candidate->field_seed = asteroid_map_seed(map);
	EntityId next = 1;
	for (const auto &instance : data.at("entities"))
	{
		EntityId id = instance.at("id");
		if (id >= Impl::field_identity_base)
			throw std::runtime_error("Authored identity overlaps reserved asteroid range");
		candidate->create(id, instance.at("definition"), instance);
		next = std::max(next, id + 1);
	}
	std::map<std::string, Json> slots;
	for (const auto &slot : data.at("ship_slots"))
		slots[slot.at("id").get<std::string>()] = slot;
	for (const auto &[slot, ship] : ships)
	{
		if (!slots.contains(slot))
			throw std::runtime_error("Unknown ship slot: " + slot);
		const auto &allowed = slots.at(slot).at("allowed_ships");
		if (std::find(allowed.begin(), allowed.end(), Json(ship)) == allowed.end())
			throw std::runtime_error("Ship is not allowed in slot: " + slot);
		candidate->slot_entities[slot] = next;
		candidate->create(next++, ship, slots.at(slot));
	}
	for (const auto &setup : compile_starting_stocks(content, data, directed_catalog, candidate->property_catalogue))
		if (ships.contains(setup.slot) && ships.at(setup.slot) == setup.definition)
		{
			const auto id = candidate->slot_entities.at(setup.slot);
			const auto &stores = candidate->definition(id).logistics.stores;
			for (const auto &[name, quantities] : setup.stores)
			{
				const auto found = std::find_if(stores.begin(), stores.end(),
				                                [&](const auto &store)
				                                {
					                                return store.id == name;
				                                });
				auto &stock = candidate->material_stock(id, std::distance(stores.begin(), found));
				// Remove replaced quantities first, so changing cargo types cannot overfill mid-setup.
				for (const auto &[resource, quantity] : quantities)
					if (const auto existing = stock.quantity(resource); existing && !stock.consume(resource, existing))
						throw std::runtime_error("Starting stock is already committed");
				for (const auto &[resource, quantity] : quantities)
					if (quantity && !stock.add(resource, quantity))
						throw std::runtime_error("Starting stock exceeds the receiving store");
			}
		}
	candidate->initialise_craft();
	if (content.scenarios.contains(map))
	{
		candidate->scenario.emplace();
		candidate->scenario->load(content, map, candidate->property_catalogue);
	}
	candidate->conversations.programs = content.conversation_programs;
	candidate->compile_conversation_starts(data);
	candidate->away_operations.configure_definitions(
	    compile_away(content, data, directed_catalog, candidate->property_catalogue));
	candidate->interdiction_definitions =
	    compile_interdictions(content, data, directed_catalog, candidate->property_catalogue);
	candidate->interdictions.resize(candidate->interdiction_definitions.size());
	candidate->sensor_findings.configure(compile_sensor_findings(content, data));
	if (candidate->scenario && !candidate->sensor_findings.empty() && !candidate->conversation_instance)
		candidate->conversation_instance = candidate->scenario->runtime().create_instance("sensor-events");
	if (candidate->scenario && !candidate->conversation_instance &&
	    (!candidate->scenario->runtime().definition().fact_bindings.empty() ||
	     !candidate->scenario->runtime().definition().event_bindings.empty() ||
	     !candidate->scenario->runtime().definition().reports.empty()))
		candidate->conversation_instance = candidate->scenario->runtime().create_instance("world-events");
	candidate->away_operations.initialize_missions();
	candidate->refresh_boarding_templates();
	if (!restoring)
	{
		candidate->initialise_rule_membership();
		candidate->prepare_rule_sources();
	}
	candidate->refresh_comms();
	candidate->update_craft_reports();
	candidate->stream_fields();
	candidate->index_contacts.run();
	// Instant capabilities are observations even before the first simulation step.
	// Timed acquisition must not gain exposure while constructing the world.
	candidate->advance_sensors(false);
	impl_ = std::move(candidate);
}
std::vector<ConsoleSpec> World::consoles(EntityId ship) const
{
	return impl_->entities.contains(ship) ? impl_->definition(ship).consoles : std::vector<ConsoleSpec>{};
}
bool World::can_claim(std::uint64_t console, EntityId ship, const std::string &name, bool sidebar) const
{
	if (impl_->entities.contains(ship) && impl_->entities.at(ship).has<CraftState>())
		return false;
	if (!console || impl_->ownership.claims().contains(console) || !impl_->entities.contains(ship))
		return false;
	auto seat = impl_->seat(ship, name);
	if (!seat || !impl_->ownership.can_claim(console, {ship, seat->id}, sidebar))
		return false;
	const auto &controls = impl_->entities.at(ship).get<EntityControls>();
	return impl_->definition(ship).explicit_stations || !seat->definition.empty() ||
	       std::any_of(controls.controllers.begin(), controls.controllers.end(),
	                   [&](const auto &controller)
	                   {
		                   return controller.definition.console == seat->id;
	                   });
}
std::vector<EntityId> World::operational_fleet(EntityId origin) const
{
	std::vector<EntityId> out;
	if (!impl_->entities.contains(origin) || impl_->operational_owner(origin) != origin)
		return out;
	for (const auto &[id, ship] : impl_->entities)
		if ((id == origin || impl_->captured_by.contains(id)) && impl_->operational_owner(id) == origin &&
		    !ship.has<CraftState>())
			out.push_back(id);
	return out;
}
bool World::prepare_console_profile(const ConsoleAddress &address, const std::string &profile)
{
	impl_->clear_presentation_snapshots();
	if (ticks() || !impl_->ownership.claims().empty() || !impl_->entities.contains(address.ship))
		return false;
	auto entity = impl_->entities.at(address.ship);
	if (!entity.has<EntityControls>())
		return false;
	auto &controls = entity.get_mut<EntityControls>();
	if (!controls.profiles.contains(address.console))
		return false;
	for (std::size_t i = 0; i < controls.layout.profiles.size(); ++i)
		if (controls.layout.profiles[i].id == profile)
		{
			if (controls.layout.fixed_profiles.contains(address.console) &&
			    controls.layout.fixed_profiles.at(address.console) != i)
				return false;
			set_console_complexity(controls, address.console, i);
			impl_->refresh_controllers(address.ship);
			return true;
		}
	return false;
}
bool World::claim(std::uint64_t console, EntityId ship, const std::string &name, bool sidebar)
{
	impl_->clear_presentation_snapshots();
	if (!can_claim(console, ship, name, sidebar))
		return false;
	const auto &seat = *impl_->seat(ship, name);
	if (!impl_->ownership.claim(console, {ship, seat.id}, sidebar))
		return false;
	auto entity = impl_->entities.at(ship);
	impl_->refresh_control(ship);
	impl_->prune_displays();
	return true;
}
std::optional<EntityId> World::ship_for_slot(const std::string &slot) const
{
	auto found = impl_->slot_entities.find(slot);
	if (found == impl_->slot_entities.end())
		return {};
	return found->second;
}
bool World::release(std::uint64_t console)
{
	impl_->clear_presentation_snapshots();
	auto found = impl_->ownership.claims().find(console);
	if (found == impl_->ownership.claims().end())
		return false;
	auto entity = impl_->entities.at(found->second.ship);
	std::erase_if(impl_->meta_pending,
	              [console](const auto &entry)
	              {
		              return entry.first == console;
	              });
	const auto released_ship = found->second.ship;
	std::erase_if(impl_->displays,
	              [console](const auto &entry)
	              {
		              return entry.second.claimant == console;
	              });
	impl_->ownership.release(console);
	if (entity.has<EntityControls>())
		impl_->refresh_controllers(released_ship);
	impl_->prune_displays();
	return true;
}
bool World::can_submit(std::uint64_t console, const OperatingCommand &command) const
{
	auto claim = impl_->ownership.claims().find(console);
	if (claim == impl_->ownership.claims().end() || claim->second.ship != command.target)
		return false;
	if (impl_->departed(command.target))
		return false;
	const auto address = impl_->command_address(console, command);
	if (address != claim->second && impl_->console_operator(address) != console)
		return false;
	if ((command.arguments.empty() || command.action.starts_with("display.")) && impl_->meta(console, command, false))
		return impl_->meta_pending.size() < 4096;
	if (impl_->entities.at(command.target).has<EntityControls>())
		return impl_->controller_proposal(address, command, true).has_value() &&
		       impl_->controller_pending.size() < 4096;
	return false;
}
bool World::submit(std::uint64_t console, const OperatingCommand &command)
{
	impl_->clear_presentation_snapshots();
	if (!can_submit(console, command))
		return false;
	if (impl_->meta(console, command, false))
	{
		impl_->meta_pending.emplace_back(console, command);
		return true;
	}
	if (impl_->entities.at(command.target).has<EntityControls>())
	{
		impl_->controller_pending.push_back(
		    *impl_->controller_proposal(impl_->command_address(console, command), command, true));
		return true;
	}
	return false;
}
void World::clear_claims()
{
	impl_->clear_presentation_snapshots();
	impl_->ownership.clear_claims();
	impl_->displays.clear();
	impl_->meta_pending.clear();
	impl_->controller_pending.clear();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			impl_->refresh_controllers(id);
	impl_->knowledge.clear_control();
}
void World::tick()
{
	impl_->clear_presentation_snapshots();
	TickProfile profile;
	impl_->retire_audio();
	// Keep committed frame numbering live after the race, but freeze its world.
	if (impl_->scenario && impl_->scenario->runtime().mission_complete())
	{
		// Presentation settings remain usable after physical mission execution stops.
		for (const auto &[claim, command] : impl_->meta_pending)
			if (command.action == "camera_mode" || command.action == "camera_direction")
				impl_->meta(claim, command, true);
		impl_->meta_pending.clear();
		impl_->controller_pending.clear();
		++impl_->ecs.get_mut<Clock>().tick;
		return;
	}
	impl_->stream_fields();
	impl_->index_contacts.run();
	// Departure is revalidated after this tick's admitted Controller changes are consumed.
	std::vector<std::pair<std::uint64_t, OperatingCommand>> departures;
	for (const auto &[claim, command] : impl_->meta_pending)
		if (command.action == "ftl.depart")
			departures.emplace_back(claim, command);
		else
			impl_->meta(claim, command, true);
	impl_->meta_pending.clear();
	impl_->prune_displays();
	for (const auto &[id, entity] : impl_->entities)
		if (!impl_->departed(id) && entity.has<PersonnelState>())
			impl_->advance_personnel(id);
	for (const auto &proposal : impl_->controller_pending)
		if (!impl_->departed(proposal.address.entity))
		{
			const auto &ctrl =
			    impl_->entities.at(proposal.address.entity).get<EntityControls>().controllers.at(proposal.controller);
			if (impl_->commit_controller(proposal))
				impl_->cancel_docking_assist(
				    proposal.address.entity, proposal.address.controller,
				    std::string(operating_action(ctrl.definition.commands.at(proposal.channel).action).name),
				    proposal.value);
		}
	impl_->controller_pending.clear();
	impl_->step_shield_effects();
	impl_->step_property_effects();
	profile.mark("admission_personnel_effects");
	if (impl_->scenario)
	{
		auto bodies = inspect();
		std::erase_if(bodies,
		              [](const auto &body)
		              {
			              return body.id >= Impl::field_identity_base;
		              });
		auto active_slots = impl_->slot_entities;
		std::erase_if(active_slots,
		              [&](const auto &entry)
		              {
			              return impl_->departed(entry.second);
		              });
		impl_->scenario->begin_tick(ticks(), std::move(active_slots), bodies);
		std::vector<ScenarioKnowledgeSnapshot> snapshots;
		for (const auto &reader : impl_->scenario->knowledge_readers())
		{
			const auto board = impl_->knowledge.project({reader.ship, reader.console});
			snapshots.push_back({reader, board.records, board.shared});
		}
		impl_->scenario->supply_knowledge(snapshots);
		impl_->scenario->step();
		impl_->scenario_spawns();
		impl_->scenario_effects();
		impl_->scenario_publications();
	}
	impl_->decision_frame.clear();
	profile.mark("scenario");
	impl_->prepare_craft();
	impl_->prepare_docking();
	profile.mark("prepare_craft_docking");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
		{
			impl_->reconcile_objective_priority(id);
			ControllerCommandSections snapshot;
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				auto &section = snapshot.emplace_back();
				for (const auto &command : controller.commands)
					section.push_back(command);
			}
			impl_->decision_frame.freeze_commands(id, std::move(snapshot));
		}
	profile.mark("command_snapshots");
	Impl::TelemetrySnapshot decision_telemetry;
	Impl::ContactSnapshot decision_contacts;
	for (const auto &[id, entity] : impl_->entities)
		for (const auto &console : impl_->definition(id).consoles)
		{
			const ConsoleAddress address{id, console.id};
			impl_->decision_frame.freeze_observation(
			    address, impl_->capture_decision_observation(address, &decision_telemetry, &decision_contacts));
		}
	profile.mark("console_observation_snapshots");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			impl_->refresh_navigation_blackboard(id);
	profile.mark("navigation_blackboards");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				const ConsoleAddress address{id, controller.definition.console};
				if (!controller.human)
				{
					const ControllerAddress target{id, controller.definition.console, controller.definition.id};
					impl_->decision_frame.freeze_inbox(target, impl_->knowledge.peek_controller(target));
				}
				if (!impl_->decision_frame.knowledge().contains(address))
					impl_->decision_frame.freeze_knowledge(address, impl_->knowledge.project(address));
			}
	impl_->step_starts.clear();
	profile.mark("knowledge_snapshots");
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    impl_->step_starts.emplace(id.id, pose.position);
	    });
	// Explicit fixed-tick phases. Each pre-collision row operates only on itself,
	// so archetype/table ordering cannot affect gameplay. No wall-clock progress.
	for (const auto &[id, entity] : impl_->entities)
		if (!impl_->departed(id) && !impl_->craft_dormant(entity) && entity.has<EntityControls>())
			impl_->run_console_controllers(entity);
	for (const auto &[claim, command] : departures)
		impl_->meta(claim, command, true);
	impl_->apply_interdiction_orders();
	profile.mark("controllers");
	impl_->commit_departures();
	impl_->apply_structural_disablement();
	impl_->initialise_rule_membership();
	impl_->prepare_rule_sources();
	impl_->step_rule_events(RuleEventPhase::Source);
	impl_->capture_dock_propulsion();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>() && !entity.has<GameplayShip>())
			for (auto child : entity.get<EquipmentIndex>().values)
			{
				const auto &fit = child.get<Equipment>();
				impl_->knowledge.ingest_telemetry(id, fit.id, impl_->telemetry(id, fit.ordinal), ticks());
			}
	impl_->run_advanced_equipment();
	impl_->refresh_rule_eligibility();
	profile.mark("equipment");
	impl_->adjust_dock_propulsion();
	impl_->step_vertical();
	impl_->service_craft();
	impl_->step_cargo();
	impl_->step_logistics();
	// Commands may change velocity this tick. Activate swept collision partners
	// after propulsion, including their starting poses, before any integration.
	impl_->stream_fields();
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    impl_->step_starts.try_emplace(id.id, pose.position);
	    });
	impl_->integrate.run();
	impl_->sync_docks();
	impl_->resolve_collisions.run();
	profile.mark("logistics_motion_collision");
	impl_->step_docking();
	impl_->step_region_rules();
	impl_->refresh_rule_eligibility();
	impl_->step_rule_clocks();
	impl_->step_rule_events(RuleEventPhase::Periodic);
	impl_->step_rule_events(RuleEventPhase::Payload);
	impl_->step_rule_events(RuleEventPhase::Shield);
	impl_->step_ordnance();
	impl_->resolve_destructions();
	impl_->refresh_rule_eligibility();
	impl_->resolve_lost_parties();
	impl_->step_teams();
	impl_->step_repairs();
	impl_->step_medical_facilities();
	impl_->index_contacts.run();
	impl_->waypoint_arrivals();
	profile.mark("combat_teams_repairs");
	if (impl_->scenario)
	{
		std::vector<ScenarioMotionSegment> motion;
		impl_->bodies.each(
		    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
		    {
			    if (id.id >= Impl::field_identity_base)
				    return;
			    const auto from = impl_->step_starts.at(id.id);
			    motion.push_back({id.id, {from.x, from.y}, {pose.position.x, pose.position.y}});
		    });
		impl_->scenario->evaluate(motion);
		impl_->scenario_publications();
	}
	++impl_->ecs.get_mut<Clock>().tick;
	++impl_->environment_tick;
	impl_->knowledge.tick(ticks());
	profile.mark("scenario_evaluation_knowledge");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<BridgeBlackboard>())
			impl_->reconcile_objective_priority(id);
	impl_->refresh_comms();
	impl_->refresh_boarding_templates();
	impl_->update_craft_reports();
	profile.mark("refresh_comms_boarding_craft");
	// Physical receipt and deadlines settle before queued station traffic can be delivered.
	impl_->update_mission_facts();
	impl_->conversation_scenarios();
	impl_->conversations.advance(ticks());
	impl_->advance_away();
	profile.mark("missions_conversations_away");
	impl_->advance_interdictions();
	impl_->advance_psychic();
	impl_->advance_sensors();
	impl_->update_mission_facts();
	impl_->finalise_destroyed_missions();
	impl_->prune_property_targets();
	impl_->refresh_rule_eligibility();
	profile.mark("missions_comms_away_sensors");
	impl_->finalise_finite_native_jobs();
	impl_->decision_frame.clear();
}
unsigned long long World::ticks() const
{
	return impl_->ecs.get<Clock>().tick;
}
std::vector<BodyView> World::inspect() const
{
	std::vector<BodyView> result;
	result.reserve(impl_->entities.size());
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &definition, const Pose &pose, const Motion &motion)
	    {
		    result.push_back(view(id.id, definition, pose, motion));
	    });
	std::sort(result.begin(), result.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	return result;
}
std::vector<SceneBody> World::scene() const
{
	std::vector<SceneBody> result;
	for (const auto &body : inspect())
	{
		SceneBody item;
		static_cast<BodyView &>(item) = body;
		item.sprite_entry = impl_->sprite_choices.at(body.id);
		item.visual_height_mm = impl_->altitude(body.id);
		if (const auto *v = impl_->entities.at(body.id).try_get<VerticalMotion>())
			item.visual_vz = v->velocity;
		if (auto found = impl_->field_rocks.find(body.id); found != impl_->field_rocks.end())
		{
			const auto &s = found->second.scene;
			item.field_instance = true;
			item.rotation_mdeg = s.rotation_mdeg;
			item.spin_mdeg_s = s.spin_mdeg_s;
		}
		if (auto systems = fittings(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.engine && system.engine->enabled)
					item.forward_thrust = std::max(item.forward_thrust, system.engine->forward_output);
		if (auto systems = gameplay_systems(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.flight)
					item.forward_thrust =
					    std::max(item.forward_thrust, std::max(0, system.flight->state.actual_longitudinal));
		const auto entity = impl_->entities.at(body.id);
		item.audio_alive = !impl_->combat_dead(entity);
		if (item.audio_alive)
		{
			if (auto at = impl_->audio_events.find(body.id); at != impl_->audio_events.end())
				item.audio = at->second;
			if (const auto *shields = entity.try_get<ShieldComponent>())
				for (const auto &c : shields->contributions)
				{
					const auto &d = c.definition.shield;
					if (!d.audio.asset.empty() && impl_->shield_available(entity, c) && c.state.warmup &&
					    c.state.strength && c.protection > 0)
					{
						SceneAudio cue{SceneAudioKind::Shield, "shield", d.audio};
						cue.peak_gain = d.audio_peak_gain;
						cue.decay = d.audio_decay;
						cue.threshold = d.audio_threshold;
						cue.full_damage = d.audio_full_damage;
						item.audio.push_back(std::move(cue));
					}
				}
		}
		if (impl_->combat_dead(entity) && !entity.has<CombatStructure>())
			continue;
		if (const auto *structure = entity.try_get<CombatStructure>())
		{
			if (impl_->combat_dead(entity))
			{
				const auto &death = entity.get<Destructible>();
				if (static_cast<std::int64_t>(ticks()) - death.destroyed_tick >= death.definition.growth_ticks)
					continue;
				item.forward_thrust = 0;
			}
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::Turret)
				{
					const auto &d = m->definition;
					item.attachments.push_back(
					    {d.id, d.get<TurretDefinition>().visual, d.get<TurretDefinition>().marker,
					     combat_angle(body.heading + m->get<TurretState>().heading),
					     d.get<TurretDefinition>().scale_permille / 1000.f, d.get<TurretDefinition>().flip_y});
				}
		}
		if (entity.has<CombatStructure>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::Weapon)
				{
					const auto &w = m->definition.get<WeaponDefinition>();
					const auto [origin, heading] = impl_->weapon_mount(entity, *m);
					if (heading >= 0 && !w.muzzle_marker.empty())
						item.muzzles.push_back({m->definition.id, w.turret, w.muzzle_marker, origin.x, origin.y});
				}
		result.push_back(std::move(item));
	}
	const auto source_count = result.size();
	for (const auto particle : impl_->projectile_rows())
	{
		const auto &p = particle.get<Projectile>();
		SceneBody shot;
		shot.id = 7000000000000000ULL + p.id;
		shot.definition = p.definition.visual;
		shot.x = p.position.x;
		shot.y = p.position.y;
		shot.vx = p.velocity.x;
		shot.vy = p.velocity.y;
		shot.heading = p.heading;
		shot.attack_source = p.source;
		shot.attack_weapon = p.weapon;
		shot.muzzle_blend = std::max(0.f, 1.f - p.age / 4.f);
		shot.muzzle_tail_blend = std::max(0.f, 1.f - (p.age - 1) / 4.f);
		shot.visual_height_mm = p.altitude;
		shot.visual_vz = p.vz;
		shot.velocity_rotation = p.definition.durability > 0;
		shot.smoke_trail = !p.definition.trail.empty();
		if (p.definition.tracer)
		{
			shot.line_style = 2;
			shot.line_end_x = p.position.x - p.velocity.x / rate;
			shot.line_end_y = p.position.y - p.velocity.y / rate;
			shot.line_end_z = p.altitude - p.vz / rate;
		}
		result.push_back(shot);
	}
	for (const auto &[id, ship] : impl_->combat_rows())
		for (auto child : ship.get<EquipmentIndex>().values)
			if (const auto *m = child.try_get<CombatMechanism>();
			    m && m->definition.kind == OperatingSystem::Weapon &&
			    m->definition.get<WeaponDefinition>().family == "beam" && m->get<WeaponState>().beam_tick >= 0 &&
			    m->get<WeaponState>().beam_tick + 1 == static_cast<std::int64_t>(ticks()))
			{
				const auto &clip = impl_->directed_mode(id, m->definition.id)->firing_audio;
				const auto end = result.begin() + source_count;
				const auto source = std::lower_bound(result.begin(), end, id,
				                                     [](const auto &body, EntityId value)
				                                     {
					                                     return body.id < value;
				                                     });
				if (!clip.asset.empty() && source != end && source->id == id && source->audio_alive)
					source->audio.push_back({SceneAudioKind::Loop, "weapon:" + m->definition.id, clip});
				const auto [origin, heading] = impl_->weapon_mount(ship, *m);
				SceneBody beam;
				beam.id = 6000000000000000ULL + id * 256 + child.get<Equipment>().ordinal;
				beam.definition = ship.get<Definition>().name;
				beam.x = origin.x;
				beam.y = origin.y;
				beam.line_end_x = m->get<WeaponState>().beam_end.x;
				beam.line_end_y = m->get<WeaponState>().beam_end.y;
				beam.line_style = 1;
				beam.attack_source = id;
				beam.attack_weapon = m->definition.id;
				beam.muzzle_blend = 1;
				result.push_back(beam);
			}
	for (const auto &v : impl_->blast_visuals)
	{
		const auto age = static_cast<std::int64_t>(ticks()) - v.tick;
		const auto &d = v.definition;
		SceneBody visual;
		visual.id = 8500000000000000ULL + v.id;
		visual.definition = d.visual;
		visual.x = v.position.x;
		visual.y = v.position.y;
		visual.visual_height_mm = v.altitude;
		visual.visual_scale = static_cast<float>(d.size) / 1000.f /
		                      std::max(.001f, d.visual.empty() ? 1.f : impl_->visual_scales.at(d.visual)) *
		                      std::min(1.f, std::max(.01f, static_cast<float>(age) / d.growth_ticks));
		visual.alpha =
		    age < d.growth_ticks ? 1.f : std::max(0.f, 1.f - static_cast<float>(age - d.growth_ticks) / d.fade_ticks);
		visual.sound = d.sound;
		visual.effect_tick = v.tick;
		if (v.kind == 1)
		{
			visual.line_style = 3;
			visual.visual_scale = d.size / 1000.f;
			visual.heading = v.bearing;
			visual.rotation_mdeg = v.arc;
		}
		result.push_back(visual);
	}
	for (const auto &[id, rock] : impl_->field_rocks)
		if (rock.loaded && rock.decorative)
			result.push_back(rock.scene);
	return result;
}
Json World::scene_environment() const
{
	auto environment = region_environment(impl_->regions, impl_->environment_tick / static_cast<double>(rate));
	environment["simulation_step"] = 1.0 / rate;
	return environment;
}

Json World::public_board(EntityId entity) const
{
	auto board = impl_->knowledge.public_board(entity);
	board["alert"] = impl_->alert_board(entity);
	board["navigation"] = impl_->navigation_board(entity);
	board["objective_board"] = impl_->objectives_board(entity);

	if (impl_->combat_duel && impl_->duel_ended())
	{
		int alive = 0;
		for (const auto &[id, ship] : impl_->combat_rows())
			if (!impl_->combat_dead(ship))
				++alive;
		board["combat_result"] = alive == 0                                       ? "Draw"
		                         : impl_->combat_dead(impl_->entities.at(entity)) ? "Defeat"
		                                                                          : "Victory";
	}

	board["sidebar"] = Json::array();
	const auto &bridge_data = impl_->definition(entity);
	for (const auto &seat : bridge_data.consoles)
		if (bridge_data.console_bridge.contains(seat.id) && bridge_data.console_bridge.at(seat.id).viewscreen)
			board["sidebar"].push_back({{"id", seat.id},
			                            {"label", impl_->console_layouts.at(seat.definition).value("name", seat.id)},
			                            {"default", bridge_data.bridge.sidebar_default == seat.id},
			                            {"visible", impl_->bridge_visible({entity, seat.id})},
			                            {"available", can_claim(UINT64_MAX, entity, seat.id, true)}});
	const auto found = impl_->displays.find(entity);
	if (found == impl_->displays.end())
		return board;
	const auto &publication = found->second;
	if (!impl_->ownership.claims().contains(publication.claimant) ||
	    (impl_->ownership.claims().at(publication.claimant) != publication.address &&
	     impl_->console_operator(publication.address) != publication.claimant))
		return board;
	const auto widget = impl_->display_widget(publication.address, publication.widget);
	if (!widget)
		return board;
	const auto view = impl_->capture_console_observation(publication.address);
	const bool radar = widget->at("kind") == "radar";
	Json value{{"revision", publication.revision}, {"console", publication.address.console},
	           {"widget", publication.widget},     {"kind", widget->at("kind")},
	           {"camera", publication.camera},     {"own", display::body_json(view.own)},
	           {"contacts", Json::array()},        {"regions", Json::array()},
	           {"waypoint", Json::object()},       {"objectives", Json::array()}};
	if (widget->at("kind") == "conversations")
	{
		auto projection = impl_->conversations.project(entity);
		value["conversation"] = Json::object();
		for (const auto &thread : projection.at("threads"))
			if (thread.at("id") == projection.at("selected"))
				value["conversation"] = thread;
		board["display"] = display::sanitise(value);
		return board;
	}
	const SystemView *sensor = nullptr;
	if (radar)
	{
		const auto selected =
		    select_system(view.systems, OperatingSystem::Radar, widget->value("system", std::string{}), false);
		for (const auto &system : view.systems)
			if (selected && system.id == *selected)
				sensor = &system;
		if (!sensor)
			return board;
		value["radar"] = display::radar_json(*sensor);
		value["sweep"] = widget->value("sweep", Json::object());
		value["bearing_contacts"] = Json::array();
		if (sensor->enabled)
			for (const auto &b : view.bearing_contacts)
				if (std::find(sensor->contact_ids.begin(), sensor->contact_ids.end(), b.id) !=
				    sensor->contact_ids.end())
					value["bearing_contacts"].push_back(projection::bearing_json(b));
		if (sensor->enabled)
			for (const auto &contact : view.contacts)
				if (std::find(sensor->contact_ids.begin(), sensor->contact_ids.end(), contact.id) !=
				    sensor->contact_ids.end())
					value["contacts"].push_back(display::body_json(contact));
	}
	for (const auto &region : view.regions)
		if (region.value(radar ? "radar" : "navigation", false) &&
		    (!radar || std::any_of(region.at("tags").begin(), region.at("tags").end(),
		                           [&](const auto &tag)
		                           {
			                           return std::find(sensor->shows.begin(), sensor->shows.end(),
			                                            tag.template get<std::string>()) != sensor->shows.end();
		                           })))
		{
			Json marker = region;
			marker["tags"] = Json::array();
			if (radar)
				for (const auto &tag : region.at("tags"))
					if (std::find(sensor->shows.begin(), sensor->shows.end(), tag.get<std::string>()) !=
					    sensor->shows.end())
						marker["tags"].push_back(tag);
			value["regions"].push_back(std::move(marker));
		}
	std::map<std::uint64_t, Json> objectives;
	for (const auto &record : board.value("shared", Json::array()))
		if (record.value("schema", "") == "phoenix.waypoint")
			value["waypoint"] = record.at("fields");
		else if (!radar && record.value("schema", "") == "phoenix.objective" &&
		         record.at("fields").contains("destination_x"))
			objectives[record.at("fields").at("id").get<std::uint64_t>()] = record.at("fields");
	for (const auto &[id, fields] : objectives)
		value["objectives"].push_back(fields);
	board["display"] = display::sanitise(value);
	return board;
}
std::optional<Observation> World::observe(std::uint64_t console) const
{
	const auto snapshot = observe_shared(console);
	return snapshot ? std::optional<Observation>{*snapshot} : std::nullopt;
}
std::shared_ptr<const Observation> World::observe_shared(std::uint64_t console) const
{
	if (const auto cached = impl_->presentation.find(console))
		return cached;
	const auto found = impl_->ownership.claims().find(console);
	if (found == impl_->ownership.claims().end())
		return {};
	auto result = observe_at(console, found->second);
	for (const auto &seat : impl_->definition(found->second.ship).consoles)
	{
		const ConsoleAddress address{found->second.ship, seat.id};
		if (address != found->second && seat.human_seeking && impl_->console_operator(address) == console &&
		    impl_->bridge_visible(address))
			result.additional_consoles.push_back(observe_at(console, address));
	}
	auto snapshot = std::make_shared<const Observation>(std::move(result));
	impl_->presentation.remember(console, snapshot);
	return snapshot;
}
Observation World::observe_at(std::uint64_t console, const ConsoleAddress &address) const
{
	auto entity = impl_->entities.at(address.ship);
	const auto &seat = *impl_->seat(address.ship, address.console);
	Observation result = impl_->capture_console_observation(
	    address, impl_->knowledge.project_json(address), Impl::ObservationDetail::Presentation,
	    &impl_->presentation.telemetry(), &impl_->presentation.contacts());
	result.gameplay["alert"] = impl_->alert_board(address.ship);
	result.gameplay["navigation"] = impl_->navigation_board(address.ship);
	result.gameplay["stance"] = impl_->stance_board(address);
	if (impl_->definition(address.ship).console_bridge.at(address.console).objectives)
		result.gameplay["objective_board"] = impl_->objectives_board(address.ship);
	result.gameplay["personnel"] = impl_->personnel_board(address);
	result.gameplay["psychic"] = impl_->psychic_board(address);
	result.gameplay["abilities"] = impl_->ability_board(address);
	if (auto rules = impl_->rules_board(address); !rules.empty())
		result.gameplay["rules"] = std::move(rules);
	result.gameplay["teams"] = impl_->team_board(address);
	result.gameplay["away_missions"] = impl_->away_board(address);
	result.gameplay["craft"] = impl_->craft_board(address);
	result.gameplay["ftl"] = impl_->ftl_board(address);
	result.gameplay["processes"] = impl_->process_board(address, &result);
	result.gameplay["mission_report"] =
	    entity.has<MissionDeparture>() ? entity.get<MissionDeparture>().report : Json(nullptr);
	if (impl_->comms_owner(address.ship) == address)
		result.gameplay["conversations"] = impl_->conversations.project(address.ship);
	if (entity.has<EntityControls>())
		result.gameplay["controllers"] = impl_->project_controllers(address, result);

	auto &buttons = result.gameplay["controls"] = Json::array();
	if (impl_->departed(address.ship))
		return result;
	for (auto child : entity.get<EquipmentIndex>().values)
		if (child.has<FtlReserve>() && impl_->ftl_owner(address.ship, child.get<Equipment>().id) == address)
			for (const auto &ctrl : entity.get<EntityControls>().controllers)
				if (ctrl.human && ctrl.definition.console == address.console)
					for (const auto &channel : ctrl.definition.commands)
						if (std::find(channel.consumers.begin(), channel.consumers.end(),
						              child.get<Equipment>().ordinal) != channel.consumers.end())
						{
							OperatingCommand command{
							    address.ship, "ftl.depart",   0, child.get<Equipment>().id, ctrl.definition.id,
							    {},           ctrl.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "ftl.depart";
							descriptor["label"] = "Leave mission";
							descriptor["enabled"] = impl_->ftl_blocker(address.ship, child).empty();
							buttons.push_back(std::move(descriptor));
							break;
						}
	const auto button = [&](const std::string &id, const std::string &label, const std::string &action,
	                        const std::string &system, std::int64_t value)
	{
		if (std::none_of(buttons.begin(), buttons.end(),
		                 [&](const auto &b)
		                 {
			                 return b.at("id") == id;
		                 }))
			buttons.push_back({{"id", id}, {"label", label}, {"action", action}, {"system", system}, {"value", value}});
	};
	for (const auto &kind : impl_->camera_widgets.at(seat.definition))
	{
		if (kind == "camera_mode" || kind == "camera_direction")
			for (int value = 0; value < (kind == "camera_mode" ? 2 : 8); ++value)
				button(kind + "." + std::to_string(value),
				       kind == "camera_mode" ? (value ? "Third person" : "First person") : camera_directions[value],
				       kind, {}, value);
	}
	const bool away = impl_->ownership.is_away(address);
	impl_->bridge_controls(address, buttons);
	button("away", away ? "Resume control" : "Away", "away", {}, away ? 0 : 1);
	if (entity.has<EntityControls>())
	{
		const auto &controls = entity.get<EntityControls>();
		Impl::ConsoleContacts ability_contacts;
		ability_contacts.emplace(address, result);
		if (!impl_->definition(address.ship).console_bridge.at(seat.id).command)
		{
			result.gameplay["profile"] = controls.layout.profiles.at(controls.profiles.at(seat.id)).id;
			result.gameplay["profile_stars"] = Json::object();
			for (std::size_t i = 0; i < controls.layout.profiles.size(); ++i)
			{
				if (controls.layout.fixed_profiles.contains(seat.id) && controls.layout.fixed_profiles.at(seat.id) != i)
					continue;
				if (controls.layout.profiles[i].stars)
					result.gameplay["profile_stars"][controls.layout.profiles[i].id] =
					    controls.layout.profiles[i].stars;
				button("profile." + controls.layout.profiles[i].id, controls.layout.profiles[i].id, "preset", {}, i);
			}
		}
		for (const auto &controller : controls.controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					const auto &channel = controller.definition.commands[i];
					const auto &state = controller.commands[i];
					if (!channel.coordinated_group.empty())
					{
						const auto key = "flight.impulse." + channel.coordinated_group;
						if (std::none_of(buttons.begin(), buttons.end(),
						                 [&](const auto &b)
						                 {
							                 return b.at("id") == key + ".1";
						                 }))
						{
							std::uint64_t next = 1;
							for (const auto &peer : controls.controllers)
								if (peer.definition.console == address.console)
									for (std::size_t j = 0; j < peer.commands.size(); ++j)
										if (peer.definition.commands[j].coordinated_group == channel.coordinated_group)
											next = std::max(next, peer.commands[j].request_id + 1);
							for (const auto value : {0, 1})
							{
								OperatingCommand c{address.ship, "flight.impulse", value, channel.coordinated_group};
								c.generation = impl_->coordinated_generation(address, channel.coordinated_group);
								c.request_id = next;
								auto descriptor = encode_operating_command(c);
								descriptor["id"] = key + "." + std::to_string(value);
								descriptor["label_key"] = value ? "Start impulse" : "Cancel impulse";
								descriptor["label"] = value ? "Start impulse" : "Cancel impulse";
								buttons.push_back(std::move(descriptor));
							}
						}
					}
					if (impl_->station_stars(address) < channel.individual_stars)
						continue;
					if (impl_->process_controls(address, controller, i, buttons))
						continue;
					if (impl_->ability_controls(address, controller, i, buttons, ability_contacts))
						continue;
					const auto scale = channel.action == OperatingAction::GroupAllocation
					                       ? std::max(state.value, channel.initial)
					                       : channel.maximum - channel.minimum;
					const auto step = std::max<std::int64_t>(1, scale / 20);
					std::set<std::int64_t> values;
					if (channel.action == OperatingAction::DirectedOperatingMode ||
					    channel.action == OperatingAction::DirectedTargetingMode)
					{
						if (channel.consumers.empty())
							continue;
						const auto child = entity.get<EquipmentIndex>().values.at(channel.consumers.front());
						const auto projection = impl_->directed_projection(address.ship, child.get<Equipment>().id);
						const auto key = channel.action == OperatingAction::DirectedOperatingMode ? "operating_modes"
						                                                                          : "targeting_modes";
						if (!projection.contains(key))
							continue;
						for (const auto &choice : projection.at(key))
						{
							if (!choice.at("compatible").get<bool>())
								continue;
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         choice.at("value").get<std::int64_t>(),
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "directed." + controller.definition.id + "." + channel.id + "." +
							                   std::to_string(command.value);
							descriptor["label"] = choice.at("label");
							descriptor["label_prefix"] = controller.definition.id + ": ";
							descriptor["selected"] = state.value == command.value;
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}
					if (channel.action == OperatingAction::MagazineSelect ||
					    channel.action == OperatingAction::WeaponMode ||
					    channel.action == OperatingAction::RepairSystem)
					{
						if (channel.consumers.empty())
							continue;
						const auto child = entity.get<EquipmentIndex>().values.at(channel.consumers.front());
						const auto *mechanism = child.try_get<CombatMechanism>();
						if (!mechanism)
							continue;
						std::vector<std::string> labels;
						if (channel.action == OperatingAction::MagazineSelect)
							labels = mechanism->definition.get<WeaponDefinition>().magazines;
						if (channel.action == OperatingAction::WeaponMode &&
						    mechanism->definition.get<WeaponDefinition>().point_defence)
							labels = {"Off", "Auto", "Manual"};
						if (channel.action == OperatingAction::RepairSystem)
						{
							labels.push_back("Automatic repairs");
							for (const auto &[equipment, compartment] :
							     entity.get<CombatStructure>().definition.equipment_compartments)
								labels.push_back(equipment);
							if (channel.hull_priority)
								labels.push_back("Compartment hull");
						}
						for (std::size_t choice = 0; choice < labels.size(); ++choice)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         static_cast<std::int64_t>(choice),
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] =
							    "combat." + controller.definition.id + "." + channel.id + "." + std::to_string(choice);
							descriptor["label"] = labels[choice];
							descriptor["label_prefix"] = controller.definition.id + ": ";
							descriptor["selected"] = state.value == static_cast<std::int64_t>(choice);
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}
					if (channel.action == OperatingAction::WeaponTarget ||
					    channel.action == OperatingAction::DirectedTarget ||
					    channel.action == OperatingAction::SensorTarget ||
					    channel.action == OperatingAction::RepairCompartment)
					{
						std::set<std::int64_t> choices{0};
						if (channel.action == OperatingAction::WeaponTarget ||
						    channel.action == OperatingAction::DirectedTarget ||
						    channel.action == OperatingAction::SensorTarget)
						{
							for (const auto &contact : result.contacts)
								if (contact.id <= 2000000000ULL)
									choices.insert(static_cast<std::int64_t>(contact.id));
							for (const auto &contact : result.bearing_contacts)
								if (contact.id <= 1000000000ULL)
									choices.insert(static_cast<std::int64_t>(contact.id));
						}
						else if (const auto *structure = entity.try_get<CombatStructure>())
							for (std::size_t c = 0; c < structure->compartments.size(); ++c)
								choices.insert(static_cast<std::int64_t>(c + 1));
						if (channel.action == OperatingAction::SensorTarget)
							for (const auto &contact : result.gameplay.at("sensors").at("contacts"))
								if (contact.at("id").get<EntityId>() <= 2000000000ULL)
									choices.insert(contact.at("id").get<std::int64_t>());
						for (const auto choice : choices)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         choice,
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] =
							    "combat." + controller.definition.id + "." + channel.id + "." + std::to_string(choice);
							descriptor["label"] =
							    choice ? controller.definition.id : "Cancel " + controller.definition.id;
							descriptor["combat_choice"] = (channel.action == OperatingAction::WeaponTarget ||
							                               channel.action == OperatingAction::DirectedTarget)
							                                  ? "weapon"
							                              : channel.action == OperatingAction::SensorTarget ? "sensor"
							                                                                                : "repair";
							if (channel.action == OperatingAction::SensorTarget)
							{
								for (const auto &other : entity.get<EntityControls>().controllers)
									for (const auto &target : other.definition.commands)
										if (other.definition.console == address.console &&
										    target.shared_target_controller == controller.definition.id &&
										    target.shared_target_channel == channel.id)
										{
											if (target.action == OperatingAction::WeaponTarget)
											{
												descriptor["shared_combat_target"] = true;
												descriptor["combat_choice"] = "weapon";
											}
											else if (target.action == OperatingAction::SensorTarget)
												descriptor["shared_scan_target"] = true;
										}
								descriptor["label"] = channel.label_key.empty()
								                          ? (choice ? "Scan target" : "Clear scan target")
								                          : (choice ? channel.label_key : "Clear " + channel.label_key);
								descriptor["label_prefix"] = controller.definition.id + ": ";
							}
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}

					if (!channel.choices.empty())
						for (const auto &[value, label] : channel.choices)
							values.insert(value);
					else if (channel.action == OperatingAction::Lateral)
						values = {-1000, 0, 1000};
					else if (channel.type == KnowledgeType::Boolean)
						values = {0, 1};
					else
						values = {std::clamp(state.value - step, channel.minimum, channel.maximum),
						          std::clamp(state.value + step, channel.minimum, channel.maximum)};
					if (channel.action != OperatingAction::Lateral && channel.choices.empty() && channel.minimum <= 0 &&
					    channel.maximum >= 0)
						values.insert(0);
					const bool ftl_toggle =
					    channel.action == OperatingAction::SystemEnabled && channel.consumers.size() == 1 &&
					    entity.get<EquipmentIndex>().values.at(channel.consumers.front()).has<FtlReserve>();
					if (ftl_toggle)
						values = {state.value ? 0 : 1};
					for (const auto value : values)
					{
						if (state.request_id == std::numeric_limits<std::uint64_t>::max())
							continue;
						OperatingCommand command{address.ship,
						                         std::string(operating_action(channel.action).name),
						                         value,
						                         {},
						                         controller.definition.id,
						                         channel.id,
						                         controller.generation,
						                         channel.semantics == CommandSemantics::Request ? state.request_id + 1
						                                                                        : 0};
						command.arguments = state.arguments;
						if (!operating_arguments_valid(channel.action, value, command.arguments))
							continue;
						auto descriptor = encode_operating_command(command);
						descriptor["id"] =
						    "controller." + controller.definition.id + "." + channel.id + "." + std::to_string(value);
						descriptor["label"] = controller.definition.id + " " + channel.id + " " + std::to_string(value);
						if (channel.action == OperatingAction::Lateral || channel.action == OperatingAction::Boost ||
						    channel.action == OperatingAction::Impulse)
						{
							descriptor["label_key"] = channel.action == OperatingAction::Lateral
							                              ? (value < 0   ? "Thrust port"
							                                 : value > 0 ? "Thrust starboard"
							                                             : "Release flight controls")
							                          : channel.action == OperatingAction::Boost
							                              ? (value ? "Hold boost" : "Release boost")
							                              : (value ? "Start impulse" : "Cancel impulse");
							descriptor["momentary"] = channel.action != OperatingAction::Impulse && value != 0;
						}
						if (channel.choices.contains(value))
							descriptor["label_key"] = channel.choices.at(value);
						else if (!channel.label_key.empty())
						{
							descriptor["label_key"] = channel.label_key;
							descriptor["label_value"] = true;
						}
						if (ftl_toggle)
						{
							descriptor["label_key"] = value ? "Turn FTL battery on" : "Turn FTL battery off";
							descriptor["label_value"] = false;
						}
						buttons.push_back(std::move(descriptor));
					}
					const auto fields = operating_arguments(channel.action);
					for (std::size_t argument = 0; argument < fields.size(); ++argument)
					{
						std::set<std::int64_t> choices;
						if (fields[argument].name == "contact")
						{
							for (const auto &contact : result.contacts)
								if (contact.id <= static_cast<EntityId>(fields[argument].maximum))
									choices.insert(contact.id);
						}
						else
							for (const auto delta : {-1000, 1000})
								choices.insert(std::clamp(state.arguments[argument] + delta, fields[argument].minimum,
								                          fields[argument].maximum));
						for (const auto value : choices)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         state.value,
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							command.arguments = state.arguments;
							command.arguments[argument] = value;
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "controller." + controller.definition.id + "." + channel.id + "." +
							                   std::string(fields[argument].name) + "." + std::to_string(value);
							descriptor["label"] = controller.definition.id + " " + channel.id + " " +
							                      std::string(fields[argument].name) + " " + std::to_string(value);
							buttons.push_back(std::move(descriptor));
						}
					}
				}
	}
	if (entity.has<EntityControls>())
		for (const auto &controller : entity.get<EntityControls>().controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (const auto &name : impl_->definition(address.ship).message_templates)
				{
					OperatingCommand command{address.ship, "message." + name,    0, {}, controller.definition.id,
					                         {},           controller.generation};
					command.console = address.console;
					if (!impl_->meta(console, command, false))
						continue;
					auto descriptor = encode_operating_command(command);
					descriptor["id"] = "message." + controller.definition.id + "." + name;
					descriptor["label"] = "Send " + name;
					buttons.push_back(std::move(descriptor));
				}
	const auto &spec = impl_->definition(address.ship);
	for (const auto &name : spec.publications)
	{
		OperatingCommand command{address.ship, "publish." + name, 0, {}};
		command.console = address.console;
		if (impl_->meta(console, command, false))
			button("publish." + name, "Publish " + name, "publish." + name, {}, 0);
	}
	for (auto &button : buttons)
		button["console"] = address.console;
	return result;
}
Json World::controller_ai(EntityId id) const
{
	Json rows = Json::array();
	auto entity = impl_->entities.find(id);
	if (entity == impl_->entities.end() || !entity->second.has<EntityControls>())
		return rows;
	for (const auto &c : entity->second.get<EntityControls>().controllers)
		rows.push_back({{"ship", id},
		                {"console", c.definition.console},
		                {"controller", c.definition.id},
		                {"eligible", !c.candidates.empty()},
		                {"human", c.human},
		                {"selected", c.selected_provider},
		                {"effective", c.effective_provider},
		                {"reason", c.fallback_reason},
		                {"next", c.next_decision},
		                {"last", c.last_decision_tick},
		                {"sequence", c.decision_sequence}});
	return rows;
}
Json World::controller_decision_snapshot(const ControllerAddress &address)
{
	return impl_->model_snapshot(address);
}
bool World::configure_controller_provider(const ControllerAddress &address, const std::string &provider)
{
	impl_->clear_presentation_snapshots();
	if (provider != "tree" && provider != "anyjev" && provider != "llamacpp" && provider != "jev")
		return false;
	auto *c = impl_->model_controller(address);
	if (!c || (provider != "tree" && c->candidates.empty()))
		return false;
	if (c->selected_provider == provider)
		return true;
	const auto index =
	    static_cast<std::size_t>(c - impl_->entities.at(address.entity).get_mut<EntityControls>().controllers.data());
	if (c->brain && !c->human)
	{
		const auto observations = impl_->controller_observations(address.entity, *c);
		impl_->cancel_controller_action(address.entity, index, "provider_changed", observations);
		const auto exit = state_tree::interrupt(c->brain->program, c->brain->continuation, observations);
		for (const auto &output : exit.outputs)
			impl_->controller_output(address.entity, index, output, observations);
	}
	++c->provider_generation;
	c->selected_provider = provider;
	c->effective_provider = "tree";
	c->fallback_reason = provider == "tree" ? "" : "connecting";
	c->next_decision = ticks();
	c->retry_count = 0;
	return true;
}
bool World::submit_controller_decision(const Json &decision)
{
	impl_->clear_presentation_snapshots();
	try
	{
		const auto &snapshot = decision.at("snapshot");
		ControllerAddress address{snapshot.at("ship"), snapshot.at("console"), snapshot.at("controller")};
		auto *c = impl_->model_controller(address);
		if (c && !c->human && impl_->encounter_owns_controller(address.entity, c->definition))
			return false;
		const auto chosen = decision.value("candidate", std::string("defer_to_tree"));
		bool cancels_activity = false;
		for (const auto &candidate : snapshot.at("candidates"))
			if (candidate.at("id") == chosen)
				for (const auto &operation : candidate.value("operations", Json::array()))
					cancels_activity = cancels_activity || operation.value("name", std::string{}) == "action.cancel";
		auto basis = snapshot.at("basis");
		auto current_basis = c ? impl_->model_basis(address.entity, *c) : Json();
		if (c && c->effective_provider == "tree" && chosen != "continue" && chosen != "defer_to_tree" &&
		    !cancels_activity)
		{
			// Tree adaptation must not permanently prevent a freshly revalidated concrete takeover.
			basis.erase("action_serial");
			basis.erase("action_status");
			current_basis.erase("action_serial");
			current_basis.erase("action_status");
		}
		if (!c || c->human || c->selected_provider == "tree" || snapshot.at("schema") != 1 ||
		    snapshot.at("provider") != c->selected_provider || snapshot.at("tick") > ticks() ||
		    (!decision.value("failed", false) && snapshot.at("expires") < ticks()) ||
		    snapshot.at("expires").get<std::uint64_t>() > snapshot.at("tick").get<std::uint64_t>() + 100 ||
		    snapshot.at("sequence") != c->decision_sequence + 1 ||
		    snapshot.at("basis").at("generation") != c->generation ||
		    snapshot.at("basis").at("provider_generation") != c->provider_generation ||
		    (!decision.value("failed", false) && basis != current_basis))
			return false;
		const auto interval = checked_unsigned(decision.value("interval_ticks", Json(40)), 10, 40, "decision interval");
		const auto index = static_cast<std::size_t>(
		    c - impl_->entities.at(address.entity).get_mut<EntityControls>().controllers.data());
		if (decision.value("failed", false) || chosen == "defer_to_tree")
		{
			++c->decision_sequence;
			impl_->model_fallback(address.entity, index,
			                      !decision.value("failed", false) && chosen == "defer_to_tree"
			                          ? "provider_deferred"
			                          : decision.value("reason", std::string("provider_failed")));
			return true;
		}
		const auto observed = impl_->controller_observations(address.entity, *c);
		Json selected;
		if (chosen != "continue")
		{
			for (const auto &candidate : model_candidates(*c, observed))
				if (candidate.at("id") == chosen)
					selected = candidate;
			if (selected.is_null())
				return false;
			bool offered = false;
			for (const auto &candidate : snapshot.at("candidates"))
				if (candidate == selected)
					offered = true;
			if (!offered)
				return false;
		}
		if (c->effective_provider == "tree")
		{
			const auto exit = state_tree::interrupt(c->brain->program, c->brain->continuation, observed);
			for (const auto &output : exit.outputs)
				impl_->controller_output(address.entity, index, output, observed);
		}
		c->effective_provider = c->selected_provider;
		c->fallback_reason.clear();
		c->retry_count = 0;
		c->next_decision = std::max<std::uint64_t>(ticks(), snapshot.at("tick").get<std::uint64_t>() + interval);
		c->last_decision_tick = ticks();
		++c->decision_sequence;
		bool accepted = true;
		if (!selected.is_null())
			for (const auto &operation : selected.at("operations"))
			{
				const auto name = operation.at("name").get<std::string>();
				std::vector<state_tree::Value> args;
				for (const auto &v : operation.at("arguments"))
					args.push_back(
					    {static_cast<state_tree::Type>(v.at("type").get<int>()), v.at("number"), v.at("text")});
				ControllerDecisionProposal proposal{address, c->generation};
				if (name.starts_with("select.") || name.starts_with("replace."))
				{
					proposal.action = name.substr(name.starts_with("select.") ? 7 : 8);
					proposal.invocation =
					    selected.at("id") == "officer-accept" ? args[0].text + "/model" : "model/" + chosen;
					if (name.starts_with("replace."))
						proposal.invocation += "/" + std::to_string(c->decision_sequence);
					proposal.inputs = {args.begin() + 1, args.end()};
				}
				else if (name == "action.cancel")
				{
					proposal.operation = "cancel";
					proposal.invocation = args[0].text;
				}
				else if (name == "acknowledge_message")
				{
					impl_->knowledge.acknowledge_controller(address, args[0].number);
					continue;
				}
				else
				{
					proposal.operation = name;
					proposal.inputs = args;
				}
				if (!impl_->admit_controller_decision(proposal, observed))
				{
					accepted = false;
					break;
				}
			}
		c->decision_history.push_back({{"orders", snapshot.at("basis").at("orders")},
		                               {"tick", ticks()},
		                               {"candidate", chosen},
		                               {"accepted", accepted},
		                               {"deviation", selected.is_null() ? Json("") : selected.at("deviation")}});
		if (c->decision_history.size() > 16)
			c->decision_history.erase(c->decision_history.begin());
		if (!selected.is_null() && !selected.at("deviation").get<std::string>().empty())
			impl_->model_deviation(address, *c, selected.at("deviation"));
		if (!accepted)
			impl_->model_fallback(address.entity, index, "decision_refused");
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
Json World::debug() const
{
	Json result{{"tick", ticks()},
	            {"active_states", Json::array()},
	            {"transition_reason", ""},
	            {"diagnostics", Json::array()},
	            {"controllers", Json::array()},
	            {"console_records", Json::array()},
	            {"ship_records", Json::array()},
	            {"pending_messages", Json::array()},
	            {"resource_grants", Json::array()},
	            {"pending_outputs", Json::array()},
	            {"truncated", 0}};
	const auto sample = [](const Json &rows)
	{
		Json out = Json::array();
		if (rows.is_array())
			for (std::size_t i = rows.size() > 8 ? rows.size() - 8 : 0; i < rows.size(); ++i)
				out.push_back(rows[i]);
		return out;
	};
	std::size_t controllers = 0, consoles = 0;
	for (const auto &[id, entity] : impl_->entities)
	{
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				if (++controllers > 32)
				{
					result["truncated"] = result["truncated"].get<int>() + 1;
					continue;
				}
				Json row{
				    {"id", std::to_string(id) + "." + controller.definition.console + "." + controller.definition.id},
				    {"context", "controller"},
				    {"entity", id},
				    {"console", controller.definition.console},
				    {"controller", controller.definition.id},
				    {"operator", controller.human ? "human" : "ai"},
				    {"generation", controller.generation},
				    {"tree", controller.definition.tree},
				    {"active_states", Json::array()},
				    {"transition_reason", ""},
				    {"diagnostics", Json::array()},
				    {"commands", Json::array()}};
				row["action"] = save_controller_action(controller);
				row["coordination"] = save_controller_coordination(controller);
				if (controller.brain)
				{
					row["active_states"] = controller.brain->debug.active;
					row["transition_reason"] = controller.brain->debug.transition;
					row["diagnostics"] = controller.brain->debug.diagnostics;
					for (const auto &state : controller.brain->debug.active)
						if (result["active_states"].size() < 128)
							result["active_states"].push_back(state);
					for (const auto &diagnostic : controller.brain->debug.diagnostics)
						if (result["diagnostics"].size() < 32)
							result["diagnostics"].push_back(diagnostic);
				}
				for (std::size_t channel = 0; channel < controller.commands.size(); ++channel)
				{
					if (channel >= 64)
					{
						result["truncated"] = result["truncated"].get<int>() + 1;
						continue;
					}
					const auto &command = controller.commands[channel];
					row["commands"].push_back({{"channel", controller.definition.commands[channel].id},
					                           {"value", command.value},
					                           {"revision", command.revision},
					                           {"consumed", command.consumed},
					                           {"request_id", command.request_id},
					                           {"accepted", command.accepted}});
				}
				result["controllers"].push_back(std::move(row));
			}
		if (auto systems = gameplay_systems(entity))
			for (std::size_t i = 0; i < systems->values.size(); ++i)
			{
				const auto system = systems->values[i];
				if (systems->power)
				{
					if (result["resource_grants"].size() < 64)
					{
						const auto &grant =
						    systems->power->grants[(*fittings(entity)).values[i].entity.get<ResourceBinding>().slot];
						result["resource_grants"].push_back({{"ship", id},
						                                     {"system", (*fittings(entity)).values[i].id},
						                                     {"requested", grant.requested},
						                                     {"paid", grant.paid},
						                                     {"supply_permille", grant.supply_permille},
						                                     {"overdrive_permille", grant.overdrive_permille},
						                                     {"telemetry", impl_->telemetry(id, i)}});
					}
					else
						result["truncated"] = result["truncated"].get<int>() + 1;
				}
			}
		bool shared = false;
		for (const auto &console : impl_->definition(id).consoles)
		{
			if (++consoles > 32)
			{
				result["truncated"] = result["truncated"].get<int>() + 1;
				continue;
			}
			const auto projection = impl_->knowledge.project_json({id, console.id});
			result["console_records"].push_back({{"ship", id},
			                                     {"console", console.id},
			                                     {"records", sample(projection.value("records", Json::array()))},
			                                     {"messages", sample(projection.value("messages", Json::array()))}});
			if (!shared)
			{
				result["ship_records"].push_back(
				    {{"ship", id}, {"records", sample(projection.value("shared", Json::array()))}});
				shared = true;
			}
		}
	}
	std::size_t scenario_controllers = 0;
	for (const auto &proposal : impl_->controller_pending)
	{
		const auto &controller =
		    impl_->entities.at(proposal.address.entity).get<EntityControls>().controllers.at(proposal.controller);
		const auto &channel = controller.definition.commands.at(proposal.channel);
		if (result["pending_outputs"].size() < 32)
			result["pending_outputs"].push_back({{"ship", proposal.address.entity},
			                                     {"console", controller.definition.console},
			                                     {"controller", controller.definition.id},
			                                     {"channel", channel.id},
			                                     {"action", operating_action(channel.action).name},
			                                     {"value", proposal.value}});
		else
			result["truncated"] = result["truncated"].get<int>() + 1;
	}
	for (const auto &[claim, command] : impl_->meta_pending)
		if (result["pending_outputs"].size() < 32)
			result["pending_outputs"].push_back({{"ship", command.target},
			                                     {"system", command.system},
			                                     {"action", command.action},
			                                     {"value", command.value}});
		else
			result["truncated"] = result["truncated"].get<int>() + 1;
	if (impl_->scenario)
		for (const auto &[id, controller] : impl_->scenario->controllers())
		{
			if (++scenario_controllers > 32)
			{
				result["truncated"] = result["truncated"].get<int>() + 1;
				continue;
			}
			result["controllers"].push_back({{"id", id},
			                                 {"tree", controller.tree},
			                                 {"context", "scenario"},
			                                 {"active_states", controller.debug.active},
			                                 {"transition_reason", controller.debug.transition},
			                                 {"diagnostics", controller.debug.diagnostics}});
			for (const auto &state : controller.debug.active)
				if (result["active_states"].size() < 128)
					result["active_states"].push_back(state);
		}
	for (const auto &message : impl_->knowledge.pending_messages())
		result["pending_messages"].push_back(
		    {{"id", message.id},
		     {"sender_system", message.value.sender_system},
		     {"destination", message.value.destination.id},
		     {"due_tick", message.delivery_tick},
		     {"expires_tick", message.value.expires_tick},
		     {"schema", impl_->knowledge.definition().schemas.at(message.value.payload.schema - 1).name},
		     {"request_key", message.value.request_key}});
	return result;
}
Json World::save() const
{
	if (!impl_->meta_pending.empty() || !impl_->controller_pending.empty())
		throw std::runtime_error("Save at a tick boundary after admitted commands execute");
	Json result{{"schema", 32},           {"tick", ticks()},       {"map", impl_->map},
	            {"ships", impl_->ships},  {"packs", impl_->packs}, {"claims", impl_->ownership.claims()},
	            {"bodies", Json::array()}};
	result["custom_properties"] = impl_->save_custom_properties();
	result["rules"] = impl_->save_rules();
	result["active_effects"] = impl_->save_active_modifiers();
	result["active_effects"]["conditions"] = impl_->active_rule_snapshot();
	result["active_effects"]["next_condition"] = impl_->next_rule_instance;
	if (impl_->display_revision)
	{
		result["display_revision"] = impl_->display_revision;
		result["displays"] = Json::array();
		for (const auto &[ship, d] : impl_->displays)
			result["displays"].push_back({{"claimant", d.claimant},
			                              {"revision", d.revision},
			                              {"address", d.address},
			                              {"widget", d.widget},
			                              {"camera", d.camera}});
	}
	if (!impl_->field_rocks.empty())
		result["asteroid_fields"] = impl_->save_fields();
	if (!impl_->regions.empty())
		result["environment_tick"] = impl_->environment_tick;
	for (const auto &[id, entity] : impl_->entities)
	{
		if (id >= Impl::field_identity_base)
			continue;
		SavedBody body{entity.get<Pose>(), entity.get<Motion>(), id, entity.get<Definition>().name};
		result["bodies"].push_back({{"body", body}, {"body_altitude", entity.get<Pose>().altitude}});
		if (entity.has<VerticalMotion>())
			result["bodies"].back()["vertical"] = entity.get<VerticalMotion>();
		if (entity.has<MissionDeparture>())
			result["bodies"].back()["mission_report"] = entity.get<MissionDeparture>().report;
		result["bodies"].back()["faction"] =
		    (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{});
		if (auto systems = fittings(entity))
		{
			auto &saved = result["bodies"].back()["systems"] = Json::object();
			for (const auto &system : systems->values)
			{
				SavedOperation operation;
				operation.heading = entity.get<Pose>().heading;
				if (system.engine)
				{
					operation.speed = system.engine->speed;
					operation.engine_enabled = system.engine->enabled;
					operation.acceleration_remainder = system.engine->remainder;
					operation.damping_remainder = system.engine->damping_remainder;
				}
				if (system.steering)
				{
					operation.heading = system.steering->heading;
					operation.turn_remainder = system.steering->remainder;
				}
				if (system.radar)
					operation.radar_enabled = system.radar->enabled;
				if (system.advanced)
				{
					operation = SavedOperation{};
					operation.heading = entity.get<Pose>().heading;
				}
				saved[system.id] = operation;
			}
		}
		if (auto systems = gameplay_systems(entity))
		{
			auto &data = result["bodies"].back()["gameplay"] = Json::array();
			for (std::size_t i = 0; i < systems->values.size(); ++i)
			{
				const auto system = systems->values[i];
				Json value{{"id", (*fittings(entity)).values[i].id},
				           {"heat_remainder", system.heat_remainder},
				           {"enabled", (*fittings(entity)).values[i].enabled}};
				if (const auto radar = (*fittings(entity)).values[i].radar)
					value["radar_enabled"] = radar->enabled;
				if (system.flight)
					value["flight"] = save_flight(system.flight->definition, system.flight->state);
				if (const auto *ftl = entity.get<EquipmentIndex>().values.at(i).try_get<FtlReserve>())
					value["ftl_discharge_remainder"] = ftl->discharge_remainder;
				data.push_back(std::move(value));
			}
			if (systems->power)
				result["bodies"].back()["resources"] = save_ship_resources(*systems->power);
		}
		if (entity.has<DockingState>())
			result["bodies"].back()["docking"] = entity.get<DockingState>();
		if (entity.has<DockConnection>())
			result["bodies"].back()["dock_connection"] = entity.get<DockConnection>();
		if (entity.has<ShipProcessors>())
			result["bodies"].back()["processors"] = impl_->save_processors(entity);
		if (entity.has<MaterialInventory>())
			result["bodies"].back()["inventory"] = entity.get<MaterialInventory>();
		if (const auto *stocks = entity.try_get<ShipStockStores>())
		{
			auto &saved_stocks = result["bodies"].back()["stocks"] = Json::object();
			for (const auto &[key, stock] : stocks->stores)
				saved_stocks[key] = stock.save();
		}
		if (entity.has<PersonnelState>())
			result["bodies"].back()["personnel"] = impl_->personnel_snapshot(id);
		if (entity.has<CombatStructure>())
			result["bodies"].back()["combat"] = impl_->save_combat(entity);
		if (entity.has<ShieldComponent>())
			result["bodies"].back()["shields"] = impl_->save_shields(entity);
		if (const auto *d = entity.try_get<Destructible>())
			result["bodies"].back()["destructible"] = *d;
		if (entity.has<EntityControls>())
			result["bodies"].back()["console_controllers"] = save_controllers(entity.get<EntityControls>());
		if (const auto *bridge = entity.try_get<BridgeBlackboard>())
			result["bodies"].back()["bridge"] = {{"condition", bridge->condition},
			                                     {"revision", bridge->revision},
			                                     {"stances", bridge->stances},
			                                     {"epochs", bridge->epochs},
			                                     {"navigation", impl_->navigation_board(id)},
			                                     {"priority_objective", bridge->priority_objective},
			                                     {"priority_revision", bridge->priority_revision}};
	}
	if (impl_->gameplay)
	{
		result["knowledge"] = impl_->knowledge.save();

		result["away"] = Json::array();
		for (const auto &[a, v] : impl_->ownership.away())
			result["away"].push_back({{"address", a}, {"value", v}});
		result["next_entity"] = impl_->next_entity;
		if (impl_->scenario)
			result["scenario"] = impl_->scenario->save();
	}
	result["conversations"] = impl_->conversations.save();
	result["away_missions"] = impl_->save_away();
	result["captured_by"] = impl_->captured_by;
	result["personnel_postings"] = impl_->personnel_postings;
	result["interdictions"] = impl_->save_interdictions();
	result["departure_groups"] = impl_->departures.save_groups();
	result["logistics"] = {{"requests", impl_->material_requests},
	                       {"batches", impl_->material_batches},
	                       {"next_request", impl_->next_material_request},
	                       {"next_batch", impl_->next_material_batch},
	                       {"cargo", impl_->cargo_deliveries},
	                       {"next_cargo", impl_->next_cargo_delivery}};
	result["craft"] = impl_->save_craft();
	result["mission_ledger"] = impl_->mission_ledger;
	result["sensors"] = impl_->save_sensors();
	result["directed"] = impl_->save_directed();
	result["psychic"] = impl_->save_psychic();
	result["abilities"] = impl_->save_abilities();
	result["processes"] = impl_->save_processes();
	result["sensor_discoveries"] = impl_->sensor_findings.discoveries();
	result["conversation_starts"] = impl_->save_conversation_starts();
	result["projectiles"] = impl_->save_projectiles();
	result["blast_visuals"] = impl_->blast_visuals;
	result["next_blast"] = impl_->next_blast;
	result["next_projectile"] = impl_->next_projectile;
	return result;
}
void World::restore(const Catalog &content, const Json &source_saved)
{
	const Json &saved = source_saved;
	validate_checkpoint_envelope(saved, content.ordered_packs);
	World candidate;
	candidate.load_impl(content, saved.at("map"), saved.at("ships").get<std::map<std::string, std::string>>(), true);
	for (auto &[id, rock] : candidate.impl_->field_rocks)
		candidate.impl_->unload_rock(rock);
	candidate.impl_->field_rocks.clear();
	candidate.impl_->field_window.clear();
	candidate.impl_->index_contacts.run();
	if (candidate.impl_->scenario)
	{
		const auto event_source = candidate.impl_->conversation_instance;
		if (saved.at("conversation_starts").at("instance") != event_source)
			throw std::runtime_error("Saved scenario event source mismatch");
		candidate.impl_->scenario->restore(content, saved.at("map"), saved.at("scenario"), event_source,
		                                   candidate.impl_->property_catalogue);
		for (const auto &[id, definition] : candidate.impl_->scenario->runtime().spawned_entities())
			if (candidate.impl_->entities.contains(id))
				throw std::runtime_error("Spawned identity overlaps initial entity");
			else
				candidate.impl_->create(id, definition, {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
		candidate.impl_->restore_spawned_craft(saved.value("craft", Json()));
	}
	candidate.impl_->restore_interdiction_ships(saved.value("interdictions", Json::array()), saved.at("bodies"));
	candidate.impl_->next_entity = checkpoint_next_entity(
	    saved, candidate.impl_->entities.empty() ? EntityId{0} : candidate.impl_->entities.rbegin()->first);
	candidate.impl_->refresh_boarding_templates();
	candidate.impl_->captured_by = saved.value("captured_by", std::map<EntityId, EntityId>{});
	std::vector<EntityId> checkpoint_entities;
	checkpoint_entities.reserve(candidate.impl_->entities.size());
	for (const auto &[id, entity] : candidate.impl_->entities)
		checkpoint_entities.push_back(id);
	CheckpointRoster checkpoint_roster(saved.at("bodies"), checkpoint_entities);
	candidate.impl_->restore_directed(saved.at("directed"));
	for (const auto &value : saved.at("bodies"))
	{
		validate_checkpoint_entity_authority(value);
		auto restored = value.at("body").get<SavedBody>();
		checkpoint_roster.accept(restored.id);
		auto entity = candidate.impl_->entities.at(restored.id);
		if (candidate.impl_->captured_by.contains(restored.id))
		{
			const auto owner = candidate.impl_->captured_by.at(restored.id);
			if (!candidate.impl_->entities.contains(owner) || owner == restored.id)
				throw std::runtime_error("Invalid capture owner");
			candidate.impl_->factions.validate(value.at("faction"));
			entity.set<FactionMembership>({value.at("faction")});
		}
		if (const auto *craft = entity.try_get<CraftState>();
		    craft && candidate.impl_->captured_by.contains(craft->home))
			entity.set<FactionMembership>(candidate.impl_->entities.at(craft->home).get<FactionMembership>());
		if (value.value("faction", std::string{}) !=
		    (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{}))
			throw std::runtime_error("Saved faction differs from fixed map membership");
		if (entity.has<Destructible>())
		{
			const auto d = value.at("destructible").get<Destructible>();
			if (Json(d.definition) != Json(entity.get<Destructible>().definition) || d.durability < 0 ||
			    d.durability > candidate.impl_->definitions.at(restored.definition).durability ||
			    d.destroyed_tick < -1 || d.destroyed_tick > saved.at("tick").get<std::int64_t>() ||
			    ((d.processed || d.rule_notified) && d.destroyed_tick < 0) || (d.processed && !d.rule_notified))
				throw std::runtime_error("Invalid destruction continuation");
			entity.set<Destructible>(d);
		}
		if (auto *stocks = entity.try_get_mut<ShipStockStores>())
		{
			const auto &saved_stocks = value.at("stocks");
			if (!saved_stocks.is_object() || saved_stocks.size() != stocks->stores.size())
				throw std::runtime_error("Saved stock roster mismatch");
			for (auto &[key, stock] : stocks->stores)
				stock.restore(saved_stocks.at(key));
		}
		else if (value.contains("stocks"))
			throw std::runtime_error("Unexpected saved stocks");
		if (entity.has<CombatStructure>())
			candidate.impl_->restore_combat(entity, value.at("combat"));
		if (entity.has<ShieldComponent>())
			candidate.impl_->restore_shields(entity, value.at("shields"));
		if (!candidate.impl_->definition(restored.id).personnel.officers.empty() && !value.contains("personnel"))
			throw std::runtime_error("Missing saved personnel");
		if (value.contains("personnel"))
			candidate.impl_->restore_personnel(restored.id, value.at("personnel"));
		if (value.contains("bridge"))
			candidate.impl_->restore_bridge(restored.id, value.at("bridge"));
		if (entity.has<EntityControls>())
		{
			candidate.impl_->restore_controllers(entity, value.at("console_controllers"));
		}
		else if (value.contains("console_controllers"))
			throw std::runtime_error("Unexpected saved Controller layout");

		validate_checkpoint_body(restored, entity.get<Definition>().name, entity.has<Movable>());
		const auto saved_altitude = value.value("body_altitude", std::int64_t{0});
		validate_checkpoint_altitude(saved_altitude);
		entity.set<Pose>({restored.position, restored.heading, saved_altitude, saved_altitude})
		    .set<Motion>({restored.velocity, restored.remainder});
		if (candidate.impl_->definition(restored.id).craft)
		{
			const auto v = value.at("vertical").get<VerticalMotion>();
			validate_checkpoint_vertical(v);
			entity.set<VerticalMotion>(v);
			entity.get_mut<Pose>().altitude = v.altitude;
			entity.get_mut<Pose>().previous_altitude = v.altitude;
			entity.get_mut<Motion>().vertical_velocity = v.velocity;
		}
		if (auto systems = fittings(entity))
		{
			auto states = value.at("systems");
			if (!states.is_object() || states.size() != systems->values.size())
				throw std::runtime_error("Fitted System roster mismatch");
			for (auto system : systems->values)
			{
				if (!states.at(system.id).contains("damping_remainder"))
					states.at(system.id)["damping_remainder"] = 0;
				for (const auto *field :
				     {"speed", "heading", "acceleration_remainder", "damping_remainder", "turn_remainder"})
					if (!states.at(system.id).at(field).is_number_integer())
						throw std::runtime_error("Fitted continuation requires integer state");
				const auto state = states.at(system.id).get<SavedOperation>();
				if (system.advanced)
				{
					SavedOperation expected;
					expected.heading = restored.heading;
					if (Json(expected) != states.at(system.id))
						throw std::runtime_error("Advanced System has unsupported legacy fields");
					continue;
				}
				if (Json(state) != states.at(system.id) ||
				    (!system.engine && (state.speed || state.acceleration_remainder || state.damping_remainder ||
				                        !state.engine_enabled)) ||
				    (!system.steering && (state.heading != restored.heading || state.turn_remainder)) ||
				    (!system.radar && !state.radar_enabled))
					throw std::runtime_error("Unsupported fitted System continuation fields");
				if (state.speed < 0 || state.heading < 0 || state.heading >= 360000 ||
				    state.acceleration_remainder < 0 || state.acceleration_remainder >= rate ||
				    state.damping_remainder < 0 || state.damping_remainder >= rate || state.turn_remainder < 0 ||
				    state.turn_remainder >= rate || (system.engine && state.speed > system.engine->max_speed))
					throw std::runtime_error("Invalid fitted System continuation");
				if (system.engine)
				{
					system.engine->speed = state.speed;
					system.engine->enabled = state.engine_enabled;
					system.engine->remainder = state.acceleration_remainder;
					system.engine->damping_remainder = state.damping_remainder;
				}
				if (system.steering)
				{
					system.steering->heading = state.heading;
					system.steering->remainder = state.turn_remainder;
				}
				if (system.radar)
					system.radar->enabled = state.radar_enabled;
			}
		}
		if (auto systems = gameplay_systems(entity))
		{
			const auto &data = value.at("gameplay");
			if (!data.is_array() || data.size() != systems->values.size())
				throw std::runtime_error("Advanced System roster mismatch");
			for (std::size_t i = 0; i < data.size(); ++i)
			{
				auto system = systems->values[i];
				const auto &state = data[i];
				if (state.at("id") != (*fittings(entity)).values[i].id ||
				    state.contains("flight") != (system.flight != nullptr) || state.contains("brain") ||
				    state.contains("timed") || state.contains("summary") || state.contains("effects"))
					throw std::runtime_error("Advanced continuation binding mismatch");
				if (system.flight)
				{
					system.flight->state = load_flight(system.flight->definition, state.at("flight"));
					if (system.flight->state.observed_tick > saved.at("tick").get<std::uint64_t>())
						throw std::runtime_error("Saved flight observation is in the future");
				}
				system.heat_remainder = state.at("heat_remainder");
				if (system.heat_remainder < 0 || system.heat_remainder >= rate)
					throw std::runtime_error("Invalid operating heat remainder");
				(*fittings(entity)).values[i].enabled = state.at("enabled");
				if (auto *ftl = entity.get<EquipmentIndex>().values.at(i).try_get_mut<FtlReserve>())
				{
					ftl->discharge_remainder =
					    projection::number(state.value("ftl_discharge_remainder", Json(0)), 0, rate - 1);
				}
				if (auto radar = (*fittings(entity)).values[i].radar)
					radar->enabled = state.at("radar_enabled").get<bool>();
			}
			if (systems->power)
				restore_ship_resources(*systems->power, value.at("resources"));
		}
	}
	{
		candidate.impl_->knowledge.restore(saved.at("knowledge"));
		if (saved.at("knowledge").at("ships").size() != candidate.impl_->entities.size())
			throw std::runtime_error("Knowledge/physical roster count mismatch");
		for (const auto &row : saved.at("knowledge").at("ships"))
		{
			const auto id = row.at("ship").get<EntityId>();
			if (!candidate.impl_->entities.contains(id) ||
			    row.at("definition") != candidate.impl_->entities.at(id).get<Definition>().name)
				throw std::runtime_error("Knowledge/physical roster mismatch");
		}
		for (const auto &entry : saved.at("away"))
		{
			auto a = entry.at("address").get<ConsoleAddress>();
			if (!candidate.impl_->entities.contains(a.ship) || !candidate.impl_->seat(a.ship, a.console))
				throw std::runtime_error("Unknown Away Console");
			candidate.impl_->ownership.set_away(a, entry.at("value").get<bool>());
		}
	}
	// Restore physical field entities and their index before display/Comms
	// reconstruction can capture a Console observation.
	if (saved.contains("asteroid_fields"))
		candidate.impl_->restore_fields(saved.at("asteroid_fields"));
	else
		candidate.impl_->stream_fields();
	candidate.impl_->index_contacts.run();
	for (const auto &[console, address] : saved.at("claims").get<std::map<std::uint64_t, ConsoleAddress>>())
	{
		const auto *seat = candidate.impl_->entities.contains(address.ship)
		                       ? candidate.impl_->seat(address.ship, address.console)
		                       : nullptr;
		if (!seat || !candidate.can_claim(console, address.ship, address.console, seat->human_seeking))
			throw std::runtime_error("Saved state invalid claim");
		if (!candidate.impl_->ownership.claim(console, address, seat->human_seeking))
			throw std::runtime_error("Saved state invalid claim");
	}

	candidate.impl_->display_revision =
	    static_cast<std::uint64_t>(display::number(saved.value("display_revision", Json(0)), 0, 9007199254740991LL));
	for (const auto &d : saved.value("displays", Json::array()))
	{
		const auto claimant = d.at("claimant").get<std::uint64_t>();
		const auto address = d.at("address").get<ConsoleAddress>();
		OperatingCommand command{address.ship, "display.toggle", 0, d.at("widget")};
		command.console = address.console;
		command.arguments = d.at("camera").get<std::vector<std::int64_t>>();
		if (!candidate.impl_->display_command(claimant, command, false) ||
		    candidate.impl_->displays.contains(address.ship))
			throw std::runtime_error("Invalid saved display source");
		const auto revision = display::number(d.at("revision"), 1, candidate.impl_->display_revision);
		candidate.impl_->displays[address.ship] = {claimant, static_cast<std::uint64_t>(revision), address,
		                                           command.system, command.arguments};
	}

	for (const auto &[id, entity] : candidate.impl_->entities)
		if (entity.has<EntityControls>())
			candidate.impl_->refresh_control(id, false);
	auto tick = saved.at("tick").get<std::int64_t>();
	if (tick < 0)
		throw std::runtime_error("Saved state invalid tick");
	candidate.impl_->ecs.get_mut<Clock>().tick = static_cast<std::uint64_t>(tick);
	candidate.impl_->environment_tick = saved.value("environment_tick", static_cast<std::uint64_t>(tick));
	candidate.impl_->refresh_comms();
	candidate.impl_->conversations.restore(saved.at("conversations"));
	candidate.impl_->restore_craft(saved.value("craft", Json()));
	candidate.impl_->restore_away(saved.value("away_missions", Json::array()));
	candidate.impl_->personnel_postings =
	    saved.value("personnel_postings", decltype(candidate.impl_->personnel_postings){});
	candidate.impl_->restore_interdictions(saved.value("interdictions", Json::array()), content);
	candidate.impl_->departures.restore_groups(saved.value("departure_groups", Json(DepartureGroups{})));
	std::set<EntityId> grouped_members;
	for (const auto &[leader, group] : candidate.impl_->departures.groups())
	{
		if (!candidate.impl_->entities.contains(leader) || !group.contains(leader) || group.size() > 32)
			throw std::runtime_error("Invalid departure group");
		for (auto member : group)
			if (!candidate.impl_->entities.contains(member) || !grouped_members.insert(member).second)
				throw std::runtime_error("Unknown or duplicate departure member");
	}
	candidate.impl_->discard_obsolete_encounter_departures();
	for (const auto &[leader, group] : candidate.impl_->departures.groups())
		for (auto member : group)
			if (!candidate.impl_->departure_member_authorised(leader, member))
				throw std::runtime_error("Unknown or duplicate departure member");
	candidate.impl_->validate_personnel_postings();

	for (const auto &[target, origin] : candidate.impl_->captured_by)
	{
		bool receipt = false;
		for (std::size_t i = 0; i < candidate.impl_->away_operations.definitions().size(); ++i)
			receipt |= candidate.impl_->away_operations.definitions()[i].target == target &&
			           candidate.impl_->operational_owner(
			               candidate.impl_->away_ship(candidate.impl_->away_operations.definitions()[i])) ==
			               candidate.impl_->operational_owner(origin) &&
			           candidate.impl_->away_operations.missions()[i].boarding.captured;
		if (!receipt)
			for (const auto &[ship, postings] : candidate.impl_->personnel_postings)
				for (const auto &[asset, posting] : postings)
					receipt |= posting.target == target && posting.status == "prize_crew" &&
					           candidate.impl_->operational_owner(ship) == candidate.impl_->operational_owner(origin);
		if (!receipt)
			for (std::size_t n = 0; n < candidate.impl_->interdictions.size(); ++n)
				receipt |= candidate.impl_->interdiction_definitions[n].target == target &&
				           candidate.impl_->interdictions[n].captured &&
				           std::find(candidate.impl_->interdictions[n].ships.begin(),
				                     candidate.impl_->interdictions[n].ships.end(),
				                     candidate.impl_->operational_owner(origin)) !=
				               candidate.impl_->interdictions[n].ships.end();
		if (!receipt)
			throw std::runtime_error("Capture without ownership receipt");
		(void)candidate.impl_->operational_owner(target);
	}
	for (const auto &[id, entity] : candidate.impl_->entities)
		if (!candidate.impl_->captured_by.contains(id))
		{
			if (entity.has<PersonnelState>())
				for (const auto &o : entity.get<PersonnelState>().officers)
					if (o.detained &&
					    !candidate.impl_->captured_posting(
					        id, "officer:" + candidate.impl_->definition(id)
					                             .personnel.officers[&o - entity.get<PersonnelState>().officers.data()]
					                             .id))
						throw std::runtime_error("Detention without capture");
			if (entity.has<EquipmentIndex>())
				for (auto child : entity.get<EquipmentIndex>().values)
					if (const auto *mechanism = child.try_get<CombatMechanism>();
					    mechanism && mechanism->definition.kind == OperatingSystem::Team &&
					    mechanism->get<TeamState>().detained &&
					    !candidate.impl_->captured_posting(id, "team:" + child.get<Equipment>().id))
						throw std::runtime_error("Team detention without capture");
		}
	candidate.impl_->validate_craft_transports();
	candidate.impl_->restore_docking(saved);
	candidate.impl_->validate_interdiction_physics();
	candidate.impl_->restore_logistics(saved);
	for (const auto &body : saved.at("bodies"))
	{
		auto entity = candidate.impl_->entities.at(body.at("body").at("id").get<EntityId>());
		if (entity.has<ShipProcessors>())
			candidate.impl_->restore_processors(entity, body.at("processors"));
		else if (body.contains("processors"))
			throw std::runtime_error("Unexpected saved processors");
	}
	candidate.impl_->validate_stock_claim_owners();
	candidate.impl_->mission_ledger = saved.value("mission_ledger", MissionLedger{});
	std::map<EntityId, Json> departure_reports;
	std::set<EntityId> departed_ships;
	for (const auto &value : saved.at("bodies"))
		if (value.contains("mission_report"))
		{
			const auto id = value.at("body").at("id").get<EntityId>();
			const auto report = ftl_projection::report(value.at("mission_report"));
			if (report.is_null() || report.at("tick") > saved.at("tick") ||
			    (report.at("reason") == "ftl" ? candidate.impl_->definition(id).ftl.empty()
			                                  : !candidate.impl_->truly_destroyed(id)))
				throw std::runtime_error("Invalid saved departure");
			departure_reports.emplace(id, report);
			departed_ships.insert(id);
		}
	candidate.impl_->suppress_custom_property_events = true;
	candidate.impl_->restore_custom_properties(saved.at("custom_properties"));
	candidate.impl_->restore_active_modifiers(saved.at("active_effects"));
	candidate.impl_->restore_abilities(saved.at("abilities"), departed_ships);
	candidate.impl_->restore_processes(saved.at("processes"), departed_ships);
	candidate.impl_->refresh_ability_work(false);
	candidate.impl_->restore_psychic(saved.value("psychic", Json::object()), departed_ships);
	candidate.impl_->restore_sensors(saved.value("sensors", Json::array()));
	candidate.impl_->validate_directed_commitments();
	candidate.impl_->sensor_findings.restore_discoveries(
	    saved.value("sensor_discoveries", SensorFindings::Discoveries{}));
	candidate.impl_->restore_conversation_starts(saved.at("conversation_starts"));
	candidate.impl_->restore_projectiles(saved);
	candidate.impl_->restore_rules(saved.at("rules"), saved.at("active_effects").at("conditions"),
	                               saved.at("active_effects").at("next_condition"));
	candidate.impl_->suppress_custom_property_events = false;

	candidate.impl_->blast_visuals = saved.value("blast_visuals", Json::array()).get<std::vector<BlastVisual>>();
	candidate.impl_->next_blast = saved.value("next_blast", std::uint64_t{1});
	for (const auto &[id, report] : departure_reports)
	{
		candidate.impl_->entities.at(id).set<MissionDeparture>({report});
		if (report.at("reason") == "ftl")
			candidate.impl_->retire_departed(id);
	}
	candidate.impl_->index_contacts.run();
	candidate.impl_->refresh_comms(false);
	candidate.impl_->validate_mission_ledger();
	candidate.impl_->validate_medical_state();
	for (const auto &[id, entity] : candidate.impl_->entities)
		if (entity.has<BridgeBlackboard>())
			candidate.impl_->reconcile_objective_priority(id);
	impl_ = std::move(candidate.impl_);
}
} // namespace phoenix
