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
};
using WeaponContacts = std::map<ConsoleAddress, std::vector<BodyView>>;
const std::vector<BodyView> &weapon_contacts(flecs::entity ship, const CombatMechanism &m, WeaponContacts &cache) const
{
	const auto owner = system_owner(ship.get<StableId>().id, m.definition.id);
	if (!cache.contains(owner))
		cache.emplace(owner, capture_weapon_contacts(owner));
	return cache.at(owner);
}
std::optional<WeaponAim> weapon_contact(flecs::entity ship, const CombatMechanism &m, WeaponContacts &cache) const
{
	const auto &contacts = weapon_contacts(ship, m, cache);
	const auto wanted = m.definition.get<WeaponDefinition>().point_defence && m.get<WeaponState>().mode == 1
	                        ? 7000000000000000ULL + m.get<WeaponState>().intercept
	                        : m.target();
	if (!wanted)
		return {};
	for (const auto &contact : contacts)
		if (contact.id == wanted)
		{
			WeaponAim aim{contact};
			if (m.get<WeaponState>().intercept && m.get<WeaponState>().mode == 1)
				for (auto missile : projectile_rows())
				{
					const auto &p = missile.get<Projectile>();
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
void prepare_weapons(flecs::entity ship)
{
	WeaponContacts contacts_by_console;
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
			{
				m->get<WeaponState>().intercept = 0;
				const auto &contacts = weapon_contacts(ship, *m, contacts_by_console);
				std::pair<std::int64_t, std::uint64_t> best{std::numeric_limits<std::int64_t>::max(), 0};
				for (auto missile : projectile_rows())
				{
					const auto &p = missile.get<Projectile>();
					if (!p.definition.durability || missile.get<Destructible>().durability <= 0 || p.source == id ||
					    d.get<WeaponDefinition>().intercept_classes.empty())
						continue;
					if (std::none_of(contacts.begin(), contacts.end(),
					                 [&](const auto &c)
					                 {
						                 return c.id == 7000000000000000ULL + p.id;
					                 }))
						continue;
					const auto offset = p.position - pose.position;
					const auto distance = fixed::length(offset) + std::abs(p.altitude);
					if (distance > d.get<WeaponDefinition>().range)
						continue;
					const auto closing = std::max<std::int64_t>(
					    1, -fixed::dot(p.velocity - ship.get<Motion>().velocity, fixed::normal(offset)) / fixed::unit);
					const auto time = p.target == id ? distance * 1000 / closing : 1000000000 + distance;
					const auto score = std::pair{time, p.id};
					if (score < best)
					{
						best = score;
						m->get<WeaponState>().intercept = p.id;
					}
				}
			}
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
			if (!weapon)
				continue;
			const auto origin = combat_world({d.x, d.y}, pose);
			const auto lead = weapon_lead(ship, *weapon, *target, origin);
			const auto &w = weapon->definition;
			const auto axis = fixed::direction(w.bearing);
			const auto lateral = (axis.x * w.y - axis.y * w.x) / fixed::unit;
			const auto forward = static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(
			    std::max<std::int64_t>(0, fixed::dot(lead - origin, lead - origin) - lateral * lateral))));
			const auto aim = combat_bearing(lead - origin) - w.bearing - combat_bearing({forward, lateral});
			const auto offset = std::clamp(combat_delta(aim - pose.heading, d.bearing), -d.arc / 2, d.arc / 2);
			const auto desired = combat_angle(d.bearing + offset);
			m->get<TurretState>().turn_remainder +=
			    d.get<TurretDefinition>().traverse * fault_effectiveness(ship, d.id) / 1000;
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
			if (fixed::length(fixed::Vec{target->body.x, target->body.y} - origin) + std::abs(target->altitude) >
			        d.get<WeaponDefinition>().range ||
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
	WeaponContacts contacts_by_console;
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
			    std::max<std::int64_t>(1, interval * 1000 / fault_effectiveness(ship, d.id));
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
			p.target = m->target();
			p.position = origin;
			p.heading = bearing;
			p.base_heading = bearing;
			p.velocity = fixed::scale(fixed::direction(bearing), payload.speed) + ship.get<Motion>().velocity;
			p.life = payload.lifetime;
			p.penetration = payload.penetration;
			p.damage = payload.damage;
			p.random = s.random;
			p.initial_distance =
			    std::max<std::int64_t>(1, fixed::length(fixed::Vec{target->body.x, target->body.y} - origin));
			if (m->get<WeaponState>().intercept && m->get<WeaponState>().mode == 1)
				p.vz = (target->altitude + target->vz * p.initial_distance / std::max<std::int64_t>(1, payload.speed)) *
				       payload.speed / p.initial_distance;
			ecs.entity().set<Projectile>(p).set<Destructible>({payload.destruction, payload.durability});
		}
}
