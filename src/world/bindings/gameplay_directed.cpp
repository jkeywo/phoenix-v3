module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::release_directed_operations(flecs::entity ship) -> void
{
	const auto id = ship.get<StableId>().id;
	for (auto operation = directed_operations.begin(); operation != directed_operations.end();)
		if (operation->first.first == id)
		{
			if (operation->second.stock_claim)
			{
				const auto magazine = instance(ship, operation->second.magazine)->entity;
				auto &stock = physical_stock(ship, magazine.get<CombatMechanism>().get<MagazineState>().stock);
				stock.release_stock("directed-launch:" + operation->first.second);
			}
			operation = directed_operations.erase(operation);
		}
		else
			++operation;
}

auto Runtime::console_detection(const ConsoleAddress &owner, ConsoleContacts &cache) const -> const Observation &
{
	if (!cache.contains(owner))
		cache.emplace(owner, capture_sensor_observation(owner));
	return cache.at(owner);
}

auto Runtime::console_contacts(const ConsoleAddress &owner, ConsoleContacts &cache) const
    -> const std::vector<BodyView> &
{
	return console_detection(owner, cache).contacts;
}

auto Runtime::directed_contact(EntityId ship, const std::string &id, EntityId wanted, ConsoleContacts &cache,
                               bool allow_bearing) const -> std::optional<DirectedAim>
{
	if (!wanted)
		return {};
	return select_directed_contact(console_detection(system_owner(ship, id), cache), wanted, altitude(ship),
	                               allow_bearing);
}

auto Runtime::directed_definition(EntityId ship, const std::string &id) const -> const DirectedDefinition *
{
	const auto &all = definition(ship).directed;
	const auto at = all.find(id);
	return at == all.end() ? nullptr : &at->second;
}

auto Runtime::directed_mode(EntityId ship, const std::string &id) const -> const DirectedModeDefinition *
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	const auto *state = fit ? fit->entity.try_get<DirectedRuntime>() : nullptr;
	return d && state ? &d->modes.at(state->operating_mode) : nullptr;
}

auto Runtime::directed_target(EntityId ship, const std::string &id) const -> EntityId
{
	const auto fit = instance(entities.at(ship), id);
	const auto *state = fit ? fit->entity.try_get<DirectedRuntime>() : nullptr;
	return state ? state->target : 0;
}

auto Runtime::directed_operation(EntityId ship, const std::string &id, DirectedOperationKind kind) const -> bool
{
	const auto *mode = directed_mode(ship, id);
	return mode && mode->operation == kind;
}

auto Runtime::directed_compatible(const DirectedDefinition &d, std::size_t mode, std::size_t targeting) const -> bool
{
	return directed_mode_compatible(d, mode, targeting);
}

auto Runtime::directed_targeting(EntityId ship, const std::string &id) const -> DirectedTargetingKind
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	return d && fit->entity.has<DirectedRuntime>()
	           ? d->targeting.at(fit->entity.get<DirectedRuntime>().targeting_mode).kind
	           : DirectedTargetingKind::Designated;
}

auto Runtime::directed_mount_target(flecs::entity ship, flecs::entity mount) const -> EntityId
{
	const auto *binding = mount.try_get<DirectedMountOwner>();
	if (!binding || !mount.get<Equipment>().enabled || structurally_disabled(ship, mount.get<Equipment>().id))
		return 0;
	const auto owner = ecs.entity(binding->equipment);
	const auto &id = owner.get<Equipment>().id;
	if (!owner.get<Equipment>().enabled || structurally_disabled(ship, id))
		return 0;
	const auto policy = directed_targeting(ship.get<StableId>().id, id);
	if (policy == DirectedTargetingKind::Off)
		return 0;
	if (policy == DirectedTargetingKind::Intercept)
	{
		const auto interception = owner.get<CombatMechanism>().get<WeaponState>().intercept;
		return interception ? 7000000000000000ULL + interception : 0;
	}
	return owner.get<DirectedRuntime>().target;
}

auto Runtime::apply_directed_mode(flecs::entity ship, flecs::entity child, bool restoring) -> void
{
	const auto &mode = *directed_mode(ship.get<StableId>().id, child.get<Equipment>().id);
	if (auto *mechanism = child.try_get_mut<CombatMechanism>();
	    mechanism && mechanism->definition.kind == OperatingSystem::Weapon)
	{
		if (apply_directed_weapon_mode(*mechanism, mode, restoring))
			mechanism->get<WeaponState>().unloading = std::max<std::int64_t>(
			    1, load_duration(ship, *mechanism, mechanism->get<WeaponState>().loaded_magazine));
	}
	if (auto *power = ship.try_get_mut<ShipResources>(); power && child.has<ResourceBinding>())
	{
		const auto slot = child.get<ResourceBinding>().slot;
		auto &binding = power->definition.systems.at(slot);
		const auto &compiled = definition(ship.get<StableId>().id).gameplay;
		binding.energy_per_second = child.get<Equipment>().kind == OperatingSystem::Radar && compiled && compiled->power
		                                ? compiled->power->systems.at(slot).energy_per_second
		                                : 0;
		binding.discrete_energy = mode.discrete_energy;
	}
}

auto Runtime::initialize_directed(flecs::entity ship) -> void
{
	const auto &all = definition(ship.get<StableId>().id).directed;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto at = all.find(child.get<Equipment>().id); at != all.end())
		{
			const auto state = initial_directed_runtime(at->second);
			child.set<DirectedRuntime>(state);
			std::string turret;
			if (const auto *weapon = child.try_get<CombatMechanism>();
			    weapon && weapon->definition.kind == OperatingSystem::Weapon)
				turret = weapon->definition.get<WeaponDefinition>().turret;
			else if (const auto sensor = definition(ship.get<StableId>().id).sensors.find(at->first);
			         sensor != definition(ship.get<StableId>().id).sensors.end())
				turret = sensor->second.turret;
			if (!turret.empty())
				instance(ship, turret)->entity.set<DirectedMountOwner>({child.id()});
			apply_directed_mode(ship, child);
		}
}

auto Runtime::directed_setting(EntityId ship, const std::string &id, const std::string &suffix, std::int64_t value,
                               bool execute) -> bool
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	if (!d || !fit || !fit->entity.has<DirectedRuntime>() || value < 0)
		return false;
	auto &state = fit->entity.get_mut<DirectedRuntime>();
	if (suffix == "operating_mode" || suffix == "targeting_mode")
	{
		const auto selection = select_directed_modes(state, *d, suffix, value, execute);
		if (selection.accepted && execute && selection.changed)
			apply_directed_mode(entities.at(ship), fit->entity);
		return selection.accepted;
	}
	if (suffix == "target")
	{
		const auto target = static_cast<EntityId>(value);
		if (target == ship)
			return false;
		if (target)
		{
			const auto observation = capture_sensor_observation(system_owner(ship, id));
			const auto *sensor = fit->entity.try_get<SensorRuntime>();
			const bool retained = sensor && (sensor->tracks.contains(target) ||
			                                 (sensor->last_bearing && sensor->last_bearing->id == target));
			if (!directed_target_visible(observation, target, retained))
				return false;
		}
		if (execute)
		{
			if (state.target != target)
				if (auto *sensor = fit->entity.try_get_mut<SensorRuntime>())
					sensor->last_bearing.reset();
			state.target = target;
			state.ready = false;
		}
		return true;
	}
	return false;
}

auto Runtime::directed_enabled(flecs::entity ship, flecs::entity child, DirectedOperationKind kind) const -> bool
{
	const auto id = ship.get<StableId>().id;
	const auto &equipment = child.get<Equipment>();
	return directed_operation(id, equipment.id, kind) && equipment.enabled &&
	       !structurally_disabled(ship, equipment.id) &&
	       (kind != DirectedOperationKind::Scan || !child.has<Radar>() || child.get<Radar>().enabled) &&
	       directed_targeting(id, equipment.id) != DirectedTargetingKind::Off;
}

auto Runtime::request_directed_resources(flecs::entity ship) -> void
{
	auto *power = ship.try_get_mut<ShipResources>();
	if (!power || !ship.has<EquipmentIndex>())
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (child.has<DirectedRuntime>() && child.has<ResourceBinding>())
		{
			const auto &id = child.get<Equipment>().id;
			const auto &mode = *directed_mode(ship.get<StableId>().id, id);
			const bool enabled = (mode.operation == DirectedOperationKind::Scan || !duel_ended()) &&
			                     directed_enabled(ship, child, mode.operation);
			const bool weapon_ready = enabled && mode.operation != DirectedOperationKind::Scan &&
			                          child.has<CombatMechanism>() &&
			                          child.get<CombatMechanism>().get<WeaponState>().ready;
			const bool broad = child.get<Equipment>().kind == OperatingSystem::Radar &&
			                   child.get<Equipment>().enabled && (!child.has<Radar>() || child.get<Radar>().enabled) &&
			                   !structurally_disabled(ship, id);
			const auto demand = directed_resource_demand(
			    mode, enabled ? directed_targeting(ship.get<StableId>().id, id) : DirectedTargetingKind::Off, enabled,
			    weapon_ready, broad);
			request_system_resource(*power, child.get<ResourceBinding>().slot, demand.activity, demand.discrete,
			                        demand.energy);
		}
}

auto Runtime::directed_projection(EntityId ship, const std::string &id) const -> Json
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	if (!d || !fit || !fit->entity.has<DirectedRuntime>())
		return Json::object();
	return project_directed_runtime(*d, fit->entity.get<DirectedRuntime>());
}

auto Runtime::save_directed() const -> Json
{
	Json result = Json::array();
	for (const auto &[id, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *state = child.try_get<DirectedRuntime>())
					result.push_back(save_directed_runtime(id, child.get<Equipment>().id, *state));
	return result;
}

auto Runtime::restore_directed(const Json &rows) -> void
{
	if (!rows.is_array() || rows.size() > 4096)
		throw std::runtime_error("Invalid saved directed capabilities");
	std::set<std::pair<EntityId, std::string>> seen;
	for (const auto &row : rows)
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved directed owner");
		const auto *d = directed_definition(ship, id);
		const auto fit = instance(entities.at(ship), id);
		if (!d || !fit || !fit->entity.has<DirectedRuntime>())
			throw std::runtime_error("Invalid saved directed selection");
		const auto state = restore_directed_runtime(row, *d, ship);
		fit->entity.set<DirectedRuntime>(state);
		apply_directed_mode(entities.at(ship), fit->entity, true);
	}
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (child.has<DirectedRuntime>() && !seen.contains({ship, child.get<Equipment>().id}))
					throw std::runtime_error("Missing saved directed capability");
}

auto Runtime::validate_directed_commitments() const -> void
{
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *state = child.try_get<DirectedRuntime>())
				{
					if (ghost_receptions.contains(state->target) && ghost_receptions.at(state->target).observer != ship)
						throw std::runtime_error("Unknown saved apparent directed target");
					if (const auto *weapon = child.try_get<CombatMechanism>();
					    weapon && weapon->definition.kind == OperatingSystem::Weapon)
					{
						validate_directed_ammunition_commitment(*state, &weapon->get<WeaponState>());
					}
					else
						validate_directed_ammunition_commitment(*state, nullptr);
				}
}
} // namespace phoenix::world_detail
