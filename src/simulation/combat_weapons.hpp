std::int64_t load_duration(flecs::entity ship, const CombatMechanism &m, std::int64_t magazine = -1) const
{
	if (m.definition.get<WeaponDefinition>().magazines.empty())
		return 0;
	const auto &mag = instance(ship, m.definition.get<WeaponDefinition>().magazines.at(
	                                     magazine < 0 ? m.get<WeaponState>().selected_magazine : magazine))
	                      ->entity.get<CombatMechanism>()
	                      .definition;
	return mag.ammunition().load_ticks ? mag.ammunition().load_ticks : m.definition.ammunition().load_ticks;
}
const ProjectileDefinition &weapon_payload(flecs::entity ship, const CombatMechanism &m) const
{
	if (!m.definition.get<WeaponDefinition>().magazines.empty())
	{
		const auto &p =
		    instance(ship, m.definition.get<WeaponDefinition>().magazines.at(m.get<WeaponState>().selected_magazine))
		        ->entity.get<CombatMechanism>()
		        .definition.ammunition()
		        .projectile;
		if (!p.visual.empty())
			return p;
	}
	return m.definition.ammunition().projectile;
}
struct WeaponAim
{
	BodyView body;
	std::int64_t altitude = 0, vz = 0;
	std::optional<std::int64_t> bearing;
};
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
std::optional<WeaponAim> weapon_contact(flecs::entity ship, const CombatMechanism &m, ConsoleContacts &cache) const
{
	const auto &contacts = console_contacts(system_owner(ship.get<StableId>().id, m.definition.id), cache);
	const auto wanted = m.definition.get<WeaponDefinition>().point_defence && m.get<WeaponState>().mode == 1
	                        ? 7000000000000000ULL + m.get<WeaponState>().intercept
	                        : m.target();
	if (!wanted)
		return {};
	for (const auto &contact : contacts)
		if (contact.id == wanted)
		{
			WeaponAim aim{contact, contact.altitude, contact.vz};
			if (m.get<WeaponState>().intercept && m.get<WeaponState>().mode == 1)
				for (auto missile : projectile_rows())
				{
					const auto &p = missile.get<Projectile>();
					if (ship.has<CraftState>() && entities.contains(p.source) &&
					    factions.relationship(ship.get<FactionMembership>().id,
					                          entities.at(p.source).get<FactionMembership>().id) != "hostile")
						continue;
					if (p.id == m.get<WeaponState>().intercept && missile.get<Destructible>().durability > 0)
					{
						aim.altitude = p.altitude;
						aim.vz = p.vz;
						return aim;
					}
				}
			else
				return aim;
		}
	if (!(m.get<WeaponState>().intercept && m.get<WeaponState>().mode == 1))
		for (const auto &b :
		     console_detection(system_owner(ship.get<StableId>().id, m.definition.id), cache).bearing_contacts)
			if (b.id == wanted)
			{
				WeaponAim aim{};
				aim.body.id = b.id;
				aim.bearing = b.bearing_mdeg;
				aim.altitude = altitude(ship.get<StableId>().id);
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
			if (d.kind != OperatingSystem::Weapon || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id))
				continue;
			if (d.get<WeaponDefinition>().point_defence && m->get<WeaponState>().mode == 1)
				m->get<WeaponState>().intercept = point_defence_target(
				    ship, d, console_contacts(system_owner(id, m->definition.id), contacts_by_console));
			if (m->get<WeaponState>().unloading)
			{
				if (--m->get<WeaponState>().unloading == 0)
				{
					auto &old =
					    instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().loaded_magazine))
					        ->entity.get_mut<CombatMechanism>();
					old.get<MagazineState>().ammo += d.get<WeaponDefinition>().ammo_use;
					old.get<MagazineState>().reserved_return -= d.get<WeaponDefinition>().ammo_use;
					m->get<WeaponState>().loaded_magazine = -1;
				}
				continue;
			}
			if (load_duration(ship, *m) && m->get<WeaponState>().loaded_magazine < 0 &&
			    !d.get<WeaponDefinition>().magazines.empty())
			{
				auto mag =
				    instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine))
				        ->entity;
				auto &a = mag.get_mut<CombatMechanism>();
				if (mag.get<Equipment>().enabled && !structurally_disabled(ship, a.definition.id) &&
				    a.get<MagazineState>().ammo >= d.get<WeaponDefinition>().ammo_use &&
				    ++m->get<WeaponState>().loading >= load_duration(ship, *m))
				{
					a.get<MagazineState>().ammo -= d.get<WeaponDefinition>().ammo_use;
					a.get<MagazineState>().reserved_return += d.get<WeaponDefinition>().ammo_use;
					m->get<WeaponState>().loaded_magazine = m->get<WeaponState>().selected_magazine;
					m->get<WeaponState>().loading = 0;
				}
			}
		}
	// The first usable hosted weapon supplies the shared mount's physical solution.
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Turret)
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			const CombatMechanism *weapon = nullptr;
			std::optional<WeaponAim> target;
			for (int priority = 0; priority < 2 && !weapon; ++priority)
				for (auto candidate : ship.get<EquipmentIndex>().values)
				{
					const auto *w = candidate.try_get<CombatMechanism>();
					if (!w || w->definition.kind != OperatingSystem::Weapon ||
					    w->definition.get<WeaponDefinition>().turret != d.id || !candidate.get<Equipment>().enabled ||
					    structurally_disabled(ship, w->definition.id) ||
					    (w->definition.get<WeaponDefinition>().point_defence && !w->get<WeaponState>().mode))
						continue;
					if ((w->definition.get<WeaponDefinition>().point_defence && w->get<WeaponState>().mode == 1) !=
					    (priority == 1))
						continue;
					if (auto found = weapon_contact(ship, *w, contacts_by_console))
					{
						weapon = w;
						target = found;
						break;
					}
				}
			const auto scanner_aim = sensor_aim(id, d.id, contacts_by_console);
			if (!weapon && !scanner_aim)
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
			    officer_value(ship.get<StableId>().id, d.id, "traverse", d.get<TurretDefinition>().traverse) *
			    fault_effectiveness(ship, d.id) / 1000;
			const auto step = m->get<TurretState>().turn_remainder / rate;
			m->get<TurretState>().turn_remainder %= rate;
			const auto current = combat_delta(m->get<TurretState>().heading, d.bearing);
			m->get<TurretState>().heading =
			    d.arc == 360000
			        ? combat_angle(m->get<TurretState>().heading +
			                       std::clamp(combat_delta(desired, m->get<TurretState>().heading), -step, step))
			        : combat_angle(d.bearing + current + std::clamp(offset - current, -step, step));
		}
	std::map<std::string, std::int64_t> reserved;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
			if (m->get<WeaponState>().cooldown || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id) ||
			    (d.get<WeaponDefinition>().point_defence && !m->get<WeaponState>().mode))
				continue;
			const auto target = weapon_contact(ship, *m, contacts_by_console);
			if (!target)
				continue;
			const auto [origin, orientation] = weapon_mount(ship, *m);
			if (orientation < 0)
				continue;
			const auto lead = weapon_lead(ship, *m, *target, origin);
			if ((!target->bearing && fixed::length(fixed::Vec{target->body.x, target->body.y} - origin) +
			                                 std::abs(target->altitude - altitude(id)) >
			                             d.get<WeaponDefinition>().range) ||
			    std::abs(combat_delta(combat_bearing(lead - origin), orientation)) > d.arc / 2)
				continue;
			if (m->get<WeaponState>().unloading)
				continue;
			if (d.get<WeaponDefinition>().family != "beam")
			{
				const auto &name = d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine);
				const auto mag = instance(ship, name)->entity;
				const auto &a = mag.get<CombatMechanism>();
				if (!mag.get<Equipment>().enabled || structurally_disabled(ship, name))
					continue;
				if (load_duration(ship, *m))
				{
					if (m->get<WeaponState>().loaded_magazine != m->get<WeaponState>().selected_magazine ||
					    m->get<WeaponState>().unloading)
						continue;
				}
				else
				{
					if (a.get<MagazineState>().ammo - reserved[name] < d.get<WeaponDefinition>().ammo_use)
						continue;
					reserved[name] += d.get<WeaponDefinition>().ammo_use;
				}
			}
			m->get<WeaponState>().ready = true;
		}
}
void fire_weapons(flecs::entity ship)
{
	// Resource allocation may have changed sensing since prepare_weapons.
	ConsoleContacts contacts_by_console;
	const auto id = ship.get<StableId>().id;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>();
		    m && m->definition.kind == OperatingSystem::Weapon && m->get<WeaponState>().ready)
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			if (const auto *power = ship.try_get<ShipResources>())
			{
				const auto slot = child.get<ResourceBinding>().slot;
				const auto &g = power->grants[slot];
				if (g.blocked || g.supply_permille < 1000 ||
				    (power->definition.systems[slot].discrete_energy && !g.discrete_paid))
					continue;
			}
			const auto target = weapon_contact(ship, *m, contacts_by_console);
			if (!target)
				continue;
			const auto [origin, orientation] = weapon_mount(ship, *m);
			if (orientation < 0)
				continue;
			const auto lead = weapon_lead(ship, *m, *target, origin);
			if ((!target->bearing && fixed::length(fixed::Vec{target->body.x, target->body.y} - origin) +
			                                 std::abs(target->altitude - altitude(id)) >
			                             d.get<WeaponDefinition>().range) ||
			    std::abs(combat_delta(combat_bearing(lead - origin), orientation)) > d.arc / 2)
				continue;
			auto bearing = combat_bearing(lead - origin);
			if (d.get<WeaponDefinition>().family == "beam")
			{
				fire_beam(ship, child, origin, bearing);
				continue;
			}
			auto &a = instance(ship, d.get<WeaponDefinition>().magazines.at(m->get<WeaponState>().selected_magazine))
			              ->entity.get_mut<CombatMechanism>();
			if (load_duration(ship, *m))
			{
				if (m->get<WeaponState>().loaded_magazine < 0)
					continue;
				a.get<MagazineState>().reserved_return -= d.get<WeaponDefinition>().ammo_use;
				m->get<WeaponState>().loaded_magazine = -1;
			}
			else
			{
				if (a.get<MagazineState>().ammo < d.get<WeaponDefinition>().ammo_use)
					continue;
				a.get<MagazineState>().ammo -= d.get<WeaponDefinition>().ammo_use;
			}
			const auto interval =
			    a.definition.ammunition().interval ? a.definition.ammunition().interval : d.ammunition().interval;
			m->get<WeaponState>().cooldown =
			    std::max<std::int64_t>(1, officer_value(ship.get<StableId>().id, d.id, "cycling", interval, true) *
			                                  1000 / fault_effectiveness(ship, d.id));
			++m->get<WeaponState>().shots;
			auto &s = ship.get_mut<CombatStructure>();
			s.random = s.random * 6364136223846793005ULL + 1;
			auto payload = weapon_payload(ship, *m);
			if (payload.spread)
				bearing = combat_angle(
				    bearing + static_cast<std::int64_t>((s.random >> 16) % (payload.spread * 2 + 1)) - payload.spread);
			if (projectile_rows().size() >= 8192)
				throw std::runtime_error("Combat projectile capacity exceeded");
			Projectile p;
			p.definition = payload;
			p.id = next_projectile++;
			p.source = id;
			p.weapon = d.id;
			p.target = m->target();
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
			    target->bearing
			        ? d.get<WeaponDefinition>().range
			        : std::max<std::int64_t>(1, fixed::length(fixed::Vec{target->body.x, target->body.y} - origin));
			p.vz = (target->altitude - p.altitude +
			        target->vz * p.initial_distance / std::max<std::int64_t>(1, payload.speed)) *
			       payload.speed / p.initial_distance;
			ecs.entity().set<Projectile>(p).set<Destructible>({payload.destruction, payload.durability});
		}
}
