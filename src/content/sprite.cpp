module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

module phoenix.content;
namespace phoenix
{
namespace
{
[[noreturn]] void bad(const std::string &path, const std::string &reason)
{
	throw std::runtime_error(path + ": " + reason);
}
float number(const Json &j, const std::string &path, float lo, float hi)
{
	if (!j.is_number())
		bad(path, "expected number");
	const auto n = j.get<float>();
	if (!std::isfinite(n) || n < lo || n > hi)
		bad(path, "number outside supported range");
	return n;
}
void vector(const Json &j, int count, const std::string &path)
{
	if (!j.is_array() || j.size() != static_cast<std::size_t>(count))
		bad(path, "wrong vector size");
}
std::array<int, 2> image(const Catalog &catalog, const Json &reference, const std::string &path)
{
	if (!reference.is_string() || !catalog.assets.contains(reference.get<std::string>()))
		bad(path, "unresolved PNG reference");
	const auto &bytes = catalog.assets.at(reference.get<std::string>());
	try
	{
		const auto decoded = decode_asset_image(bytes);
		return {decoded.width, decoded.height};
	}
	catch (const std::exception &error)
	{
		bad(path, error.what());
	}
}
} // namespace
Json dust_defaults()
{
	const DustConfig c;
	return {{"enabled", c.enabled},
	        {"count", c.count},
	        {"half_width", c.half_width},
	        {"depth", c.depth},
	        {"width", c.width},
	        {"streak_per_speed", c.streak_per_speed},
	        {"tint", c.tint},
	        {"brightness", c.brightness},
	        {"opacity", c.opacity},
	        {"textures", c.textures},
	        {"centre_fade_inner", c.centre_fade_inner},
	        {"centre_fade_outer", c.centre_fade_outer},
	        {"edge_fade", c.edge_fade}};
}
Json reference_grid_defaults()
{
	const ReferenceGridConfig c;
	return {{"minor_spacing", c.minor_spacing},
	        {"major_spacing", c.major_spacing},
	        {"minor_colour", c.minor_colour},
	        {"major_colour", c.major_colour},
	        {"opacity", c.opacity},
	        {"patch_radius", c.patch_radius},
	        {"fade_band", c.fade_band},
	        {"fade_exponent", c.fade_exponent},
	        {"plane_y", c.plane_y},
	        {"minor_line_width_px", c.minor_line_width_px},
	        {"major_line_width_px", c.major_line_width_px}};
}
DustConfig compile_dust(const Json &dust, const Catalog &catalog, bool web, const std::string &path)
{
	if (!dust.is_object())
		bad(path, "expected dust object");
	const auto defaults = dust_defaults();
	auto validate_keys = [&](const Json &j, bool profiles)
	{
		if (!j.is_object())
			bad(path, "expected dust profile object");
		for (auto it = j.begin(); it != j.end(); ++it)
			if (!defaults.contains(it.key()) && !(profiles && (it.key() == "native" || it.key() == "web")))
				bad(path + "." + it.key(), "unknown dust setting");
	};
	validate_keys(dust, true);
	Json j = defaults;
	for (auto it = dust.begin(); it != dust.end(); ++it)
		if (it.key() != "native" && it.key() != "web")
			j[it.key()] = it.value();
	const char *platform = web ? "web" : "native";
	if (dust.contains(platform))
	{
		validate_keys(dust.at(platform), false);
		j.update(dust.at(platform));
	}
	DustConfig c;
	if (!j.at("enabled").is_boolean())
		bad(path + ".enabled", "expected boolean");
	c.enabled = j.at("enabled");
	if (!j.at("count").is_number_integer())
		bad(path + ".count", "expected integer");
	c.count = static_cast<int>(number(j.at("count"), path + ".count", 0, 10000));
	for (const auto &[key, target] : std::map<std::string, float *>{{"half_width", &c.half_width},
	                                                                {"depth", &c.depth},
	                                                                {"width", &c.width},
	                                                                {"streak_per_speed", &c.streak_per_speed},
	                                                                {"opacity", &c.opacity},
	                                                                {"centre_fade_inner", &c.centre_fade_inner},
	                                                                {"centre_fade_outer", &c.centre_fade_outer},
	                                                                {"edge_fade", &c.edge_fade}})
	{
		const bool unit = key == "opacity" || key == "edge_fade";
		const bool positive = key == "half_width" || key == "depth";
		*target = number(j.at(key), path + "." + key, positive ? .001f : 0.f, unit ? 1.f : 1000000.f);
	}
	if (c.centre_fade_outer <= c.centre_fade_inner || c.centre_fade_outer > 2)
		bad(path + ".centre_fade_outer", "must exceed inner radius and be <= 2");
	for (int i = 0; i < 3; ++i)
	{
		for (const char *key : {"tint", "brightness", "textures"})
			vector(j.at(key), 3, path + "." + key);
		c.tint[i] = number(j.at("tint")[i], path + ".tint", 0, 1);
		c.brightness[i] = number(j.at("brightness")[i], path + ".brightness", 0, 16);
		if (!j.at("textures")[i].is_string())
			bad(path + ".textures", "expected three PNG references or empty defaults");
		c.textures[i] = j.at("textures")[i];
		if (!c.textures[i].empty())
			image(catalog, c.textures[i], path + ".textures");
	}
	return c;
}
ReferenceGridConfig compile_reference_grid(const Json &grid, const std::string &path)
{
	if (!grid.is_object())
		bad(path, "expected reference grid object");
	Json j = reference_grid_defaults();
	for (auto it = grid.begin(); it != grid.end(); ++it)
		if (!j.contains(it.key()) && it.key() != "first_person_plane_y")
			bad(path + "." + it.key(), "unknown grid setting");
	j.update(grid);
	ReferenceGridConfig c;
	for (const auto &[key, target] : std::map<std::string, float *>{{"minor_spacing", &c.minor_spacing},
	                                                                {"major_spacing", &c.major_spacing},
	                                                                {"patch_radius", &c.patch_radius},
	                                                                {"fade_exponent", &c.fade_exponent},
	                                                                {"minor_line_width_px", &c.minor_line_width_px},
	                                                                {"major_line_width_px", &c.major_line_width_px}})
		*target = number(j.at(key), path + "." + key, .001f, 1000000);
	c.fade_band = number(j.at("fade_band"), path + ".fade_band", 0, c.patch_radius);
	c.opacity = number(j.at("opacity"), path + ".opacity", 0, 1);
	c.plane_y = number(j.at("plane_y"), path + ".plane_y", -1000000, 1000000);
	if (j.contains("first_person_plane_y"))
		c.first_person_plane_y =
		    number(j.at("first_person_plane_y"), path + ".first_person_plane_y", -1000000, 1000000);
	const double multiple = static_cast<double>(c.major_spacing) / c.minor_spacing;
	if (multiple < 1 || std::abs(multiple - std::round(multiple)) > 1.e-4)
		bad(path + ".major_spacing", "must be a whole multiple of minor_spacing");
	for (const char *key : {"minor_colour", "major_colour"})
	{
		vector(j.at(key), 4, path + "." + key);
		auto &colour = std::string(key) == "minor_colour" ? c.minor_colour : c.major_colour;
		for (int i = 0; i < 4; ++i)
			colour[i] = number(j.at(key)[i], path + "." + key, 0, 1);
	}
	return c;
}
RadarIcon compile_radar_icon(const Json &value, const std::string &path)
{
	if (!value.is_string())
		bad(path, "expected contact, ship, asteroid or sun");
	const auto icon = radar_icon_named(value.get<std::string>());
	if (!icon)
		bad(path, "expected contact, ship, asteroid or sun");
	return *icon;
}
std::string radar_sprite_reference(const Json &render)
{
	const auto &material = render.contains("sprite") ? render.at("sprite") : render;
	return material.value("radar_sprite", std::string{});
}
std::string radar_sprite_id(const Bytes &png)
{
	return "console-assets:assets/radar/" + sha256(png) + ".png";
}
void verify_player_avatar(const Bytes &png)
{
	if (png.empty())
		return;
	int width = 0, height = 0, channels = 0;
	if (png.size() > 12288 ||
	    !stbi_info_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels) || width < 1 ||
	    height < 1 || width > 48 || height > 48)
		throw std::runtime_error("Player avatars must be PNG images up to 48 by 48 pixels and 12 KiB");
	auto *pixels = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
	if (!pixels)
		throw std::runtime_error("Player avatar PNG cannot decode");
	stbi_image_free(pixels);
}
std::string render_radar_sprite(const Json &render, const Catalog &catalog)
{
	const auto reference = radar_sprite_reference(render);
	return reference.empty() ? std::string{} : radar_sprite_id(catalog.assets.at(reference));
}
RadarIcon render_radar_icon(const Json &render)
{
	if (render.contains("sprite") && render.at("sprite").is_object())
		return compile_radar_icon(render.at("sprite").value("radar_icon", Json("contact")), "sprite.radar_icon");
	return compile_radar_icon(render.value("radar_icon", Json("contact")), "render.radar_icon");
}
const SpriteFrame &sprite_frame(const SpriteMaterial &sprite, std::uint32_t entry, float bearing)
{
	const auto &frames = sprite.entries.empty() ? sprite.frames : sprite.entries.at(entry).frames;
	if (frames.empty())
		throw std::runtime_error("Sprite entry has no frames");
	return *std::min_element(frames.begin(), frames.end(),
	                         [&](const auto &a, const auto &b)
	                         {
		                         return std::abs(std::remainder(a.azimuth - bearing, 360.f)) <
		                                std::abs(std::remainder(b.azimuth - bearing, 360.f));
	                         });
}
SpriteMaterial compile_sprite(const Json &sprite, const Catalog &catalog, const std::string &path)
{
	if (!sprite.is_object() || !sprite.contains("albedo"))
		bad(path, "expected sprite material with albedo");
	if (sprite.contains("entries"))
	{
		const auto &entries = sprite.at("entries");
		if (sprite.contains("frames") || !entries.is_array() || entries.empty() || entries.size() > 256)
			bad(path + ".entries", "expected 1..256 entries instead of top-level frames");
		SpriteMaterial result;
		for (const auto &entry : entries)
		{
			const auto name = entry.at("name").get<std::string>();
			if (!valid_identifier(name) || std::any_of(result.entries.begin(), result.entries.end(),
			                                           [&](const auto &e)
			                                           {
				                                           return e.name == name;
			                                           }))
				bad(path + ".entries", "invalid or duplicate entry name");
			auto source = sprite;
			source.erase("entries");
			source["frames"] = entry.at("frames");
			auto compiled = compile_sprite(source, catalog, path + ".entries." + name);
			if (result.entries.empty())
				result = compiled;
			result.entries.push_back({name, std::move(compiled.frames)});
		}
		return result;
	}
	SpriteMaterial material;
	material.radar_icon = compile_radar_icon(sprite.value("radar_icon", Json("contact")), path + ".radar_icon");
	material.radar_sprite = sprite.value("radar_sprite", std::string{});
	if (!material.radar_sprite.empty())
		image(catalog, material.radar_sprite, path + ".radar_sprite");
	const auto dimensions = image(catalog, sprite.at("albedo"), path + ".albedo");
	material.albedo = sprite.at("albedo");
	for (const char *field : {"normal", "depth"})
		if (sprite.contains(field) && !sprite.at(field).is_null())
		{
			if (image(catalog, sprite.at(field), path + "." + field) != dimensions)
				bad(path + "." + field, "map dimensions differ from albedo");
			(field == std::string("normal") ? material.normal : material.depth) = sprite.at(field).get<std::string>();
		}
	if (sprite.contains("emissive") && !sprite.at("emissive").is_boolean())
		bad(path + ".emissive", "expected boolean");
	material.emissive = sprite.value("emissive", false);
	if (!sprite.contains("frames") || !sprite.at("frames").is_array() ||
	    (sprite.at("frames").size() != 1 && sprite.at("frames").size() != 8))
		bad(path + ".frames", "expected one or eight frames");
	for (const auto &f : sprite.at("frames"))
	{
		SpriteFrame frame;
		frame.name = f.at("name").get<std::string>();
		if (frame.name.empty() || frame.name.size() > 128 ||
		    std::any_of(material.frames.begin(), material.frames.end(),
		                [&](const auto &v)
		                {
			                return v.name == frame.name;
		                }))
			bad(path + ".frames.name", "empty, duplicate or oversized name");
		vector(f.at("rect"), 4, path + ".frames.rect");
		for (int i = 0; i < 4; ++i)
		{
			if (!f.at("rect")[i].is_number_integer())
				bad(path + ".frames.rect", "expected integer pixels");
			frame.rect[i] = static_cast<int>(number(f.at("rect")[i], path + ".frames.rect", 0, 8192));
		}
		if (!frame.rect[2] || !frame.rect[3] || frame.rect[0] + frame.rect[2] > dimensions[0] ||
		    frame.rect[1] + frame.rect[3] > dimensions[1])
			bad(path + ".frames.rect", "frame outside image");
		vector(f.at("pivot"), 2, path + ".frames.pivot");
		for (int i = 0; i < 2; ++i)
			frame.pivot[i] = number(f.at("pivot")[i], path + ".frames.pivot", 0, static_cast<float>(frame.rect[i + 2]));
		frame.azimuth =
		    number(f.value("azimuth", Json(material.frames.size() * 45)), path + ".frames.azimuth", 0, 359.999f);
		if (std::any_of(material.frames.begin(), material.frames.end(),
		                [&](const auto &v)
		                {
			                return v.azimuth == frame.azimuth;
		                }))
			bad(path + ".frames.azimuth", "duplicate camera bearing");
		frame.depth_scale = number(f.value("depth_scale", Json(0)), path + ".frames.depth_scale", 0, 16);
		frame.depth_offset = number(f.value("depth_offset", Json(0)), path + ".frames.depth_offset", -16, 16);
		if (frame.depth_scale > 0 && material.depth.empty())
			bad(path + ".frames.depth_scale", "requires depth texture");
		const auto attachments = f.value("attachments", Json::object());
		if (!attachments.is_object() || attachments.size() > 32)
			bad(path + ".frames.attachments", "expected at most 32 named attachments");
		for (auto it = attachments.begin(); it != attachments.end(); ++it)
		{
			if (it.key().empty() || it.key().size() > 128)
				bad(path + ".frames.attachments", "invalid name");
			const auto &a = it.value();
			SpriteAttachment attachment;
			attachment.x = number(a.at("x"), path + ".attachment.x", 0, static_cast<float>(frame.rect[2]));
			attachment.y = number(a.at("y"), path + ".attachment.y", 0, static_cast<float>(frame.rect[3]));
			attachment.depth_offset =
			    number(a.value("depth_offset", Json(0)), path + ".attachment.depth_offset", -16, 16);
			if (!a.at("layer").is_number_integer())
				bad(path + ".attachment.layer", "expected signed integer");
			attachment.layer = static_cast<int>(number(a.at("layer"), path + ".attachment.layer", -32, 32));
			frame.attachments[it.key()] = attachment;
		}
		material.frames.push_back(std::move(frame));
	}
	return material;
}
std::uint32_t sprite_entry_index(const SpriteMaterial &sprite, const std::string &name)
{
	if (name.empty())
		return 0;
	if (sprite.entries.empty() && name == "default")
		return 0;
	for (std::size_t i = 0; i < sprite.entries.size(); ++i)
		if (sprite.entries[i].name == name)
			return static_cast<std::uint32_t>(i);
	bad("sprite_entry", "unknown entry " + name);
	return 0;
}
void verify_lighting(const Json &j, const std::string &path)
{
	if (!j.is_object())
		bad(path, "expected lighting object");
	for (const auto &[key, range] : std::map<std::string, std::array<float, 2>>{{"ambient", {0, 1}},
	                                                                            {"sun_intensity", {0, 8}},
	                                                                            {"hatch_spacing", {2, 32}},
	                                                                            {"hatch_thickness", {.25f, 5}},
	                                                                            {"ink_strength", {0, 1}},
	                                                                            {"shadow_bias", {.00001f, .05f}},
	                                                                            {"shadow_softness", {0, 4}}})
		if (j.contains(key))
			number(j.at(key), path + "." + key, range[0], range[1]);
	for (const char *key : {"sun_direction", "sun_color"})
		if (j.contains(key))
		{
			vector(j.at(key), 3, path + "." + key);
			float length = 0;
			for (const auto &n : j.at(key))
			{
				const auto v = number(n, path + "." + key, std::string(key) == "sun_color" ? 0.f : -1.f, 1);
				length += v * v;
			}
			if (std::string(key) == "sun_direction" && length < .0001f)
				bad(path + ".sun_direction", "direction cannot be zero");
		}
	if (j.contains("shadows") && !j.at("shadows").is_boolean())
		bad(path + ".shadows", "expected boolean");
}
Json import_sprite_sidecar(const Json &source, const std::map<std::string, std::string> &images)
{
	if (source.value("schema_version", 0) != 1 ||
	    source.value("coordinate_system", std::string{}) != "frame_pixels_top_left_y_down")
		bad("sidecar", "unsupported version or coordinates");
	if (!images.contains("sprite") || images.at("sprite").empty())
		bad("sidecar.files.sprite", "import and select the sprite PNG");
	if (source.contains("files"))
		for (const char *key : {"sprite", "normal", "depth", "radar_icon"})
			if (source.at("files").contains(key) && !source.at("files").at(key).is_null() &&
			    (!images.contains(key) || images.at(key).empty()))
				bad(std::string("sidecar.files.") + key, "import and select the declared PNG");
	Json result = {{"albedo", images.at("sprite")}, {"frames", Json::array()}};
	if (source.contains("emissive"))
		result["emissive"] = source.at("emissive");
	if (source.contains("radar_icon"))
	{
		compile_radar_icon(source.at("radar_icon"), "sidecar.radar_icon");
		result["radar_icon"] = source.at("radar_icon");
	}
	for (const char *field : {"normal", "depth"})
		if (images.contains(field))
			result[field] = images.at(field);
	if (images.contains("radar_icon"))
		result["radar_sprite"] = images.at("radar_icon");
	else if (source.contains("files") && source.at("files").contains("radar_icon"))
		bad("sidecar.files.radar_icon", "import and select the radar PNG");
	const auto sheet = source.value("sprite_sheet", Json());
	if (sheet.is_object() && sheet.contains("entries"))
	{
		result.erase("frames");
		result["entries"] = Json::array();
		for (const auto &entry : sheet.at("entries"))
		{
			auto single = source;
			single["sprite_sheet"] = {{"frames", entry.at("frames")}};
			result["entries"].push_back(
			    {{"name", entry.at("name")}, {"frames", import_sprite_sidecar(single, images).at("frames")}});
		}
		return result;
	}
	const auto frames = sheet.is_object()
	                        ? sheet.at("frames")
	                        : Json::array({{{"name", source.at("id")},
	                                        {"rect", {0, 0, source.at("image_size")[0], source.at("image_size")[1]}},
	                                        {"pivot", source.at("pivot")},
	                                        {"markers", source.value("markers", Json::object())}}});
	for (const auto &f : frames)
	{
		Json out = {{"name", f.at("name")},
		            {"rect", f.at("rect")},
		            {"pivot", f.at("pivot")},
		            {"azimuth", f.value("camera_azimuth_degrees", 0)},
		            {"depth_scale", f.value("depth_scale", images.contains("depth") ? .5 : 0)},
		            {"depth_offset", f.value("depth_offset", 0.0)},
		            {"attachments", Json::object()}};
		const auto markers = f.value("markers", Json::object());
		for (auto it = markers.begin(); it != markers.end(); ++it)
			out["attachments"][it.key()] = {
			    {"x", it.value().at(0)},
			    {"y", it.value().at(1)},
			    {"layer", it.value().at(2)},
			    {"depth_offset", f.value("attachment_depth_offsets", Json::object()).value(it.key(), Json(0))}};
		result["frames"].push_back(std::move(out));
	}
	return result;
}
void apply_sprite_sidecar(Json &render, const Json &source, const std::map<std::string, std::string> &images,
                          const Catalog &catalog, bool engine_effect)
{
	const auto converted = import_sprite_sidecar(source, images);
	const auto material = compile_sprite(converted, catalog, "imported sprite");
	const auto mode = source.value("billboard_mode", std::string(material.frames.size() == 8 ? "eight" : "camera"));
	if ((mode != "camera" && mode != "eight") ||
	    (material.entries.empty() && material.frames.size() != (mode == "eight" ? 8u : 1u)))
		bad("sidecar.billboard_mode", "choose camera for one frame or eight for eight frames");
	const auto scale = number(source.value("render_scale", Json(1)), "sidecar.render_scale", .001f, 1000000);
	Json candidate = render;
	if (engine_effect)
	{
		const auto effect = source.value("engine_effect", Json::object());
		candidate["engines"] = {
		    {"sprite", converted},
		    {"scale", number(effect.value("scale", Json(.4)), "sidecar.engine_effect.scale", .001f, 10)},
		    {"range", number(effect.value("range", Json(32)), "sidecar.engine_effect.range", .001f, 1000000)},
		    {"power", number(effect.value("power", Json(2)), "sidecar.engine_effect.power", .001f, 16)}};
	}
	else
	{
		candidate["sprite"] = converted;
		candidate["mode"] = mode;
		candidate["scale"] = scale;
		candidate.erase("images");
		candidate.erase("radar_icon");
		candidate.erase("radar_sprite");
	}
	render = std::move(candidate);
}
} // namespace phoenix
