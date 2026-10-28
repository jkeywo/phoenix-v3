module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::clear_presentation_snapshots() -> void
{
	presentation.clear();
}

Runtime::Runtime()
{
	ecs.set<Clock>({0});
	property_applications.initialise(ecs);
	active_rules.initialise(ecs);
	bodies = ecs.query_builder<const StableId, const Definition, const Pose, const Motion>().cached().build();
	flight_native = std::make_unique<NativeFlightActuators>(ecs, native_flight_ports());
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
	projectiles.initialise(ecs);
	docking_query = ecs.query_builder<const StableId, DockingState>().with(flecs::Disabled).optional().cached().build();
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
					            collisions.push_back(
					                {ids[row].id, &poses[row], &motions[row], {poses[row], motions[row], shapes[row]}});
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
			                              step_starts.at(collisions[pair.a].id), step_starts.at(collisions[pair.b].id),
			                              true))
			            {
				            dock_collision_response(pair.a, before_a);
				            dock_collision_response(pair.b, before_b);
				            collision_damage(collisions[pair.a].id, collisions[pair.b].id, before_a, before_b);
				            flight_native->cancel_ship_impulse(process_machinery, entities.at(collisions[pair.a].id));
				            flight_native->cancel_ship_impulse(process_machinery, entities.at(collisions[pair.b].id));
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
						            flight_native->cancel_ship_impulse(process_machinery,
						                                               entities.at(collisions[a].id));
						            flight_native->cancel_ship_impulse(process_machinery,
						                                               entities.at(collisions[b].id));
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

auto Runtime::compile(const Catalog &source, const DirectedCatalog &directed_catalog, SharedPropertyCatalog properties)
    -> void
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
		auto assembly = compile_world_entity(source, content, name, directed_catalog, rule_templates,
		                                     property_catalogue, ability_catalog, process_catalog, resource_catalogue);
		visual_scales[name] = assembly.visual_scale;
		if (!assembly.participants.empty())
			comms_rosters[name] = std::move(assembly.participants);
		definitions.emplace(name, std::move(assembly.definition));
	}
}

auto Runtime::create(EntityId id, const std::string &definition, const Json &transform) -> void
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
		auto child = ecs.entity().child_of(entity).set<Equipment>({v.id, v.kind, i, v.maximum, v.advanced, v.enabled});
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
			if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Shield)
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

auto Runtime::definition(EntityId ship) const -> const CompiledDefinition &
{
	return definitions.at(entities.at(ship).get<Definition>().name);
}

auto Runtime::native_owner_definitions(EntityId id) const -> NativeOwnerDefinitions
{
	const auto &data = definition(id);
	return {data.personnel, data.logistics, data.abilities, data.processes, data.psychic, data.tags};
}

auto Runtime::seat(EntityId ship, const std::string &name) const -> const ConsoleSpec *
{
	const auto &seats = definition(ship).consoles;
	for (const auto &seat : seats)
		if (seat.id == name || (name.empty() && seats.size() == 1))
			return &seat;
	return nullptr;
}

auto Runtime::instance(flecs::entity ship, const std::string &id) const -> std::optional<EquipmentRef>
{
	const auto &index = ship.get<EquipmentIndex>().by_id;
	const auto found = index.find(id);
	return found == index.end() ? std::optional<EquipmentRef>{} : equipment_ref(found->second);
}

auto Runtime::console_operator(const ConsoleAddress &address) const -> std::uint64_t
{
	return ownership.operator_for(address);
}

auto Runtime::command_address(std::uint64_t claimant, const OperatingCommand &command) const -> ConsoleAddress
{
	auto address = ownership.claims().at(claimant);
	if (!command.console.empty())
		address.console = command.console;
	return address;
}

auto Runtime::system_owner(EntityId ship, const std::string &system) const -> ConsoleAddress
{
	if (definition(ship).layout)
		return {ship, definition(ship).observation_owners.at(system)};
	for (const auto &c : definition(ship).consoles)
		if (std::find(c.controlled_systems.begin(), c.controlled_systems.end(), system) != c.controlled_systems.end())
			return {ship, c.id};
	throw std::runtime_error("System has no Console owner");
}

auto Runtime::flight_action(OperatingAction action) const -> std::optional<FlightCommandKind>
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

auto Runtime::resource_action(OperatingAction action) const -> std::optional<ResourceControl>
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
} // namespace phoenix::world_detail
