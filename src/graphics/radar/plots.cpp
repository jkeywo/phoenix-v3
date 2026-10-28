module;
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
module phoenix.graphics.radar;
import phoenix.content.catalog;

import phoenix.application.preferences;
namespace phoenix
{
void draw_region_markers(const Json &regions, const std::function<Vector2(double, double)> &point, double scale,
                         const SystemView *radar, Vector2 clip_centre, float clip_radius)
{
	for (const auto &r : regions)
	{
		if (!r.value(radar ? "radar" : "navigation", false))
			continue;
		if (radar && std::none_of(r.at("tags").begin(), r.at("tags").end(),
		                          [&](const auto &tag)
		                          {
			                          return std::find(radar->shows.begin(), radar->shows.end(),
			                                           tag.template get<std::string>()) != radar->shows.end();
		                          }))
			continue;
		const auto &c = r.at("colour");
		Color colour{static_cast<unsigned char>(c[0].template get<float>() * 255),
		             static_cast<unsigned char>(c[1].template get<float>() * 255),
		             static_cast<unsigned char>(c[2].template get<float>() * 255), 180};
		auto line = [&](Vector2 a, Vector2 b)
		{
			if (radar)
			{
				const double dx = b.x - a.x, dy = b.y - a.y, x = a.x - clip_centre.x, y = a.y - clip_centre.y;
				const double A = dx * dx + dy * dy, B = 2 * (x * dx + y * dy),
				             C = x * x + y * y - clip_radius * clip_radius;
				if (A < 1e-9)
					return;
				const double disc = B * B - 4 * A * C;
				if (disc < 0)
					return;
				const auto lo = std::max(0.0, (-B - std::sqrt(disc)) / (2 * A)),
				           hi = std::min(1.0, (-B + std::sqrt(disc)) / (2 * A));
				if (lo > hi)
					return;
				b = {a.x + static_cast<float>(dx * hi), a.y + static_cast<float>(dy * hi)};
				a = {a.x + static_cast<float>(dx * lo), a.y + static_cast<float>(dy * lo)};
			}
			DrawLineV(a, b, colour);
		};
		const double x = r.at("x"), y = r.at("y");
		if (r.at("shape") == "rectangle")
		{
			const double w = r.at("half_width"), h = r.at("half_height");
			const Vector2 corners[]{point(x - w, y - h), point(x + w, y - h), point(x + w, y + h), point(x - w, y + h)};
			for (int i = 0; i < 4; ++i)
				line(corners[i], corners[(i + 1) % 4]);
		}
		else
			for (const auto *key : {"radius", "inner_radius"})
			{
				const double radius = r.at(key);
				if (radius <= 0)
					continue;
				const int count = std::clamp(static_cast<int>(radius / scale), 32, 256);
				for (int i = 0; i < count; ++i)
				{
					const double a = i * 6.283185307179586 / count, b = (i + 1) * 6.283185307179586 / count;
					line(point(x + radius * std::cos(a), y + radius * std::sin(a)),
					     point(x + radius * std::cos(b), y + radius * std::sin(b)));
				}
			}
		const auto at = point(x, y);
		if (!radar || std::hypot(at.x - clip_centre.x, at.y - clip_centre.y) < clip_radius - 40)
			DrawText(localise(r.at("name").template get<std::string>()).c_str(), static_cast<int>(at.x + 5),
			         static_cast<int>(at.y + 5), 14, colour);
	}
}
} // namespace phoenix

namespace phoenix
{
Color radar_selection_colour(const Catalog &catalog, const std::string &source, const Json &tokens)
{
	const auto key = "selection_" + source;
	const auto fallback = source == "tactical" ? "#ff6262" : source == "waypoint" ? "#ffdb55" : "#62aaff";
	const auto pack_tokens = catalog.themes.contains(catalog.ui_theme)
	                             ? catalog.themes.at(catalog.ui_theme).value("tokens", Json::object())
	                             : Json::object();
	const auto value = tokens.value(key, pack_tokens.value(key, std::string(fallback)));
	return GetColor(static_cast<unsigned>(std::stoul(value.substr(1) + (value.size() == 7 ? "ff" : ""), nullptr, 16)));
}
void draw_radar_selection(Vector2 at, float radius, Color colour)
{
	DrawRing(at, radius - 1, radius + 1, 0, 360, 64, colour);
}
bool radar_contact_in_range(const BodyView &own, const BodyView &body, double range)
{
	return std::hypot(double(body.x - own.x), double(body.y - own.y)) <= range + body.radius;
}
Vector2 radar_contact_point(const BodyView &own, const BodyView &body, double range, Vector2 centre, float radius)
{
	const auto dx = double(body.x - own.x), dy = double(body.y - own.y);
	const auto distance = std::hypot(dx, dy);
	const auto scale = distance > range && distance > 0 ? std::max(0.0, distance - body.radius) / distance : 1.0;
	return {centre.x + float(dx * scale / range) * radius, centre.y - float(dy * scale / range) * radius};
}
void draw_radar_footprint(Vector2 at, float size, Vector2 centre, float radius, Color colour)
{
	// Clip the physical circumference to the plot, including a centre outside it.
	for (int i = 0; i < 192; ++i)
	{
		const auto a = i * 2 * PI / 192, b = (i + 1) * 2 * PI / 192;
		Vector2 from{at.x + std::cos(a) * size, at.y + std::sin(a) * size};
		Vector2 to{at.x + std::cos(b) * size, at.y + std::sin(b) * size};
		const double dx = to.x - from.x, dy = to.y - from.y, x = from.x - centre.x, y = from.y - centre.y;
		const double A = dx * dx + dy * dy, B = 2 * (x * dx + y * dy), C = x * x + y * y - radius * radius;
		const auto disc = B * B - 4 * A * C;
		if (A < 1e-9 || disc < 0)
			continue;
		const auto lo = std::max(0.0, (-B - std::sqrt(disc)) / (2 * A));
		const auto hi = std::min(1.0, (-B + std::sqrt(disc)) / (2 * A));
		if (lo <= hi)
			DrawLineV({from.x + float(dx * lo), from.y + float(dy * lo)},
			          {from.x + float(dx * hi), from.y + float(dy * hi)}, colour);
	}
}
Vector2 bearing_ring_point(Vector2 centre, float radius, std::int64_t bearing)
{
	const auto angle = static_cast<float>(bearing) / 1000 * DEG2RAD;
	return {centre.x + std::cos(angle) * radius * 1.08f, centre.y - std::sin(angle) * radius * 1.08f};
}
void draw_bearing_ring(const Observation &view, const SystemView &sensor, Vector2 centre, float radius,
                       EntityId selected, Color selection_colour)
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
				draw_radar_selection(p, 14, selection_colour);
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
void draw_radar_plot(TextureCache &textures, const Catalog &catalog, const Observation &view, const SystemView &sensor,
                     const Json &waypoint, RadarSweep &sweep, const Json &settings, Vector2 centre, float radius,
                     double time, bool reduced_motion)
{
	const auto tokens = settings.value("selection_tokens", Json::object());
	const auto selected_colour = radar_selection_colour(catalog, settings.value("selection_source", "sensors"), tokens);
	const auto waypoint_colour = radar_selection_colour(catalog, "waypoint", tokens);
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
			const auto body = std::find_if(view.contacts.begin(), view.contacts.end(),
			                               [&](const auto &value)
			                               {
				                               return value.id == contact.at("id");
			                               });
			if (body == view.contacts.end() || !radar_contact_in_range(view.own, *body, range))
				continue;
			const auto p = radar_contact_point(view.own, *body, range, centre, radius);

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
			    radar_contact_in_range(view.own, contact, range))
			{
				const auto at = radar_contact_point(view.own, contact, range, centre, radius);
				const auto size = float(contact.radius / range) * radius;
				if (size > 12)
				{
					draw_radar_footprint(point(contact.x, contact.y), size, centre, radius, SKYBLUE);
					DrawText(localise(contact.definition).c_str(), int(at.x + 8), int(at.y + 8), 14, SKYBLUE);
				}
				else
					draw_radar_body(textures, catalog, contact, at, 7, SKYBLUE);
				if (contact.id == settings.value("sensor_contact", EntityId{0}))
					draw_radar_selection(at, 14, selected_colour);
			}
	draw_bearing_ring(view, sensor, centre, radius, settings.value("sensor_contact", EntityId{0}), selected_colour);
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
			draw_radar_selection(p, 14, waypoint_colour);
		else
		{
			const float ux = static_cast<float>(dx / distance), uy = static_cast<float>(-dy / distance);
			DrawLineEx(p, {p.x - ux * 15 - uy * 7, p.y - uy * 15 + ux * 7}, 3, waypoint_colour);
			DrawLineEx(p, {p.x - ux * 15 + uy * 7, p.y - uy * 15 - ux * 7}, 3, waypoint_colour);
		}
	}
}
void draw_navigation_plot(const BodyView &own, const Json &regions, const Json &objectives, const Json &waypoint,
                          const NavigationCamera &camera, Rectangle area, bool overlay, Color waypoint_colour)
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
		draw_radar_selection(p, 14, waypoint_colour);
	}
	EndScissorMode();
}
} // namespace phoenix
