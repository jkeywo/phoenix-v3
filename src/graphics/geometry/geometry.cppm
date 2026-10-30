module;
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>
export module phoenix.graphics.geometry;
import phoenix.contracts;

export namespace phoenix
{
struct NavigationCamera
{
	double x = 0, y = 0, scale = 1000;
	bool automatic = true;
	inline void fit(double left, double bottom, double right, double top, double width, double height)
	{
		if (!automatic)
			return;
		x = (left + right) / 2;
		y = (bottom + top) / 2;
		scale = std::clamp(std::max(std::max(20000.0, right - left) / std::max(1.0, width),
		                            std::max(20000.0, top - bottom) / std::max(1.0, height)) *
		                       1.25,
		                   1.0, 10000000.0);
	}
	inline void pan(double dx, double dy)
	{
		automatic = false;
		x -= dx * scale;
		y += dy * scale;
	}
	inline void zoom(double factor, double px = 0, double py = 0)
	{
		automatic = false;
		const auto old = scale;
		scale = std::clamp(scale * factor, 1.0, 10000000.0);
		x += px * (old - scale);
		y -= py * (old - scale);
	}
};
} // namespace phoenix

export namespace phoenix
{
// Five degrees beyond the nearest-frame boundary before changing direction.
// Compare wrapped distances so north/zero crossings and large turns work alike.
inline bool retain_sprite_direction(float angle, float previous, float candidate)
{
	return std::abs(std::remainder(previous - angle, 360.f)) <=
	       std::abs(std::remainder(candidate - angle, 360.f)) + 10.f;
}
// Preserve the last valid billboard angle when velocity points along the view axis.
inline float velocity_billboard_rotation(float dx, float dy, float forward_x, float forward_y, float previous)
{
	return dx * dx + dy * dy > .0001f ? std::atan2(dy, dx) - std::atan2(forward_y, forward_x) : previous;
}
} // namespace phoenix

export namespace phoenix
{
struct ShipCameraPose
{
	std::array<double, 3> position{}, target{};
	double bearing = 0;
};
// World millimetres/heading become renderer metres; directions are clockwise
// from the ship's forward bearing. No graphics or simulation ownership here.
ShipCameraPose ship_camera_pose(double x, double y, double heading, double scale, double aspect, int mode,
                                int direction);

// Presentation copies only: no changes to authoritative bodies or observations.
class SceneMotion
{
public:
	void reset();
	void sample(std::uint64_t tick, double now, std::span<const SceneBody> bodies, std::optional<BodyView> focus);
	void render(double now, std::vector<SceneBody> &bodies, std::optional<BodyView> &focus) const;

private:
	std::uint64_t tick_ = 0;
	double received_ = 0, interval_ = .05;
	bool ready_ = false;
	std::map<EntityId, BodyView> previous_;
	std::vector<SceneBody> current_;
	std::optional<BodyView> focus_, previous_focus_;
};
} // namespace phoenix
