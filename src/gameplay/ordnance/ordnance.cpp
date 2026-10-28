module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.ordnance;
namespace phoenix
{
namespace
{
constexpr std::int64_t rate = resource_tick_hz;
}

std::optional<std::int64_t> ordnance_volume_hit(fixed::Vec a, fixed::Vec b, std::int64_t za, std::int64_t zb,
                                                CollisionBody target, fixed::Vec target_start, std::int64_t tz_start,
                                                std::int64_t tz_end, std::int64_t half_height, std::int64_t radius)
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
BlastExposure blast_surface_exposure(fixed::Vec origin, std::int64_t altitude, const Collider &shape, const Pose &pose,
                                     std::int64_t half_height)
{
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
	const auto extent = half_height;
	const auto z = std::clamp(altitude, pose.altitude - extent, pose.altitude + extent);
	const auto delta = point - origin;
	const auto dz = z - altitude;
	return {point, z,
	        static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(delta, delta) + dz * dz)))};
}
void advance_projectile(Projectile &p, const std::optional<OrdnanceTarget> &target)
{
	const auto old_z = p.altitude;
	if (p.definition.durability)
	{
		auto desired = p.heading;
		if (p.definition.guidance == "homing" && target && target->alive)
		{
			const auto target_position = target->position;
			if (fixed::length(target_position - p.position) + std::abs(p.altitude - target->altitude) <=
			    p.definition.seeker_range)
			{
				desired = combat_bearing(target_position - p.position);
				p.seeker_altitude = target->altitude;
				if (p.bearing_launch && !p.seeker_acquired)
				{
					p.seeker_acquired = true;
					p.arc_start_age = p.age;
					p.launch_altitude = p.altitude;
					p.initial_distance = std::max<std::int64_t>(1, fixed::length(target_position - p.position));
				}
			}
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
		const auto progress = std::clamp<std::int64_t>((p.age - p.arc_start_age) * p.definition.speed * 1000 /
		                                                   (std::max<std::int64_t>(1, p.initial_distance) * rate),
		                                               0, 1000);
		const auto launch_altitude = p.launch_altitude;
		const auto target_altitude = p.bearing_launch ? p.seeker_altitude : (target ? target->altitude : 0);
		p.altitude = launch_altitude + (target_altitude - launch_altitude) * progress / 1000 +
		             4 * p.definition.arc_height * progress * (1000 - progress) / 1000000;
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
fixed::Vec directed_weapon_lead(const WeaponDefinition &weapon, const ProjectileDefinition *payload,
                                const DirectedAim &target, fixed::Vec origin, fixed::Vec firing_velocity)
{
	if (target.bearing)
		return origin + fixed::scale(fixed::direction(*target.bearing), weapon.range);
	const auto &d = *payload;
	const auto travel = weapon.family == "beam"
	                        ? 0
	                        : std::min<std::int64_t>(d.lifetime * 1000 / rate,
	                                                 fixed::length(fixed::Vec{target.body.x, target.body.y} - origin) *
	                                                     1000 / std::max<std::int64_t>(1, d.speed));
	return {
	    std::clamp(target.body.x + (target.body.vx - firing_velocity.x) * travel / 1000, -fixed::unit, fixed::unit),
	    std::clamp(target.body.y + (target.body.vy - firing_velocity.y) * travel / 1000, -fixed::unit, fixed::unit)};
}
bool directed_weapon_covers(std::int64_t range, std::int64_t arc, const DirectedAim &target, fixed::Vec origin,
                            std::int64_t orientation, fixed::Vec lead, std::int64_t firing_altitude)
{
	return (target.bearing || fixed::length(fixed::Vec{target.body.x, target.body.y} - origin) +
	                                  std::abs(target.altitude - firing_altitude) <=
	                              range) &&
	       std::abs(combat_delta(combat_bearing(lead - origin), orientation)) <= arc / 2;
}

void advance_directed_turret(TurretState &state, const CombatSystemDefinition &d, fixed::Vec origin,
                             std::int64_t ship_heading, fixed::Vec lead, std::int64_t firing_bearing,
                             fixed::Vec firing_mount, std::int64_t traverse_rate)
{
	const auto axis = fixed::direction(firing_bearing);
	const auto lateral = (axis.x * firing_mount.y - axis.y * firing_mount.x) / fixed::unit;
	const auto forward = static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(
	    std::max<std::int64_t>(0, fixed::dot(lead - origin, lead - origin) - lateral * lateral))));
	const auto aim = combat_bearing(lead - origin) - firing_bearing - combat_bearing({forward, lateral});
	const auto offset = std::clamp(combat_delta(aim - ship_heading, d.bearing), -d.arc / 2, d.arc / 2);
	const auto desired = combat_angle(d.bearing + offset);
	state.turn_remainder += traverse_rate;
	const auto step = state.turn_remainder / rate;
	state.turn_remainder %= rate;
	const auto current = combat_delta(state.heading, d.bearing);
	state.heading = d.arc == 360000
	                    ? combat_angle(state.heading + std::clamp(combat_delta(desired, state.heading), -step, step))
	                    : combat_angle(d.bearing + current + std::clamp(offset - current, -step, step));
}
PenetrationTraversal::PenetrationTraversal(Projectile &p, CombatStructure &s, EntityId victim, const Pose &pose,
                                           fixed::Vec start, fixed::Vec end)
{
	if (!p.decks.contains(victim))
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
				p.decks[victim] = deck;
				break;
			}
			roll -= w;
		}
	}
	const auto a = combat_local(start, pose), b = combat_local(end, pose);
	length_ = fixed::length(b - a);
	for (std::size_t i = 0; i < s.definition.compartments.size(); ++i)
		if (const auto &c = s.definition.compartments[i]; c.deck == p.decks.at(victim))
			if (auto hit = compartment_segment(a, b, c))
				crossings_.push_back({hit->first, hit->second, i});
	std::sort(crossings_.begin(), crossings_.end(),
	          [](const auto &x, const auto &y)
	          {
		          return std::pair{x.from, x.index} < std::pair{y.from, y.index};
	          });
}
std::optional<MaterialContact> PenetrationTraversal::next(Projectile &p, const CombatStructure &s)
{
	if (pending_)
		throw std::logic_error("Resolve material contact before advancing penetration");
	while (cursor_ < crossings_.size())
	{
		if (!p.damage || !p.penetration)
			return {};
		const auto line = crossings_[cursor_++];
		const auto &d = s.definition.compartments[line.index];
		const auto &c = s.compartments[line.index];
		if (c.hp <= 0)
			continue;
		Pending pending;
		pending.distance = length_ * (line.to - line.from) / 1000000;
		pending.resistance = std::max<std::int64_t>(1, d.resistance * c.hp / d.maximum);
		pending.damage = p.damage;
		pending.penetration = p.penetration;
		pending.deposit_remainder = p.deposit_remainder;
		pending.resistance_remainder = p.resistance_remainder;
		pending.deposit = deposit_material(p, pending.distance, pending.resistance);
		pending_ = pending;
		return MaterialContact{line.index, pending.deposit};
	}
	return {};
}
void PenetrationTraversal::resolve(Projectile &p, std::int64_t absorbed, bool compartment_destroyed)
{
	if (!pending_)
		throw std::logic_error("No material contact to resolve");
	const auto pending = *pending_;
	pending_.reset();
	if (pending.deposit > absorbed && compartment_destroyed)
	{
		auto traversed = pending.distance;
		if (p.definition.deposition > 0)
			traversed = std::min(traversed, std::max<std::int64_t>(0, absorbed * 1000 - pending.deposit_remainder +
			                                                              p.definition.deposition - 1) /
			                                    p.definition.deposition);
		if (p.penetration == 0)
			traversed = std::min(traversed,
			                     (pending.penetration * 1000 - pending.resistance_remainder + pending.resistance - 1) /
			                         pending.resistance);
		p.damage = pending.damage;
		p.penetration = pending.penetration;
		p.deposit_remainder = pending.deposit_remainder;
		p.resistance_remainder = pending.resistance_remainder;
		deposit_material(p, traversed, pending.resistance);
		p.damage = pending.damage - absorbed;
	}
}
std::optional<DestructionDefinition> retire_projectile_native(Projectile &p, Destructible &component, bool warhead)
{
	component.processed = true;
	p.life = 0;
	component.durability = 0;
	p.random = p.random * 6364136223846793005ULL + 1;
	auto effect = component.definition;
	if (warhead)
	{
		effect.chance = 1000;
		effect.damage = p.definition.damage;
		effect.radius = p.definition.blast_radius;
		effect.shield_bypass = p.definition.shield_bypass;
		effect.affects_missiles = true;
		effect.payloads = p.payloads;
	}
	if ((p.random >> 16) % 1000 < static_cast<std::uint64_t>(effect.chance))
		return effect;
	return {};
}
bool retire_destroyed_structure(Destructible &d, EntityId id)
{
	d.processed = true;
	const auto roll = (id * 7919ULL + static_cast<std::uint64_t>(d.destroyed_tick) * 6364136223846793005ULL) >> 16;
	return roll % 1000 < static_cast<std::uint64_t>(d.definition.chance);
}
bool projectile_finished(const Projectile &p)
{
	return p.life <= 0 || ((p.damage <= 0 || p.penetration <= 0) && p.definition.damage > 0);
}
bool blast_visual_finished(const BlastVisual &v, std::int64_t tick)
{
	return tick - v.tick >= v.definition.growth_ticks + v.definition.fade_ticks;
}
} // namespace phoenix
