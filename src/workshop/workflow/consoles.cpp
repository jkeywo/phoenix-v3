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
import phoenix.content.documents;
import phoenix.content.ui;
import phoenix.workshop.map;
import phoenix.workshop.layout;
import phoenix.workshop.asset_preview;
import phoenix.workshop.material_preview;
import phoenix.workshop.gameplay;
import phoenix.ui.preview;

namespace phoenix
{
#include "state.hpp"
void WorkshopWorkspace::State::console_editor(WorkshopEnvironment &environment, bool inspector)
{
	auto &consoles = document.definitions()["consoles"];
	if (!inspector)
	{
		if (tool_button(tool_label("New console").c_str()))
		{
			const auto prefix = document.manifest().at("id").get<std::string>() + ".console";
			std::string id;
			unsigned number = 1;
			do
			{
				id = prefix + std::to_string(number++);
			} while (std::any_of(consoles.begin(), consoles.end(),
			                     [&](const Json &value)
			                     {
				                     return value.at("id") == id;
			                     }) ||
			         document.libraries().consoles.contains(id));
			consoles.push_back({{"id", id}, {"widgets", Json::array()}});
			consoles.back()["layout"] = convert_console_layout(consoles.back());
			layout_editor.focus();
			console = static_cast<int>(consoles.size() - 1);
			selection.kind = "Console";
		}
		for (std::size_t i = 0; i < consoles.size(); ++i)
		{
			auto id = consoles[i].at("id").get<std::string>();
			const auto name = consoles[i].value("name", std::string{});
			const auto label = (name.empty() ? id : name + " (" + id + ")") + "###" + id;
			if (ImGui::Selectable(label.c_str(), selection.kind == "Console" && console == i))
			{
				layout_editor.focus();
				console = static_cast<int>(i);
				selection.kind = "Console";
			}
		}
		return;
	}
	if (console < 0 || console >= static_cast<int>(consoles.size()))
		return;
	auto &selected = consoles[console];
	const auto original_id = selected.at("id").get<std::string>();
	if (console_rename_from != original_id)
		console_id = console_rename_from = original_id;
	ImGui::InputText(tool_label("Console ID").c_str(), &console_id);
	if (tool_button(tool_label("Rename console").c_str()))
		attempt(
		    [&]
		    {
			    document.rename_console(original_id, console_id);
			    console_rename_from = console_id;
			    status = "Console renamed; ship and Station references updated.";
		    });
	string_field("Display name", selected, "name");
	edit_field("Human seeking", selected, "human_seeking", false);
	auto &widgets = consoles[console]["widgets"];
	for (auto [kind, binding] : widget_bindings())
	{
		if (tool_button((std::string("Add ") + kind).c_str()))
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
	array_rows(widgets,
	           [&](Json &widget, std::size_t)
	           {
		           if (ImGui::TreeNode(widget.at("id").get_ref<const std::string &>().c_str()))
		           {
			           string_field("Semantic label", widget, "label");
			           if (widget.at("kind") == "radar")
			           {
				           const auto source = widget.value("selection_source", "sensors");
				           if (ImGui::BeginCombo(tool_label("Selection source").c_str(), source.c_str()))
				           {
					           for (const char *role : {"tactical", "sensors", "waypoint"})
						           if (ImGui::Selectable(role, source == role))
							           widget["selection_source"] = role;
					           ImGui::EndCombo();
				           }
				           if (!widget.contains("sweep") && tool_button(tool_label("Customize radar sweep").c_str()))
					           widget["sweep"] = {{"enabled", true},
					                              {"colour", {.1, .9, .65, .85}},
					                              {"duration_seconds", 3.0},
					                              {"speed_degrees_s", 60.0}};
				           if (widget.contains("sweep"))
				           {
					           auto &s = widget["sweep"];
					           edit_field("Sweep enabled", s, "enabled", true);
					           tool_real_field("Sweep duration (seconds)", s, "duration_seconds", 3, .1f, 30);
					           tool_real_field("Sweep speed (degrees/second)", s, "speed_degrees_s", 60, 1, 720);
					           const auto tint = s.value("colour", Json::array({.1, .9, .65, .85}));
					           float colour[4] = {tint[0], tint[1], tint[2], tint[3]};
					           if (ImGui::ColorEdit4(tool_label("Sweep colour").c_str(), colour))
						           s["colour"] = {colour[0], colour[1], colour[2], colour[3]};
				           }
			           }
			           ImGui::TextWrapped(
			               "%s", ToolMessage("Binding: {binding}",
			                                 {{"binding", widget.value("binding", std::string("Controller command"))}})
			                         .c_str());
			           for (const char *key : {"x", "y", "width", "height"})
			           {
				           int value = widget[key];
				           if (ImGui::SliderInt(tool_label(key).c_str(), &value,
				                                (std::string(key) == "x" || std::string(key) == "y") ? 0 : 1, 1000))
					           widget[key] = value;
			           }
			           const bool remove = tool_button(tool_label("Delete widget").c_str());
			           ImGui::TreePop();
			           return remove;
		           }
		           return false;
	           });
	if (ImGui::CollapsingHeader(tool_label("Bindings and knowledge").c_str()))
		gameplay.console_properties(document, environment.preferences.pseudo, selected);
	if (selected.contains("layout"))
	{
		ImGui::TextWrapped("%s", localise("Use Layout canvas to arrange and preview this Console.").c_str());
		return;
	}
	ImGui::SeparatorText(tool_label("Live Console preview").c_str());
	ImGui::TextWrapped("%s", localise("Sample data; controls affect only this preview.").c_str());
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
	// The isolated preview has canonical sample Systems, not this ship's fitted identities.
	auto sample_layout = selected;
	for (auto &widget : sample_layout["widgets"])
	{
		widget.erase("system");
		widget.erase("controller");
		widget.erase("channel");
		if (!widget.contains("binding"))
			widget["binding"] = widget.at("kind") == "throttle" ? "engine.desired_speed" : "steering.desired_heading";
	}
	try
	{
		const auto texture =
		    draw_console_preview(sample_layout, logical_width, logical_height,
		                         {hovered ? (pointer.x - origin.x) * logical_width / width : -100.f,
		                          hovered ? (pointer.y - origin.y) * logical_height / height : -100.f},
		                         hovered && ImGui::IsMouseClicked(0), ImGui::IsMouseReleased(0),
		                         hovered ? ImGui::GetIO().MouseWheel : 0.f, environment.preferences.pseudo);
		ImGui::Image(static_cast<ImTextureID>(texture.id), {static_cast<float>(width), static_cast<float>(height)},
		             {0, 1}, {1, 0});
	}
	catch (const std::exception &error)
	{
		ImGui::TextWrapped("%s", error.what());
	}
}

void WorkshopWorkspace::State::slots_editor()
{
	auto &slots = document.map()["ship_slots"];
	if (tool_button(tool_label("Add ship slot").c_str()))
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
	array_rows(slots,
	           [&](Json &slot, std::size_t)
	           {
		           if (ImGui::TreeNode(slot["id"].get_ref<const std::string &>().c_str()))
		           {
			           string_field("Name", slot, "id");
			           bool mandatory = slot.value("mandatory", false);
			           if (ImGui::Checkbox(tool_label("Mandatory ship slot").c_str(), &mandatory))
				           slot["mandatory"] = mandatory;
			           transform_fields(slot);
			           string_field("Faction (empty is unaffiliated)", slot, "faction");
			           ImGui::TextUnformatted(localise("Allowed ships:").c_str());
			           for (const auto &id : document.definition_choices(WorkshopDefinitionKind::Entity))
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
			           const bool remove = tool_button(tool_label("Remove slot").c_str());
			           ImGui::TreePop();
			           return remove;
		           }
		           return false;
	           });
}
} // namespace phoenix
