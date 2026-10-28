module;
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
export module phoenix.ui.skin;
export import phoenix.data;

export namespace phoenix
{
// Scale against one menu region, rather than the entire spanning window.
float menu_display_scale(float width, float height);
// Source-pixel metadata stays authoritative. Display scale is a presentation choice.
struct MenuSkin
{
	std::string sprite;
	int width = 0, height = 0, top = 0, right = 0, bottom = 0, left = 0;
	std::array<float, 2> label{};
	bool has_label = false;
	float scale = 1;
	static MenuSkin parse(const Json &source, float display_scale);
	std::string sprite_sheet(const std::string &name, const std::string &source = {}) const;
	std::string decorator(const std::string &name, const std::string &unit = "px") const;
	std::array<float, 2> label_position(float display_width, float display_height) const;
};
} // namespace phoenix
