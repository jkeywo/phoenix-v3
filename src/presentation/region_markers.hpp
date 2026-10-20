#pragma once
namespace phoenix
{
template <class Project>
void draw_region_markers(const Json &regions, Project point, double scale, const SystemView *radar = nullptr,
                         Vector2 clip_centre = {}, float clip_radius = 0)
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
