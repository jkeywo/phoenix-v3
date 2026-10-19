module;
#include <flecs.h>
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
#include <vector>

module phoenix.simulation;
import phoenix.content;
import phoenix.gameplay_scenario;
#include "body.hpp"
#include "spatial.hpp"
#include "gameplay_knowledge.hpp"
#include "gameplay_resources.hpp"
#include "flight.hpp"
#include "equipment.hpp"
#include "controllers.hpp"

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
void step_basic_steering(Pose &pose, Steering &turn)
{
	auto available = turn.rate + turn.remainder;
	auto step = available / rate;
	turn.remainder = available % rate;
	auto difference = (turn.heading - pose.heading + 540000) % 360000 - 180000;
	pose.heading = (pose.heading + std::clamp(difference, -step, step) + 360000) % 360000;
}
void step_basic_engine(const Pose &pose, Motion &motion, Engine &engine)
{
	engine.forward_output = 0;
	if (!engine.enabled)
		return;
	const auto forward_speed = fixed::dot(motion.velocity, fixed::direction(pose.heading)) / fixed::unit;
	const auto acceleration = engine.speed > forward_speed && forward_speed >= 0
	                              ? launch_acceleration(engine.acceleration, forward_speed, engine.max_speed)
	                              : engine.acceleration;
	auto available = acceleration + engine.remainder;
	auto step = available / rate;
	engine.remainder = available % rate;
	auto desired = fixed::scale(fixed::direction(pose.heading), engine.speed);
	auto delta = desired - motion.velocity;
	auto magnitude = fixed::length(delta);
	const auto before = motion.velocity;
	motion.velocity = magnitude <= step ? desired : motion.velocity + fixed::scale(fixed::normal(delta), step);
	const auto applied = motion.velocity - before;
	const auto forward = fixed::dot(applied, fixed::direction(pose.heading)) / fixed::unit;
	engine.forward_output = static_cast<int>(
	    std::clamp<std::int64_t>(forward * rate * 1000 / std::max<std::int64_t>(1, engine.acceleration), 0, 1000));
}

struct Clock
{
	std::uint64_t tick = 0;
};
struct CompiledGameplaySystem
{
	std::optional<FlightDefinition> flight;
	std::optional<state_tree::Program> brain;
	std::string tree;
	std::vector<RecordReadBinding> reads;
	std::vector<GameplayTreeSummaryBinding> summary_reads;
};
struct CompiledGameplay
{
	std::vector<CompiledGameplaySystem> values;
	std::optional<ShipResourcesDefinition> power;
};
struct CompiledDefinition
{
	std::optional<ControllableLayout> layout;
	std::vector<ConsoleControllerRuntime> controller_templates;
	std::map<std::string, std::string> observation_owners;
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
	Collider collider;
	std::optional<Engine> engine;
	std::optional<Steering> steering;
	std::optional<Radar> radar;
	std::optional<std::size_t> controller;
	std::vector<ConsoleSpec> consoles;
	CompiledControllers system_controllers;
	bool explicit_stations = false;
	std::optional<CompiledFittings> fitted;
	std::optional<CompiledGameplay> gameplay;
	std::vector<std::string> publications;
	std::vector<std::string> message_templates;
	struct Summary
	{
		std::string system, channel;
		std::vector<std::int64_t> levels;
		std::vector<std::string> labels;
		std::optional<std::int64_t> initial;
	};
	struct Preset
	{
		std::string id;
		std::map<std::string, std::vector<std::string>> delegated;
		std::map<std::string, std::vector<Summary>> summaries;
	};
	std::vector<Preset> presets;
	std::string default_preset;
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
	std::vector<TreeProgram> programs;
	std::string map;
	std::map<std::string, std::string> ships;
	std::map<std::string, EntityId> slot_entities;
	// This is a boundary lookup index, never the tick's iteration source.
	std::map<EntityId, flecs::entity> entities;
	std::map<std::uint64_t, ConsoleAddress> claims;
	bool explicit_stations = false;
	std::vector<PendingCommand> pending;
	std::vector<ControllerProposal> controller_pending;
	std::vector<CollisionEntry> collisions;
	std::vector<ContactEntry> contact_rows;
	std::vector<Bounds> collision_boxes, contact_boxes;
	SpatialIndex collision_index, contact_index;
	flecs::query<const StableId, const Definition, const Pose, const Motion> bodies;
	flecs::query<const Equipment, Controller> basic_controller_query;
	flecs::query<const Equipment, Steering> steering_query;
	flecs::query<const Equipment, Engine> engine_query;
	flecs::query<const Equipment, Controller *, BrainRuntime *> advanced_controller_query;
	flecs::query<const Equipment, FlightRuntime> flight_query;
	flecs::query<const Equipment> advanced_equipment_query;
	flecs::query<const Equipment, const ResourceBinding> passive_resource_query;
	flecs::query<const Equipment, const BrainRuntime> autonomous_equipment;
	flecs::query<const StableId, const GameplayShip> advanced_ships;
	flecs::system controllers, integrate, resolve_collisions, index_contacts;
	bool instance_systems = false;
	GameplayKnowledge knowledge;
	std::optional<GameplayScenarioBridge> scenario;
	EntityId next_entity = 1;
	std::map<EntityId, fixed::Vec> step_starts;
	std::map<ConsoleAddress, GameplayKnowledgeProjection> brain_snapshots;
	std::map<ConsoleAddress, Observation> observation_snapshots;
	std::map<EntityId, std::vector<std::vector<std::int64_t>>> command_snapshots;
	std::map<ControllerAddress, std::vector<CoordinationDelivery>> controller_inboxes;
	std::map<std::pair<EntityId, std::string>, std::vector<CoordinationDelivery>> inbox_snapshots;
	bool gameplay = false;
	std::vector<std::pair<std::uint64_t, OperatingCommand>> meta_pending;
	std::map<ConsoleAddress, bool> away; // Boundary control policy, never physical state.
	std::map<EntityId, std::set<std::string>> delegated;
	std::map<ConsoleAddress, std::string> preset;

	Impl()
	{
		ecs.set<Clock>({0});
		bodies = ecs.query_builder<const StableId, const Definition, const Pose, const Motion>().cached().build();
		basic_controller_query = ecs.query_builder<const Equipment, Controller>()
		                             .with<BasicEquipment>()
		                             .with(flecs::ChildOf)
		                             .second("$ship")
		                             .cached()
		                             .build();
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
		advanced_controller_query = ecs.query_builder<const Equipment, Controller *, BrainRuntime *>()
		                                .with<AdvancedEquipment>()
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
		passive_resource_query = ecs.query_builder<const Equipment, const ResourceBinding>()
		                             .without<FlightRuntime>()
		                             .with(flecs::ChildOf)
		                             .second("$ship")
		                             .cached()
		                             .build();
		autonomous_equipment = ecs.query_builder<const Equipment, const BrainRuntime>()
		                           .with(flecs::ChildOf)
		                           .second("$ship")
		                           .cached()
		                           .build();
		advanced_ships = ecs.query_builder<const StableId, const GameplayShip>().cached().build();
		controllers = ecs.system<Controller>("ShipControllers")
		                  .kind(0)
		                  .without<Equipment>()
		                  .without<Claimed>()
		                  .each(
		                      [this](flecs::entity ship, Controller &controller)
		                      {
			                      for (auto output : step_tree(programs[controller.program], controller.cursor))
				                      apply(output, legacy_component<Engine>(ship), legacy_component<Steering>(ship),
				                            legacy_component<Radar>(ship));
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
					            if (step_starts.contains(collisions[a].id) && step_starts.contains(collisions[b].id))
						            if (auto time = sweep_contact_time(collisions[a].body, collisions[b].body,
						                                               step_starts.at(collisions[a].id),
						                                               step_starts.at(collisions[b].id)))
							            swept.push_back({a, b, *time});
			            std::sort(swept.begin(), swept.end(),
			                      [](const auto &a, const auto &b)
			                      {
				                      return a.time < b.time ||
				                             (a.time == b.time && std::pair{a.a, a.b} < std::pair{b.a, b.b});
			                      });
			            for (const auto &pair : swept)
				            sweep_collide(collisions[pair.a].body, collisions[pair.b].body,
				                          step_starts.at(collisions[pair.a].id), step_starts.at(collisions[pair.b].id));
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
		knowledge.configure(content);
		gameplay = content.knowledge_definition.schemas.size() > 1 || !content.resource_definitions.empty() ||
		           !content.tree_programs.empty() || !content.scenarios.empty();
		std::map<std::string, std::size_t> tree_ids;
		for (const auto &[name, tree] : content.trees)
		{
			if (content.tree_programs.contains(name))
				continue;
			tree_ids.emplace(name, programs.size());
			programs.push_back(compile_tree(tree));
		}
		for (const auto &[name, definition] : content.entities)
		{
			CompiledDefinition result;
			if (definition.contains("control_layout"))
			{
				result.layout = compile_controllable_layout(content, name);
				for (const auto &spec : result.layout->controllers)
				{
					ConsoleControllerRuntime controller;
					controller.definition = spec;
					for (const auto &command : spec.commands)
						controller.commands.push_back({command.initial, 1});
					if (!spec.tree.empty())
					{
						if (content.tree_programs.contains(spec.tree))
						{
							controller.brain = BrainRuntime{content.tree_programs.at(spec.tree), spec.tree};
							controller.telemetry_reads =
							    controller_telemetry_bindings(content.trees.at(spec.tree), content);
							for (const auto &binding : controller_command_reads(content.trees.at(spec.tree), content))
							{
								const auto target = std::find_if(
								    result.layout->controllers.begin(), result.layout->controllers.end(),
								    [&](const auto &candidate)
								    {
									    return candidate.id == binding.controller && candidate.console == spec.console;
								    });
								const auto channel = std::find_if(target->commands.begin(), target->commands.end(),
								                                  [&](const auto &candidate)
								                                  {
									                                  return candidate.id == binding.channel;
								                                  });
								controller.command_reads.emplace_back(target - result.layout->controllers.begin(),
								                                      channel - target->commands.begin());
							}
							for (const auto &action : controller.brain->program.registry.actions)
								if (action.name.starts_with("write."))
									controller.tree_channels.push_back(static_cast<std::size_t>(
									    std::find_if(spec.commands.begin(), spec.commands.end(),
									                 [&](const auto &command)
									                 {
										                 return "write." + command.id == action.name;
									                 }) -
									    spec.commands.begin()));
							for (const auto &binding : controller_message_reads(content.trees.at(spec.tree), content))
							{
								RecordReadBinding read;
								read.schema = content.knowledge_definition.schema(binding.schema);
								read.field = content.knowledge_definition.field(read.schema, binding.field);
								controller.message_reads.push_back(read);
							}
							for (const auto &binding : gameplay_record_bindings(content.trees.at(spec.tree), content))
							{
								RecordReadBinding read;
								read.shared = binding.scope == GameplayTreeRecordScope::Ship;
								read.schema = content.knowledge_definition.schema(binding.schema);
								read.field = content.knowledge_definition.field(read.schema, binding.field);
								if (!binding.subject_parameter.empty())
									read.subject = std::stoull(binding.subject_parameter);
								controller.brain->reads.push_back(read);
							}
						}
						else
							controller.timed = Controller{tree_ids.at(spec.tree), {}};
						if (controller.timed)
							for (const auto &output : programs.at(controller.timed->program).outputs)
								if (std::count_if(spec.commands.begin(), spec.commands.end(),
								                  [&](const auto &command)
								                  {
									                  return command.action == output.action &&
									                         output.value >= command.minimum &&
									                         output.value <= command.maximum;
								                  }) != 1)
									throw std::runtime_error(
									    "Controller timed output requires exactly one compatible owned channel");
					}
					result.controller_templates.push_back(std::move(controller));
				}
				gameplay = true;
			}
			for (const auto &publication : definition.value("publications", Json::array()))
				result.publications.push_back(publication.at("id"));
			for (const auto &message : definition.value("message_templates", Json::array()))
				result.message_templates.push_back(message);
			result.default_preset = definition.value("default_preset", std::string{});
			for (const auto &p : definition.value("presets", Json::array()))
			{
				CompiledDefinition::Preset compiled;
				compiled.id = p.at("id");
				for (auto c = p.at("consoles").begin(); c != p.at("consoles").end(); ++c)
				{
					compiled.delegated[c.key()] = c.value().value("delegated", std::vector<std::string>{});
					for (const auto &v : c.value().value("summary_intents", Json::array()))
					{
						CompiledDefinition::Summary summary;
						summary.system = v.at("system");
						summary.channel = v.at("channel");
						summary.levels = v.at("levels").get<std::vector<std::int64_t>>();
						summary.labels = v.value("level_labels", std::vector<std::string>{});
						if (v.contains("default"))
							summary.initial = v.at("default");
						compiled.summaries[c.key()].push_back(std::move(summary));
					}
				}
				result.presets.push_back(std::move(compiled));
			}
			result.radar_icon = render_radar_icon(definition.at("render"));
			result.radar_sprite = render_radar_sprite(definition.at("render"), content);
			result.consoles = resolve_consoles(content, name);
			if (result.layout)
				for (const auto &fit : resolve_systems(content, name))
					result.observation_owners[fit.id] =
					    definition.at("systems").at(fit.id).value("observation_console", result.consoles.front().id);

			result.explicit_stations = definition.contains("stations");
			if (definition.contains("controllers"))
				for (auto system : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
				{
					const std::string key(operating_system_name(system));
					if (definition["controllers"].contains(key) &&
					    !content.tree_programs.contains(definition["controllers"][key].get<std::string>()))
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
			const bool advanced = uses_advanced_machinery(content, name);
			if (advanced)
			{
				result.fitted.emplace();
				result.gameplay.emplace();
				if (auto power = compile_ship_resources(content, name))
					result.gameplay->power = std::move(*power);
				for (const auto &spec : resolve_systems(content, name))
				{
					CompiledGameplaySystem runtime;
					if (spec.kind == OperatingSystem::Engine || spec.kind == OperatingSystem::Steering ||
					    spec.kind == OperatingSystem::Lateral || spec.kind == OperatingSystem::Boost ||
					    spec.kind == OperatingSystem::Impulse)
					{
						auto data = systems.at(spec.id);
						data["kind"] = std::string(operating_system_name(spec.kind));
						runtime.flight = compile_flight(data, "entities." + name + ".systems." + spec.id);
					}
					if (content.tree_programs.contains(spec.controller))
					{
						runtime.brain = content.tree_programs.at(spec.controller);
						runtime.tree = spec.controller;
						runtime.summary_reads = gameplay_summary_bindings(content.trees.at(spec.controller), content);
						for (const auto &binding : gameplay_record_bindings(content.trees.at(spec.controller), content))
						{
							RecordReadBinding read;
							read.shared = binding.scope == GameplayTreeRecordScope::Ship;
							read.schema = content.knowledge_definition.schema(binding.schema);
							read.field = content.knowledge_definition.field(read.schema, binding.field);
							read.selected = binding.subject_parameter == "selected_contact";
							if (!binding.subject_parameter.empty() && !read.selected)
								read.subject = std::stoull(binding.subject_parameter);
							runtime.reads.push_back(read);
						}
					}
					result.gameplay->values.push_back(std::move(runtime));
				}
			}
			if (result.fitted)
			{
				if (definition.contains("controller"))
					throw std::runtime_error("Instance Systems require per-System controllers");
				for (const auto &spec : resolve_systems(content, name))
				{
					CompiledEquipment runtime{spec.id, spec.kind};
					if (spec.kind == OperatingSystem::Engine)
						runtime.engine = Engine{spec.maximum, spec.response};
					else if (spec.kind == OperatingSystem::Steering)
						runtime.steering = Steering{spec.response};
					else if (spec.kind == OperatingSystem::Radar)
						runtime.radar = Radar{spec.maximum};
					if (!spec.controller.empty() && !content.tree_programs.contains(spec.controller))
						runtime.controller = Controller{tree_ids.at(spec.controller), {}};
					runtime.advanced = result.gameplay.has_value();
					runtime.maximum = spec.maximum;
					runtime.enabled = systems.at(spec.id).value("enabled", true);
					if (runtime.radar)
						runtime.radar->enabled = runtime.enabled;
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
		std::vector<CompiledEquipment> templates;
		if (data.fitted)
			templates = data.fitted->values;
		else
		{
			for (auto kind : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
			{
				CompiledEquipment value{std::string(operating_system_name(kind)), kind};
				if (kind == OperatingSystem::Engine && data.engine)
					value.engine = data.engine;
				else if (kind == OperatingSystem::Steering && data.steering)
					value.steering = data.steering;
				else if (kind == OperatingSystem::Radar && data.radar)
					value.radar = data.radar;
				else
					continue;
				value.controller = data.system_controllers.values[static_cast<std::size_t>(kind)];
				templates.push_back(std::move(value));
			}
		}
		EquipmentIndex index;
		for (std::size_t i = 0; i < templates.size(); ++i)
		{
			const auto &v = templates[i];
			auto child = ecs.entity().child_of(entity).set<Equipment>(
			    {v.id, v.kind, i, v.maximum, false, v.advanced, v.enabled});
			index.values.push_back(child);
			if (v.engine)
				child.set<Engine>(*v.engine);
			if (v.steering)
			{
				auto turn = *v.steering;
				turn.heading = pose.heading;
				child.set<Steering>(turn);
			}
			if (v.radar)
				child.set<Radar>(*v.radar);
			if (v.controller)
				child.set<Controller>(*v.controller);
			if (!data.gameplay)
			{
				child.add<BasicEquipment>();
				continue;
			}
			child.add<AdvancedEquipment>()
			    .set<ActionResult>({})
			    .set<OperatingHeat>({})
			    .set<EffectJournal>({})
			    .set<SummaryIntents>({});
			const auto &g = data.gameplay->values[i];
			if (g.flight)
			{
				auto state = initial_flight(*g.flight, pose, Motion{});
				state.enabled = v.enabled;
				child.set<FlightRuntime>({*g.flight, state});
			}
			if (g.brain)
				child.set<BrainRuntime>({*g.brain, g.tree, g.reads, g.summary_reads, {}, {}});
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
		if (data.controller)
			entity.set<Controller>({*data.controller, {}});
		if (!entities.emplace(id, entity).second)
			throw std::runtime_error("duplicate entity identity");
		construction.registry = &entities;
		construction.id = id;
		if (data.layout)
		{
			EntityControls controls;
			controls.layout = *data.layout;
			for (const auto &console : data.consoles)
				controls.profiles.emplace(console.id, data.layout->default_profile);
			controls.controllers = data.controller_templates;
			entity.set<EntityControls>(std::move(controls));
		}
		if (data.layout)
			for (const auto &system : (*fittings(entity)).values)
			{
				const auto values = telemetry(id, system.ordinal);
				knowledge.verify_reporter_fields(definition, system.id, values);
				for (const auto &controller : data.controller_templates)
					for (const auto &binding : controller.telemetry_reads)
						if (binding.system == system.id && !values.contains(binding.field))
							throw std::runtime_error("Controller observation field unavailable on fitted System " +
							                         system.id + ": " + binding.field);
			}
		construction.indexed = true;
		next_entity = std::max(next_entity, id + 1);
		knowledge.register_ship(id, definition, ecs.get<Clock>().tick);
		if (!data.default_preset.empty())
			for (const auto &p : data.presets)
				if (p.id == data.default_preset)
				{
					for (const auto &[console, systems] : p.delegated)
					{
						preset[{id, console}] = p.id;
						delegated[id].insert(systems.begin(), systems.end());
					}
					if (auto gameplay = gameplay_systems(entity))
						for (const auto &[console, summaries] : p.summaries)
							for (const auto &summary : summaries)
								if (summary.initial)
									for (std::size_t i = 0; i < data.fitted->values.size(); ++i)
										if (data.fitted->values[i].id == summary.system)
											gameplay->values[i].summary[summary.channel] = *summary.initial;
				}
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
	std::optional<EquipmentRef> instance(flecs::entity ship, const std::string &id) const
	{
		for (auto entity : ship.get<EquipmentIndex>().values)
			if (entity.get<Equipment>().id == id)
				return equipment_ref(entity);
		return {};
	}
	std::optional<std::string> target(EntityId ship, const ConsoleSpec &seat, OperatingSystem kind,
	                                  const std::string &requested) const
	{
		std::optional<std::string> result;
		const auto entity = entities.at(ship);
		for (const auto &id : seat.controlled_systems)
		{
			const auto runtime = instance(entity, id);
			const auto type = runtime            ? runtime->kind
			                  : id == "engine"   ? OperatingSystem::Engine
			                  : id == "steering" ? OperatingSystem::Steering
			                                     : OperatingSystem::Radar;
			if ((requested.empty() && type == kind) || (!requested.empty() && id == requested))
			{
				if (result)
					return {};
				result = id;
			}
		}
		return result;
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
	bool operate(flecs::entity entity, const std::string &name, TreeOutput output, bool execute)
	{
		auto fitted = fittings(entity);
		auto systems = gameplay_systems(entity);
		if (!fitted || !systems)
			return false;
		std::size_t index = 0;
		for (; index < fitted->values.size(); ++index)
			if (fitted->values[index].id == name)
				break;
		if (index == fitted->values.size() || !operating_value_valid(operating_action(output.action), output.value))
			return false;
		auto system = systems->values[index];
		if (system.flight)
			if (auto kind = flight_action(output.action))
			{
				auto candidate = system.flight->state;
				const auto result = apply_flight(system.flight->definition, candidate, {*kind, output.value},
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
#include "controller_execution.hpp"
	std::int64_t sensor_range(EntityId ship, const std::string &name) const
	{
		const auto entity = entities.at(ship);
		const auto sensor = instance(entity, name);
		if (!sensor || !sensor->radar || !sensor->enabled || !sensor->radar->enabled)
			return 0;
		auto range = sensor->radar->range;
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
				values["match_speed"] = f.matching;
				values["selected_contact"] = f.selected_contact;
				values["matched_contact"] = f.matched_contact;
				values["match_stale"] = f.match_stale;
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
				values["used_power"] = g.paid * rate;
				values["reserved_power"] = g.requested * rate;
				values["supply_limited"] = g.supply_permille < 1000;
				if (binding.store != no_resource)
				{
					const auto &d = p.definition.network.stores[binding.store];
					const auto &v = p.network.stores()[binding.store];
					values["enabled"] =
					    values.at("enabled") && v.enabled && (!system.flight || system.flight->state.enabled);
					const auto &t = p.network.telemetry()[binding.store];
					values["stored_energy"] = v.energy;
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
					        .remaining;
				}
			}
		}
		return values;
	}
#include "console_observations.hpp"
	std::vector<state_tree::Value> brain_observations(EntityId id, std::size_t index, GameplayRef system)
	{
		using state_tree::Type;
		using state_tree::Value;
		using O = GameplayTreeObservation;
		std::vector<Value> values;
		for (const auto &field : system.brain->program.registry.observations)
			values.push_back(field.initial);
		const auto put = [&](O name, Value value)
		{
			values[static_cast<std::size_t>(name)] = std::move(value);
		};
		const auto entity = entities.at(id);
		const auto fitted = (*fittings(entity)).values[index];
		const auto owner = system_owner(id, fitted.id);
		put(O::SelfEntity, Value::reference(Type::Entity, id));
		put(O::SelfSystem, Value::reference(Type::System, knowledge.system_id(id, fitted.id)));
		put(O::SelfConsole, Value::reference(Type::Console, knowledge.console_id(owner)));
		put(O::Tick, Value::integer(ecs.get<Clock>().tick));
		const auto observation = observation_snapshots.contains(owner) ? observation_snapshots.at(owner)
		                                                               : capture_console_observation(owner);
		put(O::PositionX, Value::integer(observation.own.x));
		put(O::PositionY, Value::integer(observation.own.y));
		put(O::VelocityX, Value::integer(observation.own.vx));
		put(O::VelocityY, Value::integer(observation.own.vy));
		put(O::Heading, Value::integer(observation.own.heading));
		std::map<std::string, std::int64_t> data;
		for (const auto &state : observation.systems)
			if (state.id == fitted.id)
				data = state.telemetry;
		const std::pair<O, const char *> fields[] = {{O::Enabled, "enabled"},
		                                             {O::RequestedOutput, "requested"},
		                                             {O::ActualOutput, "actual_output"},
		                                             {O::StoredEnergy, "stored_energy"},
		                                             {O::EnergyCapacity, "energy_capacity"},
		                                             {O::IncomingPower, "incoming_power"},
		                                             {O::UsedPower, "used_power"},
		                                             {O::ReservedPower, "reserved_power"},
		                                             {O::Heat, "heat"},
		                                             {O::HeatCapacity, "heat_capacity"},
		                                             {O::PassiveCooling, "passive_cooling"},
		                                             {O::AllocatedCooling, "cooling_allocation"},
		                                             {O::ActualCooling, "actual_cooling"},
		                                             {O::CoolantStock, "coolant_stock"},
		                                             {O::ThermalStopped, "thermal_stopped"},
		                                             {O::ThermalOverride, "thermal_override"},
		                                             {O::SupplyLimited, "supply_limited"},
		                                             {O::FlowCut, "flow_cut"},
		                                             {O::LinkIsolated, "link_isolated"},
		                                             {O::BoostActive, "boost"},
		                                             {O::ImpulsePhase, "impulse_phase"},
		                                             {O::ImpulseProgress, "impulse_progress"},
		                                             {O::MatchSpeedActive, "match_speed"},
		                                             {O::MatchObservationStale, "match_stale"},
		                                             {O::MatchedSpeed, "matched_speed"},
		                                             {O::Following, "following"},
		                                             {O::FollowStale, "follow_stale"}};
		for (const auto &[key, name] : fields)
			if (data.contains(name))
			{
				const auto n = data.at(name);
				put(key,
				    system.brain->program.registry.observations[static_cast<std::size_t>(key)].type == Type::Boolean
				        ? Value::boolean(n != 0)
				        : Value::integer(n));
			}
		if (data.contains("assisted"))
		{
			put(O::FlightMode, Value::integer(data.at("assisted") ? 0 : 1));
			if (data.at("matched_contact"))
				put(O::MatchedContact, Value::reference(Type::Entity, data.at("matched_contact")));
		}
		const auto inbox = inbox_snapshots.contains({id, fitted.id}) ? inbox_snapshots.at({id, fitted.id})
		                                                             : knowledge.peek_system(id, fitted.id);
		put(O::MessageAvailable, Value::boolean(!inbox.empty()));
		if (!inbox.empty())
		{
			const auto &delivery = inbox.front();
			const auto &message = delivery.message;
			const auto &p = message.value.payload;
			put(O::Message, Value::reference(Type::Message, message.id));
			put(O::MessageCursor, Value::integer(delivery.sequence));
			put(O::MessageSenderSystem, Value::reference(Type::System, message.value.sender_system));
			put(O::MessageSenderConsole, Value::reference(Type::Console, message.sender_console));
			put(O::MessageSchema, Value::string(knowledge.definition().schemas.at(p.schema - 1).name));
			put(O::MessageRequestKey, Value::string(knowledge.request_key_name(message.value.request_key)));
			put(O::MessageObservedTick, Value::integer(p.provenance.observed_tick));
			put(O::MessageDeliveredTick, Value::integer(delivery.delivered_tick));
			put(O::MessageUncertainty, Value::integer(p.provenance.uncertainty));
			for (const auto &[key, value] : p.fields)
			{
				if (auto n = std::get_if<std::int64_t>(&value))
					put(O::MessageInteger, Value::integer(*n));
				else if (auto b = std::get_if<bool>(&value))
					put(O::MessageBoolean, Value::boolean(*b));
				else
					put(O::MessageText, Value::string(std::get<std::string>(value)));
			}
		}
		const auto board = brain_snapshots.contains(owner) ? brain_snapshots.at(owner) : knowledge.project(owner);
		for (std::size_t i = 0; i < system.brain->reads.size(); ++i)
		{
			const auto &binding = system.brain->reads[i];
			const auto subject = binding.selected
			                         ? (data.contains("selected_contact") ? data.at("selected_contact") : 0)
			                     : binding.subject ? binding.subject
			                                       : id;
			const auto &records = binding.shared ? board.shared : board.records;
			// Only the newest version of each producer/publication stream may
			// supply a field; omissions, tombstones and stale versions hide older data.
			std::set<std::pair<KnowledgeId, KnowledgeId>> seen;
			for (auto record = records.rbegin(); record != records.rend(); ++record)
				if (record->value.schema == binding.schema && record->value.subject == subject &&
				    seen.emplace(record->value.provenance.source, record->publication).second &&
				    !record->value.withdrawn && !knowledge.stale(*record, ecs.get<Clock>().tick) &&
				    record->value.fields.contains(binding.field))
				{
					const auto &value = record->value.fields.at(binding.field);
					auto &target = values[static_cast<std::size_t>(O::Count) + i];
					if (auto n = std::get_if<std::int64_t>(&value))
						target = Value::integer(*n);
					else if (auto b = std::get_if<bool>(&value))
						target = Value::boolean(*b);
					else
						target = Value::string(std::get<std::string>(value));
					break;
				}
		}
		try
		{
			const auto &schemas = knowledge.definition();
			const auto key = schemas.schema("phoenix.objective");
			const KnowledgeRecord *objective = nullptr;
			for (const auto *records : {&board.records, &board.shared})
				for (const auto &record : *records)
					if (record.value.schema == key && !record.value.withdrawn &&
					    (!objective || record.value.provenance.revision > objective->value.provenance.revision))
						objective = &record;
			put(O::ObjectiveKnown, Value::boolean(objective != nullptr));
			if (objective)
			{
				const auto &fields = objective->value.fields;
				const auto status = std::get<std::string>(fields.at(schemas.field(key, "status")));
				const std::string statuses[] = {"pending", "active", "completed", "failed", "cancelled"};
				std::int64_t number = 0;
				while (number < 4 && statuses[number] != status)
					++number;
				put(O::Objective, Value::reference(Type::Objective, objective->value.subject));
				put(O::ObjectiveState, Value::integer(number));
				put(O::DestinationX,
				    Value::integer(std::get<std::int64_t>(fields.at(schemas.field(key, "destination_x")))));
				put(O::DestinationY,
				    Value::integer(std::get<std::int64_t>(fields.at(schemas.field(key, "destination_y")))));
				const auto name = std::get<std::string>(fields.at(schemas.field(key, "region")));
				if (scenario && scenario->runtime().definition().region_ids.contains(name))
					put(O::DestinationRegion,
					    Value::reference(Type::Region, scenario->runtime().definition().region_ids.at(name) + 1));
			}
		}
		catch (const std::exception &)
		{
		}
		std::optional<std::int64_t> single_summary;
		std::size_t summary_count = 0;
		for (const auto &[channel, level] : system.summary)
			if (channel != "destination.x" && channel != "destination.y")
			{
				single_summary = level;
				++summary_count;
			}
		if (summary_count == 1)
			put(O::SummaryLevel, Value::integer(*single_summary));
		for (std::size_t i = 0; i < system.brain->summary_reads.size(); ++i)
			if (system.summary.contains(system.brain->summary_reads[i].channel))
				values[static_cast<std::size_t>(O::Count) + system.brain->reads.size() + i] =
				    Value::integer(system.summary.at(system.brain->summary_reads[i].channel));
		put(O::ResultAvailable, Value::boolean(system.result.available));
		put(O::ResultSucceeded, Value::boolean(system.result.success));
		put(O::ResultReason, Value::string(system.result.reason));
		put(O::ResultInteger, Value::integer(system.result.id));
		if (system.result.id)
			put(O::ResultMessage, Value::reference(Type::Message, system.result.id));
		put(O::ResultPublication, Value::integer(system.result.id));
		return values;
	}
	void brain_output(EntityId id, std::size_t index, GameplayRef system, const state_tree::Output &output,
	                  std::span<const state_tree::Value> observations)
	{
		using A = GameplayTreeAction;
		const auto action = static_cast<A>(output.action);
		const auto &a = output.arguments;
		const auto entity = entities.at(id);
		const auto fitted = (*fittings(entity)).values[index];
		const auto owner = system_owner(id, fitted.id);
		const auto self = knowledge.system_id(id, fitted.id);
		system.result.available = true;
		system.result.success = false;
		system.result.id = 0;
		system.result.reason.clear();
		try
		{
			const auto &spec = system.brain->program.registry.actions.at(output.action);
			if (spec.effect)
			{
				std::string receipt = std::to_string(output.activation) + "/" + std::to_string(output.task);
				for (std::size_t i = 0; i < output.call_path.size(); ++i)
					receipt +=
					    "/" + std::to_string(output.call_path[i]) + ":" + std::to_string(output.call_activations.at(i));
				if (system.effects.contains(receipt))
				{
					system.result.success = true;
					return;
				}
				if (system.effects.size() >= 65536)
					throw std::runtime_error("controller effect journal exhausted");
				system.effects.insert(std::move(receipt));
			}
			if (action >= A::SendSystemMessage && action <= A::SendShipMessage)
			{
				if (static_cast<KnowledgeId>(a.at(0).number) != self)
					throw std::runtime_error("controller cannot impersonate another System");
				const auto &message = knowledge.message_definition(a.at(2).text);
				const auto &schema = knowledge.definition().schemas.at(message.schema - 1);
				CoordinationInput value;
				value.sender_system = self;
				value.origin = ControlOrigin::AI;
				value.human_seeking = message.human_seeking;
				value.presentation = message.presentation;
				value.destination = {action == A::SendSystemMessage    ? MessageDestinationKind::System
				                     : action == A::SendConsoleMessage ? MessageDestinationKind::Console
				                     : action == A::SendStationMessage ? MessageDestinationKind::Station
				                                                       : MessageDestinationKind::Ship,
				                     static_cast<KnowledgeId>(a.at(1).number)};
				value.payload.schema = message.schema;
				value.payload.producer =
				    knowledge.message_producer(owner, fitted.id, message.schema, ControlOrigin::AI);
				value.payload.subject = a.at(3).number;
				value.payload.provenance = {self, ecs.get<Clock>().tick, ecs.get<Clock>().tick, 0,
				                            EvidenceKind::Intent};
				GameplayKnowledgeMessageContext context;
				context.amount = a.at(4).number;
				context.flag = a.at(5).number != 0;
				context.detail = a.at(6).text;
				context.program = &system.brain->program;
				context.observations = observations;
				context.parameters = &output.captured_parameters;
				context.memory = &output.captured_memory;
				context.captured_intents = &output.captured_intents;
				context.elapsed = output.captured_elapsed;
				const auto bound = evaluate_gameplay_message(message, context);
				value.payload.fields = bound.fields;
				value.presentation.parameters = bound.parameters;
				if (a.at(8).number < 1 || a.at(9).number < 0 || a.at(10).number < 0 || a.at(10).number > 3)
					throw std::runtime_error("invalid Coordination timing or urgency");
				value.request_key = knowledge.request_key(a.at(7).text);
				value.delay = a.at(8).number;
				value.expires_tick = a.at(9).number ? ecs.get<Clock>().tick + a.at(9).number : 0;
				value.presentation.urgency = static_cast<std::uint8_t>(a.at(10).number);
				system.result.id = knowledge.admit_message(owner, std::move(value), ecs.get<Clock>().tick);
				system.result.success = true;
				return;
			}
			if (action == A::ReceiveMessages)
			{
				if (static_cast<KnowledgeId>(a.at(0).number) != self)
					throw std::runtime_error("unowned message receiver");
				knowledge.acknowledge_system(id, fitted.id, a.at(1).number);
				system.result.success = true;
				return;
			}
			if (action == A::WithdrawRequest)
			{
				if (static_cast<KnowledgeId>(a.at(0).number) != self)
					throw std::runtime_error("unowned request sender");
				system.result.success = knowledge.withdraw_request(
				    owner, fitted.id, knowledge.request_key(a.at(1).text), ControlOrigin::AI);
				return;
			}
			if (action == A::PublishRecord)
			{
				if (static_cast<KnowledgeId>(a.at(0).number) != knowledge.console_id(owner))
					throw std::runtime_error("unowned source Console");
				system.result.id = knowledge.activate_publication(owner, a.at(1).text, ecs.get<Clock>().tick,
				                                                  a.at(2).number, a.at(3).number != 0, a.at(4).text);
				system.result.success = true;
				return;
			}
			if (action == A::StopPublication)
			{
				if (static_cast<KnowledgeId>(a.at(0).number) != knowledge.console_id(owner))
					throw std::runtime_error("unowned source Console");
				knowledge.stop_publication(owner, a.at(1).number);
				system.result.success = true;
				return;
			}
			if (a.empty() || static_cast<KnowledgeId>(a.at(0).number) != self)
				throw std::runtime_error("controller writes another System");
			if (action == A::SetSummaryIntent)
			{
				system.summary[a.at(1).text] = a.at(2).number;
				system.result.success = true;
				return;
			}
			if (action == A::SetFollowContact)
			{
				if (!system.flight || (system.flight->definition.kind != FlightKind::Engine &&
				                       system.flight->definition.kind != FlightKind::Steering))
					throw std::runtime_error("following requires engine or steering actuator");
				const auto target = static_cast<EntityId>(a.at(1).number);
				if (target == entity.get<StableId>().id || !entities.contains(target) || a.at(2).number <= 0 ||
				    a.at(2).number > 1000000000 || a.at(3).number <= 0 || a.at(3).number > 1000000000)
					throw std::runtime_error("invalid following target, separation or speed");
				auto &flight = system.flight->state;
				flight.selected_contact = target;
				flight.following = a.at(4).number != 0;
				flight.follow_stale = false;
				flight.follow_distance = a.at(2).number;
				flight.follow_speed_limit = a.at(3).number;
				flight.assisted = true;
				flight.matching = flight.match_stale = false;
				flight.longitudinal = flight.steering = 0;
				system.result.success = true;
				return;
			}
			if (action == A::SetDestination)
			{
				if (!system.flight)
					throw std::runtime_error("destination requires flight actuator");
				if (a.at(2).number < -1000000000 || a.at(2).number > 1000000000 || a.at(3).number < -1000000000 ||
				    a.at(3).number > 1000000000)
					throw std::runtime_error("destination outside supported coordinates");
				const auto delta = fixed::Vec{a.at(2).number, a.at(3).number} - entity.get<Pose>().position;
				auto direction = delta;
				std::int64_t offset = 0;
				if (direction.y < 0)
				{
					direction = -direction;
					offset = 180000;
				}
				std::int64_t low = 0, high = 180000;
				while (high - low > 1)
				{
					const auto middle = (low + high) / 2;
					const auto forward = fixed::direction(middle);
					const auto cross = forward.x * direction.y - forward.y * direction.x;
					if (cross > 0)
						low = middle;
					else
						high = middle;
				}
				system.flight->state.desired_heading = (low + offset) % 360000;
				system.flight->state.assisted = true;
				system.flight->state.matching = false;
				system.flight->state.following = system.flight->state.follow_stale = false;
				if (system.flight->definition.kind == FlightKind::Engine)
					system.flight->state.desired_speed = system.flight->definition.maximum_speed;
				system.summary["destination.x"] = a.at(2).number;
				system.summary["destination.y"] = a.at(3).number;
				system.result.success = true;
				return;
			}
			if (action == A::SetMatchSpeed)
			{
				const auto previous = system.flight->state;
				if (!operate(entity, fitted.id, {OperatingAction::SelectContact, a.at(1).number}, true))
					throw std::runtime_error("match contact refused");
				system.result.success = operate(entity, fitted.id, {OperatingAction::MatchSpeed, a.at(2).number}, true);
				if (!system.result.success)
					system.flight->state = previous;
				return;
			}
			if (a.size() > 2 && action >= A::SetStorageChargeLimit && action <= A::SetCoolingAllocation &&
			    !a.at(1).text.empty())
			{
				const auto power = (*gameplay_systems(entity)).power;
				if (!power)
					throw std::runtime_error("resource target without network");
				const auto &b = power->definition.systems[fitted.entity.get<ResourceBinding>().slot];
				std::string target;
				if (action == A::SetStorageChargeLimit || action == A::SetStorageDischargeLimit)
				{
					if (b.store != no_resource)
						target = power->definition.network.stores[b.store].id;
				}
				else if (action == A::SetGroupAllocation)
				{
					if (b.group != no_resource)
						target = power->definition.network.groups[b.group].id;
				}
				else if (action == A::SetLinkIsolation)
				{
					if (b.conduit != no_resource)
						target = power->definition.network.links[b.conduit].id;
				}
				else if (action == A::SetCoolingAllocation)
				{
					if (b.cooling != no_resource)
						target = power->definition.network.cooling_links[b.cooling].id;
				}
				if (a.at(1).text != target)
					throw std::runtime_error("controller target is outside its bound resource");
			}
			std::optional<OperatingAction> operation;
			std::int64_t amount = a.size() > 1 ? a.at(1).number : 0;
			switch (action)
			{
			case A::SetSpeed:
				operation = OperatingAction::Speed;
				break;
			case A::SetHeading:
				operation = OperatingAction::Heading;
				break;
			case A::SetEnabled:
				operation = OperatingAction::SystemEnabled;
				break;
			case A::SetFlightMode:
				if (amount < 0 || amount > 1)
					throw std::runtime_error("unknown flight mode");
				operation = OperatingAction::Assisted;
				amount = amount == 0 ? 1 : 0;
				break;
			case A::SetLongitudinalThrust:
				operation = OperatingAction::Longitudinal;
				break;
			case A::SetLateralThrust:
				operation = OperatingAction::Lateral;
				break;
			case A::SetSteering:
				operation = OperatingAction::ManualSteering;
				break;
			case A::SetBoost:
				operation = OperatingAction::Boost;
				break;
			case A::SetImpulse:
				operation = OperatingAction::Impulse;
				break;
			case A::SetMatchSpeed:
				operation = OperatingAction::MatchSpeed;
				break;
			case A::SetGeneratorOutput:
				if (amount < 0 || amount > 1000)
					throw std::runtime_error("generator output outside permille range");
				operation = OperatingAction::GeneratorOutput;
				if (const auto power = (*gameplay_systems(entity)).power;
				    power &&
				    power->definition.systems[fitted.entity.get<ResourceBinding>().slot].generator != no_resource)
					amount =
					    amount *
					    power->definition.network
					        .generators[power->definition.systems[fitted.entity.get<ResourceBinding>().slot].generator]
					        .maximum_rate /
					    1000;
				break;
			case A::SetStorageChargeLimit:
				operation = OperatingAction::ChargeLimit;
				amount = a.at(2).number;
				break;
			case A::SetStorageDischargeLimit:
				operation = OperatingAction::DischargeLimit;
				amount = a.at(2).number;
				break;
			case A::SetGroupAllocation:
				operation = OperatingAction::GroupAllocation;
				amount = a.at(2).number;
				break;
			case A::SetLinkIsolation:
				operation = OperatingAction::LinkIsolation;
				amount = a.at(2).number;
				break;
			case A::SetCoolingAllocation:
				operation = OperatingAction::CoolingAllocation;
				amount = a.at(2).number;
				break;
			case A::SetThermalOverride:
				operation = OperatingAction::ThermalOverride;
				break;
			default:
				break;
			}
			if (operation)
				system.result.success = operate(entity, fitted.id, {*operation, amount}, true);
			if (!system.result.success)
				system.result.reason = "operating request refused";
		}
		catch (const std::exception &error)
		{
			system.result.reason = error.what();
		}
	}
	template <typename... Components>
	std::vector<flecs::entity> ordered_equipment(const flecs::query<Components...> &query, EntityId ship = 0)
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
	void run_basic_controllers()
	{
		for (auto child : ordered_equipment(basic_controller_query))
		{
			const auto &fit = child.get<Equipment>();
			if (fit.held)
				continue;
			auto &controller = child.get_mut<Controller>();
			auto ship = child.parent();
			for (auto output : step_tree(programs[controller.program], controller.cursor))
				if (ship.has<NamedEquipment>())
					apply(output, child.try_get_mut<Engine>(), child.try_get_mut<Steering>(),
					      child.try_get_mut<Radar>());
				else
					apply(output, legacy_component<Engine>(ship), legacy_component<Steering>(ship),
					      legacy_component<Radar>(ship));
		}
	}
	void run_basic_steering()
	{
		for (auto child : ordered_equipment(steering_query))
			if (auto ship = child.parent(); ship.has<Movable>())
				step_basic_steering(ship.get_mut<Pose>(), child.get_mut<Steering>());
	}
	void run_basic_engines()
	{
		for (auto child : ordered_equipment(engine_query))
			if (auto ship = child.parent(); ship.has<Movable>())
				step_basic_engine(ship.get<Pose>(), ship.get_mut<Motion>(), child.get_mut<Engine>());
	}
	void run_equipment_controllers(EntityId id)
	{
		for (auto child : ordered_equipment(advanced_controller_query, id))
		{
			auto fit = equipment_ref(child);
			if (fit.held)
				continue;
			if (fit.controller)
				for (const auto &output : step_tree(programs[fit.controller->program], fit.controller->cursor))
					operate(child.parent(), fit.id, output, true);
			if (auto brain = child.try_get_mut<BrainRuntime>())
			{
				auto system = gameplay_ref(child);
				const auto observations = brain_observations(id, fit.ordinal, system);
				if (brain->continuation.interrupted)
					state_tree::resume(brain->continuation);
				const auto result = state_tree::step(brain->program, brain->continuation, observations);
				brain->debug = result.debug;
				for (const auto &output : result.outputs)
					brain_output(id, fit.ordinal, system, output, observations);
			}
		}
	}
	struct FlightRestrictions
	{
		bool impulse = false;
		int steering_limit = 1000;
	};
	FlightRestrictions flight_restrictions(EntityId id)
	{
		FlightRestrictions result;
		for (auto child : ordered_equipment(flight_query, id))
		{
			const auto &flight = child.get<FlightRuntime>();
			if (flight.state.impulse != ImpulsePhase::Idle)
			{
				result.impulse = true;
				result.steering_limit =
				    std::min(result.steering_limit, static_cast<int>(flight.definition.impulse_steering_permille));
			}
		}
		return result;
	}
	void request_flight_resources(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		const auto tick = ecs.get<Clock>().tick;
		const auto &pose = ship.get<Pose>();
		const auto &motion = ship.get<Motion>();
		for (auto child : ordered_equipment(flight_query, id))
		{
			auto &flight = child.get_mut<FlightRuntime>();
			flight.state.steering_limit_permille = restrictions.steering_limit;
			const auto owner = system_owner(id, child.get<Equipment>().id);
			// Actuator guidance consumes the same frozen, permitted contact feed as its Console.
			const auto &observation = observation_snapshots.at(owner);
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
			auto requested = flight.state;
			if (restrictions.impulse)
				requested.boost = false;
			flight.demand = flight_demand(flight.definition, requested, pose, motion);
			if (auto power = ship.try_get_mut<ShipResources>())
				request_system_resource(*power, child.get<ResourceBinding>().slot, 0, false,
				                        (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
				                            ? 0
				                            : flight.demand.power_rate);
		}
		for (auto child : ordered_equipment(passive_resource_query, id))
		{
			const auto &fit = child.get<Equipment>();
			const auto radar = child.try_get<Radar>();
			request_system_resource(ship.get_mut<ShipResources>(), child.get<ResourceBinding>().slot,
			                        fit.enabled && (!radar || radar->enabled) ? 1000 : 0);
		}
	}
	void actuate_flight(flecs::entity child, FlightRestrictions restrictions)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		auto ship = child.parent();
		auto power = ship.try_get_mut<ShipResources>();
		const auto slot = power ? child.get<ResourceBinding>().slot : 0;
		int paid = power ? power->grants[slot].supply_permille : 1000;
		if (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
			paid = 0;
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
		flight.state.boost = requested_boost;
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
	void run_advanced_equipment()
	{
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
			if (ship.has<EntityControls>())
				run_console_controllers(ship);
			else
				run_equipment_controllers(id);
			const auto restrictions = flight_restrictions(id);
			request_flight_resources(ship, restrictions);
			if (auto power = ship.try_get_mut<ShipResources>())
				tick_ship_resources(*power);
			run_equipment_actuation(id, restrictions);
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
				scenario->confirm_spawn(request.id, ids);
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
					delegated.erase(id);
					for (auto it = preset.begin(); it != preset.end();)
						if (it->first.ship == id)
							it = preset.erase(it);
						else
							++it;
				}
				knowledge = std::move(previous);
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
				const auto &region = definition.regions.at(spec.region);
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
				    {schema.field(key, "region"), region.id},
				    {schema.field(key, "credited"),
				     objective.completion_credited || publication.status == ObjectiveStatus::Completed},
				    {schema.field(key, "destination_x"), region.centre.x},
				    {schema.field(key, "destination_y"), region.centre.y}};
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
		auto entity = entities.at(ship);
		auto fitted = fittings(entity);
		if (entity.has<EntityControls>())
		{
			refresh_controllers(ship, !transitions);
			return;
		}
		std::vector<bool> desired(fitted ? fitted->values.size() : 0, false);
		std::vector<std::pair<ConsoleAddress, std::vector<std::string>>> consoles;
		SystemMask legacy = 0;
		for (const auto &[claim, address] : claims)
			if (address.ship == ship)
			{
				std::vector<std::string> humans;
				if (!away[address])
					for (const auto &name : seat(ship, address.console)->controlled_systems)
						if (!delegated[ship].contains(name))
						{
							humans.push_back(name);
							if (fitted)
								for (std::size_t i = 0; i < fitted->values.size(); ++i)
									if (fitted->values[i].id == name)
										desired[i] = true;
						}
				if (!away[address])
					legacy |= seat(ship, address.console)->controls;
				consoles.push_back({address, std::move(humans)});
			}
		if (fitted)
		{
			auto running = gameplay_systems(entity);
			for (std::size_t i = 0; i < desired.size(); ++i)
			{
				if (transitions && running && fitted->values[i].held != desired[i])
				{
					auto system = running->values[i];
					if (desired[i] && system.brain && system.brain->continuation.initialised &&
					    !system.brain->continuation.interrupted)
					{
						const auto observations = brain_observations(ship, i, system);
						const auto output =
						    state_tree::interrupt(system.brain->program, system.brain->continuation, observations);
						for (const auto &effect : output.outputs)
							brain_output(ship, i, system, effect, observations);
					}
					if (system.flight)
						release_flight_inputs(system.flight->state);
				}
				fitted->values[i].held = desired[i];
			}
		}
		else if (definition(ship).explicit_stations)
		{
			for (auto child : entity.get<EquipmentIndex>().values)
				child.get_mut<Equipment>().held = (legacy & system_bit(child.get<Equipment>().kind)) != 0;
		}
		else if (legacy)
			entity.add<Claimed>();
		else
			entity.remove<Claimed>();
		for (const auto &[address, humans] : consoles)
			knowledge.set_console_control(address, !away[address], humans);
	}
	bool meta(std::uint64_t claimant, const OperatingCommand &command, bool execute)
	{
		if (!claims.contains(claimant) || command.target != claims.at(claimant).ship)
			return false;
		const auto address = claims.at(claimant);
		const auto &console = *seat(address.ship, address.console);
		const auto &spec = definition(address.ship);
		if (entities.at(address.ship).has<EntityControls>() && !command.action.starts_with("message.") &&
		    !command.action.starts_with("publish."))
			return controller_meta(address, command, execute);
		if (command.action == "away")
		{
			if (command.value != 0 && command.value != 1)
				return false;
			if (execute)
			{
				away[address] = command.value != 0;
				refresh_control(address.ship);
			}
			return true;
		}
		if (command.action == "delegate")
		{
			if (!entities.at(address.ship).has<NamedEquipment>())
				return false;
			const auto target = instance(entities.at(address.ship), command.system);
			if (!target)
				return false;
			if (command.value == 1 && !target->controller)
			{
				const auto running = gameplay_systems(entities.at(address.ship));
				if (!running || !running->values[static_cast<std::size_t>(target->ordinal)].brain)
					return false;
			}
			if ((command.value != 0 && command.value != 1) ||
			    std::find(console.controlled_systems.begin(), console.controlled_systems.end(), command.system) ==
			        console.controlled_systems.end())
				return false;
			if (execute)
			{
				if (command.value)
					delegated[address.ship].insert(command.system);
				else
					delegated[address.ship].erase(command.system);
				refresh_control(address.ship);
			}
			return true;
		}
		if (command.action == "preset")
		{
			if (command.value < 0 || static_cast<std::size_t>(command.value) >= spec.presets.size())
				return false;
			const auto &p = spec.presets[static_cast<std::size_t>(command.value)];
			if (!p.delegated.contains(address.console))
				return false;
			if (execute)
			{
				for (const auto &name : console.controlled_systems)
					delegated[address.ship].erase(name);
				const auto &ids = p.delegated.at(address.console);
				delegated[address.ship].insert(ids.begin(), ids.end());
				preset[address] = p.id;
				if (p.summaries.contains(address.console))
					for (const auto &summary : p.summaries.at(address.console))
						if (summary.initial)
							for (std::size_t i = 0; i < (*fittings(entities.at(address.ship))).values.size(); ++i)
								if ((*fittings(entities.at(address.ship))).values[i].id == summary.system)
									gameplay_systems(entities.at(address.ship))->values[i].summary[summary.channel] =
									    *summary.initial;
				refresh_control(address.ship);
			}
			return true;
		}
		if (command.action.starts_with("summary."))
		{
			const auto channel = command.action.substr(8);
			for (const auto &p : spec.presets)
				if (preset.contains(address) && p.id == preset.at(address) && p.summaries.contains(address.console))
					for (const auto &summary : p.summaries.at(address.console))
						if (summary.system == command.system && summary.channel == channel &&
						    std::find(summary.levels.begin(), summary.levels.end(), command.value) !=
						        summary.levels.end())
						{
							if (execute)
								for (std::size_t i = 0; i < (*fittings(entities.at(address.ship))).values.size(); ++i)
									if ((*fittings(entities.at(address.ship))).values[i].id == summary.system)
										gameplay_systems(entities.at(address.ship))->values[i].summary[channel] =
										    command.value;
							return true;
						}
			return false;
		}
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
				                         bound.parameters, true, controller_sender);
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
	SavedOperation operation(flecs::entity entity) const
	{
		SavedOperation result;
		result.heading = entity.get<Pose>().heading;
		if (auto legacy = entity.try_get<LegacyOperation>())
			result = legacy->value;
		if (auto engine = legacy_component<Engine>(entity))
		{
			result.speed = engine->speed;
			result.acceleration_remainder = engine->remainder;
			result.engine_enabled = engine->enabled;
		}
		if (auto turn = legacy_component<Steering>(entity))
		{
			result.heading = turn->heading;
			result.turn_remainder = turn->remainder;
		}
		if (auto radar = legacy_component<Radar>(entity))
			result.radar_enabled = radar->enabled;
		if (auto controller = entity.try_get<Controller>())
			result.tree = save_tree(programs[controller->program], controller->cursor);
		return result;
	}
	void restore_operation(flecs::entity entity, const SavedOperation &operation)
	{
		// Old schema-1 saves allowed inactive fields even without fitted systems.
		// Retain them only at this cold compatibility boundary.
		if ((!legacy_component<Engine>(entity) &&
		     (operation.speed || operation.acceleration_remainder || !operation.engine_enabled)) ||
		    (!legacy_component<Steering>(entity) &&
		     (operation.heading != entity.get<Pose>().heading || operation.turn_remainder)) ||
		    (!legacy_component<Radar>(entity) && !operation.radar_enabled) ||
		    (!entity.has<Controller>() &&
		     (!operation.tree.state.empty() || operation.tree.elapsed || operation.tree.entered)))
			entity.set<LegacyOperation>({operation});
		if (auto engine = legacy_component<Engine>(entity))
		{
			engine->speed = operation.speed;
			engine->remainder = operation.acceleration_remainder;
			engine->enabled = operation.engine_enabled;
		}
		if (auto turn = legacy_component<Steering>(entity))
		{
			turn->heading = operation.heading;
			turn->remainder = operation.turn_remainder;
		}
		if (auto radar = legacy_component<Radar>(entity))
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
	if (content.scenarios.contains(map))
	{
		candidate->scenario.emplace();
		candidate->scenario->load(content, map);
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
	return impl_->definition(ship).explicit_stations || entity.has<NamedEquipment>() ||
	       (entity.has<Movable>() && (legacy_component<Engine>(entity) != nullptr) &&
	        (legacy_component<Steering>(entity) != nullptr));
}
bool World::prepare_console_profile(const ConsoleAddress &address, const std::string &profile)
{
	if (ticks() || !impl_->claims.empty() || !impl_->entities.contains(address.ship))
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
			controls.profiles[address.console] = i;
			impl_->refresh_controllers(address.ship);
			return true;
		}
	return false;
}
bool World::claim(std::uint64_t console, EntityId ship, const std::string &name)
{
	if (!can_claim(console, ship, name))
		return false;
	const auto &seat = *impl_->seat(ship, name);
	impl_->claims[console] = {ship, seat.id};
	auto entity = impl_->entities.at(ship);
	impl_->refresh_control(ship);
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
	if (auto fitted = fittings(entity))
	{
		for (auto system : fitted->values)
			if (std::find(seat.controlled_systems.begin(), seat.controlled_systems.end(), system.id) !=
			    seat.controlled_systems.end())
			{
				if (system.held)
					if (auto running = gameplay_systems(entity))
						if (auto flight = system.entity.try_get_mut<FlightRuntime>())
							release_flight_inputs(flight->state);
				system.held = false;
			}
	}
	else if (impl_->definition(found->second.ship).explicit_stations)
	{
		for (auto child : entity.get<EquipmentIndex>().values)
			if (seat.controls & system_bit(child.get<Equipment>().kind))
			{
				child.get_mut<Equipment>().held = false;
				if (auto controller = child.try_get_mut<Controller>())
					controller->cursor = {};
			}
	}
	else
	{
		(*legacy_component<Engine>(entity)).speed = 0;
		(*legacy_component<Steering>(entity)).heading = entity.get<Pose>().heading;
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
	std::erase_if(impl_->meta_pending,
	              [console](const auto &entry)
	              {
		              return entry.first == console;
	              });
	impl_->knowledge.set_console_control(found->second, false);
	const auto released_ship = found->second.ship;
	impl_->claims.erase(found);
	if (entity.has<EntityControls>())
		impl_->refresh_controllers(released_ship);
	return true;
}
bool World::can_submit(std::uint64_t console, const OperatingCommand &command) const
{
	auto claim = impl_->claims.find(console);
	if (claim == impl_->claims.end() || claim->second.ship != command.target || impl_->pending.size() >= 4096)
		return false;
	if (impl_->meta(console, command, false))
		return impl_->meta_pending.size() < 4096;
	if (impl_->entities.at(command.target).has<EntityControls>())
		return impl_->controller_proposal(claim->second, command, true).has_value() &&
		       impl_->controller_pending.size() < 4096;
	auto output = decode(command);
	if (!output)
		return false;
	const auto system = operating_action(output->action).system;
	const auto target =
	    impl_->target(command.target, *impl_->seat(command.target, claim->second.console), system, command.system);
	if (!target)
		return false;
	auto entity = impl_->entities.at(command.target);
	if (auto instance = impl_->instance(entity, *target); instance && instance->advanced)
		return instance->held && impl_->operate(entity, *target, *output, false);
	if (auto instance = impl_->instance(entity, *target))
		return admissible(*output, instance->engine ? &*instance->engine : nullptr,
		                  instance->steering ? &*instance->steering : nullptr,
		                  instance->radar ? &*instance->radar : nullptr);
	return output && admissible(*output, legacy_component<Engine>(entity), legacy_component<Steering>(entity),
	                            legacy_component<Radar>(entity));
}
bool World::submit(std::uint64_t console, const OperatingCommand &command)
{
	if (!can_submit(console, command))
		return false;
	if (impl_->meta(console, command, false))
	{
		impl_->meta_pending.emplace_back(console, command);
		return true;
	}
	if (impl_->entities.at(command.target).has<EntityControls>())
	{
		impl_->controller_pending.push_back(*impl_->controller_proposal(impl_->claims.at(console), command, true));
		return true;
	}
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
		if (auto systems = gameplay_systems(entity))
			for (std::size_t i = 0; i < systems->values.size(); ++i)
				if ((*fittings(entity)).values[i].held)
					if (auto flight = (*fittings(entity)).values[i].entity.try_get_mut<FlightRuntime>())
						release_flight_inputs(flight->state);
		for (auto child : entity.get<EquipmentIndex>().values)
			child.get_mut<Equipment>().held = false;
	}
	impl_->claims.clear();
	impl_->pending.clear();
	impl_->meta_pending.clear();
	impl_->controller_pending.clear();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			impl_->refresh_controllers(id);
	impl_->knowledge.clear_control();
}
void World::tick()
{
	for (const auto &[claim, command] : impl_->meta_pending)
		impl_->meta(claim, command, true);
	impl_->meta_pending.clear();
	for (const auto &proposal : impl_->controller_pending)
		impl_->commit_controller(proposal);
	impl_->controller_pending.clear();
	for (const auto &command : impl_->pending)
	{
		auto entity = impl_->entities.at(command.target);
		if (auto instance = impl_->instance(entity, command.system); instance && instance->advanced)
		{
			if (instance->held)
				impl_->operate(entity, command.system, command.output, true);
		}
		else if (auto instance = impl_->instance(entity, command.system))
			apply(command.output, instance->engine ? &*instance->engine : nullptr,
			      instance->steering ? &*instance->steering : nullptr, instance->radar ? &*instance->radar : nullptr);
		else
			apply(command.output, legacy_component<Engine>(entity), legacy_component<Steering>(entity),
			      legacy_component<Radar>(entity));
	}
	impl_->pending.clear();
	if (impl_->scenario)
	{
		const auto bodies = inspect();
		impl_->scenario->begin_tick(ticks(), impl_->slot_entities, bodies);
		std::vector<ScenarioKnowledgeSnapshot> snapshots;
		for (const auto &reader : impl_->scenario->knowledge_readers())
		{
			const auto board = impl_->knowledge.project({reader.ship, reader.console});
			snapshots.push_back({reader, board.records, board.shared});
		}
		impl_->scenario->supply_knowledge(snapshots);
		impl_->scenario->step();
		impl_->scenario_spawns();
		impl_->scenario_publications();
	}
	impl_->brain_snapshots.clear();
	impl_->inbox_snapshots.clear();
	impl_->observation_snapshots.clear();
	impl_->command_snapshots.clear();
	impl_->controller_inboxes.clear();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
		{
			auto &snapshot = impl_->command_snapshots[id];
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				auto &section = snapshot.emplace_back();
				for (const auto &command : controller.commands)
					section.push_back(command.value);
			}
		}
	for (const auto &[id, entity] : impl_->entities)
		for (const auto &console : impl_->definition(id).consoles)
		{
			const ConsoleAddress address{id, console.id};
			impl_->observation_snapshots.emplace(address, impl_->capture_console_observation(address));
		}
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				const ConsoleAddress address{id, controller.definition.console};
				if (!controller.human)
				{
					const ControllerAddress target{id, controller.definition.console, controller.definition.id};
					impl_->controller_inboxes.emplace(target, impl_->knowledge.peek_controller(target));
				}
				if (!impl_->brain_snapshots.contains(address))
					impl_->brain_snapshots.emplace(address, impl_->knowledge.project(address));
			}
	impl_->autonomous_equipment.each(
	    [&](flecs::entity child, const Equipment &fit, const BrainRuntime &)
	    {
		    if (fit.held)
			    return;
		    const auto entity = child.parent().get<StableId>().id;
		    const auto owner = impl_->system_owner(entity, fit.id);
		    if (!impl_->brain_snapshots.contains(owner))
			    impl_->brain_snapshots.emplace(owner, impl_->knowledge.project(owner));
		    impl_->inbox_snapshots.emplace(std::pair{entity, fit.id}, impl_->knowledge.peek_system(entity, fit.id));
	    });
	impl_->step_starts.clear();
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    impl_->step_starts.emplace(id.id, pose.position);
	    });
	// Explicit fixed-tick phases. Each pre-collision row operates only on itself,
	// so archetype/table ordering cannot affect gameplay. No wall-clock progress.
	impl_->controllers.run();
	impl_->run_basic_controllers();
	impl_->run_basic_steering();
	impl_->run_basic_engines();
	impl_->run_advanced_equipment();
	impl_->integrate.run();
	impl_->resolve_collisions.run();
	impl_->index_contacts.run();
	if (impl_->scenario)
	{
		std::vector<ScenarioMotionSegment> motion;
		impl_->bodies.each(
		    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
		    {
			    const auto from = impl_->step_starts.at(id.id);
			    motion.push_back({id.id, {from.x, from.y}, {pose.position.x, pose.position.y}});
		    });
		impl_->scenario->evaluate(motion);
		impl_->scenario_publications();
	}
	++impl_->ecs.get_mut<Clock>().tick;
	impl_->knowledge.tick(ticks());
	impl_->observation_snapshots.clear();
	impl_->command_snapshots.clear();
	impl_->controller_inboxes.clear();
	impl_->brain_snapshots.clear();
	impl_->inbox_snapshots.clear();
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
		if (auto engine = legacy_component<Engine>(impl_->entities.at(body.id)))
			item.forward_thrust = engine->enabled ? engine->forward_output : 0;
		if (auto systems = fittings(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.engine && system.engine->enabled)
					item.forward_thrust = std::max(item.forward_thrust, system.engine->forward_output);
		if (auto systems = gameplay_systems(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.flight)
					item.forward_thrust =
					    std::max(item.forward_thrust, std::max(0, system.flight->state.actual_longitudinal));
		result.push_back(std::move(item));
	}
	return result;
}
Json World::public_board(EntityId entity) const
{
	return impl_->knowledge.public_board(entity);
}
std::optional<Observation> World::observe(std::uint64_t console) const
{
	auto found = impl_->claims.find(console);
	if (found == impl_->claims.end())
		return {};
	auto entity = impl_->entities.at(found->second.ship);
	const auto &seat = *impl_->seat(found->second.ship, found->second.console);
	Observation result = impl_->capture_console_observation(found->second);
	result.gameplay = impl_->knowledge.project_json(found->second);
	if (entity.has<EntityControls>())
		result.gameplay["controllers"] = impl_->project_controllers(found->second);

	auto &buttons = result.gameplay["controls"] = Json::array();
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
	const bool away = impl_->away.contains(found->second) && impl_->away.at(found->second);
	button("away", away ? "Resume control" : "Away", "away", {}, away ? 0 : 1);
	if (entity.has<EntityControls>())
	{
		const auto &controls = entity.get<EntityControls>();
		result.gameplay["profile"] = controls.layout.profiles.at(controls.profiles.at(seat.id)).id;
		for (std::size_t i = 0; i < controls.layout.profiles.size(); ++i)
			button("profile." + controls.layout.profiles[i].id, controls.layout.profiles[i].id, "preset", {}, i);
		for (const auto &controller : controls.controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					const auto &channel = controller.definition.commands[i];
					const auto &state = controller.commands[i];
					const auto step = std::max<std::int64_t>(1, (channel.maximum - channel.minimum) / 20);
					std::set<std::int64_t> values;
					if (channel.type == KnowledgeType::Boolean)
						values = {0, 1};
					else
						values = {std::clamp(state.value - step, channel.minimum, channel.maximum),
						          std::clamp(state.value + step, channel.minimum, channel.maximum)};
					if (channel.minimum <= 0 && channel.maximum >= 0)
						values.insert(0);
					for (const auto value : values)
					{
						if (state.request_id == std::numeric_limits<std::uint64_t>::max())
							continue;
						OperatingCommand command{found->second.ship,
						                         std::string(operating_action(channel.action).name),
						                         value,
						                         {},
						                         controller.definition.id,
						                         channel.id,
						                         controller.generation,
						                         channel.semantics == CommandSemantics::Request ? state.request_id + 1
						                                                                        : 0};
						auto descriptor = encode_operating_command(command);
						descriptor["id"] =
						    "controller." + controller.definition.id + "." + channel.id + "." + std::to_string(value);
						descriptor["label"] = controller.definition.id + " " + channel.id + " " + std::to_string(value);
						buttons.push_back(std::move(descriptor));
					}
				}
	}
	if (entity.has<EntityControls>())
		for (const auto &controller : entity.get<EntityControls>().controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (const auto &name : impl_->definition(found->second.ship).message_templates)
				{
					OperatingCommand command{
					    found->second.ship,   "message." + name, 0, {}, controller.definition.id, {},
					    controller.generation};
					if (!impl_->meta(console, command, false))
						continue;
					auto descriptor = encode_operating_command(command);
					descriptor["id"] = "message." + controller.definition.id + "." + name;
					descriptor["label"] = "Send " + name;
					buttons.push_back(std::move(descriptor));
				}
	for (const auto &name : seat.controlled_systems)
		if (impl_->meta(
		        console,
		        {found->second.ship, "delegate", impl_->delegated[found->second.ship].contains(name) ? 0 : 1, name},
		        false))
			button("delegate." + name,
			       (impl_->delegated[found->second.ship].contains(name) ? "Take control " : "Delegate ") + name,
			       "delegate", name, impl_->delegated[found->second.ship].contains(name) ? 0 : 1);
	const auto &spec = impl_->definition(found->second.ship);
	for (const auto &p : spec.presets)
		if (impl_->preset.contains(found->second) && p.id == impl_->preset.at(found->second) &&
		    p.summaries.contains(seat.id))
			for (const auto &summary : p.summaries.at(seat.id))
				for (std::size_t level_index = 0; level_index < summary.levels.size(); ++level_index)
				{
					const auto level = summary.levels[level_index];
					button("summary." + summary.system + "." + summary.channel + "." + std::to_string(level),
					       summary.labels.empty() ? summary.channel + " " + std::to_string(level)
					                              : summary.labels.at(level_index),
					       "summary." + summary.channel, summary.system, level);
				}
	for (std::size_t i = 0; i < spec.presets.size(); ++i)
		if (spec.presets[i].delegated.contains(seat.id))
			button("preset." + spec.presets[i].id, spec.presets[i].id, "preset", {}, i);
	for (const auto &name : spec.publications)
		if (impl_->meta(console, {found->second.ship, "publish." + name, 0, {}}, false))
			button("publish." + name, "Publish " + name, "publish." + name, {}, 0);
	for (const auto &name : spec.message_templates)
		for (const auto &system : seat.controlled_systems)
			if (impl_->meta(console, {found->second.ship, "message." + name, 0, system}, false))
				button("message." + name + "." + system, "Send " + name, "message." + name, system, 0);
	for (const auto &state : result.systems)
	{
		const auto &id = state.id;
		const auto instance = impl_->instance(entity, id);
		if (instance && instance->advanced)
		{
			if (state.controlled)
				for (const auto &action : operating_actions)
				{
					const std::string field = action.action == OperatingAction::Speed     ? "desired_speed"
					                          : action.action == OperatingAction::Heading ? "desired_heading"
					                                                                      : std::string(action.name);
					const auto current = state.telemetry.contains(field) ? state.telemetry.at(field) : 0;
					std::int64_t maximum = action.maximum, minimum = action.minimum;
					if (action.action == OperatingAction::Speed)
					{
						maximum = state.maximum;
						minimum = state.telemetry.contains("minimum_speed") ? state.telemetry.at("minimum_speed") : 0;
					}
					if (const auto power = (*gameplay_systems(entity)).power)
					{
						const auto &binding = power->definition.systems[instance->entity.get<ResourceBinding>().slot];
						const auto &network = power->definition.network;
						if (action.action == OperatingAction::GeneratorOutput && binding.generator != no_resource)
							maximum = network.generators[binding.generator].maximum_rate;
						if (action.action == OperatingAction::ChargeLimit && binding.store != no_resource)
							maximum = network.stores[binding.store].charge_rate;
						if (action.action == OperatingAction::DischargeLimit && binding.store != no_resource)
							maximum = network.stores[binding.store].discharge_rate;
						if (action.action == OperatingAction::CoolingAllocation && binding.cooling != no_resource)
							maximum = network.cooling_links[binding.cooling].capacity_rate;
						if (action.action == OperatingAction::GroupAllocation && binding.group != no_resource)
							maximum = std::max<std::int64_t>(network.groups[binding.group].budget_rate, state.maximum);
					}
					const std::int64_t step = action.boolean   ? 1
					                          : maximum > 1000 ? std::max<std::int64_t>(1, maximum / 20)
					                                           : 100;
					for (auto value : {(action.action == OperatingAction::Longitudinal ||
					                    action.action == OperatingAction::Lateral ||
					                    action.action == OperatingAction::ManualSteering)
					                       ? std::int64_t{-1000}
					                   : action.boolean ? std::int64_t{1 - current}
					                                    : std::clamp(current - step, minimum, maximum),
					                   (action.action == OperatingAction::Longitudinal ||
					                    action.action == OperatingAction::Lateral ||
					                    action.action == OperatingAction::ManualSteering)
					                       ? std::int64_t{1000}
					                       : std::clamp(current + step, minimum, maximum),
					                   std::int64_t{0}})
						if (action.action != OperatingAction::SelectContact &&
						    impl_->operate(entity, id, {action.action, value}, false))
							button("operate." + id + "." + std::string(action.name) + "." + std::to_string(value),
							       id + " " + std::string(action.name) + " " + std::to_string(value),
							       std::string(action.name), id, value);
				}
		}
	}
	for (const auto &system : result.systems)
		if (system.controlled && system.kind == OperatingSystem::Engine)
			for (const auto &contact : result.contacts)
				if (impl_->operate(entity, system.id,
				                   {OperatingAction::SelectContact, static_cast<std::int64_t>(contact.id)}, false))
					button("select." + system.id + "." + std::to_string(contact.id),
					       "Select " + contact.definition + " " + std::to_string(contact.id), "selected_contact",
					       system.id, contact.id);
	return result;
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
				if (!system.brain)
					continue;
				if (++controllers > 32)
				{
					result["truncated"] = result["truncated"].get<int>() + 1;
					continue;
				}
				const auto &name = (*fittings(entity)).values[i].id;
				result["controllers"].push_back({{"id", std::to_string(id) + "." + name},
				                                 {"context", "system"},
				                                 {"tree", system.brain->tree},
				                                 {"active_states", system.brain->debug.active},
				                                 {"transition_reason", system.brain->debug.transition},
				                                 {"diagnostics", system.brain->debug.diagnostics}});
				for (const auto &state : system.brain->debug.active)
					if (result["active_states"].size() < 128)
						result["active_states"].push_back(state);
				for (const auto &d : system.brain->debug.diagnostics)
					if (result["diagnostics"].size() < 32)
						result["diagnostics"].push_back(d);
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
	for (const auto &command : impl_->pending)
		if (result["pending_outputs"].size() < 32)
			result["pending_outputs"].push_back({{"ship", command.target},
			                                     {"system", command.system},
			                                     {"action", std::string(operating_action(command.output.action).name)},
			                                     {"value", command.output.value}});
		else
			result["truncated"] = result["truncated"].get<int>() + 1;
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
	if (!impl_->pending.empty() || !impl_->meta_pending.empty() || !impl_->controller_pending.empty())
		throw std::runtime_error("Save at a tick boundary after admitted commands execute");
	Json result{{"schema", 1},           {"tick", ticks()},       {"map", impl_->map},
	            {"ships", impl_->ships}, {"packs", impl_->packs}, {"bodies", Json::array()}};
	if (impl_->explicit_stations || impl_->instance_systems || impl_->gameplay)
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
				           {"effects", system.effects},
				           {"summary", system.summary},
				           {"result_available", system.result.available},
				           {"result_success", system.result.success},
				           {"reason", system.result.reason},
				           {"result_id", system.result.id},
				           {"heat_remainder", system.heat_remainder},
				           {"enabled", (*fittings(entity)).values[i].enabled}};
				if (const auto timed = (*fittings(entity)).values[i].controller)
					value["timed"] = save_tree(impl_->programs[timed->program], timed->cursor);
				if (const auto radar = (*fittings(entity)).values[i].radar)
					value["radar_enabled"] = radar->enabled;
				if (system.flight)
					value["flight"] = save_flight(system.flight->definition, system.flight->state);
				if (system.brain)
					value["brain"] = state_tree::save(system.brain->program, system.brain->continuation);
				data.push_back(std::move(value));
			}
			if (systems->power)
				result["bodies"].back()["resources"] = save_ship_resources(*systems->power);
		}
		if (entity.has<EntityControls>())
			result["bodies"].back()["console_controllers"] = impl_->save_controllers(entity);
		if (!entity.has<NamedEquipment>() && impl_->definition(id).explicit_stations)
		{
			auto &saved = result["bodies"].back()["controllers"] = Json::object();
			for (auto child : entity.get<EquipmentIndex>().values)
				if (auto controller = child.try_get<Controller>())
					saved[child.get<Equipment>().id] =
					    save_tree(impl_->programs[controller->program], controller->cursor);
		}
	}
	if (impl_->gameplay)
	{
		result["schema"] = 4;
		result["knowledge"] = impl_->knowledge.save();
		result["away"] = Json::array();
		for (const auto &[a, v] : impl_->away)
			result["away"].push_back({{"address", a}, {"value", v}});
		result["delegated"] = impl_->delegated;
		result["preset"] = Json::array();
		for (const auto &[a, v] : impl_->preset)
			if (!v.empty())
				result["preset"].push_back({{"address", a}, {"value", v}});
		result["next_entity"] = impl_->next_entity;
		if (impl_->scenario)
			result["scenario"] = impl_->scenario->save();
	}
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			result["schema"] = 5;
	return result;
}
void World::restore(const Catalog &content, const Json &saved)
{
	if (!saved.at("tick").is_number_integer() || saved.at("tick") < 0 ||
	    saved.at("tick") > std::numeric_limits<std::int64_t>::max())
		throw std::runtime_error("Saved state invalid tick");
	if ((saved.at("schema") != 1 && saved.at("schema") != 2 && saved.at("schema") != 3 && saved.at("schema") != 4 &&
	     saved.at("schema") != 5) ||
	    saved.at("packs") != Json(content.ordered_packs))
		throw std::runtime_error("Saved state schema/content identity mismatch");
	World candidate;
	candidate.load(content, saved.at("map"), saved.at("ships").get<std::map<std::string, std::string>>());
	if (((candidate.impl_->instance_systems && saved.at("schema") != 3 && saved.at("schema") != 4 &&
	      saved.at("schema") != 5) ||
	     (candidate.impl_->gameplay && saved.at("schema") != 4 && saved.at("schema") != 5)) ||
	    (candidate.impl_->explicit_stations && saved.at("schema") == 1))
		throw std::runtime_error("Explicit Stations require a schema-2 save");
	if (saved.at("schema") >= 4 && candidate.impl_->scenario)
	{
		candidate.impl_->scenario->restore(content, saved.at("map"), saved.at("scenario"));
		for (const auto &[id, definition] : candidate.impl_->scenario->runtime().spawned_entities())
			if (candidate.impl_->entities.contains(id))
				throw std::runtime_error("Spawned identity overlaps initial entity");
			else
				candidate.impl_->create(id, definition, {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
		candidate.impl_->next_entity = saved.at("next_entity").get<EntityId>();
		if (!candidate.impl_->entities.empty() &&
		    (candidate.impl_->next_entity <= candidate.impl_->entities.rbegin()->first ||
		     candidate.impl_->next_entity > 1000000001ULL))
			throw std::runtime_error("Saved next entity identity overlaps roster");
	}
	if (saved.at("schema") >= 4)
	{
		const auto &value = saved.at("next_entity");
		if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 1))
			throw std::runtime_error("Invalid saved entity allocator");
		candidate.impl_->next_entity = value.get<EntityId>();
		if (candidate.impl_->next_entity < 1 || candidate.impl_->next_entity > 1000000001ULL ||
		    (!candidate.impl_->entities.empty() &&
		     candidate.impl_->next_entity <= candidate.impl_->entities.rbegin()->first))
			throw std::runtime_error("Saved entity allocator overlaps roster");
	}
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
		if (entity.has<EntityControls>())
		{
			if (saved.at("schema") != 5)
				throw std::runtime_error("Controller state requires schema 5");
			candidate.impl_->restore_controllers(entity, value.at("console_controllers"));
		}
		else if (value.contains("console_controllers"))
			throw std::runtime_error("Unexpected saved Controller layout");

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
		auto engine = legacy_component<Engine>(entity);
		if (operation.speed < 0 || operation.speed > fixed::unit || (engine && operation.speed > engine->max_speed))
			throw std::runtime_error("Saved state invalid speed request");
		entity.set<Pose>({restored.position, restored.heading}).set<Motion>({restored.velocity, restored.remainder});
		if (entity.has<NamedEquipment>() &&
		    (operation.speed || operation.acceleration_remainder || operation.turn_remainder ||
		     operation.heading != restored.heading || !operation.engine_enabled || !operation.radar_enabled ||
		     !operation.tree.state.empty() || operation.tree.elapsed || operation.tree.entered))
			throw std::runtime_error("Instance ships cannot carry inactive legacy operation state");
		candidate.impl_->restore_operation(entity, operation);
		if (auto systems = fittings(entity))
		{
			const auto &states = value.at("systems");
			if (!states.is_object() || states.size() != systems->values.size())
				throw std::runtime_error("Fitted System roster mismatch");
			for (auto system : systems->values)
			{
				for (const auto *field : {"speed", "heading", "acceleration_remainder", "turn_remainder"})
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
				    state.contains("flight") != (system.flight != nullptr) ||
				    state.contains("brain") != (system.brain != nullptr))
					throw std::runtime_error("Advanced continuation binding mismatch");
				if (auto timed = (*fittings(entity)).values[i].controller)
					timed->cursor =
					    load_tree(candidate.impl_->programs[timed->program], state.at("timed").get<TreeState>());
				else if (state.contains("timed"))
					throw std::runtime_error("Unfitted timed controller continuation");
				if (system.flight)
				{
					system.flight->state = load_flight(system.flight->definition, state.at("flight"));
					if (system.flight->state.observed_tick > saved.at("tick").get<std::uint64_t>())
						throw std::runtime_error("Saved flight observation is in the future");
				}
				if (system.brain)
					system.brain->continuation = state_tree::load(system.brain->program, state.at("brain"));
				system.effects = state.at("effects").get<std::set<std::string>>();
				system.summary = state.at("summary").get<std::map<std::string, std::int64_t>>();
				if (system.effects.size() > 65536 || system.summary.size() > 256)
					throw std::runtime_error("Controller continuation limits exceeded");
				for (const auto &receipt : system.effects)
					if (receipt.empty() || receipt.size() > 1024)
						throw std::runtime_error("Invalid controller effect receipt");
				system.heat_remainder = state.at("heat_remainder");
				if (system.heat_remainder < 0 || system.heat_remainder >= rate)
					throw std::runtime_error("Invalid operating heat remainder");
				(*fittings(entity)).values[i].enabled = state.at("enabled");
				if (auto radar = (*fittings(entity)).values[i].radar)
					radar->enabled = state.at("radar_enabled").get<bool>();
				system.result.available = state.at("result_available");
				system.result.success = state.at("result_success");
				system.result.reason = state.at("reason");
				system.result.id = state.at("result_id");
				if (system.result.reason.size() > 4096)
					throw std::runtime_error("Controller result reason too long");
			}
			if (systems->power)
				restore_ship_resources(*systems->power, value.at("resources"));
		}
		if (!entity.has<NamedEquipment>() && candidate.impl_->definition(restored.id).explicit_stations)
		{
			const auto &states = value.at("controllers");
			if (!states.is_object())
				throw std::runtime_error("Invalid System controller states");
			std::size_t count = 0;
			for (auto child : entity.get<EquipmentIndex>().values)
				if (auto controller = child.try_get_mut<Controller>())
				{
					++count;
					controller->cursor = load_tree(candidate.impl_->programs[controller->program],
					                               states.at(child.get<Equipment>().id).get<TreeState>());
				}
			if (states.size() != count)
				throw std::runtime_error("System controller roster mismatch");
		}
	}
	if (saved.at("schema") >= 4)
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
		candidate.impl_->delegated = saved.at("delegated").get<std::map<EntityId, std::set<std::string>>>();
		for (const auto &[ship, names] : candidate.impl_->delegated)
		{
			if (!candidate.impl_->entities.contains(ship))
				throw std::runtime_error("Delegation names missing ship");
			for (const auto &name : names)
				if (!candidate.impl_->entities.at(ship).has<NamedEquipment>() ||
				    !candidate.impl_->instance(candidate.impl_->entities.at(ship), name))
					throw std::runtime_error("Delegation names missing System");
		}
		for (const auto &entry : saved.at("preset"))
		{
			const auto address = entry.at("address").get<ConsoleAddress>();
			const auto name = entry.at("value").get<std::string>();
			if (!candidate.impl_->entities.contains(address.ship) ||
			    !candidate.impl_->seat(address.ship, address.console))
				throw std::runtime_error("Saved preset Console missing");
			const auto &choices = candidate.impl_->definition(address.ship).presets;
			if (std::none_of(choices.begin(), choices.end(),
			                 [&](const auto &p)
			                 {
				                 return p.id == name && p.delegated.contains(address.console);
			                 }))
				throw std::runtime_error("Saved preset missing");
			candidate.impl_->preset[address] = name;
		}
		for (const auto &entry : saved.at("away"))
		{
			auto a = entry.at("address").get<ConsoleAddress>();
			if (!candidate.impl_->entities.contains(a.ship) || !candidate.impl_->seat(a.ship, a.console))
				throw std::runtime_error("Unknown Away Console");
			candidate.impl_->away[a] = entry.at("value");
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
			if (saved.at("schema") >= 4)
			{
				if (!candidate.can_claim(console, address.ship, address.console))
					throw std::runtime_error("Saved state invalid claim");
				candidate.impl_->claims.emplace(console, address);
			}
			else if (!candidate.claim(console, address.ship, address.console))
				throw std::runtime_error("Saved state invalid claim");
	}
	if (saved.at("schema") >= 4)
		for (const auto &[id, entity] : candidate.impl_->entities)
			candidate.impl_->refresh_control(id, false);
	auto tick = saved.at("tick").get<std::int64_t>();
	if (tick < 0)
		throw std::runtime_error("Saved state invalid tick");
	candidate.impl_->ecs.get_mut<Clock>().tick = static_cast<std::uint64_t>(tick);
	candidate.impl_->index_contacts.run();
	impl_ = std::move(candidate.impl_);
}
} // namespace phoenix
