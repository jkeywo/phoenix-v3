module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.weapons;
export import phoenix.gameplay.directed;
export import phoenix.resources.stocks;
export namespace phoenix
{
struct WeaponMagazine
{
	const MagazineDefinition *definition = nullptr;
	StockStore *stock = nullptr;
	bool operating = true;
};
using WeaponMagazines = std::map<std::string, WeaponMagazine>;
const AmmunitionDefinition &native_magazine_ammunition(const MagazineDefinition &, const std::string &resource);
const AmmunitionDefinition &native_weapon_ammunition(const CombatMechanism &, const WeaponMagazines &,
                                                     std::int64_t index = -1);
std::int64_t native_weapon_load_duration(const CombatMechanism &, const DirectedRuntime *, const DirectedDefinition *,
                                         const WeaponMagazines &, std::int64_t index = -1);
const ProjectileDefinition &native_weapon_payload(const CombatMechanism &, const WeaponMagazines &);
void reset_weapon_tick(WeaponState &, DirectedRuntime *);
// Loading owns finite stock transfers and return reservations, not ECS lookup or targeting.
void advance_weapon_ammunition(CombatMechanism &, DirectedRuntime &, const DirectedDefinition &, bool launch_mode,
                               const WeaponMagazines &);
bool commit_weapon_launch(WeaponState &, DirectedRuntime &, PreparedDirectedOperation &, StockStore &,
                          const std::string &weapon, std::size_t live_projectiles);
struct ProjectileLaunch
{
	EntityId source = 0, target = 0;
	std::string weapon;
	std::int64_t altitude = 0, range = 0;
	fixed::Vec velocity;
};
Projectile create_launched_projectile(PreparedDirectedOperation &, ProjectileLaunch, std::uint64_t &random,
                                      std::uint64_t &next_projectile);
} // namespace phoenix
