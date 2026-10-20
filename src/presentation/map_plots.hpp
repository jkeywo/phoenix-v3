#pragma once
#include "region_markers.hpp"
namespace phoenix
{
inline void draw_radar_plot(TextureCache &textures, const Catalog &catalog, const Observation &view,
                            const SystemView &sensor, const Json &waypoint, RadarSweep &sweep, const Json &settings,
                            Vector2 centre, float radius, double time, bool reduced_motion)
{
	DrawCircleV(centre, radius, {8, 23, 25, 150});
	for (int ring = 1; ring <= 4; ++ring)
		DrawCircleLinesV(centre, radius * ring / 4, {37, 77, 78, 255});
	DrawLineV({centre.x - radius, centre.y}, {centre.x + radius, centre.y}, {37, 77, 78, 255});
	DrawLineV({centre.x, centre.y - radius}, {centre.x, centre.y + radius}, {37, 77, 78, 255});
	const double range = std::max<std::int64_t>(1, sensor.maximum);
	auto point = [&](double x, double y)
	{
		return Vector2{centre.x + static_cast<float>((x - view.own.x) / range) * radius,
		               centre.y - static_cast<float>((y - view.own.y) / range) * radius};
	};
	sweep.draw(centre, radius, sensor, settings, time, reduced_motion);
	draw_region_markers(view.regions, point, range / radius, &sensor, centre, radius);
	draw_radar_body(textures, catalog, view.own, centre, 9, WHITE);
	if (sensor.enabled)
		for (const auto &contact : view.contacts)
			if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), contact.id) !=
			        sensor.contact_ids.end() &&
			    std::hypot(static_cast<double>(contact.x - view.own.x), static_cast<double>(contact.y - view.own.y)) <=
			        range)
				draw_radar_body(textures, catalog, contact, point(contact.x, contact.y), 7, SKYBLUE);
	if (waypoint.value("active", false))
	{
		const double dx = waypoint.value("x", 0.0) - view.own.x, dy = waypoint.value("y", 0.0) - view.own.y;
		const double distance = std::hypot(dx, dy), divisor = std::max(range, distance);
		const Vector2 p{centre.x + static_cast<float>(dx / divisor) * radius,
		                centre.y - static_cast<float>(dy / divisor) * radius};
		if (distance <= range)
			DrawCircleLinesV(p, 8, GOLD);
		else
		{
			const float ux = static_cast<float>(dx / distance), uy = static_cast<float>(-dy / distance);
			DrawLineEx(p, {p.x - ux * 15 - uy * 7, p.y - uy * 15 + ux * 7}, 3, GOLD);
			DrawLineEx(p, {p.x - ux * 15 + uy * 7, p.y - uy * 15 - ux * 7}, 3, GOLD);
		}
	}
}
inline void draw_navigation_plot(const BodyView &own, const Json &regions, const Json &objectives, const Json &waypoint,
                                 const NavigationCamera &camera, Rectangle area, bool overlay = false)
{
	BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width),
	                 static_cast<int>(area.height));
	if (!overlay)
		DrawRectangleRec(area, {8, 23, 25, 255});
	auto point = [&](double x, double y)
	{
		return Vector2{area.x + area.width / 2 + static_cast<float>((x - camera.x) / camera.scale),
		               area.y + area.height / 2 - static_cast<float>((y - camera.y) / camera.scale)};
	};
	const double grid = std::pow(10.0, std::ceil(std::log10(camera.scale * 60)));
	const auto lower = point(std::floor((camera.x - area.width * camera.scale / 2) / grid) * grid,
	                         std::floor((camera.y - area.height * camera.scale / 2) / grid) * grid);
	for (float x = lower.x; x < area.x + area.width; x += static_cast<float>(grid / camera.scale))
		DrawLineV({x, area.y}, {x, area.y + area.height}, {35, 65, 70, 255});
	for (float y = lower.y; y > area.y; y -= static_cast<float>(grid / camera.scale))
		DrawLineV({area.x, y}, {area.x + area.width, y}, {35, 65, 70, 255});
	for (const auto &o : objectives)
	{
		const auto p = point(o.at("destination_x"), o.at("destination_y"));
		DrawCircleLinesV(p, std::max(8.f, static_cast<float>(o.value("radius", 0.0) / camera.scale)), GOLD);
		DrawCircleV(p, 4, GOLD);
	}
	draw_region_markers(regions, point, camera.scale);
	draw_radar_icon(RadarIcon::ship, point(own.x, own.y), 9, static_cast<float>(own.heading), WHITE);
	if (waypoint.value("active", false))
	{
		const auto p = point(waypoint.at("x"), waypoint.at("y"));
		DrawCircleLinesV(p, 10, SKYBLUE);
		DrawLineV({p.x - 14, p.y}, {p.x + 14, p.y}, SKYBLUE);
		DrawLineV({p.x, p.y - 14}, {p.x, p.y + 14}, SKYBLUE);
	}
	EndScissorMode();
}
} // namespace phoenix
