#pragma once
namespace phoenix
{
inline void draw_alert_glow(const Json &alert, Rectangle area)
{
	if (alert.empty() || !alert.value("glow", false))
		return;
	const auto colour = alert.value("colour", std::string{});
	if (colour.size() != 7 || colour[0] != '#' ||
	    colour.substr(1).find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		return;
	const auto rgb = std::stoul(colour.substr(1), nullptr, 16);
	Color tint{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
	           static_cast<unsigned char>(rgb), 0};
	const int width = static_cast<int>(std::min({28.f, area.width / 12, area.height / 12}));
	for (int i = 0; i < width; ++i)
	{
		tint.a = static_cast<unsigned char>(130.f * (1.f - static_cast<float>(i) / width));
		DrawRectangleLinesEx({area.x + i, area.y + i, area.width - 2 * i, area.height - 2 * i}, 1, tint);
	}
}
} // namespace phoenix
