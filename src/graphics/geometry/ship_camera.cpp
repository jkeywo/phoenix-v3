module;
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

module phoenix.graphics.geometry;

namespace phoenix
{
ShipCameraPose ship_camera_pose(double x, double y, double heading, double scale, double aspect, int mode,
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
