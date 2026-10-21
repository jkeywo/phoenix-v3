std::vector<BlastVisual> blast_visuals;
std::uint64_t next_blast = 1;
std::optional<std::int64_t> volume_hit(fixed::Vec a, fixed::Vec b, std::int64_t za, std::int64_t zb,
                                       CollisionBody target, fixed::Vec target_start, std::int64_t tz_start,
                                       std::int64_t tz_end, std::int64_t half_height, std::int64_t radius = 1) const
{
	// Intersect the planar swept interval with the vertical slab interval.
	constexpr std::int64_t unit = fixed::unit;
	CollisionBody shot;
	shot.position = b;
	shot.radius = radius;
	shot.mass = 1;
	const auto enter = sweep_contact_time(shot, target, a, target_start);
	if (!enter)
		return {};
	CollisionBody reverse = shot;
	reverse.position = a;
	auto reverse_target = target;
	reverse_target.position = target_start;
	const auto back = sweep_contact_time(reverse, reverse_target, b, target.position);
	auto lo = *enter, hi = back ? unit - *back : unit;
	const auto start = za - tz_start, end = zb - tz_end, delta = end - start;
	if (!delta)
	{
		if (std::abs(start) > half_height + radius)
			return {};
	}
	else
	{
		auto zlo = (-half_height - radius - start) * unit / delta, zhi = (half_height + radius - start) * unit / delta;
		if (zlo > zhi)
			std::swap(zlo, zhi);
		lo = std::max(lo, zlo);
		hi = std::min(hi, zhi);
	}
	return lo <= hi && hi >= 0 && lo <= unit ? std::optional<std::int64_t>{std::max<std::int64_t>(0, lo)}
	                                         : std::nullopt;
}
bool blast_occluded(fixed::Vec origin, std::int64_t z, fixed::Vec end, std::int64_t ez, EntityId ignore) const
{
	bool blocked = false;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (id.id == ignore || combat_dead(e))
			    return;
		    CollisionBody b;
		    static_cast<Pose &>(b) = pose;
		    static_cast<Collider &>(b) = e.get<Collider>();
		    const auto extent = definitions.at(e.get<Definition>().name).half_height;
		    if (const auto hit = volume_hit(origin, end, z, ez, b, pose.position, 0, 0, extent);
		        hit && *hit > 0 && *hit < fixed::unit)
			    blocked = true;
	    });
	return blocked;
}
struct BlastExposure
{
	fixed::Vec point;
	std::int64_t altitude, distance;
};
BlastExposure blast_exposure(fixed::Vec origin, std::int64_t altitude, flecs::entity target) const
{
	const auto &shape = target.get<Collider>();
	const auto &pose = target.get<Pose>();
	const auto local = combat_local(origin, pose);
	fixed::Vec point;
	if (shape.box)
		point = {std::clamp(local.x, -shape.half_width, shape.half_width),
		         std::clamp(local.y, -shape.half_length, shape.half_length)};
	else
	{
		const auto distance = fixed::length(local);
		point = distance > shape.radius ? fixed::scale(fixed::normal(local), shape.radius) : local;
	}
	point = combat_world(point, pose);
	const auto extent = definitions.at(target.get<Definition>().name).half_height;
	const auto z = std::clamp(altitude, -extent, extent);
	const auto delta = point - origin;
	const auto dz = z - altitude;
	return {point, z,
	        static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(delta, delta) + dz * dz)))};
}
void emit_blast(fixed::Vec origin, std::int64_t altitude, const DestructionDefinition &d, EntityId source = 0)
{
	if (!d.visual.empty() || !d.sound.empty())
	{
		if (blast_visuals.size() >= 8192)
			blast_visuals.erase(blast_visuals.begin());
		blast_visuals.push_back({next_blast++, origin, altitude, static_cast<std::int64_t>(ecs.get<Clock>().tick), d});
	}
	if (!d.damage || !d.radius)
		return;
	for (const auto &[id, ship] : combat_rows())
	{
		if (combat_dead(ship))
			continue;
		const auto centre = ship.get<Pose>().position;
		const auto exposure = blast_exposure(origin, altitude, ship);
		const auto distance = exposure.distance;
		if (distance >= d.radius || blast_occluded(origin, altitude, exposure.point, exposure.altitude, id))
			continue;
		Projectile effect;
		effect.source = source;
		effect.heading = combat_bearing(centre - origin);
		effect.damage = d.damage * (d.radius - distance) / d.radius;
		effect.penetration = 1000000;
		effect.definition.deposition = 1000000;
		effect.definition.shield_bypass = d.shield_bypass;
		const auto &shape = ship.get<Collider>();
		const auto extent = shape.box ? shape.half_width + shape.half_length : shape.radius;
		const auto axis = fixed::direction(effect.heading);
		penetrate(effect, ship, centre - fixed::scale(axis, extent + 1), centre + fixed::scale(axis, extent + 1));
	}
	if (d.affects_missiles)
		for (auto row : projectile_rows())
		{
			auto &p = row.get_mut<Projectile>();
			if (!p.definition.durability || p.durability <= 0)
				continue;
			const auto planar = fixed::length(p.position - origin), dz = p.altitude - altitude;
			const auto distance =
			    static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(planar * planar + dz * dz)));
			if (distance < d.radius && !blast_occluded(origin, altitude, p.position, p.altitude, 0))
				p.durability = std::max<std::int64_t>(0, p.durability - d.damage * (d.radius - distance) / d.radius);
		}
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    auto *v = e.try_get_mut<Destructible>();
		    if (!v || v->durability <= 0 || e.has<CombatStructure>())
			    return;
		    const auto exposure = blast_exposure(origin, altitude, e);
		    const auto distance = exposure.distance;
		    if (distance < d.radius && !blast_occluded(origin, altitude, exposure.point, exposure.altitude, id.id))
		    {
			    v->durability = std::max<std::int64_t>(0, v->durability - d.damage * (d.radius - distance) / d.radius);
			    if (!v->durability)
				    v->destroyed_tick = ecs.get<Clock>().tick;
		    }
	    });
}
void destroy_missile(flecs::entity row, bool warhead)
{
	auto &p = row.get_mut<Projectile>();
	if (p.life <= 0)
		return;
	auto &component = row.get_mut<Destructible>();
	if (component.processed)
		return;
	component.processed = true;
	component.destroyed_tick = ecs.get<Clock>().tick;
	p.life = 0;
	p.durability = 0;
	p.random = p.random * 6364136223846793005ULL + 1;
	auto effect = component.definition;
	if (warhead)
	{
		effect.chance = 1000;
		effect.damage = p.definition.damage;
		effect.radius = p.definition.blast_radius;
		effect.shield_bypass = p.definition.shield_bypass;
		effect.affects_missiles = true;
	}
	if ((p.random >> 16) % 1000 < static_cast<std::uint64_t>(effect.chance))
		emit_blast(p.position, p.altitude, effect, p.source);
}
void resolve_destructions()
{
	// Once-only components bound chain work by the finite entity/projectile roster.
	bool changed = true;
	std::size_t iterations = 0;
	while (changed && iterations++ < 16384)
	{
		changed = false;
		for (const auto &[id, e] : entities)
			if (auto *d = e.try_get_mut<Destructible>(); d && !d->processed)
			{
				if (d->destroyed_tick < 0)
					continue;
				d->processed = true;
				changed = true;
				const auto roll =
				    (id * 7919ULL + static_cast<std::uint64_t>(d->destroyed_tick) * 6364136223846793005ULL) >> 16;
				if (roll % 1000 < static_cast<std::uint64_t>(d->definition.chance))
				{
					emit_blast(e.get<Pose>().position, 0, d->definition, id);
				}
			}
		for (auto e : projectile_rows())
		{
			const auto &p = e.get<Projectile>();
			if (p.definition.durability && p.durability <= 0 && p.life > 0)
			{
				destroy_missile(e, false);
				changed = true;
			}
		}
	}
	for (auto e : projectile_rows())
		if (e.get<Projectile>().life <= 0 || e.get<Projectile>().damage <= 0 || e.get<Projectile>().penetration <= 0)
			e.destruct();
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::erase_if(blast_visuals,
	              [&](const auto &v)
	              {
		              return tick - v.tick >= v.definition.growth_ticks + v.definition.fade_ticks;
	              });
}
void fire_beam(flecs::entity ship, flecs::entity child, fixed::Vec origin, std::int64_t bearing)
{
	auto &m = child.get_mut<CombatMechanism>();
	const auto &d = m.definition;
	m.get<WeaponState>().beam_tick = ecs.get<Clock>().tick;
	m.get<WeaponState>().beam_end = origin + fixed::scale(fixed::direction(bearing), d.get<WeaponDefinition>().range);
	std::int64_t nearest = fixed::unit + 1;
	EntityId victim = 0;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (id.id == ship.get<StableId>().id || combat_dead(e))
			    return;
		    CollisionBody target;
		    static_cast<Pose &>(target) = pose;
		    static_cast<Collider &>(target) = e.get<Collider>();
		    if (auto hit = volume_hit(origin, m.get<WeaponState>().beam_end, 0, 0, target, pose.position, 0, 0,
		                              definitions.at(e.get<Definition>().name).half_height);
		        hit && std::pair{*hit, id.id} < std::pair{nearest, victim})
		    {
			    nearest = *hit;
			    victim = id.id;
		    }
	    });
	const auto full_end = m.get<WeaponState>().beam_end;
	if (victim)
		m.get<WeaponState>().beam_end = origin + fixed::Vec{(full_end.x - origin.x) * nearest / fixed::unit,
		                                                    (full_end.y - origin.y) * nearest / fixed::unit};
	m.get<WeaponState>().beam_remainder += d.get<WeaponDefinition>().beam_damage * fault_effectiveness(ship, d.id);
	const auto damage = m.get<WeaponState>().beam_remainder / (rate * 1000);
	m.get<WeaponState>().beam_remainder %= rate * 1000;
	++m.get<WeaponState>().shots;
	if (!victim || !damage)
		return;
	auto target = entities.at(victim);
	if (target.has<CombatStructure>())
	{
		Projectile p;
		p.damage = damage;
		p.continuous = true;
		p.bypass_remainder = m.get<WeaponState>().bypass_remainder;
		p.penetration = d.ammunition().projectile.penetration;
		p.definition = d.ammunition().projectile;
		p.heading = bearing;
		penetrate(p, target, origin, full_end);
		m.get<WeaponState>().bypass_remainder = p.bypass_remainder;
	}
	else if (auto *v = target.try_get_mut<Destructible>(); v && v->durability > 0)
	{
		v->durability = std::max<std::int64_t>(0, v->durability - damage);
		if (!v->durability)
			v->destroyed_tick = ecs.get<Clock>().tick;
	}
}
void advance_ordnance(Projectile &p)
{
	const auto old_z = p.altitude;
	if (p.definition.durability)
	{
		auto desired = p.heading;
		if (p.definition.guidance == "homing" && entities.contains(p.target) && !combat_dead(entities.at(p.target)))
		{
			const auto target = entities.at(p.target).get<Pose>().position;
			if (fixed::length(target - p.position) + std::abs(p.altitude) <= p.definition.seeker_range)
				desired = combat_bearing(target - p.position);
		}
		if (p.definition.guidance == "chaotic")
		{
			p.random = p.random * 6364136223846793005ULL + 1;
			desired = combat_angle(p.base_heading +
			                       static_cast<std::int64_t>((p.random >> 16) % (p.definition.chaos * 2 + 1)) -
			                       p.definition.chaos);
		}
		p.turn_remainder += p.definition.turn_rate;
		const auto step = p.turn_remainder / rate;
		p.turn_remainder %= rate;
		p.heading = combat_angle(p.heading + std::clamp(combat_delta(desired, p.heading), -step, step));
		p.velocity = fixed::scale(fixed::direction(p.heading), p.definition.speed);
		const auto progress = std::clamp<std::int64_t>(
		    p.age * p.definition.speed * 1000 / (std::max<std::int64_t>(1, p.initial_distance) * rate), 0, 1000);
		p.altitude = 4 * p.definition.arc_height * progress * (1000 - progress) / 1000000;
		p.vz = (p.altitude - old_z) * rate;
	}
	else
	{
		p.z_remainder += p.vz;
		p.altitude += p.z_remainder / rate;
		p.z_remainder %= rate;
	}
	p.remainder = p.remainder + p.velocity;
	p.position = p.position + fixed::Vec{p.remainder.x / rate, p.remainder.y / rate};
	p.remainder.x %= rate;
	p.remainder.y %= rate;
	++p.age;
}
void step_ordnance()
{
	if (duel_ended())
	{
		for (auto e : projectile_rows())
			e.destruct();
		return;
	}
	struct Previous
	{
		fixed::Vec position;
		std::int64_t altitude;
	};
	std::map<std::uint64_t, Previous> previous;
	auto rows = projectile_rows();
	for (auto row : rows)
	{
		auto &p = row.get_mut<Projectile>();
		previous[p.id] = {p.position, p.altitude};
		advance_ordnance(p);
	}
	struct Target
	{
		EntityId id;
		flecs::entity entity;
		CollisionBody body;
		fixed::Vec start;
		std::int64_t half_height;
	};
	std::vector<Target> targets;
	std::vector<Bounds> bounds;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (combat_dead(e))
			    return;
		    CollisionBody b;
		    static_cast<Pose &>(b) = pose;
		    static_cast<Collider &>(b) = e.get<Collider>();
		    const auto start = step_starts.contains(id.id) ? step_starts.at(id.id) : pose.position;
		    targets.push_back({id.id, e, b, start, definitions.at(e.get<Definition>().name).half_height});
		    const auto radius = b.box ? b.half_width + b.half_length : b.radius;
		    bounds.push_back(Bounds::merge(Bounds::around(start, radius), Bounds::around(pose.position, radius)));
	    });
	SpatialIndex spatial;
	spatial.rebuild(bounds);
	std::vector<flecs::entity> missiles;
	std::vector<Bounds> missile_bounds;
	for (auto row : rows)
	{
		const auto &p = row.get<Projectile>();
		if (!p.definition.durability)
			continue;
		missiles.push_back(row);
		missile_bounds.push_back(Bounds::merge(Bounds::around(previous.at(p.id).position, p.definition.radius),
		                                       Bounds::around(p.position, p.definition.radius)));
	}
	SpatialIndex missile_index;
	missile_index.rebuild(missile_bounds);
	for (auto row : rows)
	{
		auto &p = row.get_mut<Projectile>();
		if (p.life <= 0 || (p.definition.durability && p.durability <= 0))
			continue;
		const auto from = previous.at(p.id);
		struct Hit
		{
			std::int64_t time;
			EntityId id;
			flecs::entity entity;
			bool missile;
		};
		std::vector<Hit> hits;
		const auto query = Bounds::merge(Bounds::around(from.position, p.definition.radius),
		                                 Bounds::around(p.position, p.definition.radius));
		for (auto at = spatial.first(query, 0); at != SpatialIndex::none; at = spatial.first(query, at + 1))
		{
			const auto &t = targets[at];
			if (t.id == p.source || combat_dead(t.entity))
				continue;
			if (auto hit = volume_hit(from.position, p.position, from.altitude, p.altitude, t.body, t.start, 0, 0,
			                          t.half_height, p.definition.radius))
				hits.push_back({*hit, t.id, t.entity, false});
			else if (p.definition.durability && t.id == p.target && p.definition.fuse_radius)
			{
				auto fuse = t.body;
				fuse.box = false;
				fuse.radius = p.definition.fuse_radius;
				if (auto hit = volume_hit(from.position, p.position, from.altitude, p.altitude, fuse, t.start, 0, 0,
				                          p.definition.fuse_radius))
					hits.push_back({*hit, t.id, t.entity, false});
			}
		}
		for (auto at = missile_index.first(query, 0); at != SpatialIndex::none; at = missile_index.first(query, at + 1))
		{
			auto e = missiles[at];
			const auto &target = e.get<Projectile>();
			if (target.id == p.id || target.source == p.source || target.durability <= 0 ||
			    p.missile_hits.contains(target.id))
				continue;
			CollisionBody shape;
			shape.position = target.position;
			shape.radius = target.definition.radius;
			shape.mass = 1;
			const auto old = previous.at(target.id);
			if (auto hit = volume_hit(from.position, p.position, from.altitude, p.altitude, shape, old.position,
			                          old.altitude, target.altitude, target.definition.radius, p.definition.radius))
				hits.push_back({*hit, target.id, e, true});
		}
		std::sort(hits.begin(), hits.end(),
		          [](const auto &a, const auto &b)
		          {
			          return std::tuple{a.time, a.missile, a.id} < std::tuple{b.time, b.missile, b.id};
		          });
		for (const auto &hit : hits)
		{
			if (p.life <= 0 || !p.damage || !p.penetration)
				break;
			const auto impact = from.position + fixed::Vec{(p.position.x - from.position.x) * hit.time / fixed::unit,
			                                               (p.position.y - from.position.y) * hit.time / fixed::unit};
			const auto z = from.altitude + (p.altitude - from.altitude) * hit.time / fixed::unit;
			if (p.definition.durability && p.definition.blast_radius)
			{
				p.position = impact;
				p.altitude = z;
				destroy_missile(row, true);
				break;
			}
			if (hit.missile)
			{
				auto &m = hit.entity.get_mut<Projectile>();
				if (m.durability <= 0)
					continue;
				const auto old = previous.at(m.id);
				CollisionBody reverse_target;
				reverse_target.position = old.position;
				reverse_target.radius = m.definition.radius;
				reverse_target.mass = 1;
				const auto reverse =
				    volume_hit(p.position, from.position, p.altitude, from.altitude, reverse_target, m.position,
				               m.altitude, old.altitude, m.definition.radius, p.definition.radius);
				const auto exit = reverse ? fixed::unit - *reverse : fixed::unit;
				if (exit < fixed::unit)
					p.missile_hits.insert(m.id);
				const auto relative = (p.position - from.position) - (m.position - old.position);
				const auto dz = (p.altitude - from.altitude) - (m.altitude - old.altitude);
				const auto path = static_cast<std::int64_t>(
				    fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(relative, relative) + dz * dz)));
				const auto distance = path * std::max<std::int64_t>(0, exit - hit.time) / fixed::unit;
				const auto deposited = deposit_material(p, distance, m.definition.resistance);
				m.durability = std::max<std::int64_t>(0, m.durability - deposited);
			}
			else if (hit.entity.has<CombatStructure>())
			{
				const auto start =
				    step_starts.contains(hit.id) ? step_starts.at(hit.id) : hit.entity.get<Pose>().position;
				penetrate(p, hit.entity, from.position + (hit.entity.get<Pose>().position - start), p.position);
			}
			else
			{
				if (auto *d = hit.entity.try_get_mut<Destructible>(); d && d->durability > 0)
				{
					d->durability = std::max<std::int64_t>(0, d->durability - p.damage);
					if (!d->durability)
						d->destroyed_tick = ecs.get<Clock>().tick;
				}
				p.damage = 0;
				p.penetration = 0;
			}
		}
		if (p.life > 0 && --p.life == 0 && p.definition.durability)
		{
			p.life = 1;
			destroy_missile(row, false);
		}
		if (std::abs(p.position.x) > fixed::unit || std::abs(p.position.y) > fixed::unit)
			p.life = 0;
	}
}
