module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

module phoenix.workshop.map;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.documents;
import phoenix.workshop.asset_preview;
import phoenix.workshop.map_navigation;

namespace phoenix
{
struct MapEditor::State
{
	MapGesture gesture;
	bool touch = false;
	float pan_x = 0, pan_y = 0, zoom = .004f;
	WorkshopAssetPreview assets;
	std::optional<Catalog> catalog;
	std::string content;
};
MapEditor::MapEditor() : state_(std::make_unique<State>())
{
}
MapEditor::~MapEditor() = default;
void MapEditor::clear()
{
	state_->gesture.contacts.clear();
	state_->assets.clear();
	state_->catalog.reset();
	state_->content.clear();
}
void add_workshop_map(WorkshopDocument &document, MapEditorSelection &selection, ToolMessage &status)
{
	const auto prefix = document.manifest().at("id").get<std::string>() + ".map";
	unsigned number = 1;
	std::string id;
	const auto &maps = document.definitions().at("maps");
	do
	{
		id = prefix + std::to_string(number++);
	} while (std::any_of(maps.begin(), maps.end(),
	                     [&](const Json &value)
	                     {
		                     return value.at("id") == id;
	                     }) ||
	         document.libraries().maps.contains(id));
	document.new_map(id);
	document.map()["name"] = "New map";
	selection.instance = 0;
	status = "Map added to the current pack. Save the pack to keep it with its templates and assets.";
}

void MapEditor::draw(WorkshopDocument &document, MapEditorSelection &selection, ToolMessage &status, bool saving,
                     const ToolFrame &frame, const std::function<void()> &save_pack)
{
	auto attempt = [&](auto action)
	{
		try
		{
			action();
		}
		catch (const std::exception &exception)
		{
			status = exception.what();
		}
	};
	auto selected_instance = [&]() -> Json *
	{
		for (auto &instance : document.map()["entities"])
			if (instance["id"] == selection.instance)
				return &instance;
		return nullptr;
	};
	tool_window(tool_label("Map").c_str());
	ImGui::TextWrapped("%s: %s", localise("Editing pack").c_str(),
	                   document.manifest().at("id").get_ref<const std::string &>().c_str());
	ImGui::BeginDisabled(saving);
	if (tool_button(tool_label("Add map").c_str()))
		attempt(
		    [&]
		    {
			    add_workshop_map(document, selection, status);
		    });
	ImGui::SameLine();
	if (tool_button(tool_label("Save pack").c_str()))
		attempt(
		    [&]
		    {
			    save_pack();
		    });
	ImGui::EndDisabled();
	if (ImGui::BeginCombo(tool_label("Map").c_str(), document.map().at("id").get_ref<const std::string &>().c_str()))
	{
		auto &maps = document.definitions()["maps"];
		for (std::size_t i = 0; i < maps.size(); ++i)
			if (ImGui::Selectable(maps[i].at("id").get_ref<const std::string &>().c_str()))
			{
				document.select_map(i);
				selection.instance = 0;
			}
		ImGui::EndCombo();
	}
	edit_field(tool_label("Map name").c_str(), document.map(), "name", std::string{});
	{
		auto &map = document.map();
		Json skybox = map.value("presentation", Json::object()).value("skybox", Json(""));
		state_->assets.pick(document, "Skybox", skybox, true);
		if (skybox.is_string() && !skybox.get<std::string>().empty())
			map["presentation"]["skybox"] = skybox;
		else if (map.contains("presentation"))
			map["presentation"].erase("skybox");
	}
	{
		Json background = document.map().value("loading_background", Json(""));
		state_->assets.pick(document, "Scenario loading background", background);
		if (background.is_string() && !background.get<std::string>().empty())
			document.map()["loading_background"] = background;
		else
			document.map().erase("loading_background");
	}
	ImGui::TextWrapped(
	    "%s", localise("Double-click: place selected definition | drag: move | middle-drag: pan | wheel: zoom | touch: "
	                   "drag to pan, pinch to zoom")
	              .c_str());
	auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 1.0f);
	size.y = std::max(size.y, 1.0f);
	ImGui::InvisibleButton("map-canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	if (!IsWindowFocused())
		state_->gesture.contacts.clear();
	state_->touch = !frame.pointers.empty() || !state_->gesture.contacts.empty() ||
	                ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
	for (auto event : frame.pointers)
	{
		event.x -= frame.area.x;
		event.y -= frame.area.y;
		const bool inside = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		                    event.x >= origin.x && event.y >= origin.y && event.x < origin.x + size.x &&
		                    event.y < origin.y + size.y;
		state_->gesture.event(event, inside, origin.x + size.x / 2, origin.y + size.y / 2, .00005f, .1f, state_->pan_x,
		                      state_->pan_y, state_->zoom);
	}
	bool hovered = ImGui::IsItemHovered();
	auto &io = ImGui::GetIO();
	if (hovered)
		state_->zoom = std::clamp(state_->zoom * std::pow(1.15f, io.MouseWheel), 0.00005f, 0.1f);
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		state_->pan_x += io.MouseDelta.x;
		state_->pan_y += io.MouseDelta.y;
	}
	ImVec2 centre{origin.x + size.x / 2 + state_->pan_x, origin.y + size.y / 2 + state_->pan_y};
	auto *draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
	draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(8, 15, 25, 255));
	float grid = 10000 * state_->zoom;
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
	for (const auto &region : document.map().value("regions", Json::array()))
	{
		const ImVec2 p{centre.x + region.value("x_mm", 0.f) * state_->zoom,
		               centre.y - region.value("y_mm", 0.f) * state_->zoom};
		const auto colour = region.contains("nebula") ? IM_COL32(160, 120, 220, 180) : IM_COL32(180, 145, 95, 180);
		const auto shape = region.value("shape", std::string("circle"));
		if (shape == "rectangle")
		{
			const auto w = region.value("half_width_mm", 10000.f) * state_->zoom;
			const auto h = region.value("half_height_mm", 10000.f) * state_->zoom;
			draw->AddRect({p.x - w, p.y - h}, {p.x + w, p.y + h}, colour);
		}
		else
		{
			draw->AddCircle(p, region.value("radius_mm", 10000.f) * state_->zoom, colour, 64);
			if (shape == "annulus")
				draw->AddCircle(p, region.value("inner_radius_mm", 5000.f) * state_->zoom, colour, 64);
		}
		draw->AddText(p, colour, region.value("id", std::string("region")).c_str());
	}
	std::uint64_t nearest = 0;
	float distance = 24;
	const auto map_content = document.definitions().dump() + document.manifest().dump();
	if (map_content != state_->content)
	{
		state_->content = map_content;
		state_->catalog.reset();
		attempt(
		    [&]
		    {
			    state_->catalog = document.verify();
		    });
	}
	auto &definitions = document.definitions()["entities"];
	for (const auto &instance : document.map()["entities"])
	{
		const bool selected = selection.kind == "Entity" && instance["id"] == selection.instance;
		bool has_icon = false;
		ImVec2 p{centre.x + instance["x_mm"].get<float>() * state_->zoom,
		         centre.y - instance["y_mm"].get<float>() * state_->zoom};
		auto definition = std::find_if(definitions.begin(), definitions.end(),
		                               [&](const auto &entity)
		                               {
			                               return entity["id"] == instance["definition"];
		                               });
		const Json *resolved = definition == definitions.end() ? nullptr : &*definition;
		if (state_->catalog)
			if (const auto found = state_->catalog->entities.find(instance.at("definition").get<std::string>());
			    found != state_->catalog->entities.end())
				resolved = &found->second;
		if (resolved && resolved->contains("render"))
			attempt(
			    [&]
			    {
				    const auto &render = resolved->at("render");
				    const auto icon = render_radar_icon(render);
				    const auto sprite = state_->assets.texture(document, radar_sprite_reference(render));
				    has_icon = sprite.id || icon != RadarIcon::contact;
				    if (has_icon)
					    draw_tool_radar_icon(draw, icon, p, instance["heading_mdeg"].get<float>(),
					                         selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(180, 225, 240, 255),
					                         sprite);
				    if (!has_icon && render.contains("sprite") && state_->catalog)
				    {
					    const auto material =
					        compile_sprite(render.at("sprite"), *state_->catalog, instance.at("definition"));
					    const auto entry = sprite_entry_index(
					        material, instance.value("sprite_entry", render.value("sprite_entry", std::string{})));
					    const auto &frame =
					        sprite_frame(material, entry, -instance.at("heading_mdeg").get<float>() / 1000.f);
					    const auto &rect = frame.rect;
					    const auto texture = state_->assets.texture(document, material.albedo);
					    if (texture.id)
					    {
						    const float half = std::clamp(render.value("scale", 1.f) * 16, 2.f, 200.f),
						                aspect = static_cast<float>(rect[2]) / static_cast<float>(rect[3]);
						    const ImVec2 uv0{static_cast<float>(rect[0]) / texture.width,
						                     static_cast<float>(rect[1]) / texture.height};
						    const ImVec2 uv1{
						        (static_cast<float>(rect[0]) + static_cast<float>(rect[2])) / texture.width,
						        (static_cast<float>(rect[1]) + static_cast<float>(rect[3])) / texture.height};
						    draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half * aspect, p.y - half},
						                   {p.x + half * aspect, p.y + half}, uv0, uv1);
					    }
				    }
				    if (!has_icon && render.contains("images") && !render["images"].empty() &&
				        render["images"][0].is_string())
				    {
					    auto texture = state_->assets.texture(document, render["images"][0]);
					    if (texture.id)
					    {
						    float half = std::clamp(render.value("scale", 1.0f) * 16, 2.0f, 200.0f);
						    draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half, p.y - half},
						                   {p.x + half, p.y + half});
					    }
				    }
			    });
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
	for (const auto &slot : document.map()["ship_slots"])
	{
		const ImVec2 point{centre.x + slot["x_mm"].get<float>() * state_->zoom,
		                   centre.y - slot["y_mm"].get<float>() * state_->zoom};
		draw_tool_radar_icon(draw, RadarIcon::ship, point, slot["heading_mdeg"].get<float>(),
		                     IM_COL32(125, 165, 180, 255));
		draw->AddText({point.x + 14, point.y}, IM_COL32(125, 165, 180, 255),
		              slot.at("id").get_ref<const std::string &>().c_str());
	}
	draw->PopClipRect();
	if (!state_->touch && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selection.definition >= 0 &&
	    selection.definition < static_cast<int>(definitions.size()))
	{
		selection.kind = "Entity";
		selection.instance = document.place(definitions[selection.definition]["id"],
		                                    static_cast<std::int64_t>((io.MousePos.x - centre.x) / state_->zoom),
		                                    static_cast<std::int64_t>((centre.y - io.MousePos.y) / state_->zoom));
	}
	else if (!state_->touch && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		selection.instance = nearest;
		if (nearest)
			selection.kind = "Entity";
	}
	if (!state_->touch && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		if (auto *instance = selected_instance())
		{
			(*instance)["x_mm"] = static_cast<std::int64_t>((io.MousePos.x - centre.x) / state_->zoom);
			(*instance)["y_mm"] = static_cast<std::int64_t>((centre.y - io.MousePos.y) / state_->zoom);
		}
	}
	ImGui::End();
}
} // namespace phoenix
