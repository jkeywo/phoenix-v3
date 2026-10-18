module;
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.tools;
import phoenix.content;
import phoenix.files;
import phoenix.presentation;
import phoenix.session.api;

namespace phoenix
{
namespace
{
std::string tool_label(const std::string &text)
{
	return localise(text) + "###" + text;
}
struct Editor
{
	FileImportInbox imports;
	WorkshopDocument document;
	std::uint64_t selected = 0;
	int definition = -1, console = -1;
	std::string status = "Import a PNG, create an entity definition, then place instances on the map.";
	std::string pack_name = "workshop", library_parent;
	std::string rename_from, template_id;
	TextureCache textures;
	SceneRenderer scene;
	RenderTexture2D scene_preview{};
	std::string preview_identity;
	std::string preview_content;
	std::optional<Catalog> preview_catalog;
	DustConfig preview_dust;
	float preview_speed = 0, preview_heading = 0;
	float preview_x = 0, preview_y = 0;
	int dust_profile = 0;
	float preview_bearing = 90, preview_elevation = 12, preview_distance = 35, preview_thrust = 1;
	int diagnostic = 0;
	Json pending_sidecar;
	std::map<std::string, std::string> sidecar_images;
	bool import_engines = false;
	bool preview_whole_map = false;
	bool started = false;
	float pan_x = 0, pan_y = 0, zoom = 0.004f;
	void clear_textures()
	{
		textures.clear();
		scene.clear();
		preview_identity.clear();
		preview_content.clear();
		preview_catalog.reset();
		if (scene_preview.id)
			UnloadRenderTexture(scene_preview);
		scene_preview = {};
	}
	Texture2D texture(const std::string &id)
	{
		auto prefix = document.manifest().at("id").get<std::string>() + ":";
		const Bytes *bytes = nullptr;
		if (id.starts_with(prefix))
		{
			auto found = document.pack().files.find(id.substr(prefix.size()));
			if (found != document.pack().files.end())
				bytes = &found->second;
		}
		else
		{
			auto found = document.libraries().assets.find(id);
			if (found != document.libraries().assets.end())
				bytes = &found->second;
		}
		if (!bytes)
			return {};
		return textures.load(id, *bytes);
	}
} editor;
template <class Function> void attempt(Function action)
{
	try
	{
		action();
	}
	catch (const std::exception &error)
	{
		editor.status = error.what();
	}
}
void string_field(const char *label, Json &object, const char *key)
{
	std::string value = object.value(key, std::string{});
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputText((std::string("##") + label).c_str(), &value))
		object[key] = value;
}
void integer_field(const char *label, Json &object, const char *key)
{
	std::int64_t value = object.value(key, static_cast<std::int64_t>(0));
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputScalar((std::string("##") + label).c_str(), ImGuiDataType_S64, &value))
		object[key] = value;
}
void transform_fields(Json &value)
{
	integer_field("X (mm)", value, "x_mm");
	integer_field("Y (mm)", value, "y_mm");
	integer_field("Heading (millidegrees)", value, "heading_mdeg");
}
Json *selected_instance()
{
	for (auto &instance : editor.document.map()["entities"])
		if (instance["id"] == editor.selected)
			return &instance;
	return nullptr;
}
void asset_picker(const char *label, Json &value)
{
	std::string current = value.is_string() ? value.get<std::string>() : "";
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo((std::string("##") + label).c_str(),
	                      current.empty() ? localise("Choose PNG").c_str() : current.c_str()))
	{
		for (const auto &[path, bytes] : editor.document.pack().files)
			if (path.starts_with("assets/") && path.ends_with(".png"))
			{
				auto reference = editor.document.manifest().at("id").get<std::string>() + ":" + path;
				if (ImGui::Selectable(reference.c_str(), reference == current))
					value = reference;
			}
		for (const auto &[reference, bytes] : editor.document.libraries().assets)
			if (reference.ends_with(".png") && !reference.starts_with("console-assets:assets/radar/"))
				if (ImGui::Selectable(reference.c_str(), reference == current))
					value = reference;
		ImGui::EndCombo();
	}
}
void real_field(const char *label, Json &object, const char *key, float fallback, float minimum, float maximum)
{
	float n = object.value(key, fallback);
	if (ImGui::DragFloat(tool_label(label).c_str(), &n, .01f, minimum, maximum))
		object[key] = std::clamp(n, minimum, maximum);
}
void colour_field(const char *label, Json &object, const char *key, const Json &fallback, bool alpha)
{
	auto value = object.value(key, fallback);
	float colour[4] = {1, 1, 1, 1};
	for (std::size_t i = 0; i < value.size(); ++i)
		colour[i] = value[i];
	const bool changed = alpha ? ImGui::ColorEdit4(tool_label(label).c_str(), colour)
	                           : ImGui::ColorEdit3(tool_label(label).c_str(), colour);
	if (changed)
	{
		object[key] = Json::array();
		for (int i = 0; i < (alpha ? 4 : 3); ++i)
			object[key].push_back(colour[i]);
	}
}
void grid_properties(Json &render)
{
	if (!ImGui::CollapsingHeader(tool_label("Reference grid").c_str()))
		return;
	bool authored = render.contains("reference_grid");
	if (ImGui::Checkbox(tool_label("Author reference grid").c_str(), &authored))
	{
		if (authored)
			render["reference_grid"] = reference_grid_defaults();
		else
			render.erase("reference_grid");
	}
	if (!authored)
		return;
	auto &j = render["reference_grid"];
	const auto defaults = reference_grid_defaults();
	for (const auto &[key, label] :
	     std::vector<std::pair<const char *, const char *>>{{"minor_spacing", "Minor spacing (m)"},
	                                                        {"major_spacing", "Major spacing (m)"},
	                                                        {"patch_radius", "Patch radius (m)"},
	                                                        {"fade_band", "Fade band (m)"},
	                                                        {"fade_exponent", "Fade exponent"},
	                                                        {"plane_y", "Plane height (m)"},
	                                                        {"minor_line_width_px", "Minor width (pixels)"},
	                                                        {"major_line_width_px", "Major width (pixels)"},
	                                                        {"opacity", "Grid opacity"}})
		real_field(label, j, key, defaults.at(key), std::string(key) == "plane_y" ? -1000000.f : 0.f,
		           std::string(key) == "opacity" ? 1.f : 1000000.f);
	colour_field("Minor grid colour", j, "minor_colour", defaults.at("minor_colour"), true);
	colour_field("Major grid colour", j, "major_colour", defaults.at("major_colour"), true);
	bool first_person = j.contains("first_person_plane_y");
	if (ImGui::Checkbox(tool_label("First-person plane height override").c_str(), &first_person))
	{
		if (first_person)
			j["first_person_plane_y"] = -10;
		else
			j.erase("first_person_plane_y");
	}
	if (first_person)
		real_field("First-person plane height (m)", j, "first_person_plane_y", -10, -1000000, 1000000);
}
void dust_properties(Json &map)
{
	if (!ImGui::CollapsingHeader(tool_label("Dust motes").c_str()))
		return;
	if (!map.contains("presentation"))
		map["presentation"] = Json::object();
	if (!map["presentation"].contains("dust"))
		map["presentation"]["dust"] = Json::object();
	auto &shared = map["presentation"]["dust"];
	const char *profiles[] = {"Shared", "Native", "Browser"};
	if (ImGui::BeginCombo(tool_label("Dust platform").c_str(), localise(profiles[editor.dust_profile]).c_str()))
	{
		for (int i = 0; i < 3; ++i)
			if (ImGui::Selectable(localise(profiles[i]).c_str(), editor.dust_profile == i))
				editor.dust_profile = i;
		ImGui::EndCombo();
	}
	Json *selected = &shared;
	auto defaults = dust_defaults();
	if (editor.dust_profile)
	{
		for (auto it = shared.begin(); it != shared.end(); ++it)
			if (it.key() != "native" && it.key() != "web")
				defaults[it.key()] = it.value();
		const char *key = editor.dust_profile == 1 ? "native" : "web";
		if (!shared.contains(key))
			shared[key] = Json::object();
		selected = &shared[key];
		if (ImGui::Button(tool_label("Reset platform overrides").c_str()))
			*selected = Json::object();
	}
	auto &j = *selected;
	bool enabled = j.value("enabled", defaults.at("enabled").get<bool>());
	if (ImGui::Checkbox(tool_label("Enable motes").c_str(), &enabled))
		j["enabled"] = enabled;
	int count = j.value("count", defaults.at("count").get<int>());
	if (ImGui::InputInt(tool_label("Mote count").c_str(), &count))
		j["count"] = std::clamp(count, 0, 10000);
	for (const auto &[key, label] :
	     std::vector<std::pair<const char *, const char *>>{{"half_width", "Field half-width (m)"},
	                                                        {"depth", "Field depth (m)"},
	                                                        {"width", "Mote width (m)"},
	                                                        {"streak_per_speed", "Streak stretch per m/s"},
	                                                        {"opacity", "Mote opacity"},
	                                                        {"centre_fade_inner", "Centre fade inner radius"},
	                                                        {"centre_fade_outer", "Centre fade outer radius"},
	                                                        {"edge_fade", "Screen edge fade"}})
		real_field(label, j, key, defaults.at(key), 0,
		           (std::string(key) == "opacity" || std::string(key) == "edge_fade") ? 1.f : 1000000.f);
	colour_field("Mote tint", j, "tint", defaults.at("tint"), false);
	auto brightness = j.value("brightness", defaults.at("brightness"));
	float values[3] = {brightness[0], brightness[1], brightness[2]};
	if (ImGui::DragFloat3(tool_label("Layer brightness").c_str(), values, .01f, 0, 16))
		j["brightness"] = {values[0], values[1], values[2]};
	for (int i = 0; i < 3; ++i)
	{
		ImGui::PushID(i);
		auto textures = j.value("textures", defaults.at("textures"));
		auto texture = textures[i];
		asset_picker("Mote texture", texture);
		if (ImGui::Button(tool_label("Use bundled texture").c_str()))
			texture = "";
		if (texture != textures[i])
		{
			textures[i] = texture;
			j["textures"] = textures;
		}
		ImGui::PopID();
	}
}
void radar_icon_picker(Json &material)
{
	Json current = material.value("radar_sprite", Json(""));
	asset_picker("Radar icon PNG", current);
	if (current != material.value("radar_sprite", Json("")))
	{
		material["radar_sprite"] = current;
		material.erase("radar_icon");
	}
	if (ImGui::Button(tool_label("Clear radar icon").c_str()))
	{
		material.erase("radar_sprite");
		material.erase("radar_icon");
	}
	if (current.is_string() && !current.get<std::string>().empty())
	{
		const auto texture = editor.texture(current.get<std::string>());
		if (texture.id)
			ImGui::Image(static_cast<ImTextureID>(texture.id), {32, 32});
	}
}
void map_radar_icon(ImDrawList *draw, RadarIcon icon, ImVec2 point, float heading, ImU32 colour, Texture2D sprite = {})
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
void material_properties(Json &sprite, bool with_radar_icon = true)
{
	if (with_radar_icon)
		radar_icon_picker(sprite);
	asset_picker("Albedo PNG", sprite["albedo"]);
	for (const char *key : {"normal", "depth"})
	{
		ImGui::PushID(key);
		bool enabled = sprite.contains(key) && !sprite.at(key).is_null();
		if (ImGui::Checkbox(tool_label(key).c_str(), &enabled))
		{
			if (enabled)
				sprite[key] = "";
			else
				sprite.erase(key);
		}
		if (enabled)
			asset_picker(key, sprite[key]);
		ImGui::PopID();
	}
	bool emissive = sprite.value("emissive", false);
	if (ImGui::Checkbox(tool_label("Emissive artwork").c_str(), &emissive))
		sprite["emissive"] = emissive;
	if (!sprite.contains("frames"))
		return;
	for (std::size_t i = 0; i < sprite["frames"].size(); ++i)
	{
		auto &frame = sprite["frames"][i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(frame.at("name").get_ref<const std::string &>().c_str()))
		{
			string_field("Frame name", frame, "name");
			int rect[4];
			for (int k = 0; k < 4; k++)
				rect[k] = frame["rect"][k];
			if (ImGui::InputInt4(tool_label("Atlas rectangle").c_str(), rect))
				frame["rect"] = {rect[0], rect[1], rect[2], rect[3]};
			float pivot[2] = {frame["pivot"][0], frame["pivot"][1]};
			if (ImGui::InputFloat2(tool_label("Frame pivot").c_str(), pivot))
				frame["pivot"] = {pivot[0], pivot[1]};
			real_field("Camera azimuth", frame, "azimuth", static_cast<float>(i) * 45, 0, 359.999f);
			real_field("Depth span / height", frame, "depth_scale", 0, 0, 16);
			real_field("Depth centre / height", frame, "depth_offset", 0, -16, 16);
			if (!frame.contains("attachments"))
				frame["attachments"] = Json::object();
			std::string remove;
			for (auto it = frame["attachments"].begin(); it != frame["attachments"].end(); ++it)
			{
				ImGui::PushID(it.key().c_str());
				auto &a = it.value();
				ImGui::TextUnformatted(it.key().c_str());
				real_field("Marker X", a, "x", 0, 0, static_cast<float>(std::max(0, rect[2])));
				real_field("Marker Y", a, "y", 0, 0, static_cast<float>(std::max(0, rect[3])));
				integer_field("Signed layer", a, "layer");
				real_field("Nozzle depth offset", a, "depth_offset", 0, -16, 16);
				if (ImGui::Button(tool_label("Remove attachment").c_str()))
					remove = it.key();
				ImGui::PopID();
			}
			if (!remove.empty())
				frame["attachments"].erase(remove);
			if (ImGui::Button(tool_label("Add engine attachment").c_str()))
			{
				int n = 1;
				while (frame["attachments"].contains("engine_" + std::to_string(n)))
					n++;
				frame["attachments"]["engine_" + std::to_string(n)] = {
				    {"x", pivot[0]}, {"y", pivot[1]}, {"layer", 0}, {"depth_offset", 0}};
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
void sprite_import(Json &render)
{
	if (ImGui::Button(tool_label("Import sprite sidecar...").c_str()))
		request_file(editor.imports, FilePurpose::WorkshopSprite);
	if (!editor.pending_sidecar.is_object())
		return;
	ImGui::TextWrapped(
	    "%s", localise("Import the PNG files first, then map each source image to an imported asset.").c_str());
	for (const char *key : {"sprite", "normal", "depth", "radar_icon"})
		if (!editor.pending_sidecar.at("files").value(key, Json()).is_null())
		{
			ImGui::PushID(key);
			ImGui::TextWrapped("%s", editor.pending_sidecar.at("files").at(key).get_ref<const std::string &>().c_str());
			Json current = editor.sidecar_images.contains(key) ? Json(editor.sidecar_images.at(key)) : Json("");
			asset_picker(key, current);
			if (current.is_string() && !current.get<std::string>().empty())
				editor.sidecar_images[key] = current.get<std::string>();
			ImGui::PopID();
		}
	ImGui::Checkbox(tool_label("Import as engine effect").c_str(), &editor.import_engines);
	auto &sidecar = editor.pending_sidecar;
	const auto sheet = sidecar.value("sprite_sheet", Json());
	const bool inferred_eight = sheet.is_object() && sheet.at("frames").size() == 8;
	bool eight = sidecar.value("billboard_mode", std::string(inferred_eight ? "eight" : "camera")) == "eight";
	if (ImGui::Checkbox(tool_label("Imported eight-direction billboard").c_str(), &eight))
		sidecar["billboard_mode"] = eight ? "eight" : "camera";
	real_field("Imported render scale", sidecar, "render_scale", 1, .001f, 1000000);
	if (ImGui::Button(tool_label("Apply sprite material").c_str()))
		attempt(
		    [&]
		    {
			    auto catalog = editor.document.libraries();
			    const auto prefix = editor.document.manifest().at("id").get<std::string>() + ":";
			    for (const auto &[path, bytes] : editor.document.pack().files)
				    if (path.starts_with("assets/"))
					    catalog.assets[prefix + path] = bytes;
			    apply_sprite_sidecar(render, editor.pending_sidecar, editor.sidecar_images, catalog,
			                         editor.import_engines);
			    editor.document.definitions()["schema"] = 3;
			    editor.pending_sidecar = nullptr;
			    editor.clear_textures();
			    editor.status = "Sprite sidecar imported. Calibrate depth in the live preview.";
		    });
}
void lighting_preview()
{
	if (!ImGui::Begin(tool_label("Lighting and sprite preview").c_str()))
	{
		ImGui::End();
		return;
	}
	auto &map = editor.document.map();
	if (!map.contains("lighting"))
		map["lighting"] = Json::object();
	auto &light = map["lighting"];
	dust_properties(map);
	if (ImGui::CollapsingHeader(tool_label("Scene lighting").c_str()))
	{
		float direction[3] = {-.6f, .6f, -.5f};
		if (light.contains("sun_direction"))
			for (int i = 0; i < 3; i++)
				direction[i] = light["sun_direction"][i];
		if (ImGui::SliderFloat3(tool_label("Sun direction").c_str(), direction, -1, 1))
		{
			light["sun_direction"] = {direction[0], direction[1], direction[2]};
			editor.document.definitions()["schema"] = 3;
		}
		float color[3] = {1, .89f, .7f};
		if (light.contains("sun_color"))
			for (int i = 0; i < 3; i++)
				color[i] = light["sun_color"][i];
		if (ImGui::ColorEdit3(tool_label("Sun colour").c_str(), color))
			light["sun_color"] = {color[0], color[1], color[2]};
		real_field("Sun intensity", light, "sun_intensity", 1, 0, 8);
		real_field("Ambient fill", light, "ambient", .14f, 0, 1);
		real_field("Hatch spacing", light, "hatch_spacing", 6, 2, 32);
		real_field("Hatch thickness", light, "hatch_thickness", 2, .25f, 5);
		real_field("Ink strength", light, "ink_strength", .9f, 0, 1);
		bool shadows = light.value("shadows", true);
		if (ImGui::Checkbox(tool_label("Cast sun shadows").c_str(), &shadows))
			light["shadows"] = shadows;
		real_field("Shadow bias", light, "shadow_bias", .001f, .00001f, .05f);
		real_field("Shadow softness", light, "shadow_softness", 1, 0, 4);
	}
	ImGui::Checkbox(tool_label("Preview whole map").c_str(), &editor.preview_whole_map);
	ImGui::SliderFloat(tool_label("Preview bearing").c_str(), &editor.preview_bearing, 0, 359);
	ImGui::SliderFloat(tool_label("Preview elevation").c_str(), &editor.preview_elevation, 5, 80);
	ImGui::SliderFloat(tool_label("Preview distance").c_str(), &editor.preview_distance, 5, 200);
	ImGui::SliderFloat(tool_label("Preview forward thrust").c_str(), &editor.preview_thrust, 0, 1);
	ImGui::DragFloat(tool_label("Preview speed (m/s)").c_str(), &editor.preview_speed, .1f, -1000, 1000);
	ImGui::SliderFloat(tool_label("Preview motion heading").c_str(), &editor.preview_heading, 0, 359);
	ImGui::InputFloat(tool_label("Preview X (m)").c_str(), &editor.preview_x);
	ImGui::InputFloat(tool_label("Preview Y (m)").c_str(), &editor.preview_y);
	if (ImGui::Button(tool_label("Reset preview motion").c_str()))
	{
		editor.preview_x = editor.preview_y = editor.preview_speed = 0;
	}
	const float motion_angle = editor.preview_heading * 3.14159265f / 180;
	editor.preview_x += std::cos(motion_angle) * editor.preview_speed * std::min(GetFrameTime(), .1f);
	editor.preview_y += std::sin(motion_angle) * editor.preview_speed * std::min(GetFrameTime(), .1f);
	const char *modes[] = {"Comic", "Smooth", "Unlit", "Normals", "Depth", "Sun visibility"};
	if (ImGui::BeginCombo(tool_label("Diagnostic view").c_str(), localise(modes[editor.diagnostic]).c_str()))
	{
		for (int i = 0; i < 6; i++)
			if (ImGui::Selectable(localise(modes[i]).c_str(), editor.diagnostic == i))
				editor.diagnostic = i;
		ImGui::EndCombo();
	}
	const auto avail = ImGui::GetContentRegionAvail();
	const int width = std::clamp(static_cast<int>(avail.x), 64, 1600),
	          height = std::clamp(static_cast<int>(avail.y), 64, 1000);
	const auto content_identity = editor.document.definitions().dump() + ":" + map.at("id").get<std::string>();
	const auto identity = content_identity + std::to_string(width) + ":" + std::to_string(height) + ":" +
	                      std::to_string(editor.definition) + ":" + std::to_string(editor.preview_whole_map) + ":" +
	                      std::to_string(editor.preview_bearing) + ":" + std::to_string(editor.preview_elevation) +
	                      ":" + std::to_string(editor.preview_distance) + ":" + std::to_string(editor.preview_thrust) +
	                      ":" + std::to_string(editor.diagnostic) + ":" + std::to_string(editor.preview_speed) + ":" +
	                      std::to_string(editor.preview_heading) + ":" + std::to_string(editor.preview_x) + ":" +
	                      std::to_string(editor.preview_y);
	if (identity != editor.preview_identity)
		attempt(
		    [&]
		    {
			    if (!editor.preview_catalog || editor.preview_content != content_identity)
			    {
				    editor.preview_catalog = editor.document.verify();
				    editor.preview_dust =
				        compile_dust(map.value("presentation", Json::object()).value("dust", Json::object()),
				                     *editor.preview_catalog,
#ifdef __EMSCRIPTEN__
				                     true,
#else
				                     false,
#endif
				                     "presentation.dust");
				    editor.preview_content = content_identity;
				    editor.scene.clear();
			    }
			    const auto &catalog = *editor.preview_catalog;
			    std::vector<SceneBody> bodies;
			    if (editor.preview_whole_map)
			    {
				    for (const auto &instance : map["entities"])
				    {
					    SceneBody b;
					    b.id = instance.at("id");
					    b.definition = instance.at("definition");
					    b.x = instance.at("x_mm");
					    b.y = instance.at("y_mm");
					    b.vx = b.vy = 0;
					    b.heading = instance.at("heading_mdeg");
					    b.forward_thrust = static_cast<int>(editor.preview_thrust * 1000);
					    bodies.push_back(std::move(b));
				    }
				    std::uint64_t id = 1000000;
				    for (const auto &slot : map["ship_slots"])
				    {
					    SceneBody b;
					    b.id = id++;
					    b.definition = slot.at("allowed_ships").at(0);
					    b.x = slot.at("x_mm");
					    b.y = slot.at("y_mm");
					    b.vx = b.vy = 0;
					    b.heading = slot.at("heading_mdeg");
					    b.forward_thrust = static_cast<int>(editor.preview_thrust * 1000);
					    bodies.push_back(std::move(b));
				    }
			    }
			    else
			    {
				    const auto &defs = editor.document.definitions()["entities"];
				    if (editor.definition >= 0 && editor.definition < static_cast<int>(defs.size()))
				    {
					    SceneBody b;
					    b.id = 1;
					    b.definition = defs[editor.definition].at("id");
					    b.x = b.y = b.vx = b.vy = b.heading = 0;
					    b.forward_thrust = static_cast<int>(editor.preview_thrust * 1000);
					    bodies.push_back(std::move(b));
				    }
			    }
			    SceneEnvironment environment;
			    environment.dust = editor.preview_dust;
			    // The selected preview hull is the sole local reference, never every map entity.
			    if (!bodies.empty())
			    {
				    auto &own = bodies.back();
				    const auto &render = catalog.entities.at(own.definition).at("render");
				    if (render.contains("reference_grid"))
					    environment.grid = compile_reference_grid(render.at("reference_grid"), "render.reference_grid");
				    own.x += static_cast<std::int64_t>(editor.preview_x * 1000);
				    own.y += static_cast<std::int64_t>(editor.preview_y * 1000);
				    own.vx = static_cast<std::int64_t>(std::cos(motion_angle) * editor.preview_speed * 1000);
				    own.vy = static_cast<std::int64_t>(std::sin(motion_angle) * editor.preview_speed * 1000);
				    environment.own = own;
			    }
			    if (!editor.scene_preview.id || editor.scene_preview.texture.width != width ||
			        editor.scene_preview.texture.height != height)
			    {
				    if (editor.scene_preview.id)
					    UnloadRenderTexture(editor.scene_preview);
				    editor.scene_preview = LoadRenderTexture(width, height);
			    }
			    const float bearing = editor.preview_bearing * 3.14159265f / 180,
			                elevation = editor.preview_elevation * 3.14159265f / 180, d = editor.preview_distance;
			    Camera3D camera{{d * std::cos(elevation) * std::cos(bearing), d * std::sin(elevation),
			                     -d * std::cos(elevation) * std::sin(bearing)},
			                    {0, 0, 0},
			                    {0, 1, 0},
			                    50,
			                    CAMERA_PERSPECTIVE};
			    if (environment.own)
			    {
				    const Vector3 centre{environment.own->x / 1000.f, 0, -environment.own->y / 1000.f};
				    camera.position.x += centre.x;
				    camera.position.z += centre.z;
				    camera.target = centre;
			    }
			    BeginTextureMode(editor.scene_preview);
			    ClearBackground({8, 12, 13, 255});
			    try
			    {
				    editor.scene.draw(catalog, bodies, camera,
				                      {0, 0, static_cast<float>(width), static_cast<float>(height)}, light, 0,
				                      static_cast<SceneDiagnostic>(editor.diagnostic),
				                      {static_cast<float>(width), static_cast<float>(height)}, environment);
			    }
			    catch (...)
			    {
				    EndTextureMode();
				    throw;
			    }
			    EndTextureMode();
			    editor.preview_identity = identity;
		    });
	if (editor.scene_preview.id)
		ImGui::Image(static_cast<ImTextureID>(editor.scene_preview.texture.id),
		             {static_cast<float>(width), static_cast<float>(height)}, {0, 1}, {1, 0});
	ImGui::End();
}
void entity_properties(Json &entity)
{
	ImGui::TextWrapped("Reusable definition: %s", entity.at("id").get_ref<const std::string &>().c_str());
	const auto id = entity.at("id").get<std::string>();
	if (editor.rename_from != id)
		editor.template_id = editor.rename_from = id;
	ImGui::InputText(tool_label("Template ID").c_str(), &editor.template_id);
	if (ImGui::Button(tool_label("Rename template").c_str()))
		attempt(
		    [&]
		    {
			    editor.document.rename_entity(id, editor.template_id);
			    editor.rename_from = editor.template_id;
			    editor.status = "Template renamed; map instances and allowed ships updated.";
		    });
	string_field("Display name", entity, "name");
	if (entity.contains("extends"))
	{
		ImGui::TextWrapped("Inherited definition. Flatten before editing its resolved fields.");
		if (ImGui::Button(tool_label("Flatten definition").c_str()))
			attempt(
			    [&]
			    {
				    entity = editor.document.verify().entities.at(entity.at("id").get<std::string>());
			    });
		return;
	}
	auto &render = entity["render"];
	grid_properties(render);
	sprite_import(render);
	bool eight = render["mode"] == "eight";
	if (!render.contains("sprite"))
	{
		radar_icon_picker(render);
		if (ImGui::Checkbox(tool_label("Eight-direction billboard").c_str(), &eight))
		{
			render["mode"] = eight ? "eight" : "camera";
			std::string image = render["images"].empty() ? "" : render["images"][0].get<std::string>();
			render["images"] = Json::array();
			for (int i = 0; i < (eight ? 8 : 1); ++i)
				render["images"].push_back(image);
		}
		double scale = render.value("scale", 1.0);
		if (ImGui::InputDouble(tool_label("Render scale").c_str(), &scale))
			render["scale"] = scale;
		for (std::size_t i = 0; i < render["images"].size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			asset_picker(eight ? "Bearing image" : "Camera-facing image", render["images"][i]);
			ImGui::PopID();
		}
	}
	else
	{
		real_field("Render scale", render, "scale", 1, .001f, 1000000);
		material_properties(render["sprite"]);
		if (render.contains("engines") && ImGui::TreeNode(tool_label("Engine effect").c_str()))
		{
			real_field("Plume scale", render["engines"], "scale", .4, .001f, 10);
			real_field("Engine light intensity", render["engines"], "power", 2, .001f, 16);
			real_field("Engine light range (m)", render["engines"], "range", 32, .001f, 1000000);
			material_properties(render["engines"]["sprite"], false);
			ImGui::TreePop();
		}
	}
	auto &systems = entity["systems"];
	if (ImGui::CollapsingHeader(tool_label("Systems").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const char *name : {"engine", "steering", "radar"})
		{
			bool enabled = systems.contains(name);
			if (ImGui::Checkbox(name, &enabled))
			{
				if (!enabled)
					systems.erase(name);
				else if (std::string(name) == "engine")
					systems[name] = {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 3000}};
				else if (std::string(name) == "steering")
					systems[name] = {{"turn_mdeg_s", 45000}};
				else
					systems[name] = {{"range_mm", 200000}};
			}
			if (!enabled)
				continue;
			ImGui::PushID(name);
			if (std::string(name) == "engine")
			{
				integer_field("Max speed (mm/s)", systems[name], "max_speed_mm_s");
				integer_field("Acceleration (mm/s2)", systems[name], "acceleration_mm_s2");
			}
			if (std::string(name) == "steering")
				integer_field("Turn rate (millidegrees/s)", systems[name], "turn_mdeg_s");
			if (std::string(name) == "radar")
				integer_field("Range (mm)", systems[name], "range_mm");
			ImGui::PopID();
		}
	}
	if (ImGui::CollapsingHeader(tool_label("Collision").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto &collision = entity["collision"];
		bool box = collision["shape"] == "box";
		int shape = box ? 1 : 0;
		const auto shapes = localise("Circle") + '\0' + localise("Oriented box") + '\0';
		if (ImGui::Combo(tool_label("Collision type").c_str(), &shape, shapes.c_str()))
		{
			box = shape == 1;
			collision["shape"] = box ? "box" : "circle";
			if (box)
			{
				collision["half_width_mm"] = 2000;
				collision["half_length_mm"] = 4000;
				collision.erase("radius_mm");
			}
			else
			{
				collision["radius_mm"] = 2000;
				collision.erase("half_width_mm");
				collision.erase("half_length_mm");
			}
		}
		if (box)
		{
			integer_field("Half width (mm)", collision, "half_width_mm");
			integer_field("Half length (mm)", collision, "half_length_mm");
		}
		else
			integer_field("Radius (mm)", collision, "radius_mm");
		integer_field("Mass (grams; 0 = static)", collision, "mass_grams");
	}
	std::string console = entity.value("console", std::string{});
	std::string controller = entity.value("controller", std::string{});
	ImGui::TextWrapped(
	    "%s",
	    localise(
	        "AI behaviour runs automatic orders when this entity is not under player control. None leaves it idle.")
	        .c_str());
	if (ImGui::BeginCombo(tool_label("AI behaviour").c_str(),
	                      controller.empty() ? localise("None").c_str() : controller.c_str()))
	{
		if (ImGui::Selectable(tool_label("None").c_str(), controller.empty()))
			entity.erase("controller");
		for (const auto &value : editor.document.definitions()["trees"])
		{
			auto id = value.at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), id == controller))
				entity["controller"] = id;
		}
		for (const auto &[id, value] : editor.document.libraries().trees)
			if (ImGui::Selectable(id.c_str(), id == controller))
				entity["controller"] = id;
		ImGui::EndCombo();
	}
	if (ImGui::BeginCombo(tool_label("Console").c_str(), console.empty() ? "None" : console.c_str()))
	{
		if (ImGui::Selectable(tool_label("None").c_str(), console.empty()))
			entity.erase("console");
		for (const auto &value : editor.document.definitions()["consoles"])
		{
			auto id = value.at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), id == console))
				entity["console"] = id;
		}
		ImGui::EndCombo();
	}
}
void console_editor(const Application &app)
{
	auto &consoles = editor.document.definitions()["consoles"];
	if (ImGui::Button(tool_label("New console").c_str()))
	{
		std::string id =
		    editor.document.manifest().at("id").get<std::string>() + ".console" + std::to_string(consoles.size() + 1);
		consoles.push_back({{"id", id}, {"widgets", Json::array()}});
		editor.console = static_cast<int>(consoles.size() - 1);
	}
	for (std::size_t i = 0; i < consoles.size(); ++i)
	{
		auto id = consoles[i].at("id").get<std::string>();
		if (ImGui::Selectable(id.c_str(), editor.console == i))
			editor.console = static_cast<int>(i);
	}
	if (editor.console < 0 || editor.console >= static_cast<int>(consoles.size()))
		return;
	auto &widgets = consoles[editor.console]["widgets"];
	for (auto [kind, binding] : widget_bindings())
	{
		if (ImGui::Button((std::string("Add ") + kind).c_str()))
		{
			std::string id;
			unsigned count = 1;
			do
			{
				id = std::string(kind) + std::to_string(count++);
			} while (std::any_of(widgets.begin(), widgets.end(),
			                     [&](const auto &w)
			                     {
				                     return w["id"] == id;
			                     }));
			widgets.push_back({{"id", id},
			                   {"kind", kind},
			                   {"binding", binding},
			                   {"label", kind},
			                   {"x", 0},
			                   {"y", 0},
			                   {"width", 500},
			                   {"height", 500}});
		}
	}
	int remove = -1;
	for (std::size_t i = 0; i < widgets.size(); ++i)
	{
		auto &widget = widgets[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(widget.at("id").get_ref<const std::string &>().c_str()))
		{
			string_field("Semantic label", widget, "label");
			ImGui::TextWrapped("Binding: %s", widget["binding"].get_ref<const std::string &>().c_str());
			for (const char *key : {"x", "y", "width", "height"})
			{
				int value = widget[key];
				if (ImGui::SliderInt(key, &value, (std::string(key) == "x" || std::string(key) == "y") ? 0 : 1, 1000))
					widget[key] = value;
			}
			if (ImGui::Button(tool_label("Delete widget").c_str()))
				remove = static_cast<int>(i);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove >= 0)
		widgets.erase(widgets.begin() + remove);
	ImGui::SeparatorText(tool_label("Live Console preview").c_str());
	ImGui::TextWrapped("Sample data; controls affect only this preview.");
	static int preview_resolution = 0;
	ImGui::Combo(tool_label("Preview resolution").c_str(), &preview_resolution,
	             "1280 x 720\0001920 x 1080\0001080 x 1920\000");
	const int logical_width = preview_resolution == 1 ? 1920 : preview_resolution == 2 ? 1080 : 1280;
	const int logical_height = preview_resolution == 1 ? 1080 : preview_resolution == 2 ? 1920 : 720;
	const auto origin = ImGui::GetCursorScreenPos();
	const int width = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x), 100, 1600);
	const int height = std::max(1, width * logical_height / logical_width);
	const auto pointer = ImGui::GetIO().MousePos;
	const bool hovered = ImGui::IsWindowHovered() && pointer.x >= origin.x && pointer.y >= origin.y &&
	                     pointer.x < origin.x + width && pointer.y < origin.y + height;
	const auto texture = draw_console_preview(consoles[editor.console], logical_width, logical_height,
	                                          {hovered ? (pointer.x - origin.x) * logical_width / width : -100.f,
	                                           hovered ? (pointer.y - origin.y) * logical_height / height : -100.f},
	                                          hovered && ImGui::IsMouseClicked(0), ImGui::IsMouseReleased(0),
	                                          hovered ? ImGui::GetIO().MouseWheel : 0.f, app.pseudo_locale);
	ImGui::Image(static_cast<ImTextureID>(texture.id), {static_cast<float>(width), static_cast<float>(height)}, {0, 1},
	             {1, 0});
}
void slots_editor()
{
	auto &slots = editor.document.map()["ship_slots"];
	if (ImGui::Button(tool_label("Add ship slot").c_str()))
	{
		std::string id;
		unsigned count = 1;
		do
		{
			id = "slot" + std::to_string(count++);
		} while (std::any_of(slots.begin(), slots.end(),
		                     [&](const auto &slot)
		                     {
			                     return slot["id"] == id;
		                     }));
		slots.push_back({{"id", id}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}, {"allowed_ships", Json::array()}});
	}
	int remove = -1;
	for (std::size_t i = 0; i < slots.size(); ++i)
	{
		auto &slot = slots[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(slot["id"].get_ref<const std::string &>().c_str()))
		{
			string_field("Name", slot, "id");
			bool mandatory = slot.value("mandatory", false);
			if (ImGui::Checkbox(tool_label("Mandatory ship slot").c_str(), &mandatory))
				slot["mandatory"] = mandatory;
			transform_fields(slot);
			ImGui::TextUnformatted("Allowed ships:");
			for (const auto &entity : editor.document.definitions()["entities"])
			{
				std::string id = entity["id"];
				auto &allowed = slot["allowed_ships"];
				auto found = std::find(allowed.begin(), allowed.end(), Json(id));
				bool selected = found != allowed.end();
				if (ImGui::Checkbox(id.c_str(), &selected))
				{
					if (selected)
						allowed.push_back(id);
					else
						allowed.erase(found);
				}
			}
			for (const auto &[id, entity] : editor.document.libraries().entities)
			{
				auto &allowed = slot["allowed_ships"];
				auto found = std::find(allowed.begin(), allowed.end(), Json(id));
				bool selected = found != allowed.end();
				if (ImGui::Checkbox(id.c_str(), &selected))
				{
					if (selected)
						allowed.push_back(id);
					else
						allowed.erase(found);
				}
			}
			if (ImGui::Button(tool_label("Remove slot").c_str()))
				remove = static_cast<int>(i);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove >= 0)
		slots.erase(slots.begin() + remove);
}
void map_view()
{
	ImGui::Begin(tool_label("Map").c_str());
	if (ImGui::BeginCombo(tool_label("Map").c_str(),
	                      editor.document.map().at("id").get_ref<const std::string &>().c_str()))
	{
		auto &maps = editor.document.definitions()["maps"];
		for (std::size_t i = 0; i < maps.size(); ++i)
			if (ImGui::Selectable(maps[i].at("id").get_ref<const std::string &>().c_str()))
			{
				editor.document.select_map(i);
				editor.selected = 0;
			}
		ImGui::EndCombo();
	}
	ImGui::TextWrapped("Double-click: place selected definition | drag: move | middle-drag: pan | wheel: zoom");
	auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 1.0f);
	size.y = std::max(size.y, 1.0f);
	ImGui::InvisibleButton("map-canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	bool hovered = ImGui::IsItemHovered();
	auto &io = ImGui::GetIO();
	if (hovered)
		editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, io.MouseWheel), 0.00005f, 0.1f);
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		editor.pan_x += io.MouseDelta.x;
		editor.pan_y += io.MouseDelta.y;
	}
	ImVec2 centre{origin.x + size.x / 2 + editor.pan_x, origin.y + size.y / 2 + editor.pan_y};
	auto *draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
	draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(8, 15, 25, 255));
	float grid = 10000 * editor.zoom;
	while (grid < 25)
		grid *= 10;
	while (grid > 250)
		grid /= 10;
	float first_x = origin.x + std::fmod(centre.x - origin.x, grid),
	      first_y = origin.y + std::fmod(centre.y - origin.y, grid);
	for (float x = first_x; x < origin.x + size.x; x += grid)
		draw->AddLine({x, origin.y}, {x, origin.y + size.y}, IM_COL32(32, 58, 70, 255));
	for (float y = first_y; y < origin.y + size.y; y += grid)
		draw->AddLine({origin.x, y}, {origin.x + size.x, y}, IM_COL32(32, 58, 70, 255));
	std::uint64_t nearest = 0;
	float distance = 24;
	auto &definitions = editor.document.definitions()["entities"];
	for (const auto &instance : editor.document.map()["entities"])
	{
		const bool selected = instance["id"] == editor.selected;
		bool has_icon = false;
		ImVec2 p{centre.x + instance["x_mm"].get<float>() * editor.zoom,
		         centre.y - instance["y_mm"].get<float>() * editor.zoom};
		auto definition = std::find_if(definitions.begin(), definitions.end(),
		                               [&](const auto &entity)
		                               {
			                               return entity["id"] == instance["definition"];
		                               });
		if (definition != definitions.end() && definition->contains("render"))
		{
			const auto &render = (*definition)["render"];
			const auto icon = render_radar_icon(render);
			const auto sprite = editor.texture(radar_sprite_reference(render));
			has_icon = sprite.id || icon != RadarIcon::contact;
			if (has_icon)
				map_radar_icon(draw, icon, p, instance["heading_mdeg"].get<float>(),
				               selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(180, 225, 240, 255), sprite);
			if (!has_icon && render.contains("sprite"))
			{
				const auto &sprite = render.at("sprite");
				const auto texture = editor.texture(sprite.at("albedo"));
				if (texture.id)
				{
					const auto &frame = sprite.at("frames").at(0), &rect = frame.at("rect");
					const float half = std::clamp(render.value("scale", 1.f) * 16, 2.f, 200.f),
					            aspect = rect[2].get<float>() / rect[3].get<float>();
					const ImVec2 uv0{rect[0].get<float>() / texture.width, rect[1].get<float>() / texture.height};
					const ImVec2 uv1{(rect[0].get<float>() + rect[2].get<float>()) / texture.width,
					                 (rect[1].get<float>() + rect[3].get<float>()) / texture.height};
					draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half * aspect, p.y - half},
					               {p.x + half * aspect, p.y + half}, uv0, uv1);
				}
			}
			if (!has_icon && render.contains("images") && !render["images"].empty() && render["images"][0].is_string())
			{
				auto texture = editor.texture(render["images"][0]);
				if (texture.id)
				{
					float half = std::clamp(render.value("scale", 1.0f) * 16, 2.0f, 200.0f);
					draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half, p.y - half},
					               {p.x + half, p.y + half});
				}
			}
		}
		if (selected || !has_icon)
			draw->AddCircle(p, selected ? 14.0f : 8.0f,
			                selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(118, 201, 232, 255));
		float angle = instance["heading_mdeg"].get<float>() * 3.14159265f / 180000;
		if (!has_icon)
			draw->AddLine(p, {p.x + 20 * std::cos(angle), p.y - 20 * std::sin(angle)}, IM_COL32(118, 201, 232, 255));
		float d = std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y);
		if (d < distance)
		{
			distance = d;
			nearest = instance["id"];
		}
	}
	for (const auto &slot : editor.document.map()["ship_slots"])
	{
		const ImVec2 point{centre.x + slot["x_mm"].get<float>() * editor.zoom,
		                   centre.y - slot["y_mm"].get<float>() * editor.zoom};
		map_radar_icon(draw, RadarIcon::ship, point, slot["heading_mdeg"].get<float>(), IM_COL32(125, 165, 180, 255));
		draw->AddText({point.x + 14, point.y}, IM_COL32(125, 165, 180, 255),
		              slot.at("id").get_ref<const std::string &>().c_str());
	}
	draw->PopClipRect();
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && editor.definition >= 0 &&
	    editor.definition < static_cast<int>(definitions.size()))
	{
		editor.selected = editor.document.place(definitions[editor.definition]["id"],
		                                        static_cast<std::int64_t>((io.MousePos.x - centre.x) / editor.zoom),
		                                        static_cast<std::int64_t>((centre.y - io.MousePos.y) / editor.zoom));
	}
	else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		editor.selected = nearest;
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		if (auto *instance = selected_instance())
		{
			(*instance)["x_mm"] = static_cast<std::int64_t>((io.MousePos.x - centre.x) / editor.zoom);
			(*instance)["y_mm"] = static_cast<std::int64_t>((centre.y - io.MousePos.y) / editor.zoom);
		}
	}
	ImGui::End();
}
} // namespace
void close_editor()
{
	editor.imports.reset();
	editor.clear_textures();
}
void open_workshop_pack(const std::string &path)
{
	editor.imports.reset();
	editor.document.load_pack(read_file(path));
	editor.started = true;
	editor.clear_textures();
	editor.pack_name = editor.document.manifest()["id"].get<std::string>();
	editor.definition = editor.document.definitions()["entities"].empty() ? -1 : 0;
	editor.console = editor.document.definitions()["consoles"].empty() ? -1 : 0;
	editor.selected =
	    editor.document.map()["entities"].empty() ? 0 : editor.document.map()["entities"][0]["id"].get<std::uint64_t>();
	editor.status = "Loaded and verified " + path;
}
void render_editor(Application &app)
{
	auto base_archive = []
	{
#ifdef __EMSCRIPTEN__
		return read_file("/assets/base.zip");
#else
		return read_file(std::string(GetApplicationDirectory()) + "assets/base.zip");
#endif
	};
	if (!editor.started && app.screen == Screen::workshop)
	{
		editor.started = true;
		attempt(
		    [&]
		    {
			    if (editor.document.manifest().at("id") != "phoenix-base")
				    editor.document.load_dependency(base_archive());
		    });
	}
	static ImVec2 dock_size{};
	auto dock = ImGui::GetID("PhoenixDock");
	auto *viewport = ImGui::GetMainViewport();
	if (dock_size.x != viewport->WorkSize.x || dock_size.y != viewport->WorkSize.y)
	{
		dock_size = viewport->WorkSize;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
		ImGuiID centre = dock, left, right, bottom;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.10f, &bottom, &centre);
		ImGui::DockBuilderDockWindow(tool_label("Status").c_str(), bottom);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, &left, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.38f, &right, &centre);
		ImGui::DockBuilderDockWindow(tool_label("Entities").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Definitions").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Properties").c_str(), right);
		ImGui::DockBuilderDockWindow(tool_label("Consoles").c_str(), right);
		ImGui::DockBuilderDockWindow(tool_label("Ship slots").c_str(), right);
		ImGui::DockBuilderDockWindow(tool_label("Map").c_str(), centre);
		ImGui::DockBuilderDockWindow(tool_label("Lighting and sprite preview").c_str(), centre);
		ImGui::DockBuilderFinish(dock);
	}
	ImGui::DockSpaceOverViewport(dock, viewport);
	bool readonly = app.screen == Screen::gm;
	attempt(
	    [&]
	    {
		    if (auto imported = readonly ? std::nullopt : editor.imports.take();
		        imported && imported->status != ImportStatus::Cancelled)
		    {
			    if (!imported->error.empty())
				    throw std::runtime_error(imported->error);
			    if (!readonly)
			    {
				    if (imported->purpose == FilePurpose::WorkshopPack)
				    {
					    editor.document.load_pack(imported->bytes);
					    editor.clear_textures();
					    editor.selected = 0;
					    editor.definition = -1;
					    editor.console = editor.document.definitions()["consoles"].empty() ? -1 : 0;
					    editor.pack_name = editor.document.manifest()["id"];
				    }
				    if (imported->purpose == FilePurpose::WorkshopLibrary)
				    {
					    editor.document.load_dependency(imported->bytes);
					    editor.clear_textures();
				    }
				    if (imported->purpose == FilePurpose::WorkshopMap)
				    {
					    editor.document.load_map(imported->bytes);
					    editor.selected = 0;
				    }
				    if (imported->purpose == FilePurpose::WorkshopImage)
				    {
					    editor.document.import_image(imported->filename, std::move(imported->bytes));
					    editor.clear_textures();
				    }
				    if (imported->purpose == FilePurpose::WorkshopSprite)
				    {
					    const auto sidecar = parse_json(imported->bytes, "sprite sidecar");
					    if (!sidecar.is_object() || !sidecar.contains("files") || !sidecar.at("files").is_object())
						    throw std::runtime_error("Sprite sidecar requires a files object");
					    editor.pending_sidecar = sidecar;
					    editor.import_engines = sidecar.contains("engine_effect");
					    editor.sidecar_images.clear();
					    for (const char *key : {"sprite", "normal", "depth", "radar_icon"})
					    {
						    const auto file = editor.pending_sidecar.at("files").value(key, Json());
						    if (!file.is_string())
							    continue;
						    const auto filename = file.get<std::string>();
						    const auto leaf = filename.substr(filename.find_last_of("/\\") == std::string::npos
						                                          ? 0
						                                          : filename.find_last_of("/\\") + 1);
						    const auto path = "assets/" + leaf;
						    if (editor.document.pack().files.contains(path))
							    editor.sidecar_images[key] =
							        editor.document.manifest().at("id").get<std::string>() + ":" + path;
						    else
						    {
							    std::vector<std::string> matches;
							    for (const auto &[asset, bytes] : editor.document.pack().files)
								    if (asset.starts_with("assets/") && asset.ends_with("/" + leaf))
									    matches.push_back(editor.document.manifest().at("id").get<std::string>() + ":" +
									                      asset);
							    for (const auto &[reference, bytes] : editor.document.libraries().assets)
								    if (reference.ends_with("/" + leaf))
									    matches.push_back(reference);
							    if (matches.size() == 1)
								    editor.sidecar_images[key] = matches.front();
						    }
					    }
				    }
				    editor.status = "Imported " + imported->filename;
			    }
		    }
	    });
	if (ImGui::BeginMainMenuBar())
	{
		if (!app.embedded_tools && ImGui::BeginMenu(tool_label("Phoenix").c_str()))
		{
			if (ImGui::MenuItem(tool_label(game_active(app) ? "Settings" : "Home").c_str()))
				invoke_navigation(app, game_active(app) ? "settings" : "home");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(tool_label("File").c_str(), !readonly))
		{
			if (ImGui::MenuItem(tool_label("New pack").c_str()))
			{
				editor.imports.reset();
				editor.document.reset();
				attempt(
				    [&]
				    {
					    editor.document.load_dependency(base_archive());
				    });
				editor.pending_sidecar = nullptr;
				editor.sidecar_images.clear();
				editor.clear_textures();
				editor.selected = 0;
				editor.definition = -1;
				editor.console = -1;
				editor.pack_name = "workshop";
			}
			if (ImGui::MenuItem(tool_label("Open pack...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(editor.imports, FilePurpose::WorkshopPack);
				    });
			if (ImGui::MenuItem(tool_label("Open base game pack").c_str()))
				attempt(
				    [&]
				    {
					    WorkshopDocument candidate;
					    candidate.load_pack(base_archive());
					    editor.document = std::move(candidate);
					    editor.clear_textures();
					    editor.pack_name = "phoenix-base";
					    editor.definition = 0;
					    editor.console = 0;
					    editor.selected = 0;
					    editor.pending_sidecar = nullptr;
					    editor.status = "Opened the base game for editing. Export to save a separate pack.";
				    });
			if (ImGui::MenuItem(tool_label("Load dependency pack...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(editor.imports, FilePurpose::WorkshopLibrary);
				    });
			if (ImGui::MenuItem(tool_label("Export verified pack...").c_str()))
				attempt(
				    [&]
				    {
					    export_file(editor.document.manifest()["id"].get<std::string>() + ".zip",
					                editor.document.export_pack());
					    editor.status = "Pack verified and exported.";
				    });
			ImGui::Separator();
			if (ImGui::MenuItem(tool_label("New map").c_str()))
				attempt(
				    [&]
				    {
					    editor.document.new_map("workshop.map" +
					                            std::to_string(editor.document.definitions()["maps"].size() + 1));
				    });
			if (ImGui::MenuItem(tool_label("Load map...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(editor.imports, FilePurpose::WorkshopMap);
				    });
			if (ImGui::MenuItem(tool_label("Save map...").c_str()))
				attempt(
				    [&]
				    {
					    export_file(editor.document.map()["id"].get<std::string>() + ".json",
					                editor.document.save_map());
				    });
			if (ImGui::MenuItem(tool_label("Import PNG...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(editor.imports, FilePurpose::WorkshopImage);
				    });
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(tool_label("Verify").c_str(), nullptr, false, !readonly))
			attempt(
			    [&]
			    {
				    editor.document.verify();
				    editor.status = "Definitions, PNGs, map references, slots and widget bindings verified.";
			    });
		ImGui::EndMainMenuBar();
	}
	if (readonly)
	{
		const auto state = app.session ? app.session->inspection() : Json(nullptr);
		ImGui::Begin(tool_label("Status").c_str());
		if (!app.status.empty())
			ImGui::TextWrapped("%s", app.status.c_str());
		ImGui::TextWrapped("%s", app.session ? app.session->connection_status().c_str()
		                                     : "Connect to a ship server to request read-only inspection.");
		ImGui::End();
		if (state.is_null())
		{
			ImGui::Begin(tool_label("Map").c_str());
			ImGui::TextWrapped("Waiting for host approval. Live world data is unavailable.");
			ImGui::End();
			return;
		}
		ImGui::Begin(tool_label("Entities").c_str());
		for (const auto &value : state.at("bodies"))
		{
			const auto &body = value.at("body");
			const auto id = body.at("id").get<std::uint64_t>();
			const auto label = std::to_string(id) + " / " + body.at("definition").get<std::string>();
			if (ImGui::Selectable(label.c_str(), editor.selected == id))
				editor.selected = id;
		}
		ImGui::End();
		ImGui::Begin(tool_label("Properties").c_str());
		for (const auto &value : state.at("bodies"))
			if (value.at("body").at("id") == editor.selected)
			{
				const auto details = value.dump(2);
				ImGui::TextUnformatted(details.c_str());
			}
		ImGui::End();
		ImGui::Begin(tool_label("Map").c_str());
		ImGui::Text("Committed tick %llu / read only", static_cast<unsigned long long>(app.session->ticks()));
		const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("Live map", ImVec2(std::max(1.f, size.x), std::max(1.f, size.y)),
		                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
		if (ImGui::IsItemHovered())
		{
			auto &io = ImGui::GetIO();
			editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, io.MouseWheel), 0.000001f, 1.f);
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
			{
				editor.pan_x += io.MouseDelta.x;
				editor.pan_y += io.MouseDelta.y;
			}
		}
		auto *draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		for (const auto &value : state.at("bodies"))
		{
			const auto &body = value.at("body");
			const auto id = body.at("id").get<std::uint64_t>();
			ImVec2 point(origin.x + size.x / 2 + editor.pan_x + body.at("position").at("x").get<float>() * editor.zoom,
			             origin.y + size.y / 2 + editor.pan_y - body.at("position").at("y").get<float>() * editor.zoom);
			RadarIcon icon = RadarIcon::contact;
			Texture2D sprite{};
			const auto &entities = app.session->catalog().entities;
			const auto definition = entities.find(body.at("definition").get<std::string>());
			if (definition != entities.end())
			{
				icon = render_radar_icon(definition->second.at("render"));
				const auto reference = radar_sprite_reference(definition->second.at("render"));
				if (!reference.empty())
					sprite = editor.textures.load(reference, app.session->catalog().assets.at(reference));
			}
			map_radar_icon(draw, icon, point, body.at("heading").get<float>(),
			               editor.selected == id ? IM_COL32(255, 220, 100, 255) : IM_COL32(180, 225, 240, 255), sprite);
			auto label = std::to_string(id);
			draw->AddText(ImVec2(point.x + 14, point.y), IM_COL32_WHITE, label.c_str());
			const auto mouse = ImGui::GetIO().MousePos;
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			    std::hypot(mouse.x - point.x, mouse.y - point.y) < 12)
				editor.selected = id;
		}
		draw->PopClipRect();
		ImGui::End();
		return;
	}
	ImGui::Begin(tool_label("Entities").c_str());
	for (const auto &instance : editor.document.map()["entities"])
	{
		auto label = std::to_string(instance.at("id").get<std::uint64_t>()) + " / " +
		             instance.at("definition").get<std::string>();
		if (ImGui::Selectable(label.c_str(), editor.selected == instance["id"]))
			editor.selected = instance["id"];
	}
	if (editor.selected)
	{
		if (ImGui::Button(tool_label("Duplicate").c_str()))
			editor.document.duplicate(editor.selected);
		if (ImGui::Button(tool_label("Delete").c_str()))
		{
			editor.document.erase(editor.selected);
			editor.selected = 0;
		}
	}
	ImGui::End();
	ImGui::Begin(tool_label("Definitions").c_str());
	ImGui::InputText(tool_label("Pack ID").c_str(), &editor.pack_name);
	if (ImGui::Button(tool_label("Apply pack ID").c_str()))
		attempt(
		    [&]
		    {
			    editor.document.rename_pack(editor.pack_name);
			    editor.clear_textures();
		    });
	if (ImGui::Button(tool_label("Import PNG").c_str()))
		attempt(
		    [&]
		    {
			    request_file(editor.imports, FilePurpose::WorkshopImage);
		    });
	if (ImGui::Button(tool_label("New entity definition").c_str()))
	{
		editor.document.create_entity();
		editor.definition = static_cast<int>(editor.document.definitions()["entities"].size() - 1);
		editor.selected = 0;
	}
	auto &definitions = editor.document.definitions()["entities"];
	for (std::size_t i = 0; i < definitions.size(); ++i)
	{
		auto id = definitions[i].at("id").get<std::string>();
		const auto name = definitions[i].value("name", std::string{});
		const auto label = (name.empty() ? id : name + " (" + id + ")") + "###" + id;
		if (ImGui::Selectable(label.c_str(), editor.definition == i))
		{
			editor.definition = static_cast<int>(i);
			editor.selected = 0;
		}
	}
	if (!editor.document.libraries().entities.empty() &&
	    ImGui::CollapsingHeader(tool_label("Dependency entities").c_str()))
	{
		for (const auto &[id, entity] : editor.document.libraries().entities)
			if (ImGui::Selectable(id.c_str(), editor.library_parent == id))
				editor.library_parent = id;
		if (!editor.library_parent.empty() && ImGui::Button(tool_label("Create derived entity").c_str()))
			attempt(
			    [&]
			    {
				    editor.document.create_entity(editor.library_parent);
				    editor.definition = static_cast<int>(definitions.size() - 1);
				    editor.selected = 0;
			    });
	}
	if (ImGui::CollapsingHeader(tool_label("Override dependency definitions").c_str()))
	{
		ImGui::TextWrapped("Copy a dependency definition into this pack and explicitly declare its override.");
		auto overrides = [&](const char *kind, const auto &table)
		{
			for (const auto &[id, value] : table)
				if (ImGui::Button((std::string("Override ") + kind + " / " + id).c_str()))
				{
					auto &local = editor.document.definitions()[kind];
					const bool exists = std::any_of(local.begin(), local.end(),
					                                [&](const auto &entry)
					                                {
						                                return entry.at("id") == id;
					                                });
					if (exists)
					{
						editor.status = "This definition is already editable in the pack: " + id;
						continue;
					}
					local.push_back(value);
					auto &declared = editor.document.manifest()["overrides"];
					if (!declared.is_array())
						declared = Json::array();
					if (std::find(declared.begin(), declared.end(), Json(id)) == declared.end())
						declared.push_back(id);
					editor.status = "Explicit override added: " + id;
				}
		};
		overrides("entities", editor.document.libraries().entities);
		overrides("maps", editor.document.libraries().maps);
		overrides("consoles", editor.document.libraries().consoles);
		overrides("trees", editor.document.libraries().trees);
	}
	ImGui::Separator();
	ImGui::TextWrapped("%s", editor.status.c_str());
	ImGui::End();
	ImGui::Begin(tool_label("Properties").c_str());
	if (auto *instance = selected_instance())
	{
		ImGui::Text("Instance %llu", static_cast<unsigned long long>(editor.selected));
		transform_fields(*instance);
		ImGui::Separator();
		for (std::size_t i = 0; i < definitions.size(); ++i)
			if (definitions[i]["id"] == (*instance)["definition"])
				editor.definition = static_cast<int>(i);
	}
	if (editor.definition >= 0 && editor.definition < static_cast<int>(definitions.size()))
		entity_properties(definitions[editor.definition]);
	else
		ImGui::TextWrapped("Create or select a reusable entity definition.");
	ImGui::End();
	ImGui::Begin(tool_label("Consoles").c_str());
	console_editor(app);
	ImGui::End();
	ImGui::Begin(tool_label("Ship slots").c_str());
	slots_editor();
	ImGui::End();
	map_view();
	lighting_preview();
	ImGui::Begin(tool_label("Status").c_str());
	if (!app.status.empty())
		ImGui::TextWrapped("%s", app.status.c_str());
	ImGui::TextWrapped("%s", editor.status.c_str());
	ImGui::End();
}
} // namespace phoenix
