module;
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
module phoenix.ui.skin;

namespace phoenix
{
float menu_display_scale(float width, float height)
{
	return std::max(1.f, std::min(width / 1280.f, height / 720.f));
}
MenuSkin MenuSkin::parse(const Json &source, float display_scale)
{
	auto invalid = []
	{
		throw std::runtime_error("Invalid menu sprite sidecar: expected a bounded single-image nine-slice");
	};
	if (source.value("schema_version", 0) != 1 ||
	    source.value("coordinate_system", "") != "frame_pixels_top_left_y_down" ||
	    !source.at("sprite_sheet").is_null() || !std::isfinite(display_scale) || display_scale <= 0)
		invalid();
	MenuSkin skin;
	skin.scale = display_scale;
	skin.sprite = source.at("files").at("sprite").get<std::string>();
	if (skin.sprite.empty() ||
	    skin.sprite.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
	        std::string::npos ||
	    skin.sprite == "." || skin.sprite == "..")
		invalid();
	const auto &size = source.at("image_size");
	if (!size.is_array() || size.size() != 2)
		invalid();
	auto dimension = [&](const nlohmann::json &value)
	{
		if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 16384)
			invalid();
		return value.get<int>();
	};
	skin.width = dimension(size[0]);
	skin.height = dimension(size[1]);
	const auto &slices = source.at("nine_slice");
	skin.top = dimension(slices.at("top"));
	skin.right = dimension(slices.at("right"));
	skin.bottom = dimension(slices.at("bottom"));
	skin.left = dimension(slices.at("left"));
	if (skin.width <= skin.left + skin.right || skin.height <= skin.top + skin.bottom)
		invalid();
	if (source.at("markers").contains("label_center"))
	{
		const auto &marker = source.at("markers").at("label_center");
		if (!marker.is_array() || marker.size() != 3 || !marker[0].is_number() || !marker[1].is_number() ||
		    !marker[2].is_number_integer())
			invalid();
		skin.label = {marker[0].get<float>(), marker[1].get<float>()};
		if (!std::isfinite(skin.label[0]) || !std::isfinite(skin.label[1]) || skin.label[0] < 0 ||
		    skin.label[0] > skin.width || skin.label[1] < 0 || skin.label[1] > skin.height)
			invalid();
		skin.has_label = true;
	}
	return skin;
}
std::string MenuSkin::sprite_sheet(const std::string &name, const std::string &source) const
{
	auto px = [](int value)
	{
		return std::to_string(value) + "px ";
	};
	return "@spritesheet " + name + " { src: " + (source.empty() ? "menu/" + sprite : source) + "; resolution: 1x; " +
	       name + "-outer: 0px 0px " + px(width) + px(height) + "; " + name + "-inner: " + px(left) + px(top) +
	       px(width - left - right) + px(height - top - bottom) + "; }\n";
}
std::string MenuSkin::decorator(const std::string &name, const std::string &unit) const
{
	return "ninepatch(" + name + "-outer, " + name + "-inner, " + std::to_string(top * scale) + unit + " " +
	       std::to_string(right * scale) + unit + " " + std::to_string(bottom * scale) + unit + " " +
	       std::to_string(left * scale) + unit + ") border-box";
}
std::array<float, 2> MenuSkin::label_position(float display_width, float display_height) const
{
	// Match RmlUi's corner compression when a surface is smaller than its borders.
	auto axis = [&](float point, float source_size, float leading, float trailing, float target_size)
	{
		float start = leading * scale, end = trailing * scale;
		if (start + end > target_size)
		{
			start = leading / (leading + trailing) * target_size;
			end = target_size - start;
		}
		if (point < leading)
			return point / leading * start;
		if (point > source_size - trailing)
			return target_size - (source_size - point) / trailing * end;
		return start + (point - leading) / (source_size - leading - trailing) * (target_size - start - end);
	};
	return {
	    axis(label[0], static_cast<float>(width), static_cast<float>(left), static_cast<float>(right), display_width),
	    axis(label[1], static_cast<float>(height), static_cast<float>(top), static_cast<float>(bottom),
	         display_height)};
}
} // namespace phoenix
