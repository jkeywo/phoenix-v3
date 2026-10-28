module;
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.workshop.workflow;
import phoenix.content.assets;
import phoenix.workshop.map;
import phoenix.workshop.layout;
import phoenix.workshop.asset_preview;
import phoenix.workshop.material_preview;
import phoenix.workshop.gameplay;
import phoenix.ui.preview;

namespace phoenix
{
#include "state.hpp"
void WorkshopWorkspace::State::string_field(const char *label, Json &object, const char *key)
{
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	edit_field((std::string("##") + label).c_str(), object, key, std::string{});
}

void WorkshopWorkspace::State::integer_field(const char *label, Json &object, const char *key)
{
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	edit_field((std::string("##") + label).c_str(), object, key, std::int64_t{0});
}

void WorkshopWorkspace::State::transform_fields(Json &value)
{
	integer_field("X (mm)", value, "x_mm");
	integer_field("Y (mm)", value, "y_mm");
	integer_field("Heading (millidegrees)", value, "heading_mdeg");
	if (value.contains("definition"))
		string_field("Sprite entry override", value, "sprite_entry");
}

Json *WorkshopWorkspace::State::selected_instance()
{
	for (auto &instance : document.map()["entities"])
		if (instance["id"] == selection.instance)
			return &instance;
	return nullptr;
}

void WorkshopWorkspace::State::radar_icon_picker(Json &material)
{
	Json current = material.value("radar_sprite", Json(""));
	assets.pick(document, "Radar icon PNG", current);
	if (current != material.value("radar_sprite", Json("")))
	{
		material["radar_sprite"] = current;
		material.erase("radar_icon");
	}
	if (tool_button(tool_label("Clear radar icon").c_str()))
	{
		material.erase("radar_sprite");
		material.erase("radar_icon");
	}
	if (current.is_string() && !current.get<std::string>().empty())
	{
		const auto texture = assets.texture(document, current.get<std::string>());
		if (texture.id)
			ImGui::Image(static_cast<ImTextureID>(texture.id), {32, 32});
	}
}

void WorkshopWorkspace::State::material_properties(Json &sprite, bool with_radar_icon)
{
	if (with_radar_icon)
		radar_icon_picker(sprite);
	assets.pick(document, "Albedo PNG", sprite["albedo"]);
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
			assets.pick(document, key, sprite[key]);
		ImGui::PopID();
	}
	bool emissive = sprite.value("emissive", false);
	if (ImGui::Checkbox(tool_label("Emissive artwork").c_str(), &emissive))
		sprite["emissive"] = emissive;
	if (sprite.contains("entries"))
	{
		for (auto &entry : sprite["entries"])
		{
			ImGui::PushID(&entry);
			string_field("Entry name", entry, "name");
			auto nested = sprite;
			nested.erase("entries");
			nested["frames"] = entry.at("frames");
			if (ImGui::TreeNode(tool_label("Entry frames").c_str()))
			{
				material_properties(nested, false);
				entry["frames"] = nested.at("frames");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (tool_button(tool_label("Duplicate sprite entry").c_str()) && !sprite["entries"].empty())
		{
			auto entry = sprite["entries"].back();
			entry["name"] = "entry-" + std::to_string(sprite["entries"].size() + 1);
			sprite["entries"].push_back(std::move(entry));
		}
		return;
	}
	if (!sprite.contains("frames"))
		return;
	if (tool_button(tool_label("Use named sprite entries").c_str()))
	{
		sprite["entries"] = Json::array({{{"name", "default"}, {"frames", sprite.at("frames")}}});
		sprite.erase("frames");
		return;
	}
	for (std::size_t i = 0; i < sprite["frames"].size(); ++i)
	{
		auto &frame = sprite["frames"][i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(frame.at("name").get_ref<const std::string &>().c_str()))
		{
			string_field("Frame name", frame, "name");
			bool mirror_x = frame.value("mirror_x", false);
			if (ImGui::Checkbox(tool_label("Mirror horizontally").c_str(), &mirror_x))
				frame["mirror_x"] = mirror_x;
			int rect[4];
			for (int k = 0; k < 4; k++)
				rect[k] = frame["rect"][k];
			if (ImGui::InputInt4(tool_label("Atlas rectangle").c_str(), rect))
				frame["rect"] = {rect[0], rect[1], rect[2], rect[3]};
			float pivot[2] = {frame["pivot"][0], frame["pivot"][1]};
			if (ImGui::InputFloat2(tool_label("Frame pivot").c_str(), pivot))
				frame["pivot"] = {pivot[0], pivot[1]};
			const auto axis = frame.value("forward", Json::array({0, -1}));
			float forward[2] = {axis[0], axis[1]};
			if (ImGui::InputFloat2(tool_label("Projected forward axis").c_str(), forward))
				frame["forward"] = {forward[0], forward[1]};
			tool_real_field("Camera azimuth", frame, "azimuth", static_cast<float>(i) * 45, 0, 359.999f);
			tool_real_field("Depth span / height", frame, "depth_scale", 0, 0, 16);
			tool_real_field("Depth centre / height", frame, "depth_offset", 0, -16, 16);
			if (!frame.contains("attachments"))
				frame["attachments"] = Json::object();
			std::string remove;
			for (auto it = frame["attachments"].begin(); it != frame["attachments"].end(); ++it)
			{
				ImGui::PushID(it.key().c_str());
				auto &a = it.value();
				ImGui::TextUnformatted(it.key().c_str());
				tool_real_field("Marker X", a, "x", 0, 0, static_cast<float>(std::max(0, rect[2])));
				tool_real_field("Marker Y", a, "y", 0, 0, static_cast<float>(std::max(0, rect[3])));
				integer_field("Signed layer", a, "layer");
				tool_real_field("Nozzle depth offset", a, "depth_offset", 0, -16, 16);
				if (tool_button(tool_label("Remove attachment").c_str()))
					remove = it.key();
				ImGui::PopID();
			}
			if (!remove.empty())
				frame["attachments"].erase(remove);
			if (tool_button(tool_label("Add engine attachment").c_str()))
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

void WorkshopWorkspace::State::sprite_import(Json &render)
{
	if (tool_button(tool_label("Import sprite sidecar...").c_str()))
		request_file(imports, FilePurpose::WorkshopSprite);
	if (!pending_sidecar.is_object())
		return;
	ImGui::TextWrapped(
	    "%s", localise("Import the PNG files first, then map each source image to an imported asset.").c_str());
	for (const char *key : {"sprite", "normal", "depth", "radar_icon"})
		if (!pending_sidecar.at("files").value(key, Json()).is_null())
		{
			ImGui::PushID(key);
			ImGui::TextWrapped("%s", pending_sidecar.at("files").at(key).get_ref<const std::string &>().c_str());
			Json current = sidecar_images.contains(key) ? Json(sidecar_images.at(key)) : Json("");
			assets.pick(document, key, current);
			if (current.is_string() && !current.get<std::string>().empty())
				sidecar_images[key] = current.get<std::string>();
			ImGui::PopID();
		}
	ImGui::Checkbox(tool_label("Import as engine effect").c_str(), &import_engines);
	auto &sidecar = pending_sidecar;
	const auto sheet = sidecar.value("sprite_sheet", Json());
	const bool inferred_eight = sheet.is_object() && sheet.contains("frames") && sheet.at("frames").size() == 8;
	bool eight = sidecar.value("billboard_mode", std::string(inferred_eight ? "eight" : "camera")) == "eight";
	if (ImGui::Checkbox(tool_label("Imported eight-direction billboard").c_str(), &eight))
		sidecar["billboard_mode"] = eight ? "eight" : "camera";
	tool_real_field("Imported render scale", sidecar, "render_scale", 1, .001f, 1000000);
	if (tool_button(tool_label("Apply sprite material").c_str()))
		attempt(
		    [&]
		    {
			    auto catalog = document.libraries();
			    const auto prefix = document.manifest().at("id").get<std::string>() + ":";
			    for (const auto &[path, bytes] : document.pack().files)
				    if (path.starts_with("assets/"))
					    catalog.assets[prefix + path] = bytes;
			    apply_sprite_sidecar(render, pending_sidecar, sidecar_images, catalog, import_engines);
			    document.definitions()["schema"] = 3;
			    pending_sidecar = nullptr;
			    clear_textures();
			    status = "Sprite sidecar imported. Calibrate depth in the live preview.";
		    });
}

void WorkshopWorkspace::State::entity_properties(Json &entity)
{
	string_field("Default faction (optional)", entity, "faction");
	ImGui::TextWrapped("%s",
	                   ToolMessage("Reusable definition: {id}", {{"id", entity.at("id").get<std::string>()}}).c_str());
	const auto id = entity.at("id").get<std::string>();
	if (rename_from != id)
		template_id = rename_from = id;
	ImGui::InputText(tool_label("Template ID").c_str(), &template_id);
	if (tool_button(tool_label("Rename template").c_str()))
		attempt(
		    [&]
		    {
			    document.rename_entity(id, template_id);
			    rename_from = template_id;
			    status = "Template renamed; map instances and allowed ships updated.";
		    });
	string_field("Display name", entity, "name");
	if (entity.contains("extends"))
	{
		ImGui::TextWrapped("%s", localise("Inherited definition. Flatten before editing its resolved fields.").c_str());
		if (tool_button(tool_label("Flatten definition").c_str()))
			attempt(
			    [&]
			    {
				    entity = document.verify().entities.at(entity.at("id").get<std::string>());
			    });
		return;
	}
	auto &render = entity["render"];
	draw_reference_grid_properties(render);
	if (render.contains("model") || render.contains("star"))
	{
		bool procedural = render.contains("star");
		if (ImGui::Checkbox(tool_label("Procedural star").c_str(), &procedural))
		{
			if (procedural)
			{
				render.erase("model");
				render["star"] = Json::object();
			}
			else
			{
				render.erase("star");
				render["model"] = {{"asset", ""}};
			}
		}
		radar_icon_picker(render);
		tool_real_field("Render scale", render, "scale", 1, .001f, 1000000);
		auto vector_edit = [&](const char *label, Json &object, const char *key, std::array<float, 3> fallback)
		{
			auto value = object.value(key, fallback);
			if (ImGui::InputFloat3(tool_label(label).c_str(), value.data()))
				object[key] = value;
		};
		if (render.contains("model"))
		{
			string_field("Model asset", render["model"], "asset");
			if (tool_button(tool_label("Import GLB model...").c_str()))
				request_file(imports, FilePurpose::WorkshopModel);
			vector_edit("Model offset", render["model"], "offset", {});
			vector_edit("Model rotation (degrees)", render["model"], "rotation", {});
			vector_edit("Model scale", render["model"], "scale", {1, 1, 1});
		}
		else
		{
			auto &star = render["star"];
			for (const char *key : {"surface_colour", "hot_colour", "cell_colour", "halo_colour"})
				vector_edit(key, star, key, {1, .7f, .2f});
			tool_real_field("Animation speed", star, "animation_speed", 1, .001f, 16);
			tool_real_field("Corona radius multiplier", star, "halo_radius_multiplier", 2.4f, .001f, 16);
		}
		if (!render.contains("attachments"))
			render["attachments"] = Json::object();
		std::string remove;
		for (auto &[name, a] : render["attachments"].items())
		{
			ImGui::PushID(name.c_str());
			ImGui::TextUnformatted(name.c_str());
			vector_edit("Attachment position", a, "position", {});
			vector_edit("Attachment direction", a, "direction", {0, 0, 1});
			if (tool_button(tool_label("Remove attachment").c_str()))
				remove = name;
			ImGui::PopID();
		}
		if (!remove.empty())
			render["attachments"].erase(remove);
		if (tool_button(tool_label("Add engine attachment").c_str()))
		{
			const auto key = "engine_" + std::to_string(render["attachments"].size() + 1);
			render["attachments"][key] = {{"position", {0, 0, 0}}, {"direction", {0, 0, 1}}};
		}
	}
	else
	{
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
				assets.pick(document, eight ? "Bearing image" : "Camera-facing image", render["images"][i]);
				ImGui::PopID();
			}
		}
		else
		{
			tool_real_field("Render scale", render, "scale", 1, .001f, 1000000);
			string_field("Sprite entry", render, "sprite_entry");
			material_properties(render["sprite"]);
			if (render.contains("engines") && ImGui::TreeNode(tool_label("Engine effect").c_str()))
			{
				tool_real_field("Plume scale", render["engines"], "scale", .4, .001f, 10);
				tool_real_field("Engine light intensity", render["engines"], "power", 2, .001f, 16);
				tool_real_field("Engine light range (m)", render["engines"], "range", 32, .001f, 1000000);
				material_properties(render["engines"]["sprite"], false);
				ImGui::TreePop();
			}
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
	ImGui::TextWrapped(
	    "%s", localise("AI behaviour belongs to Console Controllers. Edit their trees and complexity profiles below.")
	              .c_str());
	if (ImGui::BeginCombo(tool_label("Console").c_str(), console.empty() ? "None" : console.c_str()))
	{
		if (ImGui::Selectable(tool_label("None").c_str(), console.empty()))
			entity.erase("console");
		for (const auto &value : document.definitions()["consoles"])
		{
			auto id = value.at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), id == console))
				entity["console"] = id;
		}
		ImGui::EndCombo();
	}
}
} // namespace phoenix
