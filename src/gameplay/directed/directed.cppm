module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.directed;
export import phoenix.gameplay.combat.state;
export namespace phoenix
{
// Intent belongs to the directed capability, independently of native mounts.
struct DirectedRuntime
{
	std::size_t operating_mode = 0, targeting_mode = 0;
	EntityId target = 0;
	bool ready = false;
	std::string blocker;
	std::uint64_t sequence = 0;
	std::string loading_mode, loaded_mode;
	std::int64_t beam_funding_remainder = 0;
};
// Rebuilt native mount relation; the adapter resolves this ECS equipment handle.
struct DirectedMountOwner
{
	std::uint64_t equipment = 0;
};
struct DirectedAim
{
	BodyView body;
	std::int64_t altitude = 0, vz = 0;
	std::optional<std::int64_t> bearing;
};
struct PreparedDirectedOperation
{
	DirectedAim aim;
	fixed::Vec origin;
	std::int64_t bearing = 0, cycle_ticks = 1;
	std::string magazine, resource;
	AmmunitionDefinition ammunition;
	ProjectileDefinition projectile;
	std::vector<std::string> payloads;
	bool stock_claim = false, loaded = false;
};
struct DirectedModeSelection
{
	bool accepted = false, changed = false;
};
struct DirectedPower
{
	bool present = false, blocked = false, discrete_required = false, discrete_paid = false;
	int supply_permille = 1000;
};
struct DirectedDemand
{
	int activity = 0;
	bool discrete = false;
	std::int64_t energy = 0;
};
DirectedRuntime initial_directed_runtime(const DirectedDefinition &);
bool directed_mode_compatible(const DirectedDefinition &, std::size_t mode, std::size_t targeting);
DirectedModeSelection select_directed_modes(DirectedRuntime &, const DirectedDefinition &, const std::string &suffix,
                                            std::int64_t value, bool execute);
bool directed_target_visible(const Observation &, EntityId target, bool retained);
std::optional<DirectedAim> select_directed_contact(const Observation &, EntityId target, std::int64_t observer_altitude,
                                                   bool allow_bearing);
// Returns whether the adapter must start unloading with its resolved native load duration.
bool apply_directed_weapon_mode(CombatMechanism &, const DirectedModeDefinition &, bool restoring);
DirectedDemand directed_resource_demand(const DirectedModeDefinition &, DirectedTargetingKind, bool enabled,
                                        bool weapon_ready, bool broad_radar);
bool begin_directed_operation(DirectedRuntime &, DirectedOperationKind, bool enabled, bool qualified,
                              const DirectedPower &);
bool finish_directed_operation(DirectedRuntime &, bool completed);
Json project_directed_runtime(const DirectedDefinition &, const DirectedRuntime &);
Json save_directed_runtime(EntityId ship, const std::string &equipment, const DirectedRuntime &);
DirectedRuntime restore_directed_runtime(const Json &, const DirectedDefinition &, EntityId ship);
void validate_directed_ammunition_commitment(const DirectedRuntime &, const WeaponState *);
} // namespace phoenix
