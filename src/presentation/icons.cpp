module;
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <initializer_list>
#include <string>
#include <map>
#include <span>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.presentation;

namespace phoenix
{
RadarGlyph radar_icon_strokes(RadarIcon icon, Vector2 centre, float radius, float heading)
{
	RadarGlyph strokes;
	constexpr float pi = 3.14159265358979323846f;
	const float angle = icon == RadarIcon::ship ? heading * pi / 180000 : 0;
	const float c = std::cos(angle), s = std::sin(angle);
	auto point = [&](Vector2 p)
	{
		return Vector2{centre.x + radius * (p.x * c + p.y * s), centre.y + radius * (-p.x * s + p.y * c)};
	};
	auto line = [&](Vector2 a, Vector2 b)
	{
		strokes.lines[strokes.count++] = {point(a), point(b)};
	};
	auto path = [&](std::initializer_list<Vector2> points)
	{
		for (auto p = points.begin() + 1; p != points.end(); ++p)
			line(*(p - 1), *p);
	};
	if (icon == RadarIcon::ship)
	{
		// Pointed bow, broad shoulders and a recessed stern. Deliberately no
		// windows, engines or interior detail that disappears below 16 pixels.
		path({{1, 0}, {-.7f, -.65f}, {-.4f, 0}, {-.7f, .65f}, {1, 0}});
	}
	else if (icon == RadarIcon::asteroid)
	{
		path({{-.9f, -.35f},
		      {-.35f, -.9f},
		      {.45f, -.75f},
		      {.95f, -.05f},
		      {.55f, .75f},
		      {-.3f, .9f},
		      {-.85f, .4f},
		      {-.9f, -.35f}});
		line({-.45f, -.05f}, {-.1f, -.35f});
	}
	else
	{
		const float r = icon == RadarIcon::sun ? .48f : .5f;
		for (int i = 0; i < 16; ++i)
			line({r * std::cos(i * pi / 8), r * std::sin(i * pi / 8)},
			     {r * std::cos((i + 1) * pi / 8), r * std::sin((i + 1) * pi / 8)});
		if (icon == RadarIcon::sun)
			for (int i = 0; i < 8; ++i)
			{
				const float x = std::cos(i * pi / 4), y = std::sin(i * pi / 4);
				line({x * .72f, y * .72f}, {x, y});
			}
	}
	return strokes;
}
void draw_radar_icon(RadarIcon icon, Vector2 centre, float radius, float heading, Color colour)
{
	const float width = std::clamp(radius * .2f, 1.3f, 2.f);
	for (const auto &stroke : radar_icon_strokes(icon, centre, radius, heading))
		DrawLineEx(stroke[0], stroke[1], width, colour);
}
void draw_radar_body(TextureCache &textures, const Catalog &catalog, const BodyView &body, Vector2 centre, float radius,
                     Color colour)
{
	const auto image = catalog.assets.find(body.radar_sprite);
	if (!body.radar_sprite.empty() && image != catalog.assets.end())
	{
		const auto texture = textures.load(body.radar_sprite, image->second);
		if (texture.id)
		{
			const float scale = radius * 2 / std::max(texture.width, texture.height);
			const float w = texture.width * scale, h = texture.height * scale;
			DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)},
			               {centre.x, centre.y, w, h}, {w / 2, h / 2}, -body.heading / 1000.f, colour);
			return;
		}
	}
	draw_radar_icon(body.radar_icon, centre, radius, static_cast<float>(body.heading), colour);
}
} // namespace phoenix
