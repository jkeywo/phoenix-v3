module;
#include <flecs.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.simulation;
import phoenix.content;
#include "body.hpp"
#include "spatial.hpp"

namespace phoenix::fixed
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec, x, y)
}
namespace phoenix
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TreeState, state, elapsed, entered)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConsoleAddress, ship, console)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SavedBody, id, definition, position, velocity, remainder, heading)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SavedOperation, speed, heading, acceleration_remainder, turn_remainder,
                                   engine_enabled, radar_enabled, tree)
namespace
{
constexpr std::int64_t rate = World::ticks_per_second;
struct Clock
{
	std::uint64_t tick = 0;
};
struct CompiledDefinition
{
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
	Collider collider;
	std::optional<Engine> engine;
	std::optional<Steering> steering;
	std::optional<Radar> radar;
	std::optional<std::size_t> controller;
	std::vector<ConsoleSpec> consoles;
	SystemControllers system_controllers;
	bool explicit_stations = false;
	std::optional<FittedSystems> fitted;
};
struct PendingCommand
{
	std::uint64_t claimant;
	EntityId target;
	TreeOutput output;
	std::string system;
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
	return {id,
	        definition.name,
	        pose.position.x,
	        pose.position.y,
	        motion.velocity.x,
	        motion.velocity.y,
	        pose.heading,
	        definition.radar_icon,
	        definition.radar_sprite};
}
std::optional<TreeOutput> decode(const OperatingCommand &command)
{
	if (auto spec = operating_action(command.action))
		return TreeOutput{spec->action, command.value};
	return {};
}
bool admissible(TreeOutput output, const Engine *engine, const Steering *steering, const Radar *radar)
{
	return operating_permitted(
	    output.action, output.value,
	    {engine ? std::optional{engine->max_speed} : std::nullopt, steering != nullptr, radar != nullptr});
}
void apply(TreeOutput output, Engine *engine, Steering *steering, Radar *radar)
{
	// Both controllers and admitted external commands use this capability gate.
	if (!admissible(output, engine, steering, radar))
		return;
	switch (output.action)
	{
	case TreeAction::Speed:
		engine->speed = output.value;
		break;
	case TreeAction::Heading:
		steering->heading = output.value;
		break;
	case TreeAction::EngineEnabled:
		engine->enabled = output.value != 0;
		break;
	case TreeAction::RadarEnabled:
		radar->enabled = output.value != 0;
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
	std::vector<TreeProgram> programs;
	std::string map;
	std::map<std::string, std::string> ships;
	std::map<std::string, EntityId> slot_entities;
	// This is a boundary lookup index, never the tick's iteration source.
	std::map<EntityId, flecs::entity> entities;
	std::map<std::uint64_t, ConsoleAddress> claims;
	bool explicit_stations = false;
	std::vector<PendingCommand> pending;
	std::vector<CollisionEntry> collisions;
	std::vector<ContactEntry> contact_rows;
	std::vector<Bounds> collision_boxes, contact_boxes;
	SpatialIndex collision_index, contact_index;
	flecs::query<const StableId, const Definition, const Pose, const Motion> bodies;
	flecs::system controllers, system_controllers, steering, engines, fitted_controllers, fitted_steering,
	    fitted_engines, integrate, resolve_collisions, index_contacts;
	bool instance_systems = false;

	Impl()
	{
		ecs.set<Clock>({0});
		bodies = ecs.query_builder<const StableId, const Definition, const Pose, const Motion>().cached().build();
		controllers = ecs.system<Controller, Engine *, Steering *, Radar *>("Controllers")
		                  .kind(0)
		                  .without<Claimed>()
		                  .each(
		                      [this](Controller &controller, Engine *engine, Steering *turn, Radar *radar)
		                      {
			                      for (auto output : step_tree(programs[controller.program], controller.cursor))
				                      apply(output, engine, turn, radar);
		                      });
		system_controllers =
		    ecs.system<SystemControllers, const ControlClaims, Engine *, Steering *, Radar *>("SystemControllers")
		        .kind(0)
		        .each(
		            [this](SystemControllers &controllers, const ControlClaims &claims, Engine *engine, Steering *turn,
		                   Radar *radar)
		            {
			            for (std::size_t i = 0; i < controllers.values.size(); ++i)
				            if (auto &controller = controllers.values[i]; controller && !(claims.held & (1u << i)))
					            for (auto output : step_tree(programs[controller->program], controller->cursor))
						            apply(output, engine, turn, radar);
		            });
		steering = ecs.system<Pose, Steering>("Steering")
		               .kind(0)
		               .with<Movable>()
		               .each(
		                   [](Pose &pose, Steering &turn)
		                   {
			                   auto available = turn.rate + turn.remainder;
			                   auto step = available / rate;
			                   turn.remainder = available % rate;
			                   auto difference = (turn.heading - pose.heading + 540000) % 360000 - 180000;
			                   pose.heading = (pose.heading + std::clamp(difference, -step, step) + 360000) % 360000;
		                   });
		engines = ecs.system<const Pose, Motion, Engine>("Engines").kind(0).with<Movable>().each(
		    [](const Pose &pose, Motion &motion, Engine &engine)
		    {
			    engine.forward_output = 0;
			    if (!engine.enabled)
				    return;
			    auto available = engine.acceleration + engine.remainder;
			    auto step = available / rate;
			    engine.remainder = available % rate;
			    auto desired = fixed::scale(fixed::direction(pose.heading), engine.speed);
			    auto delta = desired - motion.velocity;
			    auto magnitude = fixed::length(delta);
			    const auto before = motion.velocity;
			    motion.velocity =
			        magnitude <= step ? desired : motion.velocity + fixed::scale(fixed::normal(delta), step);
			    const auto applied = motion.velocity - before;
			    const auto forward = fixed::dot(applied, fixed::direction(pose.heading)) / fixed::unit;
			    engine.forward_output = static_cast<int>(std::clamp<std::int64_t>(
			        forward * rate * 1000 / std::max<std::int64_t>(1, engine.acceleration), 0, 1000));
		    });
		fitted_controllers = ecs.system<FittedSystems>("FittedControllers")
		                         .kind(0)
		                         .each(
		                             [this](FittedSystems &systems)
		                             {
			                             for (auto &system : systems.values)
				                             if (!system.held && system.controller)
					                             for (auto output : step_tree(programs[system.controller->program],
					                                                          system.controller->cursor))
						                             apply(output, system.engine ? &*system.engine : nullptr,
						                                   system.steering ? &*system.steering : nullptr,
						                                   system.radar ? &*system.radar : nullptr);
		                             });
		fitted_steering =
		    ecs.system<Pose, FittedSystems>("FittedSteering")
		        .kind(0)
		        .with<Movable>()
		        .each(
		            [](Pose &pose, FittedSystems &systems)
		            {
			            for (auto &system : systems.values)
				            if (system.steering)
				            {
					            auto &turn = *system.steering;
					            const auto available = turn.rate + turn.remainder;
					            turn.remainder = available % rate;
					            const auto difference = (turn.heading - pose.heading + 540000) % 360000 - 180000;
					            pose.heading = (pose.heading +
					                            std::clamp(difference, -available / rate, available / rate) + 360000) %
					                           360000;
				            }
		            });
		fitted_engines =
		    ecs.system<const Pose, Motion, FittedSystems>("FittedEngines")
		        .kind(0)
		        .with<Movable>()
		        .each(
		            [](const Pose &pose, Motion &motion, FittedSystems &systems)
		            {
			            for (auto &system : systems.values)
				            if (system.engine)
				            {
					            auto &engine = *system.engine;
					            engine.forward_output = 0;
					            if (!engine.enabled)
						            continue;
					            const auto available = engine.acceleration + engine.remainder;
					            const auto step = available / rate;
					            engine.remainder = available % rate;
					            const auto desired = fixed::scale(fixed::direction(pose.heading), engine.speed);
					            const auto delta = desired - motion.velocity;
					            const auto before = motion.velocity;
					            motion.velocity = fixed::length(delta) <= step
					                                  ? desired
					                                  : motion.velocity + fixed::scale(fixed::normal(delta), step);
					            const auto forward =
					                fixed::dot(motion.velocity - before, fixed::direction(pose.heading)) / fixed::unit;
					            engine.forward_output = static_cast<int>(std::clamp<std::int64_t>(
					                forward * rate * 1000 / std::max<std::int64_t>(1, engine.acceleration), 0, 1000));
				            }
		            });
		integrate = ecs.system<Pose, Motion>("Integrate")
		                .kind(0)
		                .with<Movable>()
		                .each(
		                    [](Pose &pose, Motion &motion)
		                    {
			                    motion.velocity.x = std::clamp(motion.velocity.x, -fixed::unit, fixed::unit);
			                    motion.velocity.y = std::clamp(motion.velocity.y, -fixed::unit, fixed::unit);
			                    motion.remainder = motion.remainder + motion.velocity;
			                    pose.position =
			                        pose.position + fixed::Vec{motion.remainder.x / rate, motion.remainder.y / rate};
			                    motion.remainder.x %= rate;
			                    motion.remainder.y %= rate;
			                    pose.position.x = std::clamp(pose.position.x, -fixed::unit, fixed::unit);
			                    pose.position.y = std::clamp(pose.position.y, -fixed::unit, fixed::unit);
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
					            collisions.push_back(
					                {ids[row].id, &poses[row], &motions[row], {poses[row], motions[row], shapes[row]}});
			            }
			            std::sort(collisions.begin(), collisions.end(),
			                      [](const auto &a, const auto &b)
			                      {
				                      return a.id < b.id;
			                      });
			            collision_boxes.clear();
			            for (const auto &entry : collisions)
				            collision_boxes.push_back(collision_bounds(entry.body));
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
						            collide(collisions[a].body, collisions[b].body);
						            collision_index.update(a, collision_bounds(collisions[a].body));
						            collision_index.update(b, collision_bounds(collisions[b].body));
						            begin = b + 1;
					            }
				            }
			            // No structural changes occur in this phase; pointers never escape it.
			            for (auto &entry : collisions)
			            {
				            entry.pose->position = entry.body.position;
				            entry.motion->velocity = entry.body.velocity;
			            }
			            collisions.clear();
		            });
		index_contacts = ecs.system<const StableId, const Pose>("IndexContacts")
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
					                         contact_rows.push_back({ids[row].id, it.entity(row), poses[row].position});
					                         contact_boxes.push_back(Bounds::around(poses[row].position, 0));
				                         }
			                         }
			                         contact_index.rebuild(contact_boxes);
		                         });
	}
	void compile(const Catalog &content)
	{
		packs = content.ordered_packs;
		std::map<std::string, std::size_t> tree_ids;
		for (const auto &[name, tree] : content.trees)
		{
			tree_ids.emplace(name, programs.size());
			programs.push_back(compile_tree(tree));
		}
		for (const auto &[name, definition] : content.entities)
		{
			CompiledDefinition result;
			result.radar_icon = render_radar_icon(definition.at("render"));
			result.radar_sprite = render_radar_sprite(definition.at("render"), content);
			result.consoles = resolve_consoles(content, name);
			result.explicit_stations = definition.contains("stations");
			if (definition.contains("controllers"))
				for (auto system : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
				{
					const std::string key(operating_system_name(system));
					if (definition["controllers"].contains(key))
						result.system_controllers.values[static_cast<std::size_t>(system)] =
						    Controller{tree_ids.at(definition["controllers"][key].get<std::string>()), {}};
				}
			const auto &collision = definition.at("collision");
			result.collider.mass = collision.at("mass_grams");
			result.collider.box = collision.at("shape") == "box";
			if (result.collider.box)
			{
				result.collider.half_width = collision.at("half_width_mm");
				result.collider.half_length = collision.at("half_length_mm");
			}
			else
				result.collider.radius = collision.at("radius_mm");
			const auto &systems = definition.at("systems");
			for (const auto &spec : resolve_systems(content, name))
				if (spec.id != operating_system_name(spec.kind) || systems.at(spec.id).contains("kind"))
					result.fitted.emplace();
			if (result.fitted)
			{
				if (definition.contains("controller"))
					throw std::runtime_error("Instance Systems require per-System controllers");
				for (const auto &spec : resolve_systems(content, name))
				{
					FittedRuntime runtime{spec.id, spec.kind};
					if (spec.kind == OperatingSystem::Engine)
						runtime.engine = Engine{spec.maximum, spec.response};
					else if (spec.kind == OperatingSystem::Steering)
						runtime.steering = Steering{spec.response};
					else
						runtime.radar = Radar{spec.maximum};
					if (!spec.controller.empty())
						runtime.controller = Controller{tree_ids.at(spec.controller), {}};
					result.fitted->values.push_back(std::move(runtime));
				}
			}
			if (!result.fitted && systems.contains("engine"))
				result.engine = Engine{systems["engine"]["max_speed_mm_s"], systems["engine"]["acceleration_mm_s2"]};
			if (!result.fitted && systems.contains("steering"))
				result.steering = Steering{systems["steering"]["turn_mdeg_s"]};
			if (!result.fitted && systems.contains("radar"))
				result.radar = Radar{systems["radar"]["range_mm"]};
			if (definition.contains("controller"))
				result.controller = tree_ids.at(definition.at("controller").get<std::string>());
			definitions.emplace(name, result);
		}
	}
	void create(EntityId id, const std::string &definition, const Json &transform)
	{
		const auto &data = definitions.at(definition);
		instance_systems |= data.fitted.has_value();
		explicit_stations |= data.explicit_stations;
		Pose pose{{transform.at("x_mm"), transform.at("y_mm")}, transform.at("heading_mdeg")};
		auto entity = ecs.entity()
		                  .set<StableId>({id})
		                  .set<Definition>({definition, data.radar_icon, data.radar_sprite})
		                  .set<Pose>(pose)
		                  .set<Motion>({})
		                  .set<Collider>(data.collider);
		if (data.collider.mass)
			entity.add<Movable>();
		if (data.fitted)
		{
			auto fitted = *data.fitted;
			for (auto &system : fitted.values)
				if (system.steering)
					system.steering->heading = pose.heading;
			entity.set<FittedSystems>(std::move(fitted));
		}
		if (!data.fitted && data.engine)
			entity.set<Engine>(*data.engine);
		if (!data.fitted && data.steering)
		{
			auto turn = *data.steering;
			turn.heading = pose.heading;
			entity.set<Steering>(turn);
		}
		if (!data.fitted && data.radar)
			entity.set<Radar>(*data.radar);
		if (data.controller)
			entity.set<Controller>({*data.controller, {}});
		if (data.explicit_stations && !data.fitted)
			entity.set<SystemControllers>(data.system_controllers).set<ControlClaims>({});
		entities.emplace(id, entity);
	}
	const CompiledDefinition &definition(EntityId ship) const
	{
		return definitions.at(entities.at(ship).get<Definition>().name);
	}
	const ConsoleSpec *seat(EntityId ship, const std::string &name) const
	{
		const auto &seats = definition(ship).consoles;
		for (const auto &seat : seats)
			if (seat.id == name || (name.empty() && seats.size() == 1))
				return &seat;
		return nullptr;
	}
	FittedRuntime *instance(flecs::entity entity, const std::string &id) const
	{
		if (auto systems = entity.try_get_mut<FittedSystems>())
			for (auto &system : systems->values)
				if (system.id == id)
					return &system;
		return nullptr;
	}
	std::optional<std::string> target(EntityId ship, const ConsoleSpec &seat, OperatingSystem kind,
	                                  const std::string &requested) const
	{
		std::optional<std::string> result;
		const auto entity = entities.at(ship);
		for (const auto &id : seat.controlled_systems)
		{
			const auto *runtime = instance(entity, id);
			const auto type = runtime            ? runtime->kind
			                  : id == "engine"   ? OperatingSystem::Engine
			                  : id == "steering" ? OperatingSystem::Steering
			                                     : OperatingSystem::Radar;
			if (type == kind && (requested.empty() || id == requested))
			{
				if (result)
					return {};
				result = id;
			}
		}
		return result;
	}
	SavedOperation operation(flecs::entity entity) const
	{
		SavedOperation result;
		result.heading = entity.get<Pose>().heading;
		if (auto legacy = entity.try_get<LegacyOperation>())
			result = legacy->value;
		if (auto engine = entity.try_get<Engine>())
		{
			result.speed = engine->speed;
			result.acceleration_remainder = engine->remainder;
			result.engine_enabled = engine->enabled;
		}
		if (auto turn = entity.try_get<Steering>())
		{
			result.heading = turn->heading;
			result.turn_remainder = turn->remainder;
		}
		if (auto radar = entity.try_get<Radar>())
			result.radar_enabled = radar->enabled;
		if (auto controller = entity.try_get<Controller>())
			result.tree = save_tree(programs[controller->program], controller->cursor);
		return result;
	}
	void restore_operation(flecs::entity entity, const SavedOperation &operation)
	{
		// Old schema-1 saves allowed inactive fields even without fitted systems.
		// Retain them only at this cold compatibility boundary.
		if ((!entity.has<Engine>() &&
		     (operation.speed || operation.acceleration_remainder || !operation.engine_enabled)) ||
		    (!entity.has<Steering>() &&
		     (operation.heading != entity.get<Pose>().heading || operation.turn_remainder)) ||
		    (!entity.has<Radar>() && !operation.radar_enabled) ||
		    (!entity.has<Controller>() &&
		     (!operation.tree.state.empty() || operation.tree.elapsed || operation.tree.entered)))
			entity.set<LegacyOperation>({operation});
		if (auto engine = entity.try_get_mut<Engine>())
		{
			engine->speed = operation.speed;
			engine->remainder = operation.acceleration_remainder;
			engine->enabled = operation.engine_enabled;
		}
		if (auto turn = entity.try_get_mut<Steering>())
		{
			turn->heading = operation.heading;
			turn->remainder = operation.turn_remainder;
		}
		if (auto radar = entity.try_get_mut<Radar>())
			radar->enabled = operation.radar_enabled;
		if (auto controller = entity.try_get_mut<Controller>())
			controller->cursor = load_tree(programs[controller->program], operation.tree);
	}
};
World::World() : impl_(std::make_unique<Impl>())
{
}
World::~World() = default;
void World::load(const Catalog &content, const std::string &map, const std::map<std::string, std::string> &ships)
{
	verify_catalog(content);
	if (content.presentation)
		throw std::runtime_error("Console presentation assets cannot create an authoritative world");
	if (!content.maps.contains(map))
		throw std::runtime_error("Unknown map: " + map);
	auto candidate = std::make_unique<Impl>();
	candidate->compile(content);
	candidate->map = map;
	candidate->ships = ships;
	const auto &data = content.maps.at(map);
	EntityId next = 1;
	for (const auto &instance : data.at("entities"))
	{
		EntityId id = instance.at("id");
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
	candidate->index_contacts.run();
	impl_ = std::move(candidate);
}
std::vector<ConsoleSpec> World::consoles(EntityId ship) const
{
	return impl_->entities.contains(ship) ? impl_->definition(ship).consoles : std::vector<ConsoleSpec>{};
}
bool World::can_claim(std::uint64_t console, EntityId ship, const std::string &name) const
{
	if (!console || impl_->claims.contains(console) || !impl_->entities.contains(ship))
		return false;
	auto seat = impl_->seat(ship, name);
	if (!seat)
		return false;
	for (const auto &[owner, address] : impl_->claims)
		if (address.ship == ship && address.console == seat->id)
			return false;
	auto entity = impl_->entities.at(ship);
	return impl_->definition(ship).explicit_stations || entity.has<FittedSystems>() ||
	       (entity.has<Movable>() && entity.has<Engine>() && entity.has<Steering>());
}
bool World::claim(std::uint64_t console, EntityId ship, const std::string &name)
{
	if (!can_claim(console, ship, name))
		return false;
	const auto &seat = *impl_->seat(ship, name);
	impl_->claims[console] = {ship, seat.id};
	auto entity = impl_->entities.at(ship);
	if (auto fitted = entity.try_get_mut<FittedSystems>())
	{
		for (auto &system : fitted->values)
			system.held |= std::find(seat.controlled_systems.begin(), seat.controlled_systems.end(), system.id) !=
			               seat.controlled_systems.end();
	}
	else if (auto claims = entity.try_get_mut<ControlClaims>())
		claims->held |= seat.controls;
	else
		entity.add<Claimed>();
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
	auto found = impl_->claims.find(console);
	if (found == impl_->claims.end())
		return false;
	auto entity = impl_->entities.at(found->second.ship);
	const auto &seat = *impl_->seat(found->second.ship, found->second.console);
	if (auto fitted = entity.try_get_mut<FittedSystems>())
	{
		for (auto &system : fitted->values)
			if (std::find(seat.controlled_systems.begin(), seat.controlled_systems.end(), system.id) !=
			    seat.controlled_systems.end())
				system.held = false;
	}
	else if (auto claims = entity.try_get_mut<ControlClaims>())
	{
		claims->held &= static_cast<SystemMask>(~seat.controls);
		// Only controllers for this Console restart; other Consoles keep their state.
		auto &controllers = entity.get_mut<SystemControllers>();
		for (std::size_t i = 0; i < controllers.values.size(); ++i)
			if ((seat.controls & (1u << i)) && controllers.values[i])
				controllers.values[i]->cursor = {};
	}
	else
	{
		entity.get_mut<Engine>().speed = 0;
		entity.get_mut<Steering>().heading = entity.get<Pose>().heading;
	}
	if (auto controller = entity.try_get_mut<Controller>())
		controller->cursor = {};
	if (auto legacy = entity.try_get_mut<LegacyOperation>())
		legacy->value.tree = {};
	impl_->pending.erase(std::remove_if(impl_->pending.begin(), impl_->pending.end(),
	                                    [&](const auto &command)
	                                    {
		                                    return command.claimant == console;
	                                    }),
	                     impl_->pending.end());
	entity.remove<Claimed>();
	impl_->claims.erase(found);
	return true;
}
bool World::can_submit(std::uint64_t console, const OperatingCommand &command) const
{
	auto claim = impl_->claims.find(console);
	if (claim == impl_->claims.end() || claim->second.ship != command.target || impl_->pending.size() >= 4096)
		return false;
	auto output = decode(command);
	if (!output)
		return false;
	const auto system = operating_action(output->action).system;
	const auto target =
	    impl_->target(command.target, *impl_->seat(command.target, claim->second.console), system, command.system);
	if (!target)
		return false;
	auto entity = impl_->entities.at(command.target);
	if (auto instance = impl_->instance(entity, *target))
		return admissible(*output, instance->engine ? &*instance->engine : nullptr,
		                  instance->steering ? &*instance->steering : nullptr,
		                  instance->radar ? &*instance->radar : nullptr);
	return output && admissible(*output, entity.try_get<Engine>(), entity.try_get<Steering>(), entity.try_get<Radar>());
}
bool World::submit(std::uint64_t console, const OperatingCommand &command)
{
	if (!can_submit(console, command))
		return false;
	const auto output = *decode(command);
	const auto &seat = *impl_->seat(command.target, impl_->claims.at(console).console);
	impl_->pending.push_back(
	    {console, command.target, output,
	     *impl_->target(command.target, seat, operating_action(output.action).system, command.system)});
	return true;
}
void World::clear_claims()
{
	for (const auto &[console, address] : impl_->claims)
	{
		auto entity = impl_->entities.at(address.ship);
		entity.remove<Claimed>();
		if (auto claims = entity.try_get_mut<ControlClaims>())
			claims->held = 0;
		if (auto systems = entity.try_get_mut<FittedSystems>())
			for (auto &system : systems->values)
				system.held = false;
	}
	impl_->claims.clear();
	impl_->pending.clear();
}
void World::tick()
{
	for (const auto &command : impl_->pending)
	{
		auto entity = impl_->entities.at(command.target);
		if (auto instance = impl_->instance(entity, command.system))
			apply(command.output, instance->engine ? &*instance->engine : nullptr,
			      instance->steering ? &*instance->steering : nullptr, instance->radar ? &*instance->radar : nullptr);
		else
			apply(command.output, entity.try_get_mut<Engine>(), entity.try_get_mut<Steering>(),
			      entity.try_get_mut<Radar>());
	}
	impl_->pending.clear();
	// Explicit fixed-tick phases. Each pre-collision row operates only on itself,
	// so archetype/table ordering cannot affect gameplay. No wall-clock progress.
	impl_->controllers.run();
	impl_->system_controllers.run();
	impl_->fitted_controllers.run();
	impl_->steering.run();
	impl_->fitted_steering.run();
	impl_->engines.run();
	impl_->fitted_engines.run();
	impl_->integrate.run();
	impl_->resolve_collisions.run();
	impl_->index_contacts.run();
	++impl_->ecs.get_mut<Clock>().tick;
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
		if (auto engine = impl_->entities.at(body.id).try_get<Engine>())
			item.forward_thrust = engine->enabled ? engine->forward_output : 0;
		if (auto systems = impl_->entities.at(body.id).try_get<FittedSystems>())
			for (const auto &system : systems->values)
				if (system.engine && system.engine->enabled)
					item.forward_thrust = std::max(item.forward_thrust, system.engine->forward_output);
		result.push_back(std::move(item));
	}
	return result;
}
std::optional<Observation> World::observe(std::uint64_t console) const
{
	auto found = impl_->claims.find(console);
	if (found == impl_->claims.end())
		return {};
	auto entity = impl_->entities.at(found->second.ship);
	const auto &own = entity.get<Pose>();
	const auto &seat = *impl_->seat(found->second.ship, found->second.console);
	Observation result{view(found->second.ship, entity.get<Definition>(), own, entity.get<Motion>()), {}};
	result.console = seat.id;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.engine_enabled = false;
	result.radar_enabled = false;
	for (const auto &id : seat.feed_systems)
	{
		const auto *instance = impl_->instance(entity, id);
		SystemView state;
		state.id = id;
		state.controlled = std::find(seat.controlled_systems.begin(), seat.controlled_systems.end(), id) !=
		                   seat.controlled_systems.end();
		const auto *engine = instance         ? (instance->engine ? &*instance->engine : nullptr)
		                     : id == "engine" ? entity.try_get<Engine>()
		                                      : nullptr;
		const auto *turn = instance           ? (instance->steering ? &*instance->steering : nullptr)
		                   : id == "steering" ? entity.try_get<Steering>()
		                                      : nullptr;
		const auto *radar = instance        ? (instance->radar ? &*instance->radar : nullptr)
		                    : id == "radar" ? entity.try_get<Radar>()
		                                    : nullptr;
		if (engine)
		{
			state.kind = OperatingSystem::Engine;
			state.requested = engine->speed;
			state.maximum = engine->max_speed;
			state.enabled = engine->enabled;
		}
		else if (turn)
		{
			state.kind = OperatingSystem::Steering;
			state.requested = turn->heading;
			state.maximum = 359999;
		}
		else if (radar)
		{
			state.kind = OperatingSystem::Radar;
			state.maximum = radar->range;
			state.enabled = radar->enabled;
		}
		result.systems.push_back(std::move(state));
	}
	if (entity.has<FittedSystems>())
	{
		std::int64_t range = 0;
		for (const auto &state : result.systems)
		{
			if (state.kind == OperatingSystem::Engine)
			{
				result.requested_speed = state.requested;
				result.engine_enabled = state.enabled;
			}
			else if (state.kind == OperatingSystem::Steering)
				result.requested_heading = state.requested;
			else if (state.kind == OperatingSystem::Radar && state.enabled)
				range = std::max(range, state.maximum);
		}
		result.radar_enabled = range > 0;
		if (range)
			impl_->contact_index.each(
			    Bounds::around(own.position, range),
			    [&](std::size_t row)
			    {
				    const auto &contact = impl_->contact_rows[row];
				    if (contact.id != found->second.ship && fixed::length(contact.position - own.position) <= range)
					    result.contacts.push_back(view(contact.id, contact.entity.get<Definition>(),
					                                   contact.entity.get<Pose>(), contact.entity.get<Motion>()));
			    });
		std::sort(result.contacts.begin(), result.contacts.end(),
		          [](const auto &a, const auto &b)
		          {
			          return a.id < b.id;
		          });
		return result;
	}
	if (auto engine = entity.try_get<Engine>(); engine && (seat.feeds & system_bit(OperatingSystem::Engine)))
	{
		result.requested_speed = engine->speed;
		result.engine_enabled = engine->enabled;
	}
	if (auto turn = entity.try_get<Steering>(); turn && (seat.feeds & system_bit(OperatingSystem::Steering)))
		result.requested_heading = turn->heading;
	if (!(seat.feeds & system_bit(OperatingSystem::Radar)))
		return result;
	auto radar = entity.try_get<Radar>();
	result.radar_enabled = radar ? radar->enabled : impl_->operation(entity).radar_enabled;
	if (!radar || !radar->enabled)
		return result;
	impl_->contact_index.each(
	    Bounds::around(own.position, radar->range),
	    [&](std::size_t row)
	    {
		    const auto &contact = impl_->contact_rows[row];
		    if (contact.id != found->second.ship && fixed::length(contact.position - own.position) <= radar->range)
			    result.contacts.push_back(view(contact.id, contact.entity.get<Definition>(), contact.entity.get<Pose>(),
			                                   contact.entity.get<Motion>()));
	    });
	std::sort(result.contacts.begin(), result.contacts.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	return result;
}
Json World::save() const
{
	if (!impl_->pending.empty())
		throw std::runtime_error("Save at a tick boundary after admitted commands execute");
	Json result{{"schema", 1},           {"tick", ticks()},       {"map", impl_->map},
	            {"ships", impl_->ships}, {"packs", impl_->packs}, {"bodies", Json::array()}};
	if (impl_->explicit_stations || impl_->instance_systems)
	{
		result["schema"] = impl_->instance_systems ? 3 : 2;
		result["claims"] = impl_->claims;
	}
	else
	{
		std::map<std::uint64_t, EntityId> legacy;
		for (const auto &[owner, address] : impl_->claims)
			legacy[owner] = address.ship;
		result["claims"] = legacy;
	}
	for (const auto &[id, entity] : impl_->entities)
	{
		SavedBody body{entity.get<Pose>(), entity.get<Motion>(), id, entity.get<Definition>().name};
		result["bodies"].push_back({{"body", body}, {"operation", impl_->operation(entity)}});
		if (auto systems = entity.try_get<FittedSystems>())
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
				}
				if (system.steering)
				{
					operation.heading = system.steering->heading;
					operation.turn_remainder = system.steering->remainder;
				}
				if (system.radar)
					operation.radar_enabled = system.radar->enabled;
				if (system.controller)
					operation.tree = save_tree(impl_->programs[system.controller->program], system.controller->cursor);
				saved[system.id] = operation;
			}
		}
		if (auto controllers = entity.try_get<SystemControllers>())
		{
			auto &saved = result["bodies"].back()["controllers"] = Json::object();
			for (std::size_t i = 0; i < controllers->values.size(); ++i)
				if (const auto &controller = controllers->values[i])
					saved[std::string(operating_system_name(static_cast<OperatingSystem>(i)))] =
					    save_tree(impl_->programs[controller->program], controller->cursor);
		}
	}
	return result;
}
void World::restore(const Catalog &content, const Json &saved)
{
	if ((saved.at("schema") != 1 && saved.at("schema") != 2 && saved.at("schema") != 3) ||
	    saved.at("packs") != Json(content.ordered_packs))
		throw std::runtime_error("Saved state schema/content identity mismatch");
	World candidate;
	candidate.load(content, saved.at("map"), saved.at("ships").get<std::map<std::string, std::string>>());
	if ((candidate.impl_->instance_systems && saved.at("schema") != 3) ||
	    (candidate.impl_->explicit_stations && saved.at("schema") == 1))
		throw std::runtime_error("Explicit Stations require a schema-2 save");
	if (!saved.at("bodies").is_array() || saved.at("bodies").size() != candidate.impl_->entities.size())
		throw std::runtime_error("Saved state entity roster mismatch");
	std::set<EntityId> seen;
	for (const auto &value : saved.at("bodies"))
	{
		auto restored = value.at("body").get<SavedBody>();
		auto operation = value.at("operation").get<SavedOperation>();
		if (!seen.insert(restored.id).second || !candidate.impl_->entities.contains(restored.id))
			throw std::runtime_error("Saved state duplicate/unknown entity");
		auto entity = candidate.impl_->entities.at(restored.id);
		if (restored.definition != entity.get<Definition>().name)
			throw std::runtime_error("Saved state definition mismatch");
		if (!entity.has<Movable>() && (restored.velocity.x || restored.velocity.y))
			throw std::runtime_error("Saved static entity cannot have velocity");
		for (auto v : {restored.position.x, restored.position.y, restored.velocity.x, restored.velocity.y})
			if (v < -fixed::unit || v > fixed::unit)
				throw std::runtime_error("Saved state position/velocity outside supported bounds");
		if (restored.heading < 0 || restored.heading >= 360000 || restored.remainder.x <= -rate ||
		    restored.remainder.x >= rate || restored.remainder.y <= -rate || restored.remainder.y >= rate ||
		    operation.heading < 0 || operation.heading >= 360000 || operation.acceleration_remainder < 0 ||
		    operation.acceleration_remainder >= rate || operation.turn_remainder < 0 ||
		    operation.turn_remainder >= rate)
			throw std::runtime_error("Saved state invalid numeric remainder/heading");
		auto engine = entity.try_get<Engine>();
		if (operation.speed < 0 || operation.speed > fixed::unit || (engine && operation.speed > engine->max_speed))
			throw std::runtime_error("Saved state invalid speed request");
		entity.set<Pose>({restored.position, restored.heading}).set<Motion>({restored.velocity, restored.remainder});
		if (entity.has<FittedSystems>() &&
		    (operation.speed || operation.acceleration_remainder || operation.turn_remainder ||
		     operation.heading != restored.heading || !operation.engine_enabled || !operation.radar_enabled ||
		     !operation.tree.state.empty() || operation.tree.elapsed || operation.tree.entered))
			throw std::runtime_error("Instance ships cannot carry inactive legacy operation state");
		candidate.impl_->restore_operation(entity, operation);
		if (auto systems = entity.try_get_mut<FittedSystems>())
		{
			const auto &states = value.at("systems");
			if (!states.is_object() || states.size() != systems->values.size())
				throw std::runtime_error("Fitted System roster mismatch");
			for (auto &system : systems->values)
			{
				for (const auto *field : {"speed", "heading", "acceleration_remainder", "turn_remainder"})
					if (!states.at(system.id).at(field).is_number_integer())
						throw std::runtime_error("Fitted continuation requires integer state");
				const auto state = states.at(system.id).get<SavedOperation>();
				if (Json(state) != states.at(system.id) ||
				    (!system.engine && (state.speed || state.acceleration_remainder || !state.engine_enabled)) ||
				    (!system.steering && (state.heading != restored.heading || state.turn_remainder)) ||
				    (!system.radar && !state.radar_enabled))
					throw std::runtime_error("Unsupported fitted System continuation fields");
				if (state.speed < 0 || state.heading < 0 || state.heading >= 360000 ||
				    state.acceleration_remainder < 0 || state.acceleration_remainder >= rate ||
				    state.turn_remainder < 0 || state.turn_remainder >= rate ||
				    (system.engine && state.speed > system.engine->max_speed))
					throw std::runtime_error("Invalid fitted System continuation");
				if (system.engine)
				{
					system.engine->speed = state.speed;
					system.engine->enabled = state.engine_enabled;
					system.engine->remainder = state.acceleration_remainder;
				}
				if (system.steering)
				{
					system.steering->heading = state.heading;
					system.steering->remainder = state.turn_remainder;
				}
				if (system.radar)
					system.radar->enabled = state.radar_enabled;
				if (system.controller)
					system.controller->cursor =
					    load_tree(candidate.impl_->programs[system.controller->program], state.tree);
				else if (!state.tree.state.empty() || state.tree.elapsed || state.tree.entered)
					throw std::runtime_error("Unfitted controller continuation");
			}
		}
		if (auto controllers = entity.try_get_mut<SystemControllers>())
		{
			const auto &states = value.at("controllers");
			if (!states.is_object())
				throw std::runtime_error("Invalid System controller states");
			std::size_t count = 0;
			for (std::size_t i = 0; i < controllers->values.size(); ++i)
				if (auto &controller = controllers->values[i])
				{
					++count;
					controller->cursor =
					    load_tree(candidate.impl_->programs[controller->program],
					              states.at(std::string(operating_system_name(static_cast<OperatingSystem>(i))))
					                  .get<TreeState>());
				}
			if (states.size() != count)
				throw std::runtime_error("System controller roster mismatch");
		}
	}
	if (saved.at("schema") == 1)
	{
		for (const auto &[console, ship] : saved.at("claims").get<std::map<std::uint64_t, EntityId>>())
			if (!candidate.claim(console, ship))
				throw std::runtime_error("Saved state invalid claim");
	}
	else
	{
		for (const auto &[console, address] : saved.at("claims").get<std::map<std::uint64_t, ConsoleAddress>>())
			if (!candidate.claim(console, address.ship, address.console))
				throw std::runtime_error("Saved state invalid claim");
	}
	auto tick = saved.at("tick").get<std::int64_t>();
	if (tick < 0)
		throw std::runtime_error("Saved state invalid tick");
	candidate.impl_->ecs.get_mut<Clock>().tick = static_cast<std::uint64_t>(tick);
	candidate.impl_->index_contacts.run();
	impl_ = std::move(candidate.impl_);
}
} // namespace phoenix
