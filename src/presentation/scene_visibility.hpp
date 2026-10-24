#pragma once
#include <algorithm>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
namespace phoenix
{
inline bool scene_bounds_visible(Vector3 minimum, Vector3 maximum, Matrix vp)
{
	bool outside[6] = {true, true, true, true, true, true};
	for (int i = 0; i < 8; ++i)
	{
		const Vector3 p{i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z};
		const float x = p.x * vp.m0 + p.y * vp.m4 + p.z * vp.m8 + vp.m12,
		            y = p.x * vp.m1 + p.y * vp.m5 + p.z * vp.m9 + vp.m13,
		            z = p.x * vp.m2 + p.y * vp.m6 + p.z * vp.m10 + vp.m14,
		            w = p.x * vp.m3 + p.y * vp.m7 + p.z * vp.m11 + vp.m15;
		outside[0] &= x < -w;
		outside[1] &= x > w;
		outside[2] &= y < -w;
		outside[3] &= y > w;
		outside[4] &= z < -w;
		outside[5] &= z > w;
	}
	return std::none_of(std::begin(outside), std::end(outside),
	                    [](bool v)
	                    {
		                    return v;
	                    });
}
inline Matrix scene_camera_projection(Camera3D camera, Rectangle region)
{
	const float aspect = region.width / std::max(1.f, region.height);
	return camera.projection == CAMERA_PERSPECTIVE
	           ? MatrixPerspective(camera.fovy * DEG2RAD, aspect, rlGetCullDistanceNear(), rlGetCullDistanceFar())
	           : MatrixOrtho(-camera.fovy * aspect * .5, camera.fovy * aspect * .5, -camera.fovy * .5, camera.fovy * .5,
	                         rlGetCullDistanceNear(), rlGetCullDistanceFar());
}
} // namespace phoenix
