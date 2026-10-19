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
} // namespace phoenix
