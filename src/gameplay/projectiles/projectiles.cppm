module;
#include <flecs.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.projectiles;
export import phoenix.gameplay.weapons;
export import phoenix.gameplay.ordnance.contacts;
export import phoenix.gameplay.combat.continuation;
export namespace phoenix
{
using ProjectileTargetLookup = std::function<std::optional<OrdnanceTarget>(EntityId)>;
using ProjectileCaptureValidation = std::function<void(const Projectile &)>;
using ProjectileExpiry = std::function<void(flecs::entity)>;
struct ProjectileStep
{
	std::vector<flecs::entity> rows;
	std::map<std::uint64_t, OrdnancePrevious> previous;
};
// Owns native projectile membership and identity. Physical impacts, authored
// captures and destruction reactions remain explicit native integration paths.
class ProjectileStore
{
public:
	void initialise(flecs::world &ecs);
	std::vector<flecs::entity> rows() const;
	std::uint64_t next_identity() const;
	Projectile prepare_launch(PreparedDirectedOperation &prepared, ProjectileLaunch launch, std::uint64_t &random);
	flecs::entity publish(flecs::world &ecs, const Projectile &projectile);
	ProjectileStep advance(const ProjectileTargetLookup &lookup);
	void finish_step(flecs::entity row, const ProjectileExpiry &expire);
	void clear();
	void erase_finished();
	Json save() const;
	void restore(flecs::world &ecs, const Json &saved, std::int64_t tick, const OrdnanceRestoreCatalogue &catalogue,
	             const ProjectileCaptureValidation &validate);

private:
	flecs::query<const Projectile> query_;
	std::uint64_t next_ = 1;
};
} // namespace phoenix
