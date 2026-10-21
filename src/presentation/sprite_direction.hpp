#pragma once
#include <cmath>

namespace phoenix
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
