module;
#include <flecs.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.projectiles;
namespace phoenix
{
void ProjectileStore::initialise(flecs::world &ecs)
{
	query_ = ecs.query_builder<const Projectile>().cached().build();
}
std::vector<flecs::entity> ProjectileStore::rows() const
{
	std::vector<flecs::entity> rows;
	query_.each(
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
std::uint64_t ProjectileStore::next_identity() const
{
	return next_;
}
Projectile ProjectileStore::prepare_launch(PreparedDirectedOperation &prepared, ProjectileLaunch launch,
                                           std::uint64_t &random)
{
	return create_launched_projectile(prepared, std::move(launch), random, next_);
}
flecs::entity ProjectileStore::publish(flecs::world &ecs, const Projectile &projectile)
{
	const auto &payload = projectile.definition;
	return ecs.entity().set<Projectile>(projectile).set<Destructible>({payload.destruction, payload.durability});
}
ProjectileStep ProjectileStore::advance(const ProjectileTargetLookup &lookup)
{
	ProjectileStep step;
	step.rows = rows();
	for (auto row : step.rows)
	{
		auto &p = row.get_mut<Projectile>();
		step.previous[p.id] = {p.position, p.altitude};
		advance_projectile(p, lookup(p.target));
	}
	return step;
}
void ProjectileStore::finish_step(flecs::entity row, const ProjectileExpiry &expire)
{
	auto &p = row.get_mut<Projectile>();
	if (p.life > 0 && --p.life == 0 && p.definition.durability)
	{
		p.life = 1;
		expire(row);
	}
	if (std::abs(p.position.x) > fixed::unit || std::abs(p.position.y) > fixed::unit)
		p.life = 0;
}
void ProjectileStore::clear()
{
	for (auto row : rows())
		row.destruct();
}
void ProjectileStore::erase_finished()
{
	for (auto row : rows())
		if (projectile_finished(row.get<Projectile>()))
			row.destruct();
}
Json ProjectileStore::save() const
{
	Json result = Json::array();
	for (auto row : rows())
		result.push_back(save_projectile_continuation(row.get<Projectile>(), row.get<Destructible>()));
	return result;
}
void ProjectileStore::restore(flecs::world &ecs, const Json &saved, std::int64_t tick,
                              const OrdnanceRestoreCatalogue &catalogue, const ProjectileCaptureValidation &validate)
{
	auto restored = restore_projectile_continuations(saved, tick, catalogue);
	next_ = restored.next_projectile;
	for (auto &value : restored.values)
	{
		validate(value.projectile);
		ecs.entity().set<Projectile>(value.projectile).set<Destructible>(std::move(value.native));
	}
}
} // namespace phoenix
