#pragma once
#include <algorithm>
#include <cmath>
namespace phoenix
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
