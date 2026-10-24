#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace phoenix
{
struct ShipCameraPose
{
	std::array<double, 3> position{}, target{};
	double bearing = 0;
};
inline bool ship_camera_visible(const ShipCameraPose &camera, double x, double y, double height, double radius,
                                double aspect, double far_distance)
{
	std::array<double, 3> forward, right, up,
	    delta{x / 1000 - camera.position[0], height / 1000 - camera.position[1], -y / 1000 - camera.position[2]};
	double length = 0;
	for (int i = 0; i < 3; ++i)
	{
		forward[i] = camera.target[i] - camera.position[i];
		length += forward[i] * forward[i];
	}
	length = std::sqrt(length);
	if (length == 0)
		return false;
	for (auto &v : forward)
		v /= length;
	const double horizontal = std::hypot(forward[0], forward[2]);
	if (horizontal == 0)
		return false;
	right = {-forward[2] / horizontal, 0, forward[0] / horizontal};
	up = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
	      right[0] * forward[1] - right[1] * forward[0]};
	auto dot = [&](const auto &v)
	{
		return delta[0] * v[0] + delta[1] * v[1] + delta[2] * v[2];
	};
	const double depth = dot(forward), tangent = std::tan(25.0 * 3.14159265358979323846 / 180);
	return depth + radius > 0 && depth - radius < far_distance &&
	       std::abs(dot(right)) <=
	           depth * tangent * aspect + radius * std::sqrt(1 + tangent * tangent * aspect * aspect) &&
	       std::abs(dot(up)) <= depth * tangent + radius * std::sqrt(1 + tangent * tangent);
}
// World millimetres/heading become renderer metres; directions are clockwise
// from the ship's forward bearing. No graphics or simulation ownership here.
inline ShipCameraPose ship_camera_pose(double x, double y, double heading, double scale, double aspect, int mode,
                                       int direction)
{
	if (mode < 0 || mode > 1 || direction < 0 || direction > 7)
		throw std::invalid_argument("Invalid ship camera selection");
	constexpr double pi = 3.14159265358979323846;
	ShipCameraPose pose;
	pose.bearing = heading * pi / 180000 - direction * pi / 4;
	const double forward_x = std::cos(pose.bearing), forward_z = -std::sin(pose.bearing);
	x /= 1000;
	y /= -1000;
	if (!mode)
	{
		const double height = std::max(2.0, scale * .2), ahead = height * 8;
		const double setback = std::max(2.0, scale * .5);
		pose.position = {x - forward_x * setback, height, y - forward_z * setback};
		pose.target = {pose.position[0] + forward_x * ahead, 0, pose.position[2] + forward_z * ahead};
	}
	else
	{
		const double extent = std::max(2.0, scale) * std::max(1.0, aspect);
		pose.position = {x - forward_x * extent * 3, extent * 1.5, y - forward_z * extent * 3};
		pose.target = {x, scale * .5, y};
	}
	return pose;
}
} // namespace phoenix
