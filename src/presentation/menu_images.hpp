#pragma once
namespace phoenix
{
// Presentation-private image sources. RmlUi owns all GPU handles and fitting.
std::string register_menu_image(const Bytes &png);
void register_theme_font(const Bytes &, const std::string &family, bool fallback = false);
const Bytes *menu_image_data(const std::string &source);
void retain_menu_images(const std::set<std::string> &sources, const void *owner = nullptr);
} // namespace phoenix
