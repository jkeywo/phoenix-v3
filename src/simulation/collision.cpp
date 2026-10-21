module;
#include <algorithm>
#include <limits>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.simulation;
import phoenix.content;
#include "body.hpp"

namespace phoenix
{
using fixed::Vec;
namespace
{
Vec perpendicular(Vec axis)
{
	return {-axis.y, axis.x};
}
std::int64_t radius(const CollisionBody &body, Vec axis)
{
	auto x = fixed::direction(body.heading), y = perpendicular(x);
	return body.box ? (body.half_width * std::abs(fixed::dot(axis, x) / fixed::unit) +
	                   body.half_length * std::abs(fixed::dot(axis, y) / fixed::unit)) /
	                      fixed::unit
	                : body.radius;
}
struct Contact
{
	Vec normal;
	std::int64_t depth = 0;
};
Contact circle_box(const CollisionBody &circle, const CollisionBody &box)
{
	auto x = fixed::direction(box.heading), y = perpendicular(x), delta = circle.position - box.position;
	Vec local{fixed::dot(delta, x) / fixed::unit, fixed::dot(delta, y) / fixed::unit};
	Vec closest{std::clamp(local.x, -box.half_width, box.half_width),
	            std::clamp(local.y, -box.half_length, box.half_length)};
	auto difference = closest - local;
	auto distance = fixed::length(difference);
	if (distance >= circle.radius)
		return {};
	if (distance)
		return {fixed::normal(fixed::scale(x, difference.x) + fixed::scale(y, difference.y)), circle.radius - distance};
	auto px = box.half_width - std::abs(local.x), py = box.half_length - std::abs(local.y);
	return px <= py ? Contact{local.x >= 0 ? -x : x, circle.radius + px}
	                : Contact{local.y >= 0 ? -y : y, circle.radius + py};
}
Contact contact(const CollisionBody &a, const CollisionBody &b)
{
	if (!a.box && !b.box)
	{
		auto delta = b.position - a.position;
		auto distance = fixed::length(delta);
		return distance < a.radius + b.radius ? Contact{fixed::normal(delta), a.radius + b.radius - distance}
		                                      : Contact{};
	}
	if (!a.box)
		return circle_box(a, b);
	if (!b.box)
	{
		auto hit = circle_box(b, a);
		hit.normal = -hit.normal;
		return hit;
	}
	auto ax = fixed::direction(a.heading), bx = fixed::direction(b.heading);
	Contact result{{}, std::numeric_limits<std::int64_t>::max()};
	for (auto axis : {ax, perpendicular(ax), bx, perpendicular(bx)})
	{
		auto separation = fixed::dot(b.position - a.position, axis) / fixed::unit;
		auto depth = radius(a, axis) + radius(b, axis) - std::abs(separation);
		if (depth <= 0)
			return {};
		if (depth < result.depth)
			result = {separation >= 0 ? axis : -axis, depth};
	}
	return result;
}
std::int64_t sweep_separation(const CollisionBody &a, const CollisionBody &b)
{
	if (!a.box && !b.box)
	{
		const auto delta = b.position - a.position;
		const auto reach = a.radius + b.radius;
		return fixed::dot(delta, delta) - reach * reach;
	}
	if (!a.box || !b.box)
	{
		const auto &circle = a.box ? b : a;
		const auto &box = a.box ? a : b;
		const auto x = fixed::direction(box.heading), y = perpendicular(x), delta = circle.position - box.position;
		const Vec local{fixed::dot(delta, x) / fixed::unit, fixed::dot(delta, y) / fixed::unit};
		const Vec closest{std::clamp(local.x, -box.half_width, box.half_width),
		                  std::clamp(local.y, -box.half_length, box.half_length)};
		const auto difference = local - closest;
		return fixed::dot(difference, difference) - circle.radius * circle.radius;
	}
	const auto ax = fixed::direction(a.heading), bx = fixed::direction(b.heading);
	auto result = std::numeric_limits<std::int64_t>::min();
	for (const auto axis : {ax, perpendicular(ax), bx, perpendicular(bx)})
	{
		const auto projected = fixed::dot(b.position - a.position, axis) / fixed::unit;
		result = std::max(result, std::abs(projected) - radius(a, axis) - radius(b, axis));
	}
	return result;
}
Vec sweep_position(Vec start, Vec end, std::int64_t fraction)
{
	return {start.x + (end.x - start.x) * fraction / fixed::unit, start.y + (end.y - start.y) * fraction / fixed::unit};
}
} // namespace
void collide(CollisionBody &a, CollisionBody &b)
{
	if (!a.mass && !b.mass)
		return;
	if (std::abs(a.altitude - b.altitude) >= a.half_height + b.half_height)
		return;
	auto extent = [](const CollisionBody &body)
	{
		return body.box ? body.half_width + body.half_length : body.radius;
	};
	auto reach = extent(a) + extent(b);
	if (std::abs(a.position.x - b.position.x) > reach || std::abs(a.position.y - b.position.y) > reach)
		return;
	auto hit = contact(a, b);
	if (hit.depth <= 0)
		return;
	auto split = [&](std::int64_t total)
	{
		if (!a.mass)
			return std::pair<std::int64_t, std::int64_t>{0, total};
		if (!b.mass)
			return std::pair<std::int64_t, std::int64_t>{total, 0};
		auto first = total * b.mass / (a.mass + b.mass);
		return std::pair{first, total - first};
	};
	const auto vertical_depth = a.half_height + b.half_height - std::abs(a.altitude - b.altitude);
	if ((a.spatial || b.spatial) && vertical_depth < hit.depth)
	{
		const auto sign = b.altitude >= a.altitude ? 1 : -1;
		const auto vertical_split = [&](std::int64_t amount)
		{
			return !a.spatial   ? std::pair<std::int64_t, std::int64_t>{0, amount}
			       : !b.spatial ? std::pair<std::int64_t, std::int64_t>{amount, 0}
			                    : split(amount);
		};
		const auto [za, zb] = vertical_split(vertical_depth + 1);
		a.altitude -= sign * za;
		b.altitude += sign * zb;
		const auto closing = (a.vertical_velocity - b.vertical_velocity) * sign;
		if (closing > 0)
		{
			const auto [va, vb] = vertical_split(closing);
			a.vertical_velocity -= sign * va;
			b.vertical_velocity += sign * vb;
		}
		return;
	}
	auto [ma, mb] = split(hit.depth + 1);
	a.position = a.position - fixed::scale(hit.normal, ma);
	b.position = b.position + fixed::scale(hit.normal, mb);
	for (auto *body : {&a, &b})
	{
		body->position.x = std::clamp(body->position.x, -fixed::unit, fixed::unit);
		body->position.y = std::clamp(body->position.y, -fixed::unit, fixed::unit);
	}
	auto closing = fixed::dot(a.velocity - b.velocity, hit.normal) / fixed::unit;
	if (closing > 0)
	{
		auto [va, vb] = split(closing);
		a.velocity = a.velocity - fixed::scale(hit.normal, va);
		b.velocity = b.velocity + fixed::scale(hit.normal, vb);
	}
	for (auto *body : {&a, &b})
	{
		body->velocity.x = std::clamp(body->velocity.x, -fixed::unit, fixed::unit);
		body->velocity.y = std::clamp(body->velocity.y, -fixed::unit, fixed::unit);
	}
}
std::optional<std::int64_t> sweep_contact_time(const CollisionBody &a, const CollisionBody &b, Vec start_a, Vec start_b,
                                               bool spatial)
{
	if (!a.mass && !b.mass)
		return std::nullopt;
	const auto end_a = a.position, end_b = b.position;
	auto sample_a = a, sample_b = b;
	const auto separation = [&](std::int64_t fraction)
	{
		sample_a.position = sweep_position(start_a, end_a, fraction);
		sample_b.position = sweep_position(start_b, end_b, fraction);
		const auto planar = sweep_separation(sample_a, sample_b);
		if (!spatial)
			return planar;
		const auto za = a.previous_altitude + (a.altitude - a.previous_altitude) * fraction / fixed::unit;
		const auto zb = b.previous_altitude + (b.altitude - b.previous_altitude) * fraction / fixed::unit;
		return std::max(planar, std::abs(za - zb) - a.half_height - b.half_height);
	};
	// For fixed orientation, squared circle/convex-box distance and the maximum
	// box SAT gap are convex along relative translation. Search their minimum,
	// then the first penetration. Work depends on fraction precision rather than
	// travel distance or obstacle thickness; there is no capped motion substep.
	std::int64_t first = 0, last = fixed::unit;
	while (last - first > 12)
	{
		const auto left = first + (last - first) / 3, right = last - (last - first) / 3;
		const auto left_gap = separation(left), right_gap = separation(right);
		if (left_gap < right_gap)
			last = right;
		else if (left_gap > right_gap)
			first = left;
		else
		{
			first = left;
			last = right;
		}
	}
	auto minimum_time = first, minimum_gap = separation(first);
	for (auto fraction = first + 1; fraction <= last; ++fraction)
	{
		const auto gap = separation(fraction);
		if (gap < minimum_gap)
		{
			minimum_gap = gap;
			minimum_time = fraction;
		}
	}
	// Exact tangency has no inward penetration and needs no positional impulse.
	if (minimum_gap >= 0)
		return std::nullopt;
	first = 0;
	last = minimum_time;
	if (separation(0) < 0)
		last = 0;
	else
		while (last - first > 1)
		{
			const auto middle = first + (last - first) / 2;
			if (separation(middle) < 0)
				last = middle;
			else
				first = middle;
		}
	return last;
}
bool sweep_collide(CollisionBody &a, CollisionBody &b, Vec start_a, Vec start_b, bool spatial)
{
	const auto impact = sweep_contact_time(a, b, start_a, start_b, spatial);
	if (!impact)
		return false;
	a.position = sweep_position(start_a, a.position, *impact);
	b.position = sweep_position(start_b, b.position, *impact);
	if (spatial)
	{
		a.altitude = a.previous_altitude + (a.altitude - a.previous_altitude) * *impact / fixed::unit;
		b.altitude = b.previous_altitude + (b.altitude - b.previous_altitude) * *impact / fixed::unit;
	}
	collide(a, b);
	return true;
}
} // namespace phoenix
