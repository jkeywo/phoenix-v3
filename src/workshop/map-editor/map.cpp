module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

module phoenix.workshop.map;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.documents;
import phoenix.workshop.authoring_core;
import phoenix.workshop.asset_preview;
import phoenix.workshop.map_navigation;

namespace phoenix
{
struct MapEditor::State
{
	MapGesture gesture;
	bool touch = false, selecting_touch = false, dragging = false, box = false;
	ImVec2 start{}, current{};
	std::map<std::uint64_t, std::pair<std::int64_t, std::int64_t>> positions;
	std::set<std::uint64_t> prior;
	std::string map_id;
	ImVec2 touch_point{};
	bool touch_blocked = false;
	std::array<float, 4> canvas{};
	float pan_x = 0, pan_y = 0, zoom = .004f;
	WorkshopAssetPreview assets;
	std::optional<Catalog> catalog;
	std::string content;
};
MapEditor::MapEditor() : state_(std::make_unique<State>())
{
}
MapEditor::~MapEditor() = default;
bool MapEditor::editing() const
{
	return state_->dragging;
}
void MapEditor::touch_editing(bool enabled)
{
	state_->selecting_touch = enabled;
}
std::array<float, 4> MapEditor::canvas_bounds() const
{
	return state_->canvas;
}
void MapEditor::clear()
{
	state_->gesture.contacts.clear();
	state_->dragging = false;
	state_->positions.clear();
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
	(void)workshop_apply(document, "map.create", {{"id", id}});
	selection.clear_selection();
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
	if (!tool_window(tool_label("Map & Scenario").c_str()))
	{
		ImGui::End();
		return;
	}
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
	ImGui::SameLine();
	ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x - 60.f));
	if (ImGui::BeginCombo(tool_label("Map").c_str(), document.map().at("id").get_ref<const std::string &>().c_str()))
	{
		auto &maps = document.definitions()["maps"];
		for (std::size_t i = 0; i < maps.size(); ++i)
			if (ImGui::Selectable(maps[i].at("id").get_ref<const std::string &>().c_str()))
			{
				document.select_map(i);
				selection.clear_selection();
			}
		ImGui::EndCombo();
	}
	if (tool_button(tool_label("Scenario properties").c_str()))
		selection.kind = "Scenario";
	ImGui::SameLine();
	if (tool_button(tool_label("Ship slots").c_str()))
		selection.kind = "Ship slots";
	ImGui::SameLine();
	ImGui::Checkbox(tool_label("Touch select/move").c_str(), &state_->selecting_touch);
	if (ImGui::CollapsingHeader(tool_label("Map settings and help").c_str()))
	{
		edit_shared_field(tool_label("Map name").c_str(), document.map(), "map-editor.map.name");
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
		    "%s",
		    localise("Double-click: place selected definition | drag: move | middle-drag: pan | wheel: zoom | touch: "
		             "drag to pan, pinch to zoom")
		        .c_str());
	}

	auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 1.0f);
	size.y = std::max(size.y, 1.0f);
	state_->canvas = {origin.x, origin.y, size.x, size.y};
	ImGui::InvisibleButton("map-canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	if (!IsWindowFocused())
		state_->gesture.contacts.clear();
	state_->touch = !frame.pointers.empty() || !state_->gesture.contacts.empty() ||
	                ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
	bool touch_pressed = false, touch_released = false, touch_cancel = false;
	for (auto event : frame.pointers)
	{
		event.x -= frame.area.x;
		event.y -= frame.area.y;
		const bool inside =
		    event.x >= origin.x && event.y >= origin.y && event.x < origin.x + size.x && event.y < origin.y + size.y;
		const auto before = state_->gesture.contacts.size();
		const auto px = state_->pan_x, py = state_->pan_y;
		state_->gesture.event(event, inside, origin.x + size.x / 2, origin.y + size.y / 2, .00005f, .1f, state_->pan_x,
		                      state_->pan_y, state_->zoom);
		if (state_->selecting_touch)
		{
			const auto after = state_->gesture.contacts.size();
			if (before < 2 && after < 2)
			{
				state_->pan_x = px;
				state_->pan_y = py;
			}
			if (after > 1)
			{
				state_->touch_blocked = true;
				touch_cancel = true;
			}
			if (!state_->touch_blocked && (before == 1 || after == 1))
			{
				state_->touch_point = {event.x, event.y};
				touch_pressed = touch_pressed || (event.phase == ToolPointerSample::down && before == 0 && after == 1);
				touch_released = touch_released || event.phase == ToolPointerSample::up;
				touch_cancel = touch_cancel || event.phase == ToolPointerSample::cancel;
			}
			if (after == 0)
				state_->touch_blocked = false;
		}
	}
	bool hovered = ImGui::IsItemHovered();
	auto &io = ImGui::GetIO();
	const auto pointer = state_->touch && state_->selecting_touch ? state_->touch_point : io.MousePos;
	const bool pressed = state_->touch ? touch_pressed : ImGui::IsMouseClicked(0);
	const bool held =
	    state_->touch ? !state_->gesture.contacts.empty() && !state_->touch_blocked : ImGui::IsMouseDown(0);
	const bool moved = std::hypot(pointer.x - state_->start.x, pointer.y - state_->start.y) >= io.MouseDragThreshold;
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
	std::map<std::uint64_t, std::pair<ImVec2, ImVec2>> bounds;
	const auto map_content = std::to_string(document.revision());
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
		const bool selected =
		    selection.kind == "Entity" && selection.instances.contains(instance.at("id").get<std::uint64_t>());
		bool has_icon = false;
		ImVec2 p{centre.x + instance["x_mm"].get<float>() * state_->zoom,
		         centre.y - instance["y_mm"].get<float>() * state_->zoom};
		ImVec2 half_size{8, 8};
		auto definition = std::find_if(definitions.begin(), definitions.end(),
		                               [&](const auto &entity)
		                               {
			                               return entity["id"] == instance["definition"];
		                               });
		const Json *resolved = definition == definitions.end() ? nullptr : &*definition;
		if (state_->catalog)
			for (const auto &compiled :
			     state_->catalog->maps.at(document.map().at("id").get<std::string>()).at("entities"))
				if (compiled.at("id") == instance.at("id"))
				{
					resolved = &state_->catalog->entities.at(compiled.at("definition").get<std::string>());
					break;
				}
		if (resolved && resolved->contains("render"))
			attempt(
			    [&]
			    {
				    const auto &render = resolved->at("render");
				    const auto icon = render_radar_icon(render);
				    const auto sprite = state_->assets.texture(document, radar_sprite_reference(render));
				    has_icon = sprite.id || icon != RadarIcon::contact;
				    if (has_icon && !render.contains("sprite") && !render.contains("images"))
					    draw_tool_radar_icon(draw, icon, p, instance["heading_mdeg"].get<float>(),
					                         selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(180, 225, 240, 255),
					                         sprite);
				    if (render.contains("sprite") && state_->catalog)
				    {
					    const auto material = compile_sprite(render.at("sprite"), *state_->catalog, resolved->at("id"));
					    const auto entry = sprite_entry_index(
					        material, instance.value("sprite_entry", render.value("sprite_entry", std::string{})));
					    const auto &frame =
					        sprite_frame(material, entry, -instance.at("heading_mdeg").get<float>() / 1000.f);
					    const auto &rect = frame.rect;
					    const auto texture = state_->assets.texture(document, material.albedo);
					    if (texture.id)
					    {
						    const float half = render.value("scale", 1.f) * 500.f * state_->zoom,
						                aspect = static_cast<float>(rect[2]) / static_cast<float>(rect[3]);
						    const ImVec2 uv0{static_cast<float>(rect[0]) / texture.width,
						                     static_cast<float>(rect[1]) / texture.height};
						    const ImVec2 uv1{
						        (static_cast<float>(rect[0]) + static_cast<float>(rect[2])) / texture.width,
						        (static_cast<float>(rect[1]) + static_cast<float>(rect[3])) / texture.height};
						    half_size = {half * aspect, half};
						    has_icon = true;
						    draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half * aspect, p.y - half},
						                   {p.x + half * aspect, p.y + half}, uv0, uv1);
					    }
				    }
				    if (!render.contains("sprite") && render.contains("images") && !render["images"].empty() &&
				        render["images"][0].is_string())
				    {
					    const auto image_count = render["images"].size();
					    const auto direction =
					        image_count == 8
					            ? static_cast<std::size_t>((360000 - instance.at("heading_mdeg").get<int>() + 22500) %
					                                       360000 / 45000)
					            : 0;
					    auto texture = state_->assets.texture(document, render["images"][direction]);
					    if (texture.id)
					    {
						    float half = render.value("scale", 1.0f) * 500.f * state_->zoom;
						    half_size = {half * texture.width / texture.height, half};
						    has_icon = true;
						    draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half_size.x, p.y - half},
						                   {p.x + half_size.x, p.y + half});
					    }
				    }
			    });
		if (selected || !has_icon || half_size.x < 8.f || half_size.y < 8.f)
			draw->AddCircle(p, selected ? 14.0f : 8.0f,
			                selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(118, 201, 232, 255));
		float angle = instance["heading_mdeg"].get<float>() * 3.14159265f / 180000;
		if (!has_icon)
			draw->AddLine(p, {p.x + 20 * std::cos(angle), p.y - 20 * std::sin(angle)}, IM_COL32(118, 201, 232, 255));
		half_size.x = std::max(8.f, half_size.x);
		half_size.y = std::max(8.f, half_size.y);
		bounds[instance.at("id").get<std::uint64_t>()] = {{p.x - half_size.x, p.y - half_size.y},
		                                                  {p.x + half_size.x, p.y + half_size.y}};
		if (selected)
			draw->AddRect(bounds.at(instance.at("id").get<std::uint64_t>()).first,
			              bounds.at(instance.at("id").get<std::uint64_t>()).second, IM_COL32(255, 205, 90, 255));
		float d = std::hypot(p.x - pointer.x, p.y - pointer.y);
		if (std::abs(p.x - pointer.x) <= half_size.x && std::abs(p.y - pointer.y) <= half_size.y)
			d = std::min(d, 23.f);
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
	if (state_->map_id != document.map().at("id").get<std::string>())
	{
		if (!state_->map_id.empty())
			selection.clear_selection();
		state_->map_id = document.map().at("id");
		state_->dragging = false;
	}
	for (auto it = selection.instances.begin(); it != selection.instances.end();)
		if (!bounds.contains(*it))
			it = selection.instances.erase(it);
		else
			++it;
	if (!selection.instances.contains(selection.instance))
		selection.instance = selection.instances.empty() ? 0 : *selection.instances.begin();
	const bool pointer_edit = !state_->touch || state_->selecting_touch;
	if (!state_->touch && pointer_edit && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !nearest &&
	    selection.definition >= 0 && selection.definition < static_cast<int>(definitions.size()))
	{
		selection.select(document.place(definitions[selection.definition]["id"],
		                                static_cast<std::int64_t>((pointer.x - centre.x) / state_->zoom),
		                                static_cast<std::int64_t>((centre.y - pointer.y) / state_->zoom)));
		state_->dragging = false;
	}
	else if (pointer_edit && (hovered || touch_pressed) && pressed)
	{
		state_->prior = selection.instances;
		if (nearest)
			selection.kind = "Entity";
		if (nearest && (!selection.instances.contains(nearest) || io.KeyShift))
			selection.select(nearest, io.KeyShift);
		else if (!nearest && !io.KeyShift)
			selection.clear_selection();
		state_->start = state_->current = pointer;
		state_->box = !nearest;
		state_->dragging = true;
		state_->positions.clear();
		for (const auto &entity : document.map()["entities"])
			if (selection.instances.contains(entity.at("id").get<std::uint64_t>()))
				state_->positions[entity.at("id").get<std::uint64_t>()] = {entity.at("x_mm"), entity.at("y_mm")};
	}
	if (state_->dragging)
	{
		state_->current = pointer;
		const bool cancel = touch_cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) || !IsWindowFocused();
		if (!state_->box)
		{
			const bool moved =
			    std::hypot(pointer.x - state_->start.x, pointer.y - state_->start.y) >= io.MouseDragThreshold;
			std::int64_t dx = static_cast<std::int64_t>(std::llround((pointer.x - state_->start.x) / state_->zoom)),
			             dy = -static_cast<std::int64_t>(std::llround((pointer.y - state_->start.y) / state_->zoom));
			for (const auto &[id, p] : state_->positions)
			{
				dx = std::clamp(dx, -1000000000LL - p.first, 1000000000LL - p.first);
				dy = std::clamp(dy, -1000000000LL - p.second, 1000000000LL - p.second);
			}
			if (moved || cancel)
				for (auto &entity : document.map()["entities"])
					if (const auto found = state_->positions.find(entity.at("id").get<std::uint64_t>());
					    found != state_->positions.end())
					{
						entity["x_mm"] = found->second.first + (cancel ? 0 : dx);
						entity["y_mm"] = found->second.second + (cancel ? 0 : dy);
					}
		}
		else if (moved)
		{
			ImVec2 lo{std::min(state_->start.x, pointer.x), std::min(state_->start.y, pointer.y)},
			    hi{std::max(state_->start.x, pointer.x), std::max(state_->start.y, pointer.y)};
			draw->AddRectFilled(lo, hi, IM_COL32(80, 170, 230, 35));
			draw->AddRect(lo, hi, IM_COL32(80, 170, 230, 255));
			selection.instances = io.KeyShift ? state_->prior : std::set<std::uint64_t>{};
			for (const auto &[id, b] : bounds)
				if (b.second.x >= lo.x && b.first.x <= hi.x && b.second.y >= lo.y && b.first.y <= hi.y)
					selection.instances.insert(id);
			selection.instance = selection.instances.empty() ? 0 : *selection.instances.begin();
			selection.kind = "Entity";
		}
		if (cancel)
		{
			selection.instances = state_->prior;
			selection.instance = selection.instances.empty() ? 0 : *selection.instances.begin();
		}
		if (cancel || !held || touch_released)
			state_->dragging = false;
	}
	ImGui::End();
}
} // namespace phoenix
