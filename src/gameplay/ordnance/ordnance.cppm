module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.ordnance;
export import phoenix.gameplay.directed;
export import phoenix.world.spatial;
export namespace phoenix
{
struct OrdnanceTarget
{
	fixed::Vec position;
	std::int64_t altitude = 0;
	bool alive = true;
};
struct BlastExposure
{
	fixed::Vec point;
	std::int64_t altitude = 0, distance = 0;
};
std::optional<std::int64_t> ordnance_volume_hit(fixed::Vec a, fixed::Vec b, std::int64_t za, std::int64_t zb,
                                                CollisionBody target, fixed::Vec target_start, std::int64_t tz_start,
                                                std::int64_t tz_end, std::int64_t half_height, std::int64_t radius = 1);
BlastExposure blast_surface_exposure(fixed::Vec origin, std::int64_t altitude, const Collider &, const Pose &,
                                     std::int64_t half_height);
void advance_projectile(Projectile &, const std::optional<OrdnanceTarget> &);
fixed::Vec directed_weapon_lead(const WeaponDefinition &, const ProjectileDefinition *, const DirectedAim &,
                                fixed::Vec origin, fixed::Vec firing_velocity);
bool directed_weapon_covers(std::int64_t range, std::int64_t arc, const DirectedAim &, fixed::Vec origin,
                            std::int64_t orientation, fixed::Vec lead, std::int64_t firing_altitude);
void advance_directed_turret(TurretState &, const CombatSystemDefinition &, fixed::Vec origin,
                             std::int64_t ship_heading, fixed::Vec lead, std::int64_t firing_bearing,
                             fixed::Vec firing_mount, std::int64_t traverse_rate);
struct MaterialContact
{
	std::size_t compartment = 0;
	std::int64_t deposit = 0;
};
// Advances one material crossing at a time. The native adapter applies the complete
// damage/reaction boundary between next() and resolve(), then supplies actual absorption.
class PenetrationTraversal
{
	struct Crossing
	{
		std::int64_t from = 0, to = 0;
		std::size_t index = 0;
	};
	struct Pending
	{
		std::int64_t distance = 0, resistance = 0, deposit = 0, damage = 0, penetration = 0;
		std::int64_t deposit_remainder = 0, resistance_remainder = 0;
	};
	std::vector<Crossing> crossings_;
	std::size_t cursor_ = 0;
	std::int64_t length_ = 0;
	std::optional<Pending> pending_;

public:
	PenetrationTraversal(Projectile &, CombatStructure &, EntityId victim, const Pose &, fixed::Vec start,
	                     fixed::Vec end);
	std::optional<MaterialContact> next(Projectile &, const CombatStructure &);
	void resolve(Projectile &, std::int64_t absorbed, bool compartment_destroyed);
};
// Called after the once-only native Destroy reaction has been queued by the adapter.
std::optional<DestructionDefinition> retire_projectile_native(Projectile &, Destructible &, bool warhead);
bool retire_destroyed_structure(Destructible &, EntityId);
bool projectile_finished(const Projectile &);
bool blast_visual_finished(const BlastVisual &, std::int64_t tick);
} // namespace phoenix
