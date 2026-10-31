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
import phoenix.workshop.authoring_core;
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

void WorkshopWorkspace::State::bulk_instance_properties()
{
	ImGui::Text("%zu %s", selection.instances.size(), localise("selected instances").c_str());
	static std::int64_t dx = 0, dy = 0;
	ImGui::InputScalar(tool_label("Offset X (mm)").c_str(), ImGuiDataType_S64, &dx);
	ImGui::InputScalar(tool_label("Offset Y (mm)").c_str(), ImGuiDataType_S64, &dy);
	if (tool_button(tool_label("Translate selection").c_str()))
		attempt(
		    [&]
		    {
			    document.translate_instances(selection.instances, dx, dy);
			    dx = dy = 0;
		    });
	Json descriptions;
	attempt(
	    [&]
	    {
		    descriptions = workshop_instance_fields(document, selection.instances);
	    });
	if (descriptions.is_null())
		return;
	std::string signature = document.map().at("id").get<std::string>() + ":";
	for (const auto id : selection.instances)
		signature += std::to_string(id) + ",";
	if (signature != bulk_selection || (bulk_revision != document.revision() && !ImGui::IsAnyItemActive()))
	{
		bulk_values.clear();
		bulk_selection = signature;
		bulk_revision = document.revision();
	}
	std::function<void(const Json &)> fields;
	fields = [&](const Json &rows)
	{
		for (const auto &field : rows)
		{
			const auto path = field.at("path").get<std::string>();
			if (field.contains("children"))
			{
				if (ImGui::TreeNode(path.c_str(), "%s", field.at("label").get_ref<const std::string &>().c_str()))
				{
					fields(field.at("children"));
					ImGui::TreePop();
				}
				continue;
			}
			const auto first = field.at("value");
			const bool mixed = field.at("mixed");
			if (!bulk_values.contains(path))
				bulk_values[path] = first;
			auto &value = bulk_values.at(path);
			ImGui::PushID(path.c_str());
			if (mixed)
				ImGui::TextUnformatted(localise("Mixed").c_str());
			const auto title = field.at("label").get<std::string>();
			bool changed = false;
			const auto field_label = tool_label(title);
			if (title == "sprite_entry")
			{
				if (ImGui::BeginCombo(field_label.c_str(),
				                      mixed ? localise("Mixed").c_str() : value.get_ref<const std::string &>().c_str()))
				{
					for (const auto &entry : field.at("choices").get<std::vector<std::string>>())
						if (ImGui::Selectable(entry.empty() ? tool_label("Default").c_str() : entry.c_str(),
						                      value == entry))
						{
							value = entry;
							changed = true;
						}
					ImGui::EndCombo();
				}
			}
			else if (value.is_string())
			{
				auto v = value.get<std::string>();
				changed = edit_value(field_label.c_str(), v);
				value = v;
			}
			else if (value.is_boolean())
			{
				auto v = value.get<bool>();
				changed = edit_value(field_label.c_str(), v);
				value = v;
			}
			else if (value.is_number_integer() && title != "scale")
			{
				auto v = value.get<std::int64_t>();
				changed = edit_value(field_label.c_str(), v);
				value = v;
			}
			else if (value.is_number())
			{
				auto v = value.get<double>();
				changed = edit_value(field_label.c_str(), v);
				value = v;
			}
			if ((changed && (value.is_boolean() || title == "sprite_entry")) || ImGui::IsItemDeactivatedAfterEdit())
				attempt(
				    [&]
				    {
					    workshop_instance_field(document, selection.instances, path, value);
				    });
			ImGui::PopID();
		}
	};
	attempt(
	    [&]
	    {
		    fields(descriptions);
	    });
	std::set<std::string> overridden;
	for (const auto &instance : document.map()["entities"])
		if (selection.instances.contains(instance.at("id").get<std::uint64_t>()))
			for (const auto &[key, value] : instance.value("overrides", Json::object()).items())
				overridden.insert(key);
	for (const auto &key : overridden)
	{
		ImGui::PushID(key.c_str());
		if (tool_button((localise("Revert override") + " " + key).c_str()))
			attempt(
			    [&]
			    {
				    document.edit_instances(selection.instances, Json::object(), Json::object(), {key});
			    });
		ImGui::PopID();
	}
	if (tool_button(tool_label("Revert all instance overrides").c_str()))
		attempt(
		    [&]
		    {
			    document.edit_instances(selection.instances, Json::object(), Json::object(), {}, true);
		    });
}
Json *WorkshopWorkspace::State::selected_instance()
{
	if (!selection.instance || document.definitions().at("maps").empty())
		return nullptr;
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
		(void)workshop_custom_action(material, "radar.icon", {{"value", current}});
	}
	if (tool_button(tool_label("Clear radar icon").c_str()))
		(void)workshop_custom_action(material, "radar.icon", {{"value", nullptr}});

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
		edit_shared_field(tool_label(key).c_str(), sprite, (std::string("material.") + key).c_str());
		const bool enabled = sprite.contains(key);

		if (enabled)
			assets.pick(document, key, sprite[key]);
		ImGui::PopID();
	}
	edit_shared_field(tool_label("Emissive artwork").c_str(), sprite, "material.emissive");
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
			(void)workshop_custom_action(sprite, "sprite.entry.duplicate");
		}
		return;
	}
	if (!sprite.contains("frames"))
		return;
	if (tool_button(tool_label("Use named sprite entries").c_str()))
	{
		(void)workshop_custom_action(sprite, "sprite.entries");
		return;
	}
	for (std::size_t i = 0; i < sprite["frames"].size(); ++i)
	{
		auto &frame = sprite["frames"][i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(frame.at("name").get_ref<const std::string &>().c_str()))
		{
			string_field("Frame name", frame, "name");
			edit_shared_field(tool_label("Mirror horizontally").c_str(), frame, "frame.mirror");
			edit_shared_field(tool_label("Atlas rectangle").c_str(), frame, "frame.rect");
			edit_shared_field(tool_label("Frame pivot").c_str(), frame, "frame.pivot");
			edit_shared_field(tool_label("Projected forward axis").c_str(), frame, "frame.forward");
			edit_shared_control(tool_label("Camera azimuth").c_str(), frame, "frame.azimuth", {{"frame", i}});

			edit_shared_field(tool_label("Depth span / height").c_str(), frame, "workflow.appearance.depth_scale");
			edit_shared_field(tool_label("Depth centre / height").c_str(), frame, "workflow.appearance.depth_offset");
			if (!frame.contains("attachments"))
				frame["attachments"] = workshop_seed("workflow.appearance.attachments");
			std::string remove;
			for (auto it = frame["attachments"].begin(); it != frame["attachments"].end(); ++it)
			{
				ImGui::PushID(it.key().c_str());
				auto &a = it.value();
				ImGui::TextUnformatted(it.key().c_str());
				edit_shared_control(tool_label("Marker X").c_str(), frame, "frame.marker.x",
				                    {{"attachment", it.key()}});
				edit_shared_control(tool_label("Marker Y").c_str(), frame, "frame.marker.y",
				                    {{"attachment", it.key()}});
				integer_field("Signed layer", a, "layer");
				edit_shared_field(tool_label("Nozzle depth offset").c_str(), a,
				                  "workflow.appearance.depth_offset.bounded");
				if (tool_button(tool_label("Remove attachment").c_str()))
					remove = it.key();
				ImGui::PopID();
			}
			if (!remove.empty())
				frame["attachments"].erase(remove);
			if (tool_button(tool_label("Add engine attachment").c_str()))
			{
				(void)workshop_custom_action(frame, "sprite.attachment.add");
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
	edit_shared_field(tool_label("Imported eight-direction billboard").c_str(), sidecar, "sidecar.billboard");
	edit_shared_field(tool_label("Imported render scale").c_str(), sidecar, "workflow.appearance.render_scale");
	if (tool_button(tool_label("Apply sprite material").c_str()))
		attempt(
		    [&]
		    {
			    workshop_sprite_apply(document, render, pending_sidecar, Json(sidecar_images), import_engines);
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
	if (tool_button(tool_label("Open ship setup").c_str()))
		gameplay.open_ship(id);
	if (rename_from != id)
		template_id = rename_from = id;
	tool_prepare_action("Rename template");
	ImGui::InputText(tool_label("Template ID").c_str(), &template_id);
	tool_action_line("Rename template");
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
				    (void)workshop_apply(document, "flatten", {{"kind", "entities"}, {"id", entity.at("id")}});
			    });
		return;
	}
	auto &render = entity["render"];
	draw_reference_grid_properties(render);
	if (render.contains("model") || render.contains("star"))
	{
		edit_shared_field(tool_label("Procedural star").c_str(), render, "render.procedural");
		radar_icon_picker(render);
		edit_shared_field(tool_label("Render scale").c_str(), render, "workflow.appearance.scale");

		if (render.contains("model"))
		{
			string_field("Model asset", render["model"], "asset");
			if (tool_button(tool_label("Import GLB model...").c_str()))
				request_file(imports, FilePurpose::WorkshopModel);
			edit_shared_field(tool_label("Model offset").c_str(), render["model"], "model.offset");
			edit_shared_field(tool_label("Model rotation (degrees)").c_str(), render["model"], "model.rotation");
			edit_shared_field(tool_label("Model scale").c_str(), render["model"], "model.scale");
		}
		else
		{
			auto &star = render["star"];
			for (const char *key : {"surface_colour", "hot_colour", "cell_colour", "halo_colour"})
				edit_shared_field(tool_label(key).c_str(), star, (std::string("star.") + key).c_str());
			edit_shared_field(tool_label("Animation speed").c_str(), star, "workflow.appearance.animation_speed");
			edit_shared_field(tool_label("Corona radius multiplier").c_str(), star,
			                  "workflow.appearance.halo_radius_multiplier");
		}
		if (!render.contains("attachments"))
			render["attachments"] = workshop_seed("workflow.appearance.attachments");
		std::string remove;
		for (auto &[name, a] : render["attachments"].items())
		{
			ImGui::PushID(name.c_str());
			ImGui::TextUnformatted(name.c_str());
			edit_shared_field(tool_label("Attachment position").c_str(), a, "attachment.position");
			edit_shared_field(tool_label("Attachment direction").c_str(), a, "attachment.direction");
			if (tool_button(tool_label("Remove attachment").c_str()))
				remove = name;
			ImGui::PopID();
		}
		if (!remove.empty())
			render["attachments"].erase(remove);
		if (tool_button(tool_label("Add engine attachment").c_str()))
		{
			(void)workshop_custom_action(render, "model.attachment.add");
		}
	}
	else
	{
		sprite_import(render);
		bool eight = render["mode"] == "eight";
		if (!render.contains("sprite"))
		{
			radar_icon_picker(render);
			edit_shared_field(tool_label("Eight-direction billboard").c_str(), render, "render.billboard");
			eight = render.at("mode") == "eight";
			edit_shared_field(tool_label("Render scale").c_str(), render, "render.scale");

			for (std::size_t i = 0; i < render["images"].size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				assets.pick(document, eight ? "Bearing image" : "Camera-facing image", render["images"][i]);
				ImGui::PopID();
			}
		}
		else
		{
			edit_shared_field(tool_label("Render scale").c_str(), render, "workflow.appearance.scale");
			string_field("Sprite entry", render, "sprite_entry");
			material_properties(render["sprite"]);
			if (render.contains("engines") && ImGui::TreeNode(tool_label("Engine effect").c_str()))
			{
				edit_shared_field(tool_label("Plume scale").c_str(), render["engines"],
				                  "workflow.appearance.scale.bounded");
				edit_shared_field(tool_label("Engine light intensity").c_str(), render["engines"],
				                  "workflow.appearance.power");
				edit_shared_field(tool_label("Engine light range (m)").c_str(), render["engines"],
				                  "workflow.appearance.range");
				material_properties(render["engines"]["sprite"], false);
				ImGui::TreePop();
			}
		}
	}
	if (ImGui::CollapsingHeader(tool_label("Collision").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto &collision = entity["collision"];
		bool box = collision["shape"] == "box";
		edit_shared_field(tool_label("Collision type").c_str(), collision, "collision.shape");
		box = collision.at("shape") == "box";

		if (box)
		{
			integer_field("Half width (mm)", collision, "half_width_mm");
			integer_field("Half length (mm)", collision, "half_length_mm");
		}
		else
			integer_field("Radius (mm)", collision, "radius_mm");
		integer_field("Mass (grams; 0 = static)", collision, "mass_grams");
	}
	ImGui::TextWrapped(
	    "%s", localise("AI behaviour belongs to Console Controllers. Edit their trees and complexity profiles below.")
	              .c_str());
	edit_shared_choice(tool_label("Console").c_str(), entity, "console",
	                   document.definition_choices(WorkshopDefinitionKind::Console), true);
}
} // namespace phoenix
