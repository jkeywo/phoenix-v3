// World::Impl combat adapter. All decisions originate at Controllers; mechanisms consume admitted intent.
std::vector<Projectile> projectiles;
std::uint64_t next_projectile = 1;
bool combat_duel = false;
bool duel_ended() const
{
	if (!combat_duel)
		return false;
	for (const auto &[id, e] : entities)
		if (combat_dead(e))
			return true;
	return false;
}
bool combat_dead(flecs::entity ship) const
{
	const auto *s = ship.try_get<CombatStructure>();
	return s && s->destroyed_tick >= 0;
}
bool structurally_disabled(flecs::entity ship, const std::string &equipment) const
{
	const auto *s = ship.try_get<CombatStructure>();
	if (!s)
		return false;
	if (auto fit = instance(ship, equipment); fit && fit->kind == OperatingSystem::RepairTeam)
		return s->destroyed_tick >= 0;
	if (s->destroyed_tick >= 0)
		return true;
	const auto at = s->definition.equipment_compartments.find(equipment);
	return at != s->definition.equipment_compartments.end() &&
	       s->compartments.at(s->definition.compartment_ids.at(at->second)).disabled;
}
void combat_damage(flecs::entity ship, std::size_t compartment, std::int64_t amount)
{
	auto &s = ship.get_mut<CombatStructure>();
	if (s.destroyed_tick >= 0 || amount <= 0)
		return;
	auto &c = s.compartments.at(compartment);
	c.hp = std::max<std::int64_t>(0, c.hp - amount);
	if (!c.hp)
		c.disabled = true;
	if (std::all_of(s.compartments.begin(), s.compartments.end(),
	                [](const auto &v)
	                {
		                return v.hp == 0;
	                }))
	{
		s.destroyed_tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	}
}
bool operate_combat(flecs::entity ship, flecs::entity child, OperatingRequest output, bool execute)
{
	auto &m = child.get_mut<CombatMechanism>();
	if (!output.arguments.empty())
		return false;
	if (output.action == OperatingAction::SystemEnabled)
	{
		if (output.value != 0 && output.value != 1)
			return false;
		if (execute)
			child.get_mut<Equipment>().enabled = output.value != 0;
		return true;
	}
	if (output.action == OperatingAction::WeaponTarget && m.definition.kind == OperatingSystem::Weapon)
	{
		const auto target = static_cast<EntityId>(output.value);
		if (output.value < 0 || target == ship.get<StableId>().id)
			return false;
		if (target)
		{
			const auto owner = system_owner(ship.get<StableId>().id, m.definition.id);
			const auto observation = capture_console_observation(owner);
			if (std::none_of(observation.contacts.begin(), observation.contacts.end(),
			                 [&](const auto &c)
			                 {
				                 return c.id == target;
			                 }))
				return false;
		}
		if (execute)
		{
			m.target = target;
			if (target && !m.definition.turret.empty())
				for (auto other : ship.get<EquipmentIndex>().values)
					if (auto *w = other.try_get_mut<CombatMechanism>())
					{
						if (w->definition.id == m.definition.turret)
							w->target = target;
						if (w->definition.kind == OperatingSystem::Weapon &&
						    w->definition.turret == m.definition.turret && w->target)
							w->target = target;
					}
		}
		return true;
	}
	if (output.action == OperatingAction::RepairCompartment && m.definition.kind == OperatingSystem::RepairTeam)
	{
		const auto count = static_cast<std::int64_t>(ship.get<CombatStructure>().compartments.size());
		if (output.value < 0 || output.value > count)
			return false;
		if (execute)
			m.destination = output.value - 1;
		return true;
	}
	return false;
}
void combat_telemetry(flecs::entity ship, flecs::entity child, std::map<std::string, std::int64_t> &v) const
{
	const auto &fit = child.get<Equipment>();
	const bool stopped = structurally_disabled(ship, fit.id);
	v["structural_disabled"] = stopped;
	if (stopped)
		v["enabled"] = 0;
	if (const auto *m = child.try_get<CombatMechanism>())
	{
		v["target"] = m->target;
		v["requested"] = m->definition.kind == OperatingSystem::RepairTeam ? m->destination + 1
		                                                                   : static_cast<std::int64_t>(m->target);
		v["ready"] = m->ready;
		v["heading_mdeg"] = m->heading;
		v["ammo"] = m->ammo;
		v["capacity"] = m->definition.capacity;
		v["reload_progress"] = m->refill;
		v["reload_ticks"] = m->definition.refill_ticks;
		v["shots"] = m->shots;
		v["team_location"] = m->location;
		v["team_destination"] = m->destination;
		v["travel_remaining"] = m->travel;
	}
}
void apply_structural_disablement()
{
	// Stop actuation without overwriting accepted enable/mode settings.
	for (auto &[id, ship] : entities)
		if (ship.has<CombatStructure>())
		{
			if (combat_dead(ship))
			{
				ship.remove<Movable>();
				ship.get_mut<Motion>().velocity = {};
			}
		}
}
void prepare_combat(flecs::entity ship)
{
	if (!ship.has<CombatStructure>() || duel_ended())
		return;
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>())
		{
			m->ready = false;
			if (m->cooldown > 0)
				--m->cooldown;
		}
	// Turret steering is a separate mechanism, shared by any hosted weapons.
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Turret)
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			const auto owner = system_owner(id, d.id);
			const auto observed = capture_console_observation(owner);
			const auto target = std::find_if(observed.contacts.begin(), observed.contacts.end(),
			                                 [&](const auto &c)
			                                 {
				                                 return c.id == m->target;
			                                 });
			if (target == observed.contacts.end())
				continue;
			std::int64_t speed = 100000;
			for (auto weapon : ship.get<EquipmentIndex>().values)
				if (const auto *w = weapon.try_get<CombatMechanism>(); w && w->definition.turret == d.id && w->target)
				{
					speed = w->definition.projectile.speed;
					break;
				}
			const auto origin = combat_world({d.x, d.y}, pose);
			const auto travel = fixed::length(fixed::Vec{target->x, target->y} - origin) * 1000 / speed;
			const auto lead = fixed::Vec{target->x + (target->vx - ship.get<Motion>().velocity.x) * travel / 1000,
			                             target->y + (target->vy - ship.get<Motion>().velocity.y) * travel / 1000};
			const auto wanted = combat_delta(combat_bearing(lead - origin), pose.heading);
			const auto offset = std::clamp(combat_delta(wanted, d.bearing), -d.arc / 2, d.arc / 2);
			const auto desired = combat_angle(d.bearing + offset);
			m->turn_remainder += d.traverse;
			const auto step = m->turn_remainder / rate;
			m->turn_remainder %= rate;
			// Traverse within the authored interval, never through a blind sector.
			const auto current = combat_delta(m->heading, d.bearing);
			m->heading = combat_angle(d.bearing + current + std::clamp(offset - current, -step, step));
		}
	std::map<std::string, std::int64_t> reserved;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
			if (!m->target || m->cooldown || !child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			const auto mag = instance(ship, d.magazine)->entity;
			if (!mag.get<Equipment>().enabled || structurally_disabled(ship, d.magazine) ||
			    mag.get<CombatMechanism>().ammo < d.ammo_use)
				continue;
			std::int64_t orientation = pose.heading + d.bearing;
			fixed::Vec mount{d.x, d.y};
			if (!d.turret.empty())
			{
				const auto turret = instance(ship, d.turret)->entity;
				const auto &t = turret.get<CombatMechanism>();
				if (!turret.get<Equipment>().enabled || structurally_disabled(ship, d.turret))
					continue;
				orientation = pose.heading + t.heading + d.bearing;
				mount = {t.definition.x + d.x, t.definition.y + d.y};
			}
			const auto observed = capture_console_observation(system_owner(id, d.id));
			const auto target = std::find_if(observed.contacts.begin(), observed.contacts.end(),
			                                 [&](const auto &c)
			                                 {
				                                 return c.id == m->target;
			                                 });
			if (target == observed.contacts.end())
				continue;
			const auto origin = combat_world(mount, pose);
			const auto travel = fixed::length(fixed::Vec{target->x, target->y} - origin) * 1000 / d.projectile.speed;
			const auto lead = fixed::Vec{target->x + (target->vx - ship.get<Motion>().velocity.x) * travel / 1000,
			                             target->y + (target->vy - ship.get<Motion>().velocity.y) * travel / 1000};
			m->ready = std::abs(combat_delta(combat_bearing(lead - origin), orientation)) <= d.arc / 2;
			if (m->ready)
			{
				const auto available = mag.get<CombatMechanism>().ammo - reserved[d.magazine];
				if (available < d.ammo_use)
					m->ready = false;
				else
					reserved[d.magazine] += d.ammo_use;
			}
		}
}
void request_combat_resources(flecs::entity ship)
{
	auto *power = ship.try_get_mut<ShipResources>();
	if (!power)
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			const bool enabled = child.get<Equipment>().enabled && !structurally_disabled(ship, d.id);
			const auto index = child.get<ResourceBinding>().slot;
			if (d.kind == OperatingSystem::Magazine)
				request_system_resource(*power, index, 0, false,
				                        enabled && d.reload == "energy" && m->ammo < d.capacity ? d.reload_energy : 0);
			if (d.kind == OperatingSystem::Weapon)
				request_system_resource(*power, index, 0, enabled && m->ready);
		}
}
void actuate_combat(flecs::entity ship)
{
	if (!ship.has<CombatStructure>() || duel_ended())
		return;
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	auto equipment = [&](const std::string &name)
	{
		return instance(ship, name)->entity;
	};
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>())
		{
			const auto &d = m->definition;
			if (!child.get<Equipment>().enabled || structurally_disabled(ship, d.id))
				continue;
			if (d.kind == OperatingSystem::Magazine && d.reload != "none" && m->ammo < d.capacity)
			{
				bool paid = d.reload == "infinite";
				if (d.reload == "energy")
					if (const auto *p = ship.try_get<ShipResources>())
						paid = p->grants.at(child.get<ResourceBinding>().slot).supply_permille == 1000 &&
						       !p->grants.at(child.get<ResourceBinding>().slot).blocked;
				if (paid && ++m->refill >= d.refill_ticks)
				{
					m->ammo = std::min(d.capacity, m->ammo + d.refill_amount);
					m->refill = 0;
				}
			}
		}

	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &d = m->definition;
			if (!m->ready || !m->target || m->cooldown || !child.get<Equipment>().enabled ||
			    structurally_disabled(ship, d.id))
				continue;
			auto mag = equipment(d.magazine);
			auto &ammo = mag.get_mut<CombatMechanism>();
			if (!mag.get<Equipment>().enabled || structurally_disabled(ship, d.magazine) || ammo.ammo < d.ammo_use)
				continue;
			std::int64_t orientation = pose.heading + d.bearing;
			fixed::Vec mount{d.x, d.y};
			if (!d.turret.empty())
			{
				auto turret = equipment(d.turret);
				const auto &t = turret.get<CombatMechanism>();
				if (!turret.get<Equipment>().enabled || structurally_disabled(ship, d.turret))
					continue;
				orientation = pose.heading + t.heading + d.bearing;
				mount = {t.definition.x + d.x, t.definition.y + d.y};
			}
			const auto owner = system_owner(id, d.id);
			const auto observed = capture_console_observation(owner);
			const auto target = std::find_if(observed.contacts.begin(), observed.contacts.end(),
			                                 [&](const auto &c)
			                                 {
				                                 return c.id == m->target;
			                                 });
			if (target == observed.contacts.end())
				continue;
			const auto origin = combat_world(mount, pose);
			const auto travel = fixed::length(fixed::Vec{target->x, target->y} - origin) * 1000 / d.projectile.speed;
			const auto lead = fixed::Vec{target->x + (target->vx - ship.get<Motion>().velocity.x) * travel / 1000,
			                             target->y + (target->vy - ship.get<Motion>().velocity.y) * travel / 1000};
			auto bearing = combat_bearing(lead - origin);
			if (std::abs(combat_delta(bearing, orientation)) > d.arc / 2)
				continue;
			if (const auto *power = ship.try_get<ShipResources>())
			{
				const auto &grant = power->grants.at(child.get<ResourceBinding>().slot);
				const auto &binding = power->definition.systems.at(child.get<ResourceBinding>().slot);
				if (grant.blocked || (binding.discrete_energy && !grant.discrete_paid))
					continue;
			}
			m->ready = true;
			ammo.ammo -= d.ammo_use;
			m->cooldown = d.interval;
			++m->shots;
			auto &structure = ship.get_mut<CombatStructure>();
			structure.random = structure.random * 6364136223846793005ULL + 1;
			if (d.projectile.spread)
				bearing =
				    combat_angle(bearing +
				                 static_cast<std::int64_t>((structure.random >> 16) %
				                                           static_cast<std::uint64_t>(d.projectile.spread * 2 + 1)) -
				                 d.projectile.spread);
			if (projectiles.size() >= 8192)
				throw std::runtime_error("Combat projectile capacity exceeded");
			Projectile p;
			p.id = next_projectile++;
			p.source = id;
			p.position = origin;
			p.heading = bearing;
			p.velocity = fixed::scale(fixed::direction(bearing), d.projectile.speed) + ship.get<Motion>().velocity;
			p.life = d.projectile.lifetime;
			p.penetration = d.projectile.penetration;
			p.damage = d.projectile.damage;
			p.definition = d.projectile;
			projectiles.push_back(p);
		}
}
void penetrate(Projectile &p, flecs::entity ship, fixed::Vec start, fixed::Vec end)
{
	auto &s = ship.get_mut<CombatStructure>();
	if (combat_dead(ship))
		return;
	const auto id = ship.get<StableId>().id;
	if (!p.decks.contains(id))
	{
		std::map<std::string, std::int64_t> weights;
		for (const auto &c : s.definition.compartments)
			weights[c.deck] = c.deck_weight;
		std::int64_t total = 0;
		for (const auto &[deck, w] : weights)
			total += w;
		s.random = s.random * 6364136223846793005ULL + 1;
		auto roll = static_cast<std::int64_t>((s.random >> 16) % static_cast<std::uint64_t>(total));
		for (const auto &[deck, w] : weights)
		{
			if (roll < w)
			{
				p.decks[id] = deck;
				break;
			}
			roll -= w;
		}
	}
	const auto a = combat_local(start, ship.get<Pose>()), b = combat_local(end, ship.get<Pose>());
	const auto length = fixed::length(b - a);
	struct Traversal
	{
		std::int64_t from, to;
		std::size_t index;
	};
	std::vector<Traversal> lines;
	for (std::size_t i = 0; i < s.definition.compartments.size(); ++i)
		if (const auto &c = s.definition.compartments[i]; c.deck == p.decks.at(id))
			if (auto hit = compartment_segment(a, b, c))
				lines.push_back({hit->first, hit->second, i});
	std::sort(lines.begin(), lines.end(),
	          [](const auto &x, const auto &y)
	          {
		          return std::pair{x.from, x.index} < std::pair{y.from, y.index};
	          });
	for (const auto &line : lines)
	{
		if (!p.damage || !p.penetration)
			break;
		const auto &d = s.definition.compartments[line.index];
		auto &c = s.compartments[line.index];
		auto distance = length * (line.to - line.from) / 1000000;
		const auto resistance = std::max<std::int64_t>(1, d.resistance * c.hp / d.maximum);
		const auto cost = (distance * resistance + p.resistance_remainder) / 1000;
		const bool stops = cost >= p.penetration;
		if (stops)
			distance = std::min(distance, (p.penetration * 1000 + resistance - 1) / resistance);
		const auto deposit =
		    stops ? p.damage : std::min(p.damage, (distance * p.definition.deposition + p.deposit_remainder) / 1000);
		p.deposit_remainder = (distance * p.definition.deposition + p.deposit_remainder) % 1000;
		p.resistance_remainder = (distance * resistance + p.resistance_remainder) % 1000;
		p.penetration = stops ? 0 : std::max<std::int64_t>(0, p.penetration - cost);
		p.damage -= deposit;
		combat_damage(ship, line.index, deposit);
	}
}
void step_projectiles()
{
	if (duel_ended())
	{
		projectiles.clear();
		return;
	}
	for (auto &p : projectiles)
	{
		const auto from = p.position;
		p.remainder = p.remainder + p.velocity;
		const fixed::Vec delta{p.remainder.x / rate, p.remainder.y / rate};
		p.remainder.x %= rate;
		p.remainder.y %= rate;
		p.position = p.position + delta;
		struct Hit
		{
			std::int64_t time;
			EntityId id;
			fixed::Vec start, end;
		};
		std::vector<Hit> hits;
		CollisionBody shot;
		shot.position = p.position;
		shot.radius = 1;
		shot.mass = 1;
		for (const auto &[id, entity] : entities)
			if (id != p.source && !combat_dead(entity))
			{
				const auto &pose = entity.get<Pose>();
				const auto &shape = entity.get<Collider>();
				const auto begin = step_starts.contains(id) ? step_starts.at(id) : pose.position;
				const auto radius = shape.box ? shape.half_width + shape.half_length : shape.radius;
				const auto minx = std::min(from.x, p.position.x), maxx = std::max(from.x, p.position.x),
				           miny = std::min(from.y, p.position.y), maxy = std::max(from.y, p.position.y);
				if (maxx < std::min(begin.x, pose.position.x) - radius ||
				    minx > std::max(begin.x, pose.position.x) + radius ||
				    maxy < std::min(begin.y, pose.position.y) - radius ||
				    miny > std::max(begin.y, pose.position.y) + radius)
					continue;
				CollisionBody target;
				static_cast<Pose &>(target) = pose;
				static_cast<Collider &>(target) = shape;
				if (auto time = sweep_contact_time(shot, target, from, begin))
					hits.push_back({*time, id, from + (pose.position - begin), p.position});
			}
		std::sort(hits.begin(), hits.end(),
		          [](const auto &a, const auto &b)
		          {
			          return std::pair{a.time, a.id} < std::pair{b.time, b.id};
		          });
		for (const auto &hit : hits)
		{
			if (!p.damage || !p.penetration)
				break;
			auto ship = entities.at(hit.id);
			if (ship.has<CombatStructure>())
				penetrate(p, ship, hit.start, hit.end);
			else
			{
				p.damage = 0;
				p.penetration = 0;
			}
		}
		--p.life;
	}
	std::erase_if(projectiles,
	              [](const auto &p)
	              {
		              return p.life <= 0 || p.damage <= 0 || p.penetration <= 0 ||
		                     std::abs(p.position.x) > fixed::unit || std::abs(p.position.y) > fixed::unit;
	              });
}
std::pair<std::int64_t, std::int64_t> repair_route(const CombatStructure &s, std::size_t from, std::size_t to) const
{
	const auto n = s.compartments.size();
	const auto infinity = std::int64_t{1000000000000};
	std::vector<std::int64_t> distance(n, infinity), first(n, -1);
	std::set<std::size_t> visited;
	distance[from] = 0;
	for (std::size_t iteration = 0; iteration < n; ++iteration)
	{
		std::size_t at = n;
		for (std::size_t i = 0; i < n; ++i)
			if (!visited.contains(i) && (at == n || distance[i] < distance[at]))
				at = i;
		if (at == n || distance[at] == infinity)
			break;
		if (at == to)
			break;
		visited.insert(at);
		for (const auto &edge : s.definition.connections)
		{
			const auto next = edge.from == at ? edge.to : edge.to == at ? edge.from : n;
			if (next == n)
				continue;
			const auto cost = distance[at] + edge.ticks;
			if (cost < distance[next])
			{
				distance[next] = cost;
				first[next] = at == from ? static_cast<std::int64_t>(next) : first[at];
			}
		}
	}
	if (first[to] < 0)
		return {-1, 0};
	for (const auto &edge : s.definition.connections)
		if ((edge.from == from && edge.to == static_cast<std::size_t>(first[to])) ||
		    (edge.to == from && edge.from == static_cast<std::size_t>(first[to])))
			return {first[to], edge.ticks};
	return {-1, 0};
}
void step_repairs()
{
	if (duel_ended())
		return;
	for (auto &[id, ship] : entities)
		if (auto *s = ship.try_get_mut<CombatStructure>(); s && !combat_dead(ship))
		{
			std::map<std::size_t, std::vector<flecs::entity>> workers;
			for (auto child : ship.get<EquipmentIndex>().values)
				if (auto *m = child.try_get_mut<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::RepairTeam)
				{
					if (!child.get<Equipment>().enabled)
						continue;
					if (m->travel > 0)
					{
						if (--m->travel == 0)
						{
							m->location = m->edge_to;
							m->edge_to = -1;
							m->edge_total = 0;
						}
						else
							continue;
					}
					if (m->destination < 0)
						continue;
					if (m->location != m->destination)
					{
						const auto route = repair_route(*s, static_cast<std::size_t>(m->location),
						                                static_cast<std::size_t>(m->destination));
						m->edge_to = route.first;
						m->travel = route.second;
						m->edge_total = route.second;
						continue;
					}
					workers[static_cast<std::size_t>(m->location)].push_back(child);
				}
			for (auto &[index, teams] : workers)
			{
				std::sort(teams.begin(), teams.end(),
				          [](auto a, auto b)
				          {
					          const auto &x = a.template get<CombatMechanism>();
					          const auto &y = b.template get<CombatMechanism>();
					          return x.definition.repair_rate != y.definition.repair_rate
					                     ? x.definition.repair_rate > y.definition.repair_rate
					                     : x.definition.id < y.definition.id;
				          });
				auto &c = s->compartments[index];
				for (std::size_t rank = 0; rank < teams.size(); ++rank)
				{
					const auto &d = teams[rank].get<CombatMechanism>().definition;
					c.work += d.repair_rate * 1000000 / (1000 + d.stack_penalty * static_cast<std::int64_t>(rank));
				}
				const auto restored = c.work / (rate * 1000);
				c.work %= rate * 1000;
				c.hp = std::min(s->definition.compartments[index].maximum, c.hp + restored);
				if (c.disabled && c.hp >= s->definition.compartments[index].recovery)
					c.disabled = false;
			}
		}
}
void collision_damage(EntityId a, EntityId b, const CollisionBody &before_a, const CollisionBody &before_b)
{
	if (duel_ended())
		return;
	const auto direction = fixed::normal(before_b.position - before_a.position);
	const auto closing =
	    std::max<std::int64_t>(0, fixed::dot(before_a.velocity - before_b.velocity, direction) / fixed::unit);
	for (const auto id : {a, b})
		if (auto ship = entities.at(id); ship.has<CombatStructure>())
		{
			auto &s = ship.get_mut<CombatStructure>();
			if (closing < s.definition.collision_threshold)
				continue;
			const auto mass = before_a.mass && before_b.mass ? std::min(before_a.mass, before_b.mass)
			                                                 : std::max(before_a.mass, before_b.mass);
			const auto metres = std::min<std::int64_t>(closing / 1000, 10000);
			const auto damage = std::min<std::int64_t>(1000000, std::max<std::int64_t>(1, mass / 1000000) * metres *
			                                                        metres / 100 * s.definition.collision_scale);
			const auto contact = combat_local(id == a ? before_b.position : before_a.position, ship.get<Pose>());
			std::size_t nearest = 0;
			std::int64_t best = std::numeric_limits<std::int64_t>::max();
			for (std::size_t i = 0; i < s.definition.compartments.size(); ++i)
			{
				const auto &c = s.definition.compartments[i];
				const auto distance = fixed::length(contact - fixed::Vec{c.x, c.y});
				if (distance < best)
				{
					best = distance;
					nearest = i;
				}
			}
			combat_damage(ship, nearest, damage);
		}
}
void project_combat(const ConsoleAddress &address, Observation &result) const
{
	const auto ship = entities.at(address.ship);
	const auto *s = ship.try_get<CombatStructure>();
	if (!s)
		return;
	Json output = {{"destroyed", s->destroyed_tick >= 0},
	               {"compartments", Json::array()},
	               {"teams", Json::array()},
	               {"arcs", Json::array()}};
	for (std::size_t i = 0; i < s->compartments.size(); ++i)
		if (address.console == s->definition.repair_console)
		{
			const auto &d = s->definition.compartments[i];
			const auto &c = s->compartments[i];
			output["compartments"].push_back({{"id", d.id},
			                                  {"index", i + 1},
			                                  {"deck", d.deck},
			                                  {"hp", c.hp},
			                                  {"maximum", d.maximum},
			                                  {"disabled", c.disabled},
			                                  {"x_mm", d.x},
			                                  {"y_mm", d.y},
			                                  {"half_width_mm", d.half_width},
			                                  {"half_length_mm", d.half_length}});
		}
	const auto &feeds = seat(address.ship, address.console)->feed_systems;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			if (d.kind == OperatingSystem::RepairTeam && address.console == s->definition.repair_console)
				output["teams"].push_back({{"id", d.id},
				                           {"location", m->location},
				                           {"destination", m->destination},
				                           {"travel", m->travel},
				                           {"edge_to", m->edge_to},
				                           {"edge_total", m->edge_total}});
			if (std::find(feeds.begin(), feeds.end(), d.id) == feeds.end())
				continue;
			if (d.kind == OperatingSystem::Weapon || d.kind == OperatingSystem::Turret)
			{
				auto heading = ship.get<Pose>().heading + (d.kind == OperatingSystem::Turret ? m->heading : d.bearing);
				if (!d.turret.empty())
					heading += instance(ship, d.turret)->entity.get<CombatMechanism>().heading;
				output["arcs"].push_back(
				    {{"id", d.id},
				     {"kind", std::string(operating_system_name(d.kind))},
				     {"bearing_mdeg", combat_angle(heading)},
				     {"rest_mdeg", combat_angle(ship.get<Pose>().heading + d.bearing)},
				     {"arc_mdeg", d.arc},
				     {"active", m->target != 0},
				     {"disabled", structurally_disabled(ship, d.id) || !child.get<Equipment>().enabled}});
			}
		}
	if (combat_duel && duel_ended())
	{
		int alive = 0;
		for (const auto &[id, e] : entities)
			if (e.has<CombatStructure>() && !combat_dead(e))
				++alive;
		output["result"] = alive == 0 ? "Draw" : combat_dead(ship) ? "Defeat" : "Victory";
	}
	result.gameplay["combat"] = std::move(output);
}
Json save_combat(flecs::entity ship) const
{
	const auto &s = ship.get<CombatStructure>();
	Json j = {{"destroyed_tick", s.destroyed_tick},
	          {"random", s.random},
	          {"compartments", Json::array()},
	          {"systems", Json::object()}};
	for (const auto &c : s.compartments)
		j["compartments"].push_back({{"hp", c.hp}, {"work", c.work}, {"disabled", c.disabled}});
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			j["systems"][m->definition.id] = {
			    {"target", m->target},     {"heading", m->heading},   {"turn_remainder", m->turn_remainder},
			    {"cooldown", m->cooldown}, {"ammo", m->ammo},         {"refill", m->refill},
			    {"shots", m->shots},       {"location", m->location}, {"destination", m->destination},
			    {"edge_to", m->edge_to},   {"travel", m->travel},     {"edge_total", m->edge_total},
			    {"ready", m->ready}};
	return j;
}
void restore_combat(flecs::entity ship, const Json &j)
{
	auto &s = ship.get_mut<CombatStructure>();
	s.destroyed_tick = checked_integer(j.at("destroyed_tick"), -1, static_cast<std::int64_t>(1000000000000LL),
	                                   "combat.destroyed_tick");
	s.random = j.at("random");
	if (j.at("compartments").size() != s.compartments.size())
		throw std::runtime_error("Combat compartment roster mismatch");
	for (std::size_t i = 0; i < s.compartments.size(); ++i)
	{
		const auto &v = j.at("compartments")[i];
		auto &c = s.compartments[i];
		c.hp = checked_integer(v.at("hp"), 0, s.definition.compartments[i].maximum, "compartment.hp");
		c.work = checked_integer(v.at("work"), 0, rate * 1000 - 1, "compartment.work");
		c.disabled = v.at("disabled");
		if ((c.hp == 0 && !c.disabled) || (c.hp >= s.definition.compartments[i].recovery && c.disabled))
			throw std::runtime_error("Invalid structural disablement");
	}
	if (j.at("systems").size() != s.definition.systems.size())
		throw std::runtime_error("Combat equipment roster mismatch");
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>())
		{
			const auto &v = j.at("systems").at(m->definition.id);
			const auto n = static_cast<std::int64_t>(s.compartments.size());
			m->target = checked_integer(v.at("target"), 0, 1000000000, "weapon.target");
			m->heading = checked_integer(v.at("heading"), 0, 359999, "turret.heading");
			m->turn_remainder = checked_integer(v.at("turn_remainder"), 0, rate - 1, "turret.remainder");
			m->cooldown = checked_integer(v.at("cooldown"), 0, m->definition.interval, "weapon.cooldown");
			m->ammo = checked_integer(v.at("ammo"), 0, m->definition.capacity, "magazine.ammo");
			m->refill = checked_integer(v.at("refill"), 0, m->definition.refill_ticks - 1, "magazine.refill");
			m->shots = checked_integer(v.at("shots"), 0, 1000000000000LL, "weapon.shots");
			m->location = checked_integer(v.at("location"), 0, n - 1, "team.location");
			m->destination = checked_integer(v.at("destination"), -1, n - 1, "team.destination");
			m->edge_to = checked_integer(v.at("edge_to"), -1, n - 1, "team.edge_to");
			m->travel = checked_integer(v.at("travel"), 0, 1000000, "team.travel");
			m->edge_total = checked_integer(v.at("edge_total"), 0, 1000000, "team.edge_total");
			m->ready = v.at("ready");
			if ((m->travel > 0) != (m->edge_to >= 0) || m->travel > m->edge_total)
				throw std::runtime_error("Invalid team traversal");
		}
	if (s.destroyed_tick >= 0)
		ship.remove<Movable>();
}
Json save_projectiles() const
{
	Json out = Json::array();
	for (const auto &p : projectiles)
		out.push_back({{"id", p.id},
		               {"source", p.source},
		               {"position", p.position},
		               {"velocity", p.velocity},
		               {"remainder", p.remainder},
		               {"heading", p.heading},
		               {"life", p.life},
		               {"penetration", p.penetration},
		               {"damage", p.damage},
		               {"deposit_remainder", p.deposit_remainder},
		               {"resistance_remainder", p.resistance_remainder},
		               {"decks", p.decks},
		               {"definition",
		                {{"visual", p.definition.visual},
		                 {"speed", p.definition.speed},
		                 {"lifetime", p.definition.lifetime},
		                 {"penetration", p.definition.penetration},
		                 {"damage", p.definition.damage},
		                 {"deposition", p.definition.deposition},
		                 {"spread", p.definition.spread}}}});
	return out;
}
void restore_projectiles(const Json &saved)
{
	next_projectile = saved.value("next_projectile", std::uint64_t{1});
	const auto list = saved.value("projectiles", Json::array());
	if (!list.is_array() || list.size() > 8192)
		throw std::runtime_error("Invalid projectile list");
	std::set<std::uint64_t> seen;
	for (const auto &v : list)
	{
		Projectile p;
		p.id = v.at("id");
		p.source = v.at("source");
		p.position = v.at("position").get<fixed::Vec>();
		p.velocity = v.at("velocity").get<fixed::Vec>();
		p.remainder = v.at("remainder").get<fixed::Vec>();
		p.heading = v.at("heading");
		p.life = v.at("life");
		p.penetration = v.at("penetration");
		p.damage = v.at("damage");
		p.deposit_remainder = v.at("deposit_remainder");
		p.resistance_remainder = v.at("resistance_remainder");
		p.decks = v.at("decks").get<std::map<EntityId, std::string>>();
		const auto &d = v.at("definition");
		p.definition = {d.at("visual"), d.at("speed"),      d.at("lifetime"), d.at("penetration"),
		                d.at("damage"), d.at("deposition"), d.at("spread")};
		if (!seen.insert(p.id).second || p.id >= next_projectile || !entities.contains(p.source) ||
		    !definitions.contains(p.definition.visual) || p.life < 1 || p.life > p.definition.lifetime ||
		    p.damage < 1 || p.damage > p.definition.damage || p.penetration < 1 ||
		    p.penetration > p.definition.penetration || std::abs(p.position.x) > fixed::unit ||
		    std::abs(p.position.y) > fixed::unit || std::abs(p.velocity.x) > fixed::unit ||
		    std::abs(p.velocity.y) > fixed::unit || std::abs(p.remainder.x) >= rate ||
		    std::abs(p.remainder.y) >= rate || p.deposit_remainder < 0 || p.deposit_remainder >= 1000 ||
		    p.resistance_remainder < 0 || p.resistance_remainder >= 1000)
			throw std::runtime_error("Invalid projectile continuation");
		for (const auto &[id, deck] : p.decks)
			if (!entities.contains(id) || !entities.at(id).has<CombatStructure>() ||
			    std::none_of(entities.at(id).get<CombatStructure>().definition.compartments.begin(),
			                 entities.at(id).get<CombatStructure>().definition.compartments.end(),
			                 [&](const auto &c)
			                 {
				                 return c.deck == deck;
			                 }))
				throw std::runtime_error("Invalid projectile deck selection");
		projectiles.push_back(p);
	}
}

void run_combat_controller(flecs::entity entity, std::size_t index)
{
	const auto id = entity.get<StableId>().id;
	auto &controller = entity.get_mut<EntityControls>().controllers[index];
	const ConsoleAddress address{id, controller.definition.console};
	const auto frozen = observation_snapshots.find(address);
	if (frozen == observation_snapshots.end())
		return;
	const auto &observed = frozen->second;
	const BodyView *target = nullptr;
	std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
	for (const auto &contact : observed.contacts)
		if (contact.radar_icon == RadarIcon::ship)
		{
			const auto distance = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
			if (distance < nearest)
			{
				nearest = distance;
				target = &contact;
			}
		}
	const auto combat = observed.gameplay.value("combat", Json::object());
	for (std::size_t field = 0; field < controller.commands.size(); ++field)
	{
		const auto &spec = controller.definition.commands[field];
		const auto &state = controller.commands[field];
		auto value = state.value;
		auto args = state.arguments;
		switch (spec.action)
		{
		case OperatingAction::WeaponTarget:
			value = target ? static_cast<std::int64_t>(target->id) : 0;
			break;
		case OperatingAction::Heading:
			if (target)
				value = combat_bearing({target->x - observed.own.x, target->y - observed.own.y});
			break;
		case OperatingAction::Speed:
			if (target)
				value = nearest > 80000 ? spec.maximum / 2 : nearest < 35000 ? 0 : spec.maximum / 5;
			else
				value = 0;
			break;
		case OperatingAction::RepairCompartment:
		{
			std::int64_t lowest = 1001;
			for (const auto &c : combat.value("compartments", Json::array()))
			{
				const auto ratio = c.at("hp").get<std::int64_t>() * 1000 / c.at("maximum").get<std::int64_t>();
				if (ratio < 1000 && ratio < lowest)
				{
					lowest = ratio;
					value = c.at("index");
				}
			}
			break;
		}
		default:
			continue;
		}
		if (value < spec.minimum || value > spec.maximum)
			continue;
		OperatingCommand command{id,
		                         std::string(operating_action(spec.action).name),
		                         value,
		                         {},
		                         controller.definition.id,
		                         spec.id,
		                         controller.generation};
		command.arguments = args;
		if (auto proposal = controller_proposal(address, command, false))
			commit_controller(*proposal);
	}
}
