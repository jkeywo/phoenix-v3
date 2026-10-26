const AmmunitionDefinition &magazine_ammunition(const CombatMechanism &magazine, const std::string &resource) const
{
	const auto &definition = magazine.definition.get<MagazineDefinition>();
	const auto at = definition.ammunition_profiles.find(resource);
	return at == definition.ammunition_profiles.end() ? definition.ammunition : at->second;
}
const AmmunitionDefinition &magazine_ammunition(flecs::entity ship, const CombatMechanism &magazine) const
{
	return magazine_ammunition(magazine, physical_stock(ship, magazine.get<MagazineState>().stock).selected());
}
const AmmunitionDefinition &weapon_ammunition(flecs::entity ship, const CombatMechanism &weapon,
                                              std::int64_t magazine = -1) const
{
	const auto &state = weapon.get<WeaponState>();
	const auto index = magazine >= 0                ? magazine
	                   : state.loaded_magazine >= 0 ? state.loaded_magazine
	                                                : state.selected_magazine;
	const auto &mag =
	    instance(ship, weapon.definition.get<WeaponDefinition>().magazines.at(index))->entity.get<CombatMechanism>();
	const auto &stock = physical_stock(ship, mag.get<MagazineState>().stock);
	const auto &resource = state.loaded_magazine == index ? state.loaded_resource : stock.selected();
	return magazine_ammunition(mag, resource);
}
std::int64_t load_duration(flecs::entity ship, const CombatMechanism &m, std::int64_t magazine = -1) const
{
	if (m.definition.get<WeaponDefinition>().magazines.empty())
		return 0;
	const auto &ammunition = weapon_ammunition(ship, m, magazine);
	if (ammunition.load_ticks)
		return ammunition.load_ticks;
	const auto child = instance(ship, m.definition.id)->entity;
	if (const auto *state = child.try_get<DirectedRuntime>())
	{
		const auto &selected = m.get<WeaponState>();
		const auto &mode_id = selected.loaded_magazine >= 0 && (magazine < 0 || magazine == selected.loaded_magazine)
		                          ? state->loaded_mode
		                          : state->loading_mode;
		if (!mode_id.empty())
		{
			const auto &modes = directed_definition(ship.get<StableId>().id, m.definition.id)->modes;
			const auto mode = std::find_if(modes.begin(), modes.end(),
			                               [&](const auto &v)
			                               {
				                               return v.id == mode_id;
			                               });
			if (mode == modes.end() || mode->operation != DirectedOperationKind::Launch)
				throw std::runtime_error("Invalid committed loading mode");
			return mode->ammunition.load_ticks;
		}
	}
	return m.definition.ammunition().load_ticks;
}
const ProjectileDefinition &weapon_payload(flecs::entity ship, const CombatMechanism &m) const
{
	if (!m.definition.get<WeaponDefinition>().magazines.empty())
	{
		const auto &p = weapon_ammunition(ship, m).projectile;
		if (!p.visual.empty())
			return p;
	}
	return m.definition.ammunition().projectile;
}
using WeaponAim = DirectedAim;
std::optional<WeaponAim> weapon_contact(flecs::entity ship, const CombatMechanism &m, ConsoleContacts &cache) const
{
	const auto id = ship.get<StableId>().id;
	const bool intercept = directed_targeting(id, m.definition.id) == DirectedTargetingKind::Intercept;
	const auto wanted =
	    intercept ? 7000000000000000ULL + m.get<WeaponState>().intercept : directed_target(id, m.definition.id);
	if (intercept && !m.get<WeaponState>().intercept)
		return {};
	auto aim = directed_contact(id, m.definition.id, wanted, cache, !intercept);
	if (!aim)
		return {};
	if (!intercept)
		return aim->body.disposition == "hostile" ? aim : std::nullopt;
	for (auto missile : projectile_rows())
	{
		const auto &p = missile.get<Projectile>();
		if (p.id != m.get<WeaponState>().intercept || missile.get<Destructible>().durability <= 0)
			continue;
		if (ship.has<CraftState>() && entities.contains(p.source) &&
		    factions.relationship(ship.get<FactionMembership>().id,
		                          entities.at(p.source).get<FactionMembership>().id) != "hostile")
			continue;
		aim->altitude = p.altitude;
		aim->vz = p.vz;
		return aim;
	}
	return {};
}
std::pair<fixed::Vec, std::int64_t> weapon_mount(flecs::entity ship, const CombatMechanism &m) const
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
fixed::Vec weapon_lead(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target, fixed::Vec origin) const
{
	if (target.bearing)
		return origin + fixed::scale(fixed::direction(*target.bearing), m.definition.get<WeaponDefinition>().range);
	const auto &d = weapon_payload(ship, m);
	const auto travel = m.definition.get<WeaponDefinition>().family == "beam"
	                        ? 0
	                        : std::min<std::int64_t>(d.lifetime * 1000 / rate,
	                                                 fixed::length(fixed::Vec{target.body.x, target.body.y} - origin) *
	                                                     1000 / std::max<std::int64_t>(1, d.speed));
	return {std::clamp(target.body.x + (target.body.vx - ship.get<Motion>().velocity.x) * travel / 1000, -fixed::unit,
	                   fixed::unit),
	        std::clamp(target.body.y + (target.body.vy - ship.get<Motion>().velocity.y) * travel / 1000, -fixed::unit,
	                   fixed::unit)};
}
bool weapon_covers(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target, fixed::Vec origin,
                   std::int64_t orientation, fixed::Vec lead) const
{
	return (target.bearing || fixed::length(fixed::Vec{target.body.x, target.body.y} - origin) +
	                                  std::abs(target.altitude - altitude(ship.get<StableId>().id)) <=
	                              m.definition.get<WeaponDefinition>().range) &&
	       std::abs(combat_delta(combat_bearing(lead - origin), orientation)) <= m.definition.arc / 2;
}
std::uint64_t point_defence_target(flecs::entity ship, const CombatSystemDefinition &d,
                                   const std::vector<BodyView> &contacts) const
{
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	std::pair<std::int64_t, std::uint64_t> best{std::numeric_limits<std::int64_t>::max(), 0};
	for (auto missile : projectile_rows())
	{
		const auto &p = missile.get<Projectile>();
		if (!p.definition.durability || missile.get<Destructible>().durability <= 0 || p.source == id ||
		    d.get<WeaponDefinition>().intercept_classes.empty())
			continue;
		if (ship.has<CraftState>() &&
		    (!entities.contains(p.source) ||
		     factions.relationship(ship.get<FactionMembership>().id,
		                           entities.at(p.source).get<FactionMembership>().id) != "hostile"))
			continue;
		if (std::none_of(contacts.begin(), contacts.end(),
		                 [&](const auto &c)
		                 {
			                 return c.id == 7000000000000000ULL + p.id;
		                 }))
			continue;
		const auto offset = p.position - pose.position;
		const auto distance = fixed::length(offset) + std::abs(p.altitude - altitude(id));
		if (distance > d.get<WeaponDefinition>().range)
			continue;
		const auto closing = std::max<std::int64_t>(
		    1, -fixed::dot(p.velocity - ship.get<Motion>().velocity, fixed::normal(offset)) / fixed::unit);
		const auto time = p.target == id ? distance * 1000 / closing : 1000000000 + distance;
		const auto score = std::pair{time, p.id};
		if (score < best)
			best = score;
	}
	return best.second;
}
void prepare_weapons(flecs::entity ship)
{
	release_directed_operations(ship);
	ConsoleContacts contacts_by_console;
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			m->get<WeaponState>().ready = false;
			if (m->get<WeaponState>().cooldown > 0)
				--m->get<WeaponState>().cooldown;
			const auto &d = m->definition;
			if (child.has<DirectedRuntime>())
			{
				auto &directed = child.get_mut<DirectedRuntime>();
				directed.ready = false;
				directed.blocker = "Target or operation not ready";
			}
			if (d.kind != OperatingSystem::Weapon || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id))
				continue;
			if (directed_targeting(id, d.id) == DirectedTargetingKind::Intercept)
				m->get<WeaponState>().intercept = point_defence_target(
				    ship, d, console_contacts(system_owner(id, m->definition.id), contacts_by_console));
			if (m->get<WeaponState>().unloading)
			{
				if (--m->get<WeaponState>().unloading == 0)
				{
					auto &old =
					    instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().loaded_magazine))
					        ->entity.get_mut<CombatMechanism>();
					if (!physical_stock(ship, old.get<MagazineState>().stock).receive_reserved("weapon:" + d.id))
						throw std::runtime_error("Missing loaded ammunition return commitment");
					m->get<WeaponState>().loaded_resource.clear();
					m->get<WeaponState>().loaded_quantity = 0;
					m->get<WeaponState>().loaded_magazine = -1;
					child.get_mut<DirectedRuntime>().loaded_mode.clear();
				}
				continue;
			}
			if (!directed_operation(id, d.id, DirectedOperationKind::Launch))
				continue;
			if (load_duration(ship, *m) && m->get<WeaponState>().loaded_magazine < 0 &&
			    !d.get<WeaponDefinition>().magazines.empty())
			{
				auto mag =
				    instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine))
				        ->entity;
				auto &a = mag.get_mut<CombatMechanism>();
				auto &stock = physical_stock(ship, a.get<MagazineState>().stock);
				auto quantity = d.get<WeaponDefinition>().ammo_use;
				const auto &loading_mode = child.get<DirectedRuntime>().loading_mode;
				if (!loading_mode.empty())
					for (const auto &mode : directed_definition(id, d.id)->modes)
						if (mode.id == loading_mode)
							quantity = mode.ammo_use;
				if (mag.get<Equipment>().enabled && !structurally_disabled(ship, a.definition.id) &&
				    stock.available(stock.selected()) >= quantity)
				{
					if (!m->get<WeaponState>().loading)
						child.get_mut<DirectedRuntime>().loading_mode = directed_mode(id, d.id)->id;
					if (++m->get<WeaponState>().loading < load_duration(ship, *m))
						continue;
					StockTransaction transaction;
					const auto resource = stock.selected();
					if (!transaction.consume(stock, resource, quantity) ||
					    !transaction.reserve_capacity(stock, "weapon:" + d.id, resource, quantity) ||
					    !transaction.commit())
						continue;
					m->get<WeaponState>().loaded_resource = resource;
					m->get<WeaponState>().loaded_quantity = quantity;
					m->get<WeaponState>().loaded_magazine = m->get<WeaponState>().selected_magazine;
					m->get<WeaponState>().loading = 0;
					child.get_mut<DirectedRuntime>().loaded_mode = child.get<DirectedRuntime>().loading_mode;
					child.get_mut<DirectedRuntime>().loading_mode.clear();
					if (quantity != d.get<WeaponDefinition>().ammo_use)
						m->get<WeaponState>().unloading = std::max<std::int64_t>(1, load_duration(ship, *m));
				}
			}
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
			const auto owner = binding->equipment;
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
			const auto axis = fixed::direction(bearing);
			const auto lateral = (axis.x * mount.y - axis.y * mount.x) / fixed::unit;
			const auto forward = static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(
			    std::max<std::int64_t>(0, fixed::dot(lead - origin, lead - origin) - lateral * lateral))));
			const auto aim = combat_bearing(lead - origin) - bearing - combat_bearing({forward, lateral});
			const auto offset = std::clamp(combat_delta(aim - pose.heading, d.bearing), -d.arc / 2, d.arc / 2);
			const auto desired = combat_angle(d.bearing + offset);
			m->get<TurretState>().turn_remainder +=
			    effective_property(ship, d.id, PropertyKey::traverse, d.get<TurretDefinition>().traverse);
			const auto step = m->get<TurretState>().turn_remainder / rate;
			m->get<TurretState>().turn_remainder %= rate;
			const auto current = combat_delta(m->get<TurretState>().heading, d.bearing);
			m->get<TurretState>().heading =
			    d.arc == 360000
			        ? combat_angle(m->get<TurretState>().heading +
			                       std::clamp(combat_delta(desired, m->get<TurretState>().heading), -step, step))
			        : combat_angle(d.bearing + current + std::clamp(offset - current, -step, step));
		}
	std::size_t planned_projectiles = projectile_rows().size();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
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
			directed_operations.emplace(std::pair{id, d.id}, std::move(prepared));
			m->get<WeaponState>().ready = true;
			if (child.has<DirectedRuntime>())
				child.get_mut<DirectedRuntime>().ready = true;
		}
}
void fire_weapons(flecs::entity ship)
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
			const auto *target = &prepared.aim;
			const auto origin = prepared.origin;
			auto bearing = prepared.bearing;
			const auto operation = directed_mode(id, d.id)->operation;
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
				    const auto &payload = prepared.projectile;
				    if (projectile_rows().size() >= 8192)
					    return false;
				    if (prepared.loaded)
				    {
					    if (m->get<WeaponState>().loaded_magazine != m->get<WeaponState>().selected_magazine ||
					        !stock.release_capacity("weapon:" + d.id))
						    return false;
					    m->get<WeaponState>().loaded_magazine = -1;
					    m->get<WeaponState>().loaded_resource.clear();
					    m->get<WeaponState>().loaded_quantity = 0;
					    child.get_mut<DirectedRuntime>().loaded_mode.clear();
				    }
				    else if (!stock.consume_claim("directed-launch:" + d.id))
					    return false;
				    prepared.stock_claim = false;
				    m->get<WeaponState>().cooldown = prepared.cycle_ticks;
				    ++m->get<WeaponState>().shots;
				    record_interdiction_attack(ship.get<StableId>().id, directed_target(ship.get<StableId>().id, d.id));
				    psychic_weapon_emission(ship.get<StableId>().id);
				    auto &s = ship.get_mut<CombatStructure>();
				    s.random = s.random * 6364136223846793005ULL + 1;
				    if (payload.spread)
					    bearing = combat_angle(bearing +
					                           static_cast<std::int64_t>((s.random >> 16) % (payload.spread * 2 + 1)) -
					                           payload.spread);
				    Projectile p;
				    p.definition = payload;
				    p.payloads = std::move(prepared.payloads);
				    p.id = next_projectile++;
				    p.source = id;
				    p.weapon = d.id;
				    p.target = directed_target(ship.get<StableId>().id, d.id);
				    p.position = origin;
				    p.altitude = altitude(id);
				    p.launch_altitude = p.altitude;
				    p.heading = bearing;
				    p.base_heading = bearing;
				    p.velocity = fixed::scale(fixed::direction(bearing), payload.speed) + ship.get<Motion>().velocity;
				    p.life = payload.lifetime;
				    p.penetration = payload.penetration;
				    p.damage = payload.damage;
				    p.random = s.random;
				    p.bearing_launch = target->bearing.has_value();
				    p.seeker_altitude = p.launch_altitude;
				    p.initial_distance =
				        target->bearing ? d.get<WeaponDefinition>().range
				                        : std::max<std::int64_t>(
				                              1, fixed::length(fixed::Vec{target->body.x, target->body.y} - origin));
				    p.vz = (target->altitude - p.altitude +
				            target->vz * p.initial_distance / std::max<std::int64_t>(1, payload.speed)) *
				           payload.speed / p.initial_distance;
				    ecs.entity().set<Projectile>(p).set<Destructible>({payload.destruction, payload.durability});
				    emit_audio(ship, {SceneAudioKind::Shot, "weapon:" + d.id, directed_mode(id, d.id)->firing_audio});
				    return true;
			    });
		}
	release_directed_operations(ship);
}
