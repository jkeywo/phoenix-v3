module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.graphics.geometry;
import phoenix.contracts;

namespace phoenix
{
namespace
{
BodyView blend(const BodyView &before, const BodyView &after, double alpha, double interval)
{
	if (before.id != after.id || before.definition != after.definition)
		return after;
	// A displacement beyond motion plus one hull diameter is a relocation.
	const auto speed =
	    std::max(std::hypot(double(before.vx), double(before.vy)), std::hypot(double(after.vx), double(after.vy)));
	const auto allowance = speed * interval + std::max(1000.0, 2.0 * after.radius);
	if (std::hypot(double(after.x) - before.x, double(after.y) - before.y) > allowance)
		return after;
	auto result = after;
	const auto linear = [alpha](std::int64_t a, std::int64_t b)
	{
		return std::llround(double(a) + (double(b) - a) * alpha);
	};
	result.x = linear(before.x, after.x);
	result.y = linear(before.y, after.y);
	result.altitude = linear(before.altitude, after.altitude);
	const auto turn = std::remainder(double(after.heading) - before.heading, 360000.0);
	result.heading = (std::llround(before.heading + turn * alpha) % 360000 + 360000) % 360000;
	return result;
}
} // namespace
void SceneMotion::reset()
{
	ready_ = false;
	previous_.clear();
	current_.clear();
	focus_.reset();
	previous_focus_.reset();
}
void SceneMotion::sample(std::uint64_t tick, double now, std::span<const SceneBody> bodies,
                         std::optional<BodyView> focus)
{
	const bool same_focus = (!focus && !focus_) || (focus && focus_ && focus->id == focus_->id);
	if (ready_ && tick == tick_ && same_focus)
		return;
	if (!same_focus || (ready_ && (tick < tick_ || now < received_)))
		reset();
	previous_.clear();
	if (ready_)
		for (const auto &body : current_)
			previous_.emplace(body.id, body);
	previous_focus_ = ready_ ? focus_ : std::nullopt;
	interval_ = ready_ ? std::max(.05, double(tick - tick_) * .05) : .05;
	// Keep a simulation-paced presentation clock across ordinary delivery jitter.
	// Rebase after a stall rather than extrapolating or replaying an old backlog.
	const auto expected = received_ + interval_;
	received_ = ready_ && std::abs(now - expected) < .05 ? expected : now;
	current_.assign(bodies.begin(), bodies.end());
	focus_ = std::move(focus);
	tick_ = tick;
	ready_ = true;
}
void SceneMotion::render(double now, std::vector<SceneBody> &bodies, std::optional<BodyView> &focus) const
{
	const double alpha = std::clamp((interval_ - .05 + std::max(0.0, now - received_)) / interval_, 0.0, 1.0);
	bodies = current_;
	for (auto &body : bodies)
		if (const auto found = previous_.find(body.id); found != previous_.end())
			static_cast<BodyView &>(body) = blend(found->second, body, alpha, interval_);
	focus = focus_;
	if (focus && previous_focus_)
		*focus = blend(*previous_focus_, *focus, alpha, interval_);
	// The camera and owning hull must use exactly the same rendered pose.
	if (focus)
		for (const auto &body : bodies)
			if (body.id == focus->id)
			{
				*focus = static_cast<const BodyView &>(body);
				break;
			}
}
} // namespace phoenix
