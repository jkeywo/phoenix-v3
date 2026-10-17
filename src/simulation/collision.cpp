#include "body.hpp"
#include <algorithm>
#include <limits>
namespace phoenix
{
using fixed::Vec;
namespace
{
Vec perpendicular(Vec axis)
{
	return {-axis.y, axis.x};
}
std::int64_t radius(const Body &body, Vec axis)
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
Contact circle_box(const Body &circle, const Body &box)
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
Contact contact(const Body &a, const Body &b)
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
} // namespace
void collide(Body &a, Body &b)
{
	if (!a.mass && !b.mass)
		return;
	auto extent = [](const Body &body)
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
} // namespace phoenix
