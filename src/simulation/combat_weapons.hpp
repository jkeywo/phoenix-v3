std::int64_t load_duration(flecs::entity ship, const CombatMechanism &m, std::int64_t magazine = -1) const
{
	if (m.definition.magazines.empty())
		return 0;
	const auto &mag = instance(ship, m.definition.magazines.at(magazine < 0 ? m.selected_magazine : magazine))
	                      ->entity.get<CombatMechanism>()
	                      .definition;
	return mag.load_ticks ? mag.load_ticks : m.definition.load_ticks;
}
const ProjectileDefinition &weapon_payload(flecs::entity ship, const CombatMechanism &m) const
{
	if (!m.definition.magazines.empty())
	{
		const auto &p = instance(ship, m.definition.magazines.at(m.selected_magazine))
		                    ->entity.get<CombatMechanism>()
		                    .definition.projectile;
		if (!p.visual.empty())
			return p;
	}
	return m.definition.projectile;
}
struct WeaponAim
{
	BodyView body;
	std::int64_t altitude = 0, vz = 0;
};
std::optional<WeaponAim> weapon_contact(flecs::entity ship, const CombatMechanism &m) const
{
	const auto observed = capture_console_observation(system_owner(ship.get<StableId>().id, m.definition.id));
	const auto wanted = m.definition.point_defence && m.mode == 1 ? 7000000000000000ULL + m.intercept : m.target;
	if (!wanted)
		return {};
	for (const auto &contact : observed.contacts)
		if (contact.id == wanted)
		{
			WeaponAim aim{contact};
			if (m.intercept && m.mode == 1)
				for (auto missile : projectile_rows())
				{
					const auto &p = missile.get<Projectile>();
					if (p.id == m.intercept && p.durability > 0)
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
	if (!d.turret.empty())
	{
		const auto turret = instance(ship, d.turret)->entity;
		if (!turret.get<Equipment>().enabled || structurally_disabled(ship, d.turret))
			return {{}, -1};
		const auto &t = turret.get<CombatMechanism>();
		heading = pose.heading + t.heading + d.bearing;
		mount = combat_world(mount, Pose{{t.definition.x, t.definition.y}, t.heading});
	}
	return {combat_world(mount, pose), combat_angle(heading)};
}
fixed::Vec weapon_lead(flecs::entity ship, const CombatMechanism &m, const WeaponAim &target, fixed::Vec origin) const
{
	const auto &d = weapon_payload(ship, m);
	const auto travel = m.definition.family == "beam"
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
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>())
		{
			m->ready = false;
			if (m->cooldown > 0)
				--m->cooldown;
			const auto &d = m->definition;
			if (d.kind != OperatingSystem::Weapon || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id))
				continue;
			if (d.point_defence && m->mode == 1)
			{
				m->intercept = 0;
				const auto observed = capture_console_observation(system_owner(id, d.id));
				std::pair<std::int64_t, std::uint64_t> best{std::numeric_limits<std::int64_t>::max(), 0};
				for (auto missile : projectile_rows())
				{
					const auto &p = missile.get<Projectile>();
					if (!p.definition.durability || p.durability <= 0 || p.source == id || d.intercept_classes.empty())
						continue;
					if (std::none_of(observed.contacts.begin(), observed.contacts.end(),
					                 [&](const auto &c)
					                 {
						                 return c.id == 7000000000000000ULL + p.id;
					                 }))
						continue;
					const auto offset = p.position - pose.position;
					const auto distance = fixed::length(offset) + std::abs(p.altitude);
					if (distance > d.range)
						continue;
					const auto closing = std::max<std::int64_t>(
					    1, -fixed::dot(p.velocity - ship.get<Motion>().velocity, fixed::normal(offset)) / fixed::unit);
					const auto time = p.target == id ? distance * 1000 / closing : 1000000000 + distance;
					const auto score = std::pair{time, p.id};
					if (score < best)
					{
						best = score;
						m->intercept = p.id;
					}
				}
			}
			if (m->unloading)
			{
				if (--m->unloading == 0)
				{
					auto &old = instance(ship, d.magazines.at(m->loaded_magazine))->entity.get_mut<CombatMechanism>();
					old.ammo += d.ammo_use;
					old.reserved_return -= d.ammo_use;
					m->loaded_magazine = -1;
				}
				continue;
			}
			if (load_duration(ship, *m) && m->loaded_magazine < 0 && !d.magazines.empty())
			{
				auto mag = instance(ship, d.magazines.at(m->selected_magazine))->entity;
				auto &a = mag.get_mut<CombatMechanism>();
				if (mag.get<Equipment>().enabled && !structurally_disabled(ship, a.definition.id) &&
				    a.ammo >= d.ammo_use && ++m->loading >= load_duration(ship, *m))
				{
					a.ammo -= d.ammo_use;
					a.reserved_return += d.ammo_use;
					m->loaded_magazine = m->selected_magazine;
					m->loading = 0;
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
					if (!w || w->definition.kind != OperatingSystem::Weapon || w->definition.turret != d.id ||
					    !candidate.get<Equipment>().enabled || structurally_disabled(ship, w->definition.id) ||
					    (w->definition.point_defence && !w->mode))
						continue;
					if ((w->definition.point_defence && w->mode == 1) != (priority == 1))
						continue;
					if (auto found = weapon_contact(ship, *w))
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
			m->turn_remainder += d.traverse * fault_effectiveness(ship, d.id) / 1000;
			const auto step = m->turn_remainder / rate;
			m->turn_remainder %= rate;
			const auto current = combat_delta(m->heading, d.bearing);
			m->heading = d.arc == 360000
			                 ? combat_angle(m->heading + std::clamp(combat_delta(desired, m->heading), -step, step))
			                 : combat_angle(d.bearing + current + std::clamp(offset - current, -step, step));
		}
	std::map<std::string, std::int64_t> reserved;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
			if (m->cooldown || !child.get<Equipment>().enabled || structurally_disabled(ship, d.id) ||
			    (d.point_defence && !m->mode))
				continue;
			const auto target = weapon_contact(ship, *m);
			if (!target)
				continue;
			const auto [origin, orientation] = weapon_mount(ship, *m);
			if (orientation < 0)
				continue;
			const auto lead = weapon_lead(ship, *m, *target, origin);
			if (fixed::length(fixed::Vec{target->body.x, target->body.y} - origin) + std::abs(target->altitude) >
			        d.range ||
			    std::abs(combat_delta(combat_bearing(lead - origin), orientation)) > d.arc / 2)
				continue;
			if (m->unloading)
				continue;
			if (d.family != "beam")
			{
				const auto &name = d.magazines.at(m->selected_magazine);
				const auto mag = instance(ship, name)->entity;
				const auto &a = mag.get<CombatMechanism>();
				if (!mag.get<Equipment>().enabled || structurally_disabled(ship, name))
					continue;
				if (load_duration(ship, *m))
				{
					if (m->loaded_magazine != m->selected_magazine || m->unloading)
						continue;
				}
				else
				{
					if (a.ammo - reserved[name] < d.ammo_use)
						continue;
					reserved[name] += d.ammo_use;
				}
			}
			m->ready = true;
		}
}
void fire_weapons(flecs::entity ship)
{
	const auto id = ship.get<StableId>().id;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>();
		    m && m->definition.kind == OperatingSystem::Weapon && m->ready)
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
			const auto target = weapon_contact(ship, *m);
			if (!target)
				continue;
			const auto [origin, orientation] = weapon_mount(ship, *m);
			if (orientation < 0)
				continue;
			const auto lead = weapon_lead(ship, *m, *target, origin);
			auto bearing = combat_bearing(lead - origin);
			if (d.family == "beam")
			{
				fire_beam(ship, child, origin, bearing);
				continue;
			}
			auto &a = instance(ship, d.magazines.at(m->selected_magazine))->entity.get_mut<CombatMechanism>();
			if (load_duration(ship, *m))
			{
				if (m->loaded_magazine < 0)
					continue;
				a.reserved_return -= d.ammo_use;
				m->loaded_magazine = -1;
			}
			else
			{
				if (a.ammo < d.ammo_use)
					continue;
				a.ammo -= d.ammo_use;
			}
			const auto interval = a.definition.interval ? a.definition.interval : d.interval;
			m->cooldown = std::max<std::int64_t>(1, interval * 1000 / fault_effectiveness(ship, d.id));
			++m->shots;
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
			p.target = m->target;
			p.position = origin;
			p.heading = bearing;
			p.base_heading = bearing;
			p.velocity = fixed::scale(fixed::direction(bearing), payload.speed) + ship.get<Motion>().velocity;
			p.life = payload.lifetime;
			p.penetration = payload.penetration;
			p.damage = payload.damage;
			p.durability = payload.durability;
			p.random = s.random;
			p.initial_distance =
			    std::max<std::int64_t>(1, fixed::length(fixed::Vec{target->body.x, target->body.y} - origin));
			if (m->intercept && m->mode == 1)
				p.vz = (target->altitude + target->vz * p.initial_distance / std::max<std::int64_t>(1, payload.speed)) *
				       payload.speed / p.initial_distance;
			ecs.entity().set<Projectile>(p).set<Destructible>({payload.destruction, payload.durability});
		}
}
