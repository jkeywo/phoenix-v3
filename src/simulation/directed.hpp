// World::Impl shared directed intent, qualification and native-operation dispatch.
struct DirectedAim
{
	BodyView body;
	std::int64_t altitude = 0, vz = 0;
	std::optional<std::int64_t> bearing;
};
// Prepared during the ordinary physical-input phase, consumed in this tick's
// actuation phase. No save can expose these transient stock claims.
struct PreparedDirectedOperation
{
	DirectedAim aim;
	fixed::Vec origin;
	std::int64_t bearing = 0, cycle_ticks = 1;
	std::string magazine, resource;
	AmmunitionDefinition ammunition;
	ProjectileDefinition projectile;
	std::vector<PayloadDefinition> payloads;
	bool stock_claim = false, loaded = false;
};
std::map<std::pair<EntityId, std::string>, PreparedDirectedOperation> directed_operations;
void release_directed_operations(flecs::entity ship)
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
using ConsoleContacts = std::map<ConsoleAddress, Observation>;
const Observation &console_detection(const ConsoleAddress &owner, ConsoleContacts &cache) const
{
	if (!cache.contains(owner))
		cache.emplace(owner, capture_sensor_observation(owner));
	return cache.at(owner);
}
const std::vector<BodyView> &console_contacts(const ConsoleAddress &owner, ConsoleContacts &cache) const
{
	return console_detection(owner, cache).contacts;
}
std::optional<DirectedAim> directed_contact(EntityId ship, const std::string &id, EntityId wanted,
                                            ConsoleContacts &cache, bool allow_bearing = true) const
{
	if (!wanted)
		return {};
	const auto &observation = console_detection(system_owner(ship, id), cache);
	for (const auto &contact : observation.contacts)
		if (contact.id == wanted)
			return DirectedAim{contact, contact.altitude, contact.vz};
	if (allow_bearing)
		for (const auto &bearing : observation.bearing_contacts)
			if (bearing.id == wanted)
			{
				DirectedAim aim;
				aim.body.id = wanted;
				aim.bearing = bearing.bearing_mdeg;
				aim.altitude = altitude(ship);
				return aim;
			}
	return {};
}
const DirectedDefinition *directed_definition(EntityId ship, const std::string &id) const
{
	const auto &all = definition(ship).directed;
	const auto at = all.find(id);
	return at == all.end() ? nullptr : &at->second;
}
const DirectedModeDefinition *directed_mode(EntityId ship, const std::string &id) const
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	const auto *state = fit ? fit->entity.try_get<DirectedRuntime>() : nullptr;
	return d && state ? &d->modes.at(state->operating_mode) : nullptr;
}
bool directed_operation(EntityId ship, const std::string &id, DirectedOperationKind kind) const
{
	const auto *mode = directed_mode(ship, id);
	return mode && mode->operation == kind;
}
bool directed_compatible(const DirectedDefinition &d, std::size_t mode, std::size_t targeting) const
{
	if (mode >= d.modes.size() || targeting >= d.targeting.size())
		return false;
	const auto &allowed = d.modes[mode].targeting;
	return allowed.empty() || std::find(allowed.begin(), allowed.end(), d.targeting[targeting].id) != allowed.end();
}
DirectedTargetingKind directed_targeting(EntityId ship, const std::string &id) const
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	return d && fit->entity.has<DirectedRuntime>()
	           ? d->targeting.at(fit->entity.get<DirectedRuntime>().targeting_mode).kind
	           : DirectedTargetingKind::Designated;
}
void apply_directed_mode(flecs::entity ship, flecs::entity child, bool restoring = false)
{
	const auto &mode = *directed_mode(ship.get<StableId>().id, child.get<Equipment>().id);
	if (auto *mechanism = child.try_get_mut<CombatMechanism>();
	    mechanism && mechanism->definition.kind == OperatingSystem::Weapon)
	{
		auto &weapon = mechanism->definition.get<WeaponDefinition>();
		weapon.range = mode.range;
		if (mode.operation != DirectedOperationKind::Scan)
		{
			weapon.ammunition = mode.ammunition;
			weapon.ammo_use = mode.ammo_use;
		}
		weapon.standby_energy = mode.standby_energy;
		weapon.beam_damage = mode.beam_damage;
		if (mode.operation != DirectedOperationKind::Scan)
			weapon.family = mode.operation == DirectedOperationKind::Beam ? "beam" : "projectile";
		if (!restoring)
		{
			mechanism->get<WeaponState>().ready = false;
			mechanism->get<WeaponState>().beam_tick = -1;
		}
		const auto &selection = child.get<DirectedRuntime>();
		const auto &policies = directed_definition(ship.get<StableId>().id, child.get<Equipment>().id)->targeting;
		const auto policy = policies.at(selection.targeting_mode).kind;
		mechanism->get<WeaponState>().mode = policy == DirectedTargetingKind::Off         ? 0
		                                     : policy == DirectedTargetingKind::Intercept ? 1
		                                                                                  : 2;
		if (!restoring && mode.operation == DirectedOperationKind::Launch &&
		    mechanism->get<WeaponState>().loaded_magazine >= 0 &&
		    mechanism->get<WeaponState>().loaded_quantity != mode.ammo_use && !mechanism->get<WeaponState>().unloading)
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
void initialize_directed(flecs::entity ship)
{
	const auto &all = definition(ship.get<StableId>().id).directed;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto at = all.find(child.get<Equipment>().id); at != all.end())
		{
			DirectedRuntime state;
			state.operating_mode = at->second.initial_mode;
			state.targeting_mode = at->second.initial_targeting;
			child.set<DirectedRuntime>(state);
			apply_directed_mode(ship, child);
		}
}
bool directed_setting(EntityId ship, const std::string &id, const std::string &suffix, std::int64_t value, bool execute)
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	if (!d || !fit || !fit->entity.has<DirectedRuntime>() || value < 0)
		return false;
	auto &state = fit->entity.get_mut<DirectedRuntime>();
	if (suffix == "operating_mode" || suffix == "targeting_mode")
	{
		const auto mode = suffix == "operating_mode" ? static_cast<std::size_t>(value) : state.operating_mode;
		const auto targeting = suffix == "targeting_mode" ? static_cast<std::size_t>(value) : state.targeting_mode;
		if (!directed_compatible(*d, mode, targeting))
			return false;
		if (execute)
		{
			const bool changed = state.operating_mode != mode || state.targeting_mode != targeting;
			state.operating_mode = mode;
			state.targeting_mode = targeting;
			if (changed)
			{
				state.ready = false;
				state.blocker = "Mode changed";
				apply_directed_mode(entities.at(ship), fit->entity);
			}
		}
		return true;
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
			if (!retained &&
			    std::none_of(observation.contacts.begin(), observation.contacts.end(),
			                 [&](const auto &c)
			                 {
				                 return c.id == target;
			                 }) &&
			    std::none_of(observation.bearing_contacts.begin(), observation.bearing_contacts.end(),
			                 [&](const auto &c)
			                 {
				                 return c.id == target;
			                 }))
				return false;
		}
		if (execute)
		{
			state.target = target;
			state.ready = false;
			if (auto *weapon = fit->entity.try_get_mut<CombatMechanism>();
			    weapon && weapon->definition.kind == OperatingSystem::Weapon)
				weapon->target() = target;
			if (auto *sensor = fit->entity.try_get_mut<SensorRuntime>())
			{
				if (sensor->target != target)
					sensor->last_bearing.reset();
				sensor->target = target;
			}
		}
		return true;
	}
	return false;
}
bool directed_enabled(flecs::entity ship, flecs::entity child, DirectedOperationKind kind) const
{
	const auto id = ship.get<StableId>().id;
	const auto &equipment = child.get<Equipment>();
	return directed_operation(id, equipment.id, kind) && equipment.enabled &&
	       !structurally_disabled(ship, equipment.id) &&
	       (kind != DirectedOperationKind::Scan || !child.has<Radar>() || child.get<Radar>().enabled) &&
	       directed_targeting(id, equipment.id) != DirectedTargetingKind::Off;
}
void request_directed_resources(flecs::entity ship)
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
			bool working = enabled;
			if (mode.operation != DirectedOperationKind::Scan)
				working =
				    working && child.has<CombatMechanism>() && child.get<CombatMechanism>().get<WeaponState>().ready;
			const auto standby =
			    enabled && directed_targeting(ship.get<StableId>().id, id) == DirectedTargetingKind::Intercept
			        ? mode.standby_energy
			        : 0;
			const bool broad = child.get<Equipment>().kind == OperatingSystem::Radar &&
			                   child.get<Equipment>().enabled && (!child.has<Radar>() || child.get<Radar>().enabled) &&
			                   !structurally_disabled(ship, id);
			const auto operation_rate = working ? mode.energy_per_second : 0;
			if (operation_rate > resource_limit - standby)
				throw std::runtime_error("Directed operating rate exceeds supported bound");
			request_system_resource(*power, child.get<ResourceBinding>().slot, broad ? 1000 : 0,
			                        working && mode.operation == DirectedOperationKind::Launch,
			                        operation_rate + standby);
		}
}
template <class Operation>
bool execute_directed(flecs::entity ship, flecs::entity child, DirectedOperationKind kind, bool qualified,
                      Operation &&operation)
{
	if (!child.has<DirectedRuntime>())
		return false;
	auto &state = child.get_mut<DirectedRuntime>();
	state.ready = false;
	if (!directed_enabled(ship, child, kind))
	{
		state.blocker = "Directed operation inactive";
		return false;
	}
	if (!qualified)
	{
		state.blocker = "Target or operation not ready";
		return false;
	}
	if (const auto *power = ship.try_get<ShipResources>(); power && child.has<ResourceBinding>())
	{
		const auto slot = child.get<ResourceBinding>().slot;
		const auto &grant = power->grants.at(slot);
		if (grant.blocked ||
		    (kind == DirectedOperationKind::Beam ? grant.supply_permille <= 0 : grant.supply_permille < 1000) ||
		    (kind == DirectedOperationKind::Launch && power->definition.systems.at(slot).discrete_energy &&
		     !grant.discrete_paid))
		{
			state.blocker = "Insufficient operating power";
			return false;
		}
	}
	state.ready = true;
	state.blocker.clear();
	if (!operation())
	{
		state.ready = false;
		state.blocker = "Required operation unavailable";
		return false;
	}
	state.sequence = std::min<std::uint64_t>(9007199254740991ULL, state.sequence + 1);
	return true;
}
Json directed_projection(EntityId ship, const std::string &id) const
{
	const auto *d = directed_definition(ship, id);
	const auto fit = instance(entities.at(ship), id);
	if (!d || !fit || !fit->entity.has<DirectedRuntime>())
		return Json::object();
	const auto &state = fit->entity.get<DirectedRuntime>();
	Json result{{"operating_modes", Json::array()},
	            {"targeting_modes", Json::array()},
	            {"operating_mode", state.operating_mode},
	            {"targeting_mode", state.targeting_mode},
	            {"target", state.target},
	            {"ready", state.ready},
	            {"blocker", state.blocker},
	            {"sequence", state.sequence}};
	for (std::size_t i = 0; i < d->modes.size(); ++i)
		result["operating_modes"].push_back({{"id", d->modes[i].id},
		                                     {"label", d->modes[i].label},
		                                     {"value", i},
		                                     {"compatible", directed_compatible(*d, i, state.targeting_mode)}});
	for (std::size_t i = 0; i < d->targeting.size(); ++i)
		result["targeting_modes"].push_back({{"id", d->targeting[i].id},
		                                     {"label", d->targeting[i].label},
		                                     {"value", i},
		                                     {"compatible", directed_compatible(*d, state.operating_mode, i)}});
	return result;
}
Json save_directed() const
{
	Json result = Json::array();
	for (const auto &[id, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *state = child.try_get<DirectedRuntime>())
					result.push_back({{"ship", id},
					                  {"system", child.get<Equipment>().id},
					                  {"operating_mode", state->operating_mode},
					                  {"targeting_mode", state->targeting_mode},
					                  {"target", state->target},
					                  {"sequence", state->sequence},
					                  {"loading_mode", state->loading_mode},
					                  {"loaded_mode", state->loaded_mode},
					                  {"beam_funding_remainder", state->beam_funding_remainder},
					                  {"ready", state->ready},
					                  {"blocker", state->blocker}});
	return result;
}
void restore_directed(const Json &rows)
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
		DirectedRuntime state;
		state.operating_mode = checked_integer(row.at("operating_mode"), 0, 255, "directed.operating_mode");
		state.targeting_mode = checked_integer(row.at("targeting_mode"), 0, 255, "directed.targeting_mode");
		state.target = checked_integer(row.at("target"), 0, 7999999999999999LL, "directed.target");
		if (!d || !fit || !fit->entity.has<DirectedRuntime>() ||
		    !directed_compatible(*d, state.operating_mode, state.targeting_mode) || state.target == ship ||
		    (ghost_receptions.contains(state.target) && ghost_receptions.at(state.target).observer != ship))
			throw std::runtime_error("Invalid saved directed selection");
		state.sequence = checked_integer(row.at("sequence"), 0, 9007199254740991LL, "directed.sequence");
		state.loading_mode = row.at("loading_mode").get<std::string>();
		state.loaded_mode = row.at("loaded_mode").get<std::string>();
		state.beam_funding_remainder =
		    checked_integer(row.at("beam_funding_remainder"), 0, 999, "directed.beam_funding_remainder");
		for (const auto &mode : {state.loading_mode, state.loaded_mode})
			if (!mode.empty() && std::none_of(d->modes.begin(), d->modes.end(),
			                                  [&](const auto &m)
			                                  {
				                                  return m.id == mode && m.operation == DirectedOperationKind::Launch;
			                                  }))
				throw std::runtime_error("Invalid saved loading mode");
		state.ready = row.at("ready").get<bool>();
		state.blocker = row.at("blocker").get<std::string>();
		if (state.blocker.size() > 256)
			throw std::runtime_error("Invalid saved directed blocker");
		if (const auto *weapon = fit->entity.try_get<CombatMechanism>();
		    weapon && weapon->definition.kind == OperatingSystem::Weapon)
		{
			const auto &native = weapon->get<WeaponState>();
			const auto kind = d->targeting.at(state.targeting_mode).kind;
			const auto native_mode = kind == DirectedTargetingKind::Off         ? 0
			                         : kind == DirectedTargetingKind::Intercept ? 1
			                                                                    : 2;
			if (weapon->target() != state.target || native.mode != native_mode ||
			    (native.loading > 0) != !state.loading_mode.empty() ||
			    (native.loaded_magazine >= 0) != !state.loaded_mode.empty())
				throw std::runtime_error("Saved weapon state disagrees with directed intent");
		}
		if (const auto *sensor = fit->entity.try_get<SensorRuntime>(); sensor && sensor->target != state.target)
			throw std::runtime_error("Saved scanner target disagrees with directed intent");
		fit->entity.set<DirectedRuntime>(state);
		apply_directed_mode(entities.at(ship), fit->entity, true);
	}
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (child.has<DirectedRuntime>() && !seen.contains({ship, child.get<Equipment>().id}))
					throw std::runtime_error("Missing saved directed capability");
}
// Select the authored mode before native save validators inspect mode-dependent
// ammunition and resource grants. Full target/receipt validation follows restore.
void prepare_restore_directed(const Json &rows)
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
		const auto mode =
		    static_cast<std::size_t>(checked_integer(row.at("operating_mode"), 0, 255, "directed.operating_mode"));
		const auto targeting =
		    static_cast<std::size_t>(checked_integer(row.at("targeting_mode"), 0, 255, "directed.targeting_mode"));
		if (!d || !fit || !fit->entity.has<DirectedRuntime>() || !directed_compatible(*d, mode, targeting))
			throw std::runtime_error("Invalid saved directed selection");
		auto &state = fit->entity.get_mut<DirectedRuntime>();
		state.operating_mode = mode;
		state.targeting_mode = targeting;
		state.loading_mode = row.at("loading_mode").get<std::string>();
		state.loaded_mode = row.at("loaded_mode").get<std::string>();
		apply_directed_mode(entities.at(ship), fit->entity, true);
	}
}
