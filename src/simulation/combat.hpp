#pragma once
namespace phoenix
{
struct CompartmentState
{
	std::int64_t hp = 0, work = 0;
	bool disabled = false;
};
struct Destructible
{
	DestructionDefinition definition;
	std::int64_t durability = 0, destroyed_tick = -1;
	bool processed = false;
};
struct BlastVisual
{
	std::uint64_t id = 0;
	fixed::Vec position;
	std::int64_t altitude = 0, tick = 0;
	DestructionDefinition definition;
	int kind = 0;
	std::int64_t bearing = 0, arc = 360000;
};
struct FaultState
{
	bool active = false, diagnosed = false;
	std::int64_t work = 0;
};
struct CombatStructure
{
	CombatDefinition definition;
	std::vector<CompartmentState> compartments;
	std::uint64_t random = 1, access_revision = 1;
	std::set<std::string> closed_routes;
	std::map<std::string, std::vector<FaultState>> faults;
};
struct WeaponState
{
	EntityId target = 0;
	std::int64_t cooldown = 0, shots = 0;
	bool ready = false;
	std::int64_t selected_magazine = 0, loaded_magazine = -1, loading = 0, unloading = 0, mode = 1;
	std::int64_t beam_remainder = 0, beam_tick = -1, bypass_remainder = 0;
	fixed::Vec beam_end;
	std::uint64_t intercept = 0;
};
struct TurretState
{
	EntityId target = 0;
	std::int64_t heading = 0, turn_remainder = 0;
};
struct MagazineState
{
	std::int64_t ammo = 0, refill = 0, reserved_return = 0;
};
struct TeamState
{
	// Route endpoints retain a physical position when assignment changes mid-edge.
	std::int64_t location = 0, destination = -1, edge_to = -1, travel = 0, edge_total = 0, repair_system = -1;
	bool repair_auto = true, detained = false;
	bool compartment_assignment = false;
	std::int64_t medical_compartment = -1;
	std::uint64_t revision = 1;
	std::string order, activity, patient, condition = "healthy";
	std::int64_t treatment = 0;
	bool work_done = false;
	state_tree::Instance continuation;
};
struct ShieldState
{
	std::int64_t strength = 0, warmup = 0, recharge_remainder = 0, discharge_remainder = 0, discharge_credit = 0;
	bool discharge = false;
};
// Reserves belong to the protected ship. Equipment is only an optional payer/control binding.
struct ShieldContribution
{
	std::uint64_t effect_id = 0;
	std::string source, equipment, definition_id;
	ShieldContributionDefinition definition;
	ShieldState state;
	std::int64_t expiry = -1;
	bool operating = true;
	std::int64_t quoted_rate = 0, quoted_sustain = 0, quoted_discharge = 0, protection = 1000;
};
struct ShieldComponent
{
	std::vector<ShieldContribution> contributions;
	std::uint64_t next_effect = 1;
};
struct ShieldReaction
{
	std::uint64_t id = 0, due = 0, hit = 0, effect = 0;
	EntityId attacker = 0, protected_ship = 0;
	std::string contribution, source, definition;
	std::size_t group = 0;
	std::int64_t presented = 0, modified = 0, negated = 0, absorbed = 0, residual = 0, strength = 0;
	ShieldReactionDefinition execution;
};
struct ShieldBindingState
{
};
struct CombatMechanism
{
	CombatSystemDefinition definition;
	std::variant<WeaponState, TurretState, MagazineState, TeamState, ShieldBindingState> state;
	template <class T> T &get()
	{
		return std::get<T>(state);
	}
	template <class T> const T &get() const
	{
		return std::get<T>(state);
	}
	EntityId &target()
	{
		return definition.kind == OperatingSystem::Turret ? get<TurretState>().target : get<WeaponState>().target;
	}
	EntityId target() const
	{
		return definition.kind == OperatingSystem::Turret ? get<TurretState>().target : get<WeaponState>().target;
	}
};
struct Projectile
{
	std::uint64_t id = 0;
	EntityId source = 0;
	std::string weapon;
	fixed::Vec position, velocity, remainder;
	std::int64_t heading = 0, life = 0, penetration = 0, damage = 0, deposit_remainder = 0, resistance_remainder = 0;
	ProjectileDefinition definition;
	EntityId target = 0;
	std::int64_t altitude = 0, vz = 0, z_remainder = 0, age = 0, initial_distance = 0, base_heading = 0;
	std::int64_t turn_remainder = 0;
	std::int64_t launch_altitude = 0;
	bool bearing_launch = false, seeker_acquired = false;
	std::int64_t arc_start_age = 0;
	std::int64_t seeker_altitude = 0;
	std::uint64_t random = 1;
	bool continuous = false;
	std::int64_t bypass_remainder = 0;
	std::set<EntityId> shield_hits;
	std::set<std::uint64_t> missile_hits;
	std::map<EntityId, std::string> decks;
};
// Material cost is integrated in millimetres, preserving fractional work across crossings/ticks.
inline std::int64_t deposit_material(Projectile &p, std::int64_t distance, std::int64_t resistance)
{
	resistance = std::max<std::int64_t>(1, resistance);
	const auto cost = (distance * resistance + p.resistance_remainder) / 1000;
	const bool stops = cost >= p.penetration;
	if (stops)
		distance = std::min(distance, (p.penetration * 1000 - p.resistance_remainder + resistance - 1) / resistance);
	const auto deposit =
	    stops ? p.damage : std::min(p.damage, (distance * p.definition.deposition + p.deposit_remainder) / 1000);
	p.deposit_remainder = (distance * p.definition.deposition + p.deposit_remainder) % 1000;
	p.resistance_remainder = (distance * resistance + p.resistance_remainder) % 1000;
	p.penetration = stops ? 0 : p.penetration - cost;
	p.damage -= deposit;
	return deposit;
}
inline std::int64_t combat_angle(std::int64_t v)
{
	return (v % 360000 + 360000) % 360000;
}
inline std::int64_t combat_delta(std::int64_t a, std::int64_t b)
{
	return (combat_angle(a) - combat_angle(b) + 540000) % 360000 - 180000;
}
inline std::int64_t combat_bearing(fixed::Vec v)
{
	if (!v.x && !v.y)
		return 0;
	const auto x = std::abs(v.x), y = std::abs(v.y);
	std::int64_t lo = 0, hi = 90000;
	while (hi - lo > 1)
	{
		const auto m = (lo + hi) / 2;
		const auto d = fixed::direction(m);
		if (d.y * x < d.x * y)
			lo = m;
		else
			hi = m;
	}
	const auto a = (lo + hi) / 2;
	return v.x >= 0 ? (v.y >= 0 ? a : combat_angle(-a)) : (v.y >= 0 ? 180000 - a : 180000 + a);
}
inline fixed::Vec combat_local(fixed::Vec world, const Pose &pose)
{
	const auto f = fixed::direction(pose.heading);
	const auto p = world - pose.position;
	return {fixed::dot(p, f) / fixed::unit, (-p.x * f.y + p.y * f.x) / fixed::unit};
}
inline fixed::Vec combat_world(fixed::Vec local, const Pose &pose)
{
	const auto f = fixed::direction(pose.heading);
	return pose.position +
	       fixed::Vec{(local.x * f.x - local.y * f.y) / fixed::unit, (local.x * f.y + local.y * f.x) / fixed::unit};
}
inline std::optional<std::pair<std::int64_t, std::int64_t>> compartment_segment(fixed::Vec a, fixed::Vec b,
                                                                                const CompartmentDefinition &c)
{
	constexpr std::int64_t precision = 1000000;
	std::int64_t enter = 0, exit = precision;
	const auto slab = [&](std::int64_t start, std::int64_t end, std::int64_t centre, std::int64_t half)
	{
		const auto delta = end - start;
		if (!delta)
			return start >= centre - half && start < centre + half;
		auto low = (centre - half - start) * precision / delta, high = (centre + half - start) * precision / delta;
		if (low > high)
			std::swap(low, high);
		enter = std::max(enter, low);
		exit = std::min(exit, high);
		return enter < exit;
	};
	if (!slab(a.x, b.x, c.x, c.half_width) || !slab(a.y, b.y, c.y, c.half_length))
		return {};
	return std::pair{enter, exit};
}
} // namespace phoenix
