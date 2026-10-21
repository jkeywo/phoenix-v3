#include "combat_effects.hpp"
// World::Impl combat adapter. All decisions originate at Controllers; mechanisms consume admitted intent.
std::vector<flecs::entity> projectile_rows() const
{
	std::vector<flecs::entity> rows;
	projectile_query.each(
	    [&](flecs::entity e, const Projectile &)
	    {
		    rows.push_back(e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](auto a, auto b)
	          {
		          return a.template get<Projectile>().id < b.template get<Projectile>().id;
	          });
	return rows;
}
std::vector<std::pair<EntityId, flecs::entity>> combat_rows() const
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	combat_ships.each(
	    [&](flecs::entity e, const StableId &id, const CombatStructure &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}
std::uint64_t next_projectile = 1;
bool combat_duel = false;
bool duel_ended() const
{
	if (!combat_duel)
		return false;
	for (const auto &[id, e] : combat_rows())
		if (combat_dead(e))
			return true;
	return false;
}
bool combat_dead(flecs::entity ship) const
{
	const auto *d = ship.try_get<Destructible>();
	return d && d->destroyed_tick >= 0;
}
bool structurally_disabled(flecs::entity ship, const std::string &equipment) const
{
	const auto *s = ship.try_get<CombatStructure>();
	if (!s)
		return false;
	if (auto fit = instance(ship, equipment); fit && fit->kind == OperatingSystem::RepairTeam)
		return combat_dead(ship);
	if (combat_dead(ship))
		return true;
	if (!fault_effectiveness(ship, equipment))
		return true;
	const auto at = s->definition.equipment_compartments.find(equipment);
	return at != s->definition.equipment_compartments.end() &&
	       s->compartments.at(s->definition.compartment_ids.at(at->second)).disabled;
}
void combat_damage(flecs::entity ship, std::size_t compartment, std::int64_t amount)
{
	auto &s = ship.get_mut<CombatStructure>();
	if (combat_dead(ship) || amount <= 0)
		return;
	damage_faults(ship, compartment, amount);
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
		ship.get_mut<Destructible>().destroyed_tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
		ship.get_mut<Motion>().velocity = {};
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
	if (output.action == OperatingAction::MagazineSelect && m.definition.kind == OperatingSystem::Weapon)
	{
		if (output.value < 0 || output.value >= static_cast<std::int64_t>(m.definition.magazines.size()))
			return false;
		if (execute && m.selected_magazine != output.value)
		{
			if (m.loaded_magazine >= 0 && !m.unloading)
				m.unloading = load_duration(ship, m, m.loaded_magazine);
			m.selected_magazine = output.value;
			m.loading = 0;
		}
		return true;
	}
	if (output.action == OperatingAction::WeaponMode && m.definition.point_defence)
	{
		if (output.value < 0 || output.value > 2)
			return false;
		if (execute)
		{
			m.mode = output.value;
			if (m.mode != 2)
				m.target = 0;
		}
		return true;
	}
	if (output.action == OperatingAction::ShieldDischarge && m.definition.kind == OperatingSystem::Shield)
	{
		if (output.value < 0 || output.value > 1)
			return false;
		if (execute)
			m.discharge = output.value != 0;
		return true;
	}
	if (output.action == OperatingAction::RepairSystem && m.definition.kind == OperatingSystem::RepairTeam)
	{
		const auto &s = ship.get<CombatStructure>();
		if (output.value < 0 || output.value > static_cast<std::int64_t>(s.definition.equipment_compartments.size()))
			return false;
		if (execute)
		{
			m.repair_system = output.value - 1;
			m.repair_auto = output.value == 0;
			if (!output.value)
				m.destination = -1;
			if (output.value)
			{
				auto it = s.definition.equipment_compartments.begin();
				std::advance(it, m.repair_system);
				m.destination = s.definition.compartment_ids.at(it->second);
			}
		}
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
			if (m.definition.point_defence)
				m.mode = target ? 2 : 1;
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
		{
			m.destination = output.value - 1;
			m.repair_auto = output.value == 0;
			m.repair_system = -1;
		}
		return true;
	}
	return false;
}
void combat_telemetry(flecs::entity ship, flecs::entity child, std::map<std::string, std::int64_t> &v) const
{
	const auto &fit = child.get<Equipment>();
	const bool stopped = structurally_disabled(ship, fit.id);
	const auto *structure = ship.try_get<CombatStructure>();
	const auto compartment = structure ? structure->definition.equipment_compartments.find(fit.id)
	                                   : std::map<std::string, std::string>::const_iterator{};
	v["structural_disabled"] =
	    structure && compartment != structure->definition.equipment_compartments.end() &&
	    structure->compartments.at(structure->definition.compartment_ids.at(compartment->second)).disabled;
	v["damaged"] = fault_effectiveness(ship, fit.id) < 1000;
	v["destroyed"] = 0;
	v["overheated"] = 0;
	if (structure && structure->faults.contains(fit.id))
		for (std::size_t i = 0; i < structure->faults.at(fit.id).size(); ++i)
			if (structure->faults.at(fit.id)[i].active && structure->definition.faults.at(fit.id)[i].destroys)
				v["destroyed"] = 1;
	if (const auto *power = ship.try_get<ShipResources>())
	{
		const auto slot = child.get<ResourceBinding>().slot;
		const auto store = power->definition.systems.at(slot).store;
		if (store != no_resource)
			v["overheated"] = power->network.stores()[store].thermal_stop;
	}

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
	for (const auto &[id, ship] : combat_rows())
		if (ship.has<CombatStructure>())
		{
			if (combat_dead(ship))
			{
				ship.remove<Movable>();
				ship.get_mut<Motion>().velocity = {};
			}
		}
}
#include "combat_weapons.hpp"
void prepare_combat(flecs::entity ship)
{
	if (!ship.has<CombatStructure>())
		return;
	if (!duel_ended())
		prepare_weapons(ship);
	else
		for (auto child : ship.get<EquipmentIndex>().values)
			if (auto *m = child.try_get_mut<CombatMechanism>())
				m->ready = false;
}
void request_combat_resources(flecs::entity ship)
{
	request_shields(ship);
	auto *power = ship.try_get_mut<ShipResources>();
	if (!power)
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			const bool enabled = !duel_ended() && child.get<Equipment>().enabled && !structurally_disabled(ship, d.id);
			const auto index = child.get<ResourceBinding>().slot;
			if (d.kind == OperatingSystem::Magazine)
				request_system_resource(
				    *power, index, 0, false,
				    enabled && d.reload == "energy" && m->ammo + m->reserved_return < d.capacity ? d.reload_energy : 0);
			if (d.kind == OperatingSystem::Weapon)
				request_system_resource(*power, index, enabled && m->ready ? 1000 : 0,
				                        enabled && m->ready && d.family != "beam",
				                        enabled && d.point_defence && m->mode == 1 ? d.standby_energy : 0);
		}
}
void actuate_combat(flecs::entity ship)
{
	if (!ship.has<CombatStructure>() || duel_ended())
		return;
	const auto id = ship.get<StableId>().id;
	const auto &pose = ship.get<Pose>();
	actuate_shields(ship);
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
			if (d.kind == OperatingSystem::Magazine && d.reload != "none" && m->ammo + m->reserved_return < d.capacity)
			{
				bool paid = d.reload == "infinite";
				if (d.reload == "energy")
					if (const auto *p = ship.try_get<ShipResources>())
						paid = p->grants.at(child.get<ResourceBinding>().slot).supply_permille == 1000 &&
						       !p->grants.at(child.get<ResourceBinding>().slot).blocked;
				if (paid && ++m->refill >= d.refill_ticks)
				{
					m->ammo = std::min(d.capacity - m->reserved_return, m->ammo + d.refill_amount);
					m->refill = 0;
				}
			}
		}

	fire_weapons(ship);
}

void penetrate(Projectile &p, flecs::entity ship, fixed::Vec start, fixed::Vec end)
{
	auto &s = ship.get_mut<CombatStructure>();
	if (combat_dead(ship))
		return;
	const auto id = ship.get<StableId>().id;
	if (p.shield_hits.insert(id).second)
		p.damage = absorb_shields(ship, p.heading, p.damage, p.definition.shield_bypass,
		                          p.continuous ? &p.bypass_remainder : nullptr);
	if (!p.damage)
		return;
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
		const auto distance = length * (line.to - line.from) / 1000000;
		const auto deposit = deposit_material(p, distance, d.resistance * c.hp / d.maximum);
		combat_damage(ship, line.index, deposit);
	}
}
#include "combat_ordnance.hpp"
void step_projectiles()
{
	step_ordnance();
}
std::pair<std::int64_t, std::int64_t> repair_route(const CombatStructure &s, std::size_t from, std::size_t to,
                                                   std::int64_t *total_cost = nullptr) const
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
	if (total_cost)
		*total_cost = distance[to];
	if (first[to] < 0)
		return {-1, 0};
	for (const auto &edge : s.definition.connections)
		if ((edge.from == from && edge.to == static_cast<std::size_t>(first[to])) ||
		    (edge.to == from && edge.from == static_cast<std::size_t>(first[to])))
			return {first[to], edge.ticks};
	return {-1, 0};
}
using RepairFault = std::pair<std::string, std::size_t>;
struct RepairCandidate
{
	std::int64_t score = 0;
	std::optional<RepairFault> fault;
};
RepairCandidate repair_candidate(flecs::entity ship, std::size_t compartment, const std::string &equipment = {},
                                 const std::set<RepairFault> &claimed = {}) const
{
	const auto &s = ship.get<CombatStructure>();
	const auto &c = s.compartments[compartment];
	const auto &d = s.definition.compartments[compartment];
	RepairCandidate best;
	if ((equipment.empty() || !equipment_destroyed(ship, equipment)) && c.hp < d.maximum)
		best.score = c.disabled ? 10000 : (d.maximum - c.hp) * 1000 / d.maximum;
	for (const auto &[id, defs] : s.definition.faults)
	{
		if (equipment_destroyed(ship, id) || (!equipment.empty() && equipment != id) ||
		    s.definition.compartment_ids.at(s.definition.equipment_compartments.at(id)) != compartment)
			continue;
		for (std::size_t i = 0; i < defs.size(); ++i)
		{
			const RepairFault key{id, i};
			if (!s.faults.at(id)[i].active || defs[i].destroys || claimed.contains(key))
				continue;
			const auto score =
			    (defs[i].effect == "disabled" || defs[i].effect == "feed" || !defs[i].effectiveness ? 10000 : 1000) +
			    defs[i].severity;
			if (score > best.score)
				best = {score, key};
		}
	}
	return best;
}
void step_repairs()
{
	if (duel_ended())
		return;
	for (const auto &[id, ship] : combat_rows())
	{
		auto &s = ship.get_mut<CombatStructure>();
		if (combat_dead(ship))
			continue;
		std::map<std::size_t, std::vector<flecs::entity>> structural_workers;
		std::set<std::pair<std::string, std::size_t>> claimed;
		for (auto child : ship.get<EquipmentIndex>().values)
			if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::RepairTeam)
			{
				if (!child.get<Equipment>().enabled || !fault_effectiveness(ship, m->definition.id))
					continue;
				if (m->travel > 0)
				{
					if (--m->travel > 0)
						continue;
					m->location = m->edge_to;
					m->edge_to = -1;
					m->edge_total = 0;
				}
				// Inspection is performed whenever a team reaches a physical compartment.
				for (auto &[equipment, states] : s.faults)
					if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) ==
					    static_cast<std::size_t>(m->location))
						for (auto &state : states)
							if (state.active)
								state.diagnosed = true;
				if (m->repair_auto)
				{
					std::tuple<std::int64_t, std::int64_t, std::int64_t> best{
					    0, std::numeric_limits<std::int64_t>::min(), 0};
					m->destination = -1;
					for (std::size_t i = 0; i < s.compartments.size(); ++i)
					{
						std::int64_t travel_cost = 0;
						const auto route = repair_route(s, m->location, i, &travel_cost);
						if (i != static_cast<std::size_t>(m->location) && route.first < 0)
							continue;
						const auto score = repair_candidate(ship, i, {}, claimed).score;
						if (!score)
							continue;
						const auto key = std::tuple{score, -travel_cost, -static_cast<std::int64_t>(i)};
						if (key > best)
						{
							best = key;
							m->destination = i;
						}
					}
				}
				if (m->destination < 0)
					continue;
				if (m->destination != m->location)
				{
					const auto route = repair_route(s, m->location, m->destination);
					m->edge_to = route.first;
					m->travel = route.second;
					m->edge_total = route.second;
					continue;
				}
				std::string priority;
				if (m->repair_system >= 0)
				{
					auto at = s.definition.equipment_compartments.begin();
					std::advance(at, m->repair_system);
					priority = at->first;
				}
				const auto index = static_cast<std::size_t>(m->location);
				const auto candidate = repair_candidate(ship, index, priority, claimed);
				const auto &task = candidate.fault;
				if (task)
				{
					claimed.insert(*task);
					auto &f = s.faults.at(task->first)[task->second];
					const auto &d = s.definition.faults.at(task->first)[task->second];
					f.work += m->definition.repair_rate * 1000 * fault_effectiveness(ship, m->definition.id) / 1000;
					if (f.work >= d.repair_work * rate * 1000)
					{
						f.active = false;
						f.work = 0;
					}
				}
				else if (candidate.score > 0)
					structural_workers[index].push_back(child);
			}
		for (auto &[index, teams] : structural_workers)
		{
			std::sort(teams.begin(), teams.end(),
			          [](auto a, auto b)
			          {
				          const auto &x = a.template get<CombatMechanism>().definition;
				          const auto &y = b.template get<CombatMechanism>().definition;
				          return x.repair_rate != y.repair_rate ? x.repair_rate > y.repair_rate : x.id < y.id;
			          });
			auto &c = s.compartments[index];
			for (std::size_t rank = 0; rank < teams.size(); ++rank)
			{
				const auto &d = teams[rank].get<CombatMechanism>().definition;
				c.work += d.repair_rate * 1000000 / (1000 + d.stack_penalty * static_cast<std::int64_t>(rank)) *
				          fault_effectiveness(ship, d.id) / 1000;
			}
			c.hp = std::min(s.definition.compartments[index].maximum, c.hp + c.work / (rate * 1000));
			c.work %= rate * 1000;
			if (c.disabled && c.hp >= s.definition.compartments[index].recovery)
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
			const auto metres = std::min<std::int64_t>(closing / 1000, 1000);
			const auto damage =
			    std::min<std::int64_t>(1000000, std::clamp<std::int64_t>(mass / 1000000, 1, 1000000) * metres * metres /
			                                        100 * s.definition.collision_scale);
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
	Json output = {{"destroyed", combat_dead(ship)},
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
		for (const auto &[id, e] : combat_rows())
			if (e.has<CombatStructure>() && !combat_dead(e))
				++alive;
		output["result"] = alive == 0 ? "Draw" : combat_dead(ship) ? "Defeat" : "Victory";
	}
	output["faults"] = Json::array();
	output["shields"] = Json::array();
	output["weapons"] = Json::array();
	for (const auto &[equipment, defs] : s->definition.faults)
	{
		if (address.console != s->definition.repair_console &&
		    std::find(feeds.begin(), feeds.end(), equipment) == feeds.end())
			continue;
		for (std::size_t i = 0; i < defs.size(); ++i)
		{
			const auto &state = s->faults.at(equipment)[i];
			const auto &d = defs[i];
			if (!state.active)
				continue;
			output["faults"].push_back({{"system", equipment},
			                            {"compartment", s->definition.equipment_compartments.at(equipment)},
			                            {"symptom", d.symptom},
			                            {"diagnosed", state.diagnosed},
			                            {"diagnosis", state.diagnosed ? d.diagnosis : std::string{}},
			                            {"destroyed", d.destroys},
			                            {"work", state.work / (rate * 1000)},
			                            {"required_work", d.repair_work}});
		}
	}
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			const auto at = std::find(feeds.begin(), feeds.end(), d.id);
			if (at == feeds.end())
				continue;
			if (d.kind == OperatingSystem::Shield)
				output["shields"].push_back({{"id", d.id},
				                             {"strength", m->strength},
				                             {"capacity", d.capacity},
				                             {"warmup", m->warmup},
				                             {"warmup_ticks", d.warmup_ticks},
				                             {"arc_mdeg", d.arc},
				                             {"bearing_mdeg", combat_angle(ship.get<Pose>().heading + d.bearing)},
				                             {"layer", d.layer},
				                             {"discharging", m->discharge}});
			if (d.kind == OperatingSystem::Weapon)
				output["weapons"].push_back(
				    {{"id", d.id},
				     {"magazines", d.magazines},
				     {"selected", m->selected_magazine},
				     {"loaded", m->loaded_magazine},
				     {"loading", m->loading},
				     {"unloading", m->unloading},
				     {"load_ticks", load_duration(ship, *m)},
				     {"unload_ticks", m->loaded_magazine < 0 ? 0 : load_duration(ship, *m, m->loaded_magazine)},
				     {"mode", m->mode},
				     {"point_defence", d.point_defence},
				     {"intercept", m->intercept}});
		}
	result.gameplay["combat"] = std::move(output);
}
Json save_combat(flecs::entity ship) const
{
	const auto &s = ship.get<CombatStructure>();
	Json j = {{"random", s.random}, {"faults", s.faults}, {"compartments", Json::array()}, {"systems", Json::object()}};
	for (const auto &c : s.compartments)
		j["compartments"].push_back({{"hp", c.hp}, {"work", c.work}, {"disabled", c.disabled}});
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			j["systems"][m->definition.id] = {{"target", m->target},
			                                  {"heading", m->heading},
			                                  {"turn_remainder", m->turn_remainder},
			                                  {"cooldown", m->cooldown},
			                                  {"ammo", m->ammo},
			                                  {"refill", m->refill},
			                                  {"shots", m->shots},
			                                  {"location", m->location},
			                                  {"destination", m->destination},
			                                  {"edge_to", m->edge_to},
			                                  {"travel", m->travel},
			                                  {"edge_total", m->edge_total},
			                                  {"ready", m->ready},
			                                  {"selected_magazine", m->selected_magazine},
			                                  {"loaded_magazine", m->loaded_magazine},
			                                  {"loading", m->loading},
			                                  {"unloading", m->unloading},
			                                  {"reserved_return", m->reserved_return},
			                                  {"mode", m->mode},
			                                  {"strength", m->strength},
			                                  {"warmup", m->warmup},
			                                  {"recharge_remainder", m->recharge_remainder},
			                                  {"discharge_remainder", m->discharge_remainder},
			                                  {"discharge_credit", m->discharge_credit},
			                                  {"beam_remainder", m->beam_remainder},
			                                  {"repair_system", m->repair_system},
			                                  {"beam_tick", m->beam_tick},
			                                  {"beam_end", m->beam_end},
			                                  {"discharge", m->discharge},
			                                  {"intercept", m->intercept},
			                                  {"bypass_remainder", m->bypass_remainder},
			                                  {"repair_auto", m->repair_auto}};
	return j;
}
void restore_combat(flecs::entity ship, const Json &j)
{
	auto &s = ship.get_mut<CombatStructure>();
	s.random = j.at("random");
	s.faults = j.value("faults", Json(s.faults)).get<std::map<std::string, std::vector<FaultState>>>();
	for (const auto &[id, defs] : s.definition.faults)
		if (!s.faults.contains(id) || s.faults.at(id).size() != defs.size())
			throw std::runtime_error("Fault roster mismatch");
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
	const auto destroyed = std::all_of(s.compartments.begin(), s.compartments.end(),
	                                   [](const auto &c)
	                                   {
		                                   return c.hp == 0;
	                                   });
	if (destroyed != combat_dead(ship))
		throw std::runtime_error("Inconsistent ship destruction continuation");
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
			m->cooldown = checked_integer(v.at("cooldown"), 0, 1000000000, "weapon.cooldown");
			m->ammo = checked_integer(v.at("ammo"), 0, m->definition.capacity, "magazine.ammo");
			m->refill = checked_integer(v.at("refill"), 0, m->definition.refill_ticks - 1, "magazine.refill");
			m->shots = checked_integer(v.at("shots"), 0, 1000000000000LL, "weapon.shots");
			m->location = checked_integer(v.at("location"), 0, n - 1, "team.location");
			m->destination = checked_integer(v.at("destination"), -1, n - 1, "team.destination");
			m->edge_to = checked_integer(v.at("edge_to"), -1, n - 1, "team.edge_to");
			m->travel = checked_integer(v.at("travel"), 0, 1000000, "team.travel");
			m->edge_total = checked_integer(v.at("edge_total"), 0, 1000000, "team.edge_total");
			m->ready = v.at("ready");
			m->selected_magazine = v.at("selected_magazine").get<decltype(m->selected_magazine)>();
			m->loaded_magazine = v.at("loaded_magazine").get<decltype(m->loaded_magazine)>();
			m->loading = v.at("loading").get<decltype(m->loading)>();
			m->unloading = v.at("unloading");
			m->reserved_return = v.at("reserved_return").get<decltype(m->reserved_return)>();
			m->mode = v.at("mode").get<decltype(m->mode)>();
			m->strength = v.at("strength").get<decltype(m->strength)>();
			m->warmup = v.at("warmup").get<decltype(m->warmup)>();
			m->recharge_remainder = v.at("recharge_remainder").get<decltype(m->recharge_remainder)>();
			m->discharge_remainder = v.at("discharge_remainder").get<decltype(m->discharge_remainder)>();
			m->discharge_credit = v.at("discharge_credit");
			if (m->discharge_credit < 0 ||
			    m->discharge_credit >= m->definition.recharge_energy * m->definition.discharge_efficiency ||
			    m->discharge_remainder < 0)
				throw std::runtime_error("Invalid shield conversion progress");
			m->beam_remainder = v.at("beam_remainder").get<decltype(m->beam_remainder)>();
			m->repair_system = v.at("repair_system").get<decltype(m->repair_system)>();
			m->beam_tick = v.at("beam_tick").get<decltype(m->beam_tick)>();
			m->beam_end = v.at("beam_end").get<decltype(m->beam_end)>();
			m->discharge = v.at("discharge").get<decltype(m->discharge)>();
			m->intercept = v.at("intercept").get<decltype(m->intercept)>();
			m->bypass_remainder = v.at("bypass_remainder");
			m->repair_auto = v.at("repair_auto");
			if (m->selected_magazine < 0 ||
			    (!m->definition.magazines.empty() &&
			     m->selected_magazine >= static_cast<std::int64_t>(m->definition.magazines.size())) ||
			    m->loaded_magazine < -1 ||
			    m->loaded_magazine >= static_cast<std::int64_t>(m->definition.magazines.size()) ||
			    m->reserved_return < 0 || m->ammo + m->reserved_return > m->definition.capacity || m->strength < 0 ||
			    m->strength > m->definition.capacity || m->warmup < 0 || m->warmup > m->definition.warmup_ticks ||
			    m->mode < 0 || m->mode > 2)
				throw std::runtime_error("Invalid extended combat state");

			if ((m->travel > 0) != (m->edge_to >= 0) || m->travel > m->edge_total)
				throw std::runtime_error("Invalid team traversal");
		}
	std::map<std::string, std::int64_t> reserved;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			if (m->loaded_magazine >= 0)
			{
				if ((!m->unloading && m->loaded_magazine != m->selected_magazine) ||
				    !load_duration(ship, *m, m->loaded_magazine))
					throw std::runtime_error("Invalid loaded ammunition commitment");
				reserved[m->definition.magazines.at(m->loaded_magazine)] += m->definition.ammo_use;
			}
			if (m->unloading < 0 ||
			    (m->unloading &&
			     (m->loaded_magazine < 0 || m->unloading > load_duration(ship, *m, m->loaded_magazine) || m->loading)))
				throw std::runtime_error("Invalid unload progress");
			if (m->loading < 0 ||
			    (load_duration(ship, *m) > 0 ? m->loading >= load_duration(ship, *m) : m->loading != 0) ||
			    m->bypass_remainder < 0 || m->bypass_remainder >= 1000)
				throw std::runtime_error("Invalid weapon progress");
		}
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>();
		    m && m->definition.kind == OperatingSystem::Magazine && m->reserved_return != reserved[m->definition.id])
			throw std::runtime_error("Unbalanced magazine return reservation");
	if (combat_dead(ship))
		ship.remove<Movable>();
}
Json save_projectiles() const
{
	Json out = Json::array();
	for (const auto particle : projectile_rows())
	{
		const auto &p = particle.get<Projectile>();
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
		               {"definition", p.definition},
		               {"target", p.target},
		               {"altitude", p.altitude},
		               {"vz", p.vz},
		               {"z_remainder", p.z_remainder},
		               {"durability", p.durability},
		               {"age", p.age},
		               {"initial_distance", p.initial_distance},
		               {"base_heading", p.base_heading},
		               {"turn_remainder", p.turn_remainder},
		               {"random", p.random},
		               {"shield_hits", p.shield_hits},
		               {"missile_hits", p.missile_hits}});
	}
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
		p.definition = v.at("definition").get<ProjectileDefinition>();
		p.target = v.at("target").get<decltype(p.target)>();
		p.altitude = v.at("altitude").get<decltype(p.altitude)>();
		p.vz = v.at("vz").get<decltype(p.vz)>();
		p.z_remainder = v.at("z_remainder").get<decltype(p.z_remainder)>();
		p.durability = v.at("durability").get<decltype(p.durability)>();
		p.age = v.at("age").get<decltype(p.age)>();
		p.initial_distance = v.at("initial_distance").get<decltype(p.initial_distance)>();
		p.base_heading = v.at("base_heading").get<decltype(p.base_heading)>();
		p.turn_remainder = v.at("turn_remainder").get<decltype(p.turn_remainder)>();
		p.random = v.at("random").get<decltype(p.random)>();
		p.shield_hits = v.at("shield_hits").get<decltype(p.shield_hits)>();
		p.missile_hits = v.at("missile_hits").get<decltype(p.missile_hits)>();
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
		ecs.entity().set<Projectile>(p).set<Destructible>({p.definition.destruction, p.durability});
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
			if (!spec.consumers.empty())
				if (const auto *m =
				        entity.get<EquipmentIndex>().values.at(spec.consumers.front()).try_get<CombatMechanism>();
				    m && m->definition.point_defence)
					value = 0;
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
		case OperatingAction::RepairSystem:
			value = 0;
			break;
		case OperatingAction::WeaponMode:
			value = 1;
			break;
		case OperatingAction::MagazineSelect:
		{
			if (spec.consumers.empty())
				continue;
			const auto *m = entity.get<EquipmentIndex>().values.at(spec.consumers.front()).try_get<CombatMechanism>();
			if (!m)
				continue;
			std::int64_t best_range = std::numeric_limits<std::int64_t>::max();
			for (std::size_t i = 0; i < m->definition.magazines.size(); ++i)
			{
				const auto &mag = instance(entity, m->definition.magazines[i])->entity.get<CombatMechanism>();
				if (mag.ammo < m->definition.ammo_use && m->loaded_magazine != static_cast<std::int64_t>(i))
					continue;
				const auto &p =
				    mag.definition.projectile.visual.empty() ? m->definition.projectile : mag.definition.projectile;
				const auto reach = p.speed * p.lifetime / rate;
				if (reach >= nearest && reach < best_range)
				{
					best_range = reach;
					value = i;
				}
			}
			break;
		}
		case OperatingAction::ShieldDischarge:
		{
			value = 0;
			const auto shields = combat.value("shields", Json::array());
			std::int64_t lowest = 1000, own = 0;
			for (const auto &shield : shields)
			{
				const auto fraction =
				    shield.at("strength").get<std::int64_t>() * 1000 / shield.at("capacity").get<std::int64_t>();
				lowest = std::min(lowest, fraction);
				if (shield.at("id") == controller.definition.id)
					own = fraction;
			}
			if (lowest < 500 && own > lowest + 250)
				value = 1;
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
