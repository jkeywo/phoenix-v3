#pragma once
#include "region_markers.hpp"
namespace phoenix
{
inline Vector2 bearing_ring_point(Vector2 centre, float radius, std::int64_t bearing)
{
	const auto angle = static_cast<float>(bearing) / 1000 * DEG2RAD;
	return {centre.x + std::cos(angle) * radius * 1.08f, centre.y - std::sin(angle) * radius * 1.08f};
}
inline void draw_bearing_ring(const Observation &view, const SystemView &sensor, Vector2 centre, float radius,
                              EntityId selected = 0)
{
	if (!sensor.enabled)
		return;
	bool any = false;
	for (const auto &b : view.bearing_contacts)
		if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), b.id) != sensor.contact_ids.end())
		{
			any = true;
			auto p = bearing_ring_point(centre, radius, b.bearing_mdeg);
			DrawCircleV(p, 5, SKYBLUE);
			if (b.id == selected)
				DrawCircleLinesV(p, 10, GOLD);
			DrawText(TextFormat("#%llu", static_cast<unsigned long long>(b.id)), static_cast<int>(p.x + 8),
			         static_cast<int>(p.y - 6), 14, SKYBLUE);
		}
	if (any)
	{
		DrawCircleLinesV(centre, radius * 1.08f, {80, 135, 155, 180});
		DrawText(localise("Range unknown").c_str(), static_cast<int>(centre.x - radius),
		         static_cast<int>(centre.y - radius * 1.2f), 14, SKYBLUE);
	}
}
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
	if (settings.value("fire_arcs", false))
	{
		int layer = 0;
		for (const auto &arc : view.gameplay.value("combat", Json::object()).value("arcs", Json::array()))
		{
			const bool turret = arc.at("kind") == "turret";
			const auto centre_bearing = arc.at(turret ? "rest_mdeg" : "bearing_mdeg").get<float>() / 1000.f;
			const auto width = arc.at("arc_mdeg").get<float>() / 1000.f;
			const float r = radius * (turret ? .92f : .76f) - static_cast<float>(layer++ % 5) * 3;
			const Color colour = arc.at("disabled").get<bool>() ? GRAY
			                     : turret                       ? Color{140, 160, 180, 180}
			                     : arc.at("active").get<bool>() ? GOLD
			                                                    : Color{110, 215, 195, 210};
			const auto point_at = [&](float angle)
			{
				const float a = angle * DEG2RAD;
				return Vector2{centre.x + std::cos(a) * r, centre.y - std::sin(a) * r};
			};
			const float begin = centre_bearing - width / 2;
			const int segments = std::max(2, static_cast<int>(width / 4));
			for (int i = 0; i < segments; ++i)
				if (!turret || i % 2 == 0)
					DrawLineEx(point_at(begin + width * i / segments), point_at(begin + width * (i + 1) / segments),
					           turret ? 1.f : 2.f, colour);
			if (width < 360)
			{
				DrawLineV(centre, point_at(begin), colour);
				DrawLineV(centre, point_at(begin + width), colour);
			}
		}
	}
	const auto sensing = view.gameplay.value("sensors", Json::object());
	auto sector = [&](Vector2 origin, float bearing, float width, float length, Color colour)
	{
		auto at = [&](float angle)
		{
			return Vector2{origin.x + std::cos(angle * DEG2RAD) * length,
			               origin.y - std::sin(angle * DEG2RAD) * length};
		};
		const int segments = std::max(2, static_cast<int>(width / 4));
		const float begin = bearing - width / 2;
		for (int i = 0; i < segments; ++i)
			DrawLineV(at(begin + width * i / segments), at(begin + width * (i + 1) / segments), colour);
		if (width < 360)
		{
			DrawLineV(origin, at(begin), colour);
			DrawLineV(origin, at(begin + width), colour);
		}
	};
	for (const auto &scanner : sensing.value("scanners", Json::array()))
		if (scanner.at("arc_mdeg") != 360000)
		{
			sector(point(scanner.at("x"), scanner.at("y")), scanner.at("bearing_mdeg").get<float>() / 1000,
			       scanner.at("arc_mdeg").get<float>() / 1000,
			       static_cast<float>(std::min<double>(radius, scanner.at("range_mm").get<float>() / range * radius)),
			       {130, 180, 255, 150});
		}
	for (const auto &contact : sensing.value("contacts", Json::array()))
		if (contact.at("id") == settings.value("sensor_contact", EntityId{0}) && contact.contains("x"))
		{
			const auto p = point(contact.at("x"), contact.at("y"));
			DrawCircleLinesV(p, 11, GOLD);
			if (settings.value("contact_arcs", false) && contact.at("findings").contains("contact_arcs"))
				for (const auto &arc : contact.at("findings").at("contact_arcs").at("data"))
					sector(p, arc.at("bearing_mdeg").get<float>() / 1000, arc.at("arc_mdeg").get<float>() / 1000, 30,
					       ORANGE);
		}
	for (const auto &t : sensing.value("tracks", Json::array()))
	{
		const auto &b = t.at("body");
		const auto at = point(b.at("x"), b.at("y"));
		if (std::hypot(at.x - centre.x, at.y - centre.y) <= radius)
		{
			DrawCircleLinesV(at, 7, GRAY);
			const auto uncertainty = std::min<float>(radius * .25f, t.at("uncertainty").get<float>() / range * radius);
			DrawCircleLinesV(at, std::max(9.f, uncertainty), Color{130, 130, 130, 100});
		}
	}
	sweep.draw(centre, radius, sensor, settings, time, reduced_motion);
	draw_region_markers(view.regions, point, range / radius, &sensor, centre, radius);
	draw_radar_body(textures, catalog, view.own, centre, 9, WHITE);
	if (sensor.enabled)
		for (const auto &contact : view.contacts)
			if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), contact.id) !=
			        sensor.contact_ids.end() &&
			    std::hypot(static_cast<double>(contact.x - view.own.x), static_cast<double>(contact.y - view.own.y)) <=
			        range)
			{
				draw_radar_body(textures, catalog, contact, point(contact.x, contact.y), 7, SKYBLUE);
				if (contact.id == settings.value("sensor_contact", EntityId{0}))
					DrawCircleLinesV(point(contact.x, contact.y), 12, GOLD);
			}
	draw_bearing_ring(view, sensor, centre, radius, settings.value("sensor_contact", EntityId{0}));
	for (const auto &f : view.gameplay.value("psychic", Json::object()).value("fading", Json::array()))
	{
		const auto &b = f.at("body");
		const double x = b.at("x"), y = b.at("y");
		if (sensor.enabled && std::hypot(x - view.own.x, y - view.own.y) <= range)
		{
			const auto at = point(x, y);
			const auto alpha = static_cast<unsigned char>(f.at("opacity").get<int>() * 255 / 1000);
			DrawCircleLinesV(at, 7, Color{102, 191, 255, alpha});
		}
	}
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
		if (!o.contains("destination_x"))
			continue;
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
