module;
#include "native_headers.hpp"

module phoenix.simulation;
import :combat_bindings;

namespace phoenix::world_detail
{
auto Runtime::report_weapon_ammunition(EntityId ship, flecs::entity weapon) -> void
{
	const auto &mechanism = weapon.get<CombatMechanism>();
	const auto &definition = mechanism.definition.get<WeaponDefinition>();
	if (definition.magazines.empty())
		return;
	const auto &state = mechanism.get<WeaponState>();
	const auto &magazine = definition.magazines.at(state.selected_magazine);
	const auto magazines = weapon_magazines(entities.at(ship), mechanism);
	const auto &store = magazines.at(magazine);
	const auto &stock = *store.stock;
	const auto &resource = stock.selected();
	if (resource.empty())
		return;
	const auto loaded = state.loaded_magazine == state.selected_magazine && state.loaded_resource == resource
	                        ? state.loaded_quantity
	                        : 0;
	const auto available = stock.available(resource);
	const auto quantity = available + loaded;
	const auto *mode = directed_mode(ship, mechanism.definition.id);
	const auto use = mode && mode->ammo_use > 0 ? mode->ammo_use : definition.ammo_use;
	const bool empty = quantity < use;
	const bool low = !empty && quantity <= stock.definition().capacity / 4 / stock.size(resource);
	const auto now = ecs.get<Clock>().tick;
	const auto &system = mechanism.definition.id;
	const auto &label = stock.catalogue().at(resource).label;
	constexpr auto empty_status = std::numeric_limits<KnowledgeKey>::max() - 8;
	constexpr auto low_status = empty_status - 1;
	constexpr auto blocked_status = empty_status - 2;
	const auto send = [&](const std::string &source, const std::string &affected, const std::string &event)
	{
		for (const auto &recipient : equipment_alert_recipients(ship, source, affected))
			knowledge.emit_equipment_alert(ship, source, recipient, event, system, label, now, resource);
	};
	if (knowledge.status_change(ship, system, low_status, low, now, 0, 0, 0, false) && low)
		send(system, magazine, "ammo-low");
	if (knowledge.status_change(ship, system, empty_status, empty, now, 0, 0, 0, false) && empty)
		send(system, magazine, "ammo-empty");
	const bool blocked = store.operating && weapon.get<Equipment>().enabled && !state.unloading &&
	                     state.loaded_magazine < 0 && available < use &&
	                     load_duration(entities.at(ship), mechanism) > 0 &&
	                     directed_operation(ship, system, DirectedOperationKind::Launch);
	if (knowledge.status_change(ship, system, blocked_status, blocked, now, 0, 0, 0, false) && blocked)
		send(magazine, system, "reload-blocked");
}

auto Runtime::weapon_magazines(flecs::entity ship, const CombatMechanism &weapon) const -> WeaponMagazines
{
	WeaponMagazines result;
	for (const auto &id : weapon.definition.get<WeaponDefinition>().magazines)
	{
		const auto magazine = instance(ship, id)->entity;
		const auto &m = magazine.get<CombatMechanism>();
		result.emplace(id, WeaponMagazine{&m.definition.get<MagazineDefinition>(),
		                                  &physical_stock(ship, m.get<MagazineState>().stock),
		                                  magazine.get<Equipment>().enabled && !structurally_disabled(ship, id)});
	}
	return result;
}

auto Runtime::magazine_ammunition(const CombatMechanism &magazine, const std::string &resource) const
    -> const AmmunitionDefinition &
{
	return native_magazine_ammunition(magazine.definition.get<MagazineDefinition>(), resource);
}

auto Runtime::magazine_ammunition(flecs::entity ship, const CombatMechanism &magazine) const
    -> const AmmunitionDefinition &
{
	return magazine_ammunition(magazine, physical_stock(ship, magazine.get<MagazineState>().stock).selected());
}

auto Runtime::weapon_ammunition(flecs::entity ship, const CombatMechanism &weapon, std::int64_t magazine) const
    -> const AmmunitionDefinition &
{
	return native_weapon_ammunition(weapon, weapon_magazines(ship, weapon), magazine);
}

auto Runtime::load_duration(flecs::entity ship, const CombatMechanism &m, std::int64_t magazine) const -> std::int64_t
{
	if (m.definition.get<WeaponDefinition>().magazines.empty())
		return 0;
	const auto child = instance(ship, m.definition.id)->entity;
	const auto *state = child.try_get<DirectedRuntime>();
	return native_weapon_load_duration(m, state,
	                                   state ? directed_definition(ship.get<StableId>().id, m.definition.id) : nullptr,
	                                   weapon_magazines(ship, m), magazine);
}

auto Runtime::weapon_payload(flecs::entity ship, const CombatMechanism &m) const -> const ProjectileDefinition &
{
	return native_weapon_payload(m, weapon_magazines(ship, m));
}

auto Runtime::weapon_contact(flecs::entity ship, const CombatMechanism &m, ConsoleContacts &cache) const
    -> std::optional<WeaponAim>
{
	const auto id = ship.get<StableId>().id;
	const bool intercept = directed_targeting(id, m.definition.id) == DirectedTargetingKind::Intercept;
	if (intercept && m.definition.get<WeaponDefinition>().intercept_classes.empty())
		return {};
	const auto wanted =
	    intercept ? 7000000000000000ULL + m.get<WeaponState>().intercept : directed_target(id, m.definition.id);
	if (intercept && !m.get<WeaponState>().intercept)
		return {};
	auto aim = directed_contact(id, m.definition.id, wanted, cache, !intercept);
	if (!aim)
		return {};
	if (!intercept)
	{
		if (aim->body.disposition == "hostile")
			return aim;
		// Offensive authority comes from permitted hostile evidence, including
		// retained acquired identity. Scenario roles do not identify anonymous tracks.
		return {};
	}
	for (auto missile : projectile_rows())
	{
		const auto &p = missile.get<Projectile>();
		if (p.id != m.get<WeaponState>().intercept || missile.get<Destructible>().durability <= 0)
			continue;
		aim->altitude = p.altitude;
		aim->vz = p.vz;
		return aim;
	}
	return {};
}

auto Runtime::weapon_mount(flecs::entity ship, const CombatMechanism &m) const -> std::pair<fixed::Vec, std::int64_t>
{
	const auto &d = m.definition;
	const auto &pose = ship.get<Pose>();
	fixed::Vec mount{d.x, d.y};
	auto heading = pose.heading + d.bearing;
	if (!d.get<WeaponDefinition>().turret.empty())
	{
		const auto turret = instance(ship, d.get<WeaponDefinition>().turret)->entity;
		if (!turret.get<Equipment>().enabled || structurally_disabled(ship, d.get<WeaponDefinition>().turret))
			return {{}, -1};
		const auto &t = turret.get<CombatMechanism>();
		heading = pose.heading + t.get<TurretState>().heading + d.bearing;
		mount = combat_world(mount, Pose{{t.definition.x, t.definition.y}, t.get<TurretState>().heading});
	}
	return {combat_world(mount, pose), combat_angle(heading)};
}

auto Runtime::weapon_lead(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target,
                          fixed::Vec origin) const -> fixed::Vec
{
	return directed_weapon_lead(m.definition.get<WeaponDefinition>(),
	                            target.bearing ? nullptr : &weapon_payload(ship, m), target, origin,
	                            ship.get<Motion>().velocity);
}

auto Runtime::weapon_covers(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target, fixed::Vec origin,
                            std::int64_t orientation, fixed::Vec lead) const -> bool
{
	return directed_weapon_covers(m.definition.get<WeaponDefinition>().range, m.definition.arc, target, origin,
	                              orientation, lead, altitude(ship.get<StableId>().id));
}

auto Runtime::prepare_weapons(flecs::entity ship) -> void
{
	release_directed_operations(ship);
	ConsoleContacts contacts_by_console;
	std::map<std::string, bool> manual_requests;
	if (ship.has<EntityControls>())
		for (auto &controller : ship.get_mut<EntityControls>().controllers)
			for (std::size_t i = 0; i < controller.commands.size(); ++i)
				if (controller.definition.commands[i].action == OperatingAction::WeaponFire)
				{
					auto &state = controller.commands[i];
					const bool manual = controller_command_human(controller, i) ||
					                    controller_operation_human(controller, "weapon_salvo");
					for (const auto index : controller.definition.commands[i].consumers)
						if (manual)
						{
							const auto child = ship.get<EquipmentIndex>().values.at(index);
							const auto &loaded = child.get<CombatMechanism>().get<WeaponState>();
							manual_requests[child.get<Equipment>().id] =
							    state.request_id && state.consumed < state.revision && state.value != 0 &&
							    loaded.loaded_quantity > 0 && !loaded.unloading;
						}
					if (state.consumed < state.revision)
					{
						state.consumed = state.revision;
						state.accepted = false;
					}
				}
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			reset_weapon_tick(m->get<WeaponState>(), child.try_get_mut<DirectedRuntime>());
			const auto &d = m->definition;

			if (d.kind != OperatingSystem::Weapon || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id))
				continue;
			if (directed_targeting(id, d.id) == DirectedTargetingKind::Intercept)
			{
				// Target identity belongs to the admitted Controller order.
				// Machinery tracks and fires only while current sensing permits it.
				const auto accepted = directed_target(id, d.id);
				m->get<WeaponState>().intercept = accepted >= 7000000000000000ULL && accepted < 8000000000000000ULL
				                                      ? accepted - 7000000000000000ULL
				                                      : 0;
			}
			const bool launch = directed_operation(id, d.id, DirectedOperationKind::Launch);
			if (m->get<WeaponState>().unloading || launch)
				advance_weapon_ammunition(*m, child.get_mut<DirectedRuntime>(), *directed_definition(id, d.id), launch,
				                          weapon_magazines(ship, *m));
		}
	// The fitting's unique directed owner supplies its selected native aiming solution.
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Turret)
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			const auto *binding = child.try_get<DirectedMountOwner>();
			if (!binding)
				continue;
			const auto owner = ecs.entity(binding->equipment);
			const auto &owner_id = owner.get<Equipment>().id;
			if (!owner.get<Equipment>().enabled || structurally_disabled(ship, owner_id) ||
			    directed_targeting(id, owner_id) == DirectedTargetingKind::Off)
				continue;
			const bool scanning = directed_operation(id, owner_id, DirectedOperationKind::Scan);
			const auto *weapon = scanning ? nullptr : owner.try_get<CombatMechanism>();
			const auto target =
			    weapon ? weapon_contact(ship, *weapon, contacts_by_console) : std::optional<WeaponAim>{};
			const auto scanner_aim = scanning ? sensor_aim(id, owner_id, contacts_by_console)
			                                  : std::optional<std::pair<fixed::Vec, std::int64_t>>{};
			if (!target && !scanner_aim)
				continue;
			const auto origin = combat_world({d.x, d.y}, pose);
			const auto lead = scanner_aim ? scanner_aim->first : weapon_lead(ship, *weapon, *target, origin);
			const auto bearing = scanner_aim ? scanner_aim->second : weapon->definition.bearing;
			const auto mount = weapon ? fixed::Vec{weapon->definition.x, weapon->definition.y} : fixed::Vec{};
			advance_directed_turret(
			    m->get<TurretState>(), d, origin, pose.heading, lead, bearing, mount,
			    effective_property(ship, d.id, PropertyKey::traverse, d.get<TurretDefinition>().traverse));
		}
	std::size_t planned_projectiles = projectile_rows().size();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
			if (manual_requests.contains(d.id) && !manual_requests.at(d.id))
				continue;
			if (m->get<WeaponState>().cooldown || !child.get<Equipment>().enabled ||
			    effective_property(ship, d.id, PropertyKey::cycling, 1000) == 0 || structurally_disabled(ship, d.id) ||
			    directed_operation(id, d.id, DirectedOperationKind::Scan) ||
			    directed_targeting(id, d.id) == DirectedTargetingKind::Off)
				continue;
			const auto target = weapon_contact(ship, *m, contacts_by_console);
			if (!target)
				continue;
			const auto [origin, orientation] = weapon_mount(ship, *m);
			if (orientation < 0)
				continue;
			const auto lead = weapon_lead(ship, *m, *target, origin);
			if (!weapon_covers(ship, *m, *target, origin, orientation, lead))
				continue;
			if (m->get<WeaponState>().unloading)
				continue;
			PreparedDirectedOperation prepared;
			prepared.aim = *target;
			prepared.origin = origin;
			prepared.bearing = combat_bearing(lead - origin);
			if (d.get<WeaponDefinition>().family != "beam")
			{
				if (planned_projectiles >= 8192)
					continue;
				const auto &name = d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine);
				const auto mag = instance(ship, name)->entity;
				const auto &a = mag.get<CombatMechanism>();
				if (!mag.get<Equipment>().enabled || structurally_disabled(ship, name))
					continue;
				prepared.magazine = name;
				auto &stock = physical_stock(ship, a.get<MagazineState>().stock);
				prepared.loaded = load_duration(ship, *m) != 0;
				prepared.resource = prepared.loaded ? m->get<WeaponState>().loaded_resource : stock.selected();
				prepared.ammunition = weapon_ammunition(ship, *m);
				const auto interval =
				    prepared.ammunition.interval ? prepared.ammunition.interval : d.ammunition().interval;
				prepared.cycle_ticks = std::max<std::int64_t>(
				    1, interval * 1000 / effective_property(ship, d.id, PropertyKey::cycling, 1000));
				prepared.projectile = weapon_payload(ship, *m);
				prepared.payloads = capture_directed_payloads(ship, *m, prepared.resource);
				(void)payload_capture_layout(prepared.payloads);
				(void)payload_capture_layout(prepared.projectile.destruction.payloads);
				if (prepared.loaded)
				{
					if (m->get<WeaponState>().loaded_magazine != m->get<WeaponState>().selected_magazine ||
					    m->get<WeaponState>().unloading)
						continue;
				}
				else
				{
					if (!stock.claim_stock("directed-launch:" + d.id, prepared.resource,
					                       d.get<WeaponDefinition>().ammo_use))
						continue;
					prepared.stock_claim = true;
				}
				++planned_projectiles;
			}
			else
			{
				prepared.payloads = capture_directed_payloads(ship, *m, {});
				(void)payload_capture_layout(prepared.payloads);
			}
			directed_operations.emplace(std::pair{id, d.id}, std::move(prepared));
			m->get<WeaponState>().ready = true;
			if (child.has<DirectedRuntime>())
				child.get_mut<DirectedRuntime>().ready = true;
		}
}

auto Runtime::fire_weapons(flecs::entity ship) -> void
{
	// Funding cannot retract the permitted aim used to prepare this operation.
	// Stock claims keep processors from consuming its ammunition before commit.
	const auto id = ship.get<StableId>().id;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>();
		    m && m->definition.kind == OperatingSystem::Weapon && m->get<WeaponState>().ready)
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			const auto at = directed_operations.find({id, d.id});
			if (at == directed_operations.end())
				continue;
			auto &prepared = at->second;
			const auto origin = prepared.origin;
			auto bearing = prepared.bearing;
			const auto operation = directed_mode(id, d.id)->operation;
			const auto before_shots = m->get<WeaponState>().shots;
			execute_directed(
			    ship, child, operation, m->get<WeaponState>().ready,
			    [&]()
			    {
				    if (operation == DirectedOperationKind::Beam)
				    {
					    record_interdiction_attack(id, directed_target(id, d.id));
					    fire_beam(ship, child, origin, bearing);
					    return true;
				    }
				    auto &a =
				        instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine))
				            ->entity.get_mut<CombatMechanism>();
				    auto &stock = physical_stock(ship, a.get<MagazineState>().stock);
				    if (!commit_weapon_launch(m->get<WeaponState>(), child.get_mut<DirectedRuntime>(), prepared, stock,
				                              d.id, projectile_rows().size()))
					    return false;
				    record_interdiction_attack(ship.get<StableId>().id, directed_target(ship.get<StableId>().id, d.id));
				    psychic_weapon_emission(ship.get<StableId>().id);
				    auto &s = ship.get_mut<CombatStructure>();
				    auto p = projectiles.prepare_launch(prepared,
				                                        {id, directed_target(id, d.id), d.id, altitude(id),
				                                         d.get<WeaponDefinition>().range, ship.get<Motion>().velocity},
				                                        s.random);
				    p.rule_captures = capture_payload_rule_facts(id, d.id, p.payloads);
				    p.destruction_rule_captures =
				        capture_payload_rule_facts(id, d.id, p.definition.destruction.payloads);
				    projectiles.publish(ecs, p);
				    emit_audio(ship, {SceneAudioKind::Shot, "weapon:" + d.id, directed_mode(id, d.id)->firing_audio});
				    return true;
			    });
			if (m->get<WeaponState>().shots > before_shots && ship.has<EntityControls>())
				for (auto &controller : ship.get_mut<EntityControls>().controllers)
					for (std::size_t i = 0; i < controller.commands.size(); ++i)
						if (controller.definition.commands[i].action == OperatingAction::WeaponFire &&
						    (controller_command_human(controller, i) ||
						     controller_operation_human(controller, "weapon_salvo")))
							for (const auto index : controller.definition.commands[i].consumers)
								if (ship.get<EquipmentIndex>().values.at(index).get<Equipment>().id == d.id &&
								    controller.commands[i].request_id)
									controller.commands[i].accepted = true;
		}
	release_directed_operations(ship);
}
} // namespace phoenix::world_detail
