module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <cstdlib>
#include <optional>
#include <span>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.ordnance.contacts;
namespace phoenix
{
struct OrdnanceContactIndex::State
{
	std::vector<OrdnanceBodySweep> bodies;
	std::vector<OrdnanceMissileSweep> missiles;
	SpatialIndex body_index, missile_index;
};
OrdnanceContactIndex::OrdnanceContactIndex(std::vector<OrdnanceBodySweep> bodies,
                                           std::vector<OrdnanceMissileSweep> missiles)
    : state_(std::make_unique<State>())
{
	state_->bodies = std::move(bodies);
	state_->missiles = std::move(missiles);
	std::vector<Bounds> bounds;
	for (const auto &target : state_->bodies)
	{
		const auto &b = target.body;
		const auto radius = b.box ? b.half_width + b.half_length : b.radius;
		bounds.push_back(Bounds::merge(Bounds::around(target.start, radius), Bounds::around(b.position, radius)));
	}
	state_->body_index.rebuild(bounds);
	bounds.clear();
	for (const auto &target : state_->missiles)
		bounds.push_back(Bounds::merge(Bounds::around(target.previous.position, target.radius),
		                               Bounds::around(target.position, target.radius)));
	state_->missile_index.rebuild(bounds);
}
OrdnanceContactIndex::~OrdnanceContactIndex() = default;
std::vector<OrdnanceContact> OrdnanceContactIndex::contacts(const Projectile &p, const OrdnancePrevious &from) const
{
	std::vector<OrdnanceContact> hits;
	const auto query = Bounds::merge(Bounds::around(from.position, p.definition.radius),
	                                 Bounds::around(p.position, p.definition.radius));
	for (auto at = state_->body_index.first(query, 0); at != SpatialIndex::none;
	     at = state_->body_index.first(query, at + 1))
	{
		const auto &t = state_->bodies[at];
		if (t.id == p.source)
			continue;
		if (auto hit = ordnance_volume_hit(from.position, p.position, from.altitude, p.altitude, t.body, t.start,
		                                   t.body.altitude, t.body.altitude, t.half_height, p.definition.radius))
			hits.push_back({*hit, t.id, at, false});
		else if (p.definition.durability && t.id == p.target && p.definition.fuse_radius)
		{
			auto fuse = t.body;
			fuse.box = false;
			fuse.radius = p.definition.fuse_radius;
			if (auto hit = ordnance_volume_hit(from.position, p.position, from.altitude, p.altitude, fuse, t.start,
			                                   t.body.altitude, t.body.altitude, p.definition.fuse_radius))
				hits.push_back({*hit, t.id, at, false});
		}
	}
	for (auto at = state_->missile_index.first(query, 0); at != SpatialIndex::none;
	     at = state_->missile_index.first(query, at + 1))
	{
		const auto &target = state_->missiles[at];
		if (target.id == p.id || target.source == p.source || p.missile_hits.contains(target.id))
			continue;
		CollisionBody shape;
		shape.position = target.position;
		shape.radius = target.radius;
		shape.mass = 1;
		if (auto hit = ordnance_volume_hit(from.position, p.position, from.altitude, p.altitude, shape,
		                                   target.previous.position, target.previous.altitude, target.altitude,
		                                   target.radius, p.definition.radius))
			hits.push_back({*hit, target.id, at, true});
	}
	std::sort(hits.begin(), hits.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tuple{a.time, a.missile, a.id} < std::tuple{b.time, b.missile, b.id};
	          });
	return hits;
}

std::uint64_t select_native_point_defence_target(EntityId observer, fixed::Vec position, fixed::Vec velocity,
                                                 std::int64_t altitude, std::int64_t range,
                                                 std::span<const PointDefenceCandidate> candidates)
{
	std::pair<std::int64_t, std::uint64_t> best{std::numeric_limits<std::int64_t>::max(), 0};
	for (const auto &p : candidates)
	{
		const auto offset = p.position - position;
		const auto distance = fixed::length(offset) + std::abs(p.altitude - altitude);
		if (distance > range)
			continue;
		const auto closing =
		    std::max<std::int64_t>(1, -fixed::dot(p.velocity - velocity, fixed::normal(offset)) / fixed::unit);
		const auto time = p.target == observer ? distance * 1000 / closing : 1000000000 + distance;
		const auto score = std::pair{time, p.id};
		if (score < best)
			best = score;
	}
	return best.second;
}
} // namespace phoenix
