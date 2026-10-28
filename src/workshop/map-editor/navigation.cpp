module;
#include <imgui.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>

module phoenix.workshop.map_navigation;
import phoenix.graphics.radar;

namespace phoenix
{
void draw_tool_radar_icon(ImDrawList *draw, RadarIcon icon, ImVec2 point, float heading, ImU32 colour, Texture2D sprite)
{
	if (sprite.id)
	{
		const float angle = -heading * 3.14159265f / 180000;
		const float scale = 20.f / std::max(sprite.width, sprite.height);
		auto corner = [&](float x, float y)
		{
			return ImVec2{point.x + x * std::cos(angle) - y * std::sin(angle),
			              point.y + x * std::sin(angle) + y * std::cos(angle)};
		};
		const float w = sprite.width * scale / 2, h = sprite.height * scale / 2;
		draw->AddImageQuad(static_cast<ImTextureID>(sprite.id), corner(-w, -h), corner(w, -h), corner(w, h),
		                   corner(-w, h), {0, 0}, {1, 0}, {1, 1}, {0, 1}, colour);
		return;
	}
	for (const auto &stroke : radar_icon_strokes(icon, {point.x, point.y}, 10, heading))
		draw->AddLine({stroke[0].x, stroke[0].y}, {stroke[1].x, stroke[1].y}, colour, 1.8f);
}
} // namespace phoenix
