module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.layout;
import phoenix.content.documents;
import phoenix.workshop.authoring_core;
import phoenix.content.packs;
import phoenix.content.ui;
import phoenix.ui.preview;

namespace phoenix
{
namespace
{
void layout_string_field(const char *label, Json &object, const char *key)
{
	edit_field(tool_label(label).c_str(), object, key, std::string{});
}
} // namespace
struct LayoutEditor::State
{
	float drag_start_x = 0, drag_start_y = 0;
	Json drag_rect, drag_root;
	std::string drag_kind, drag_group, drag_layout;
	int drop_index = -1;
	float drop_x = 0, drop_y = 0;
	bool canvas_drag = false;
	std::string rename_from;
	WorkshopDefinitionKind rename_kind = WorkshopDefinitionKind::Layout;
	std::string last_error;
	std::string selected, component = "gameplay", asset_name = "", asset_id = "button", theme_name;
	std::string drag_source, drag_target, drag_component;
	std::string widget_context;
	int serial = 1;
	LayoutPreviewSettings preview;
	bool interact = false, compact = false, snap = true, resizing = false, focus_canvas = false, hud = false;
};

Json *layout_find_node(Json &node, const std::string &identity, const std::string &prefix = {})
{
	return workshop_layout_node(node, identity, prefix);
}
Json *layout_find_parent(Json &node, const std::string &identity, const std::string &prefix = {})
{
	const auto id = prefix + node.value("id", std::string{});
	if (node.contains("children"))
		for (auto &child : node["children"])
		{
			if (id + "." + child.value("id", std::string{}) == identity)
				return &node;
			if (auto *found = layout_find_parent(child, identity, id + "."))
				return found;
		}
	return nullptr;
}
template <class State> void layout_hierarchy(State &layout_workspace, Json &node, const std::string &prefix = {})
{
	const auto id = prefix + node.value("id", std::string{});
	ImGui::PushID(id.c_str());
	const auto caption =
	    node.value("label", node.value("id", std::string{})) + " (" + node.value("kind", std::string("group")) + ")";
	const bool open =
	    ImGui::TreeNodeEx(caption.c_str(), ImGuiTreeNodeFlags_OpenOnArrow |
	                                           (layout_workspace.selected == id ? ImGuiTreeNodeFlags_Selected : 0));
	if (ImGui::IsItemClicked())
		layout_workspace.selected = id;
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("PHOENIX_LAYOUT", id.c_str(), id.size() + 1);
		ImGui::TextUnformatted(caption.c_str());
		ImGui::EndDragDropSource();
	}
	if (node.contains("children") && ImGui::BeginDragDropTarget())
	{
		if (const auto *payload = ImGui::AcceptDragDropPayload("PHOENIX_LAYOUT"))
		{
			layout_workspace.drag_source = static_cast<const char *>(payload->Data);
			layout_workspace.drag_target = id;
		}
		if (const auto *payload = ImGui::AcceptDragDropPayload("PHOENIX_COMPONENT"))
		{
			layout_workspace.drag_component = static_cast<const char *>(payload->Data);
			layout_workspace.drag_target = id;
		}
		if (const auto *payload = ImGui::AcceptDragDropPayload("PHOENIX_LAYOUT_REF"))
		{
			layout_workspace.drag_layout = static_cast<const char *>(payload->Data);
			layout_workspace.drag_target = id;
		}
		ImGui::EndDragDropTarget();
	}
	if (open)
	{
		if (node.contains("children"))
			for (auto &child : node["children"])
				layout_hierarchy(layout_workspace, child, id + ".");
		ImGui::TreePop();
	}
	ImGui::PopID();
}
bool layout_remove_node(Json &node, const std::string &identity, const std::string &prefix = {})
{
	return workshop_layout_remove(node, identity, prefix);
}
LayoutEditor::LayoutEditor() : state_(std::make_unique<State>())
{
}
LayoutEditor::~LayoutEditor() = default;
LayoutPreviewSettings &LayoutEditor::preview()
{
	return state_->preview;
}
void LayoutEditor::focus()
{
	state_->focus_canvas = true;
}
const std::string &LayoutEditor::error() const
{
	return state_->last_error;
}
void LayoutEditor::draw(WorkshopDocument &document, int console_index, bool pseudo, FileImportInbox &imports,
                        ToolMessage &status)
{
	auto &layout_workspace = *state_;
	auto &consoles = document.definitions()["consoles"];
	if (console_index < 0 || console_index >= static_cast<int>(consoles.size()))
		return;
	auto &console = consoles[console_index];
	auto apply = [&](Json &owner, const char *operation, const Json &args) -> Json
	{
		try
		{
			return workshop_layout_action(document, owner, operation, args);
		}
		catch (const std::exception &error)
		{
			layout_workspace.last_error = error.what();
			status = error.what();
			return Json();
		}
	};
	tool_window(tool_label("Layout hierarchy").c_str());
	if (console.value("viewscreen", false))
		ImGui::Checkbox(tool_label("Edit Viewscreen HUD").c_str(), &layout_workspace.hud);
	else
		layout_workspace.hud = false;
	ImGui::Checkbox(tool_label("Edit station header").c_str(), &layout_workspace.preview.header);
	if (layout_workspace.preview.header && !console.contains("header"))
		(void)apply(console, "convert", {{"field", "header"}});
	const char *layout_field = layout_workspace.preview.header ? "header"
	                           : layout_workspace.hud          ? "hud_layout"
	                                                           : "layout";
	const char *override_field = layout_workspace.preview.header ? "header_overrides"
	                             : layout_workspace.hud          ? "hud_layout_overrides"
	                                                             : "layout_overrides";
	if (!console.contains(layout_field))
	{
		ImGui::TextWrapped(
		    "%s",
		    localise("Legacy Console. Convert explicitly to edit responsive layout; bindings are preserved.").c_str());
		if (tool_button(tool_label("Create responsive layout").c_str()))
		{
			(void)apply(console, "convert", {{"field", layout_field}});
		}
		ImGui::End();
		return;
	}
	layout_workspace.last_error.clear();
	try
	{
		auto catalog = document.preview_ui();
		if (ImGui::BeginCombo(tool_label("Layout template").c_str(),
		                      console[layout_field].is_string()
		                          ? console[layout_field].get_ref<const std::string &>().c_str()
		                          : localise("Local layout").c_str()))
		{
			for (const auto &[id, value] : catalog->layouts)
				if (ImGui::Selectable(id.c_str()))
				{
					(void)apply(console, "reference", {{"field", layout_field}, {"id", id}});
				}
			ImGui::EndCombo();
		}
		tool_prepare_action("Rename layout template");
		ImGui::InputText(tool_label("Reusable asset ID").c_str(), &layout_workspace.asset_name);
		if (console[layout_field].is_string())
			tool_action_line("Rename layout template");
		if (console[layout_field].is_string() && tool_button(tool_label("Rename layout template").c_str()))
		{
			layout_workspace.rename_from = console[layout_field].get<std::string>();
			layout_workspace.rename_kind = WorkshopDefinitionKind::Layout;
		}
		if (console[layout_field].is_string())
		{
			ImGui::TextWrapped("%s", localise("Referenced layout. Detach to edit a local copy.").c_str());
			if (tool_button(tool_label("Detach layout").c_str()))
			{
				(void)apply(console, "detach", {{"field", layout_field}});
			}
			if (!console.contains(override_field) && tool_button(tool_label("Override layout locally").c_str()))
				(void)apply(console, "override", {{"field", layout_field}});
			if (console.contains(override_field) && tool_button(tool_label("Revert layout override").c_str()))
				(void)apply(console, "revert", {{"field", layout_field}});
		}
		const bool read_only = console[layout_field].is_string() && !console.contains(override_field);
		Json inherited_definition = read_only ? catalog->layouts.at(console[layout_field].get<std::string>()) : Json{};
		auto &definition = read_only                           ? inherited_definition
		                   : console[layout_field].is_string() ? console[override_field]
		                                                       : console[layout_field];
		ImGui::BeginDisabled(read_only);
		ImGui::Checkbox(tool_label("Edit compact arrangement").c_str(), &layout_workspace.compact);
		if (layout_workspace.compact && !definition.contains("compact"))
		{
			if (tool_button(tool_label("Override compact arrangement").c_str()))
			{
				(void)apply(definition, "compact", {{"enabled", true}});
			}
			else
				ImGui::TextWrapped("%s", localise("Automatic compact pages use named groups.").c_str());
		}
		if (layout_workspace.compact && definition.contains("compact") &&
		    tool_button(tool_label("Restore automatic compact layout").c_str()))
			(void)apply(definition, "compact", {{"enabled", false}});
		if (layout_workspace.compact && !definition.contains("compact"))
		{
			ImGui::EndDisabled();
			ImGui::End();
			return;
		}
		auto &root = definition[layout_workspace.compact ? "compact" : "root"];
		layout_hierarchy(layout_workspace, root);
		if (!layout_workspace.drag_component.empty() || !layout_workspace.drag_source.empty() ||
		    !layout_workspace.drag_kind.empty() || !layout_workspace.drag_group.empty() ||
		    !layout_workspace.drag_layout.empty())
		{
			try
			{
				Json drop{
				    {"source", layout_workspace.drag_source},        {"destination", layout_workspace.drag_target},
				    {"index", layout_workspace.drop_index},          {"component", layout_workspace.drag_component},
				    {"group", layout_workspace.drag_group},          {"kind", layout_workspace.drag_kind},
				    {"layout_ref", layout_workspace.drag_layout},    {"x", static_cast<int>(layout_workspace.drop_x)},
				    {"y", static_cast<int>(layout_workspace.drop_y)}};
				layout_workspace.selected = workshop_layout_drop(root, drop, *catalog).get<std::string>();
			}
			catch (const std::exception &error)
			{
				layout_workspace.last_error = error.what();
				status = error.what();
			}
			layout_workspace.drag_component.clear();
			layout_workspace.drag_source.clear();
			layout_workspace.drag_kind.clear();
			layout_workspace.drag_group.clear();
			layout_workspace.drag_layout.clear();
			layout_workspace.drop_index = -1;
		}
		if (tool_button(tool_label("Save layout template").c_str()) && !layout_workspace.asset_name.empty())
		{
			(void)apply(definition, "save-template", {{"id", layout_workspace.asset_name}});
		}
		ImGui::EndDisabled();
		ImGui::End();
		auto *selected = layout_find_node(root, layout_workspace.selected);
		if (tool_window(tool_label("Layout palette").c_str()))
		{
			ImGui::BeginDisabled(read_only);
			ImGui::InputText(tool_label("Component target").c_str(), &layout_workspace.component);
			auto palette = workshop_layout_palette(document, console, layout_workspace.preview.header,
			                                       layout_workspace.widget_context);
			layout_workspace.widget_context = palette.at("context");
			if (!layout_workspace.preview.header &&
			    ImGui::BeginCombo(tool_label("Widget ship and console context").c_str(),
			                      layout_workspace.widget_context.c_str()))
			{
				for (const auto &[context, ship] : palette.at("contexts").items())
					if (ImGui::Selectable(context.c_str(), context == layout_workspace.widget_context))
						layout_workspace.widget_context = context;
				ImGui::EndCombo();
			}
			palette = workshop_layout_palette(document, console, layout_workspace.preview.header,
			                                  layout_workspace.widget_context);
			const auto targets = palette.at("targets").get<std::vector<std::string>>();

			if (ImGui::BeginCombo(tool_label("Available components").c_str(), layout_workspace.component.c_str()))
			{
				for (const auto &target : targets)
					if (ImGui::Selectable(target.c_str()))
						layout_workspace.component = target;
				ImGui::EndCombo();
			}
			selected = layout_find_node(root, layout_workspace.selected);
			if (std::getenv("PHOENIX_WORKSHOP_PROFILE"))
				ImGui::SetScrollY(std::fmod(ImGui::GetFrameCount() * 12.f, std::max(1.f, ImGui::GetScrollMaxY())));
			ImGuiListClipper palette_clipper;
			palette_clipper.Begin(static_cast<int>(targets.size()));
			while (palette_clipper.Step())
				for (int i = palette_clipper.DisplayStart; i < palette_clipper.DisplayEnd; ++i)
				{
					const auto &target = targets[i];
					ImGui::Selectable(target.c_str(), layout_workspace.component == target);
					if (ImGui::IsItemClicked())
						layout_workspace.component = target;
					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("PHOENIX_COMPONENT", target.c_str(), target.size() + 1);
						ImGui::TextUnformatted(localise(target).c_str());
						ImGui::EndDragDropSource();
					}
				}
			for (const auto &target : targets)
				if (target.starts_with("control/") && target.ends_with("/complete"))
				{
					const auto scope = "scope/" + target;
					const auto label = localise("Drag Controller scope") + " " + target;
					ImGui::Selectable(label.c_str());
					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("PHOENIX_COMPONENT", scope.c_str(), scope.size() + 1);
						ImGui::TextUnformatted(label.c_str());
						ImGui::EndDragDropSource();
					}
				}
			for (const auto &entry : palette.at("equipment"))
			{
				const auto target = entry.at("target").get<std::string>();
				const auto label = localise("Drag equipment meter") + " " + entry.at("label").get<std::string>();
				ImGui::Selectable(label.c_str());
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PHOENIX_COMPONENT", target.c_str(), target.size() + 1);
					ImGui::TextUnformatted(label.c_str());
					ImGui::EndDragDropSource();
				}
			}
			for (const auto &[kind, binding] : widget_bindings())
				if (tool_button(tool_label(std::string("Create ") + kind + " widget").c_str()))
				{
					const auto at = std::string("/") +
					                (console[layout_field].is_string() ? override_field : layout_field) + "/" +
					                (layout_workspace.compact ? "compact" : "root");
					(void)apply(
					    console, "widget.add",
					    {{"kind", kind},
					     {"layout", at},
					     {"parent", selected && selected->contains("children") ? layout_workspace.selected
					                                                           : root.at("id").get<std::string>()}});
					selected = layout_find_node(root, layout_workspace.selected);
				}

			for (const char *kind : {"row", "column", "wrap", "tabs", "instruments", "cutout", "overlay"})
			{
				ImGui::Selectable(tool_label(std::string("Drag ") + kind).c_str());
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PHOENIX_CONTAINER", kind, std::strlen(kind) + 1);
					ImGui::TextUnformatted(localise(kind).c_str());
					ImGui::EndDragDropSource();
				}
			}
			for (const auto &[id, group] : catalog->ui_groups)
			{
				ImGui::Selectable((localise("Drag group") + " " + id).c_str());
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PHOENIX_GROUP", id.c_str(), id.size() + 1);
					ImGui::TextUnformatted(id.c_str());
					ImGui::EndDragDropSource();
				}
			}
			for (const auto &[id, layout] : catalog->layouts)
			{
				ImGui::Selectable((localise("Drag layout") + " " + id).c_str());
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PHOENIX_LAYOUT_REF", id.c_str(), id.size() + 1);
					ImGui::TextUnformatted(id.c_str());
					ImGui::EndDragDropSource();
				}
			}
			for (const char *kind : {"component", "row", "column", "wrap", "tabs", "instruments", "cutout", "overlay"})
				if (tool_button(tool_label(std::string("Add ") + kind).c_str()))
				{
					(void)apply(
					    root, "node.add",
					    {{"kind", kind},
					     {"component", layout_workspace.component},
					     {"parent", selected && selected->contains("children") ? layout_workspace.selected
					                                                           : root.at("id").get<std::string>()}});
					selected = layout_find_node(root, layout_workspace.selected);
				}
			if (selected)
			{
				if (tool_button(tool_label("Save reusable group").c_str()) && !layout_workspace.asset_name.empty())
				{
					(void)apply(*selected, "group.save", {{"id", layout_workspace.asset_name}});
				}
				for (const auto &[id, group] : catalog->ui_groups)
					if (selected->contains("children") && tool_button((localise("Insert group") + " " + id).c_str()))
						(void)apply(root, "node.add", {{"group", id}, {"parent", layout_workspace.selected}});
				for (const auto &[id, layout] : catalog->layouts)
					if (selected->contains("children") && tool_button((localise("Insert layout") + " " + id).c_str()))
						(void)apply(root, "node.add", {{"layout_ref", id}, {"parent", layout_workspace.selected}});
			}
			ImGui::EndDisabled();
		}
		ImGui::End();
		if (tool_window(tool_label("Layout properties").c_str()))
		{
			edit_shared_field(tool_label("Show as station tab").c_str(), console, "console.tab");
			ImGui::BeginDisabled(read_only);
			selected = layout_find_node(root, layout_workspace.selected);
			if (selected)
			{
				layout_string_field("Panel name", *selected, "label");
				if (auto *parent = layout_find_parent(root, layout_workspace.selected))
				{
					auto &children = (*parent)["children"];
					auto found =
					    std::find_if(children.begin(), children.end(),
					                 [&](const Json &row)
					                 {
						                 return row.value("id", std::string{}) == selected->value("id", std::string{});
					                 });
					if (found != children.end())
					{
						const auto index = found - children.begin();
						ImGui::BeginDisabled(index == 0);
						const bool up = tool_button(tool_label("Move panel up").c_str());
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(index + 1 == static_cast<std::ptrdiff_t>(children.size()));
						const bool down = tool_button(tool_label("Move panel down").c_str());
						ImGui::EndDisabled();
						if (up || down)
						{
							(void)apply(root, "node.reorder",
							            {{"node", layout_workspace.selected}, {"direction", up ? -1 : 1}});
							selected = layout_find_node(root, layout_workspace.selected);
						}
						const auto split = layout_workspace.selected.find_last_of('.');
						const auto parent_id = layout_workspace.selected.substr(0, split);
						const auto grand = parent_id.find_last_of('.');
						if (grand != std::string::npos &&
						    tool_button(tool_label("Move panel out of container").c_str()))
						{
							layout_workspace.drag_source = layout_workspace.selected;
							layout_workspace.drag_target = parent_id.substr(0, grand);
						}
					}
				}

				if (tool_button(tool_label("Duplicate panel").c_str()))
				{
					(void)apply(root, "node.duplicate",
					            {{"node", layout_workspace.selected}, {"console", console.at("id")}});
					selected = layout_find_node(root, layout_workspace.selected);
				}
				for (int direction : {-1, 1})
					if (tool_button(tool_label(direction < 0 ? "Move earlier" : "Move later").c_str()))
					{
						(void)apply(root, "node.reorder",
						            {{"node", layout_workspace.selected}, {"direction", direction}});
						selected = layout_find_node(root, layout_workspace.selected);
					}
				edit_shared_field(tool_label("Alignment").c_str(), *selected, "layout-editor.layout.align");
				edit_shared_field(tool_label("Corner inset").c_str(), *selected, "layout-editor.layout.inset");
				edit_shared_field(tool_label("Scrollable contents").c_str(), *selected, "layout-editor.layout.scroll");
				if (selected->value("kind", "") == "cutout")
					for (const char *key : {"x", "y", "radius"})
					{
						auto value = selected->value("circle", Json::object());
						if (edit_shared_field(tool_label(std::string("Circle ") + key).c_str(), value,
						                      (std::string("layout-editor.layout.circle.") + key).c_str()))
							(*selected)["circle"] = value;
					}
				edit_shared_field(tool_label("Overlay / cutout region").c_str(), *selected,
				                  "layout-editor.layout.region");
				for (const char *key : {"x", "y", "width", "height"})
				{
					auto value = selected->value("rect", Json::object());
					if (edit_shared_field(tool_label(std::string("Instrument ") + key).c_str(), value,
					                      (std::string("layout-editor.layout.rect.") + key).c_str()))
						(*selected)["rect"] = value;
				}
				for (const char *key :
				     {"min_width", "min_height", "width", "height", "gap", "padding", "stretch", "aspect"})
					edit_shared_field(tool_label(key).c_str(), *selected,
					                  (std::string("layout-editor.layout.geometry.") + key).c_str());
				for (const char *key : {"min_stars", "max_stars"})
				{
					auto value = selected->value("when", Json::object());
					if (edit_shared_field(tool_label(key).c_str(), value,
					                      (std::string("layout-editor.layout.when.") + key).c_str()))
						(*selected)["when"] = value;
				}

				for (const auto &component : console_ui_components())
					if (selected->value("component", "") == component.id && !component.feature.empty())
						ImGui::TextWrapped("Requires available %s capability on this station.",
						                   component.feature.c_str());
				edit_shared_field(tool_label("Override component appearance").c_str(), *selected,
				                  "layout.style.enabled");
				const bool styled = selected->contains("style");

				if (styled)
				{
					layout_string_field("Component text colour", (*selected)["style"], "text");
					edit_shared_field(tool_label("Component font size").c_str(), (*selected)["style"],
					                  "layout-editor.layout.style.font_size");
				}
				if (selected->value("kind", "") == "component")
					layout_string_field("Binding target", *selected, "component");
				if (selected->contains("group") && tool_button(tool_label("Rename reusable group").c_str()))
				{
					layout_workspace.rename_from = selected->at("group").get<std::string>();
					layout_workspace.rename_kind = WorkshopDefinitionKind::UiGroup;
				}
				if ((selected->contains("group") || selected->contains("layout_ref")) &&
				    tool_button(tool_label("Detach reusable reference").c_str()))
				{
					(void)apply(*selected, "group.detach", Json::object());
				}
				if (tool_button(tool_label("Delete layout node").c_str()))
				{
					(void)apply(root, "node.delete", {{"node", layout_workspace.selected}});
					selected = nullptr;
				}
			}
			if (tool_button(tool_label("Import font").c_str()))
				request_file(imports, FilePurpose::WorkshopFont);
			ImGui::SeparatorText(tool_label("Theme").c_str());
			const auto current_theme = console.value("theme", std::string{});
			if (ImGui::BeginCombo(tool_label("Console theme").c_str(),
			                      current_theme.empty() ? "Pack default" : current_theme.c_str()))
			{
				if (ImGui::Selectable("Pack default"))
					console.erase("theme");
				for (const auto &[id, theme] : catalog->themes)
					if (ImGui::Selectable(id.c_str()))
						console["theme"] = id;
				ImGui::EndCombo();
			}
			if (std::string_view(layout_field) == "layout")
			{
				const auto pages = expand_ui_groups(*catalog, definition.at("root"));
				if (pages.value("kind", "") == "tabs")
					for (const auto &page : pages.at("children"))
					{
						const auto key = pages.at("id").get<std::string>() + "." + page.at("id").get<std::string>();
						const auto theme = console.value("page_themes", Json::object()).value(key, std::string{});
						const auto caption = tool_label("Page theme") + " · " + page.value("label", key);
						if (ImGui::BeginCombo(caption.c_str(), theme.empty() ? "Console default" : theme.c_str()))
						{
							if (ImGui::Selectable("Console default"))
								(void)apply(console, "theme.page", {{"page", key}, {"theme", ""}});
							for (const auto &[id, definition] : catalog->themes)
								if (ImGui::Selectable(id.c_str(), id == theme))
									(void)apply(console, "theme.page", {{"page", key}, {"theme", id}});
							ImGui::EndCombo();
						}
					}
			}
			tool_prepare_action("Rename theme");
			ImGui::InputText(tool_label("New reusable ID").c_str(), &layout_workspace.asset_name);
			if (!current_theme.empty())
				tool_action_line("Rename theme");
			if (!current_theme.empty() && tool_button(tool_label("Rename theme").c_str()))
			{
				layout_workspace.rename_from = current_theme;
				layout_workspace.rename_kind = WorkshopDefinitionKind::Theme;
			}
			if (tool_button(tool_label("Create theme").c_str()))
			{
				const auto id = apply(console, "theme.create", Json::object());
				if (id.is_string())
					layout_workspace.theme_name = id.get<std::string>();
			}
			for (auto &theme : document.definitions()["themes"])
				if (theme.at("id") == console.value("theme", std::string{}))
				{
					for (const auto &[name, token] : theme.at("tokens").items())
						edit_shared_control(tool_label(name).c_str(), theme, "theme.token", {{"token", name}});

					if (tool_button(tool_label("Use theme for pack and menus").c_str()))
						(void)apply(theme, "theme.pack", Json::object());
					if (ImGui::BeginCombo(tool_label("Artwork slot").c_str(), layout_workspace.asset_id.c_str()))
					{
						for (const auto &slot : ui_theme_artwork_slots())
							if (ImGui::Selectable(slot.c_str(), layout_workspace.asset_id == slot))
								layout_workspace.asset_id = slot;
						ImGui::EndCombo();
					}
					for (const auto &slot : ui_theme_artwork_slots())
						if (slot == layout_workspace.asset_id && theme.at("assets").contains(slot))
						{
							edit_shared_control(tool_label(slot + " artwork scale").c_str(), theme, "theme.scale",
							                    {{"slot", slot}});
							if (slot == "console-background")
								continue;
							edit_shared_control(tool_label(std::string("Nine-slice ") + slot).c_str(), theme,
							                    "theme.sliced", {{"slot", slot}});
							if (theme.contains("slices") && theme.at("slices").contains(slot))
								edit_shared_control(tool_label(slot + " insets").c_str(), theme, "theme.insets",
								                    {{"slot", slot}});
						}
					if (ImGui::BeginCombo(tool_label("Artwork asset").c_str(), "Choose imported asset"))
					{
						for (const auto &[id, bytes] : catalog->assets)
							if (ImGui::Selectable(id.c_str()) && !layout_workspace.asset_id.empty())
								(void)workshop_set_control(theme, "theme.asset", id,
								                           {{"slot", layout_workspace.asset_id}});
						ImGui::EndCombo();
					}
				}
			ImGui::EndDisabled();
		}
		ImGui::End();
		if (layout_workspace.focus_canvas)
		{
			ImGui::SetNextWindowFocus();
			layout_workspace.focus_canvas = false;
		}
		if (tool_window(tool_label("Layout canvas").c_str()))
		{
			ImGui::Checkbox(tool_label("Interact preview").c_str(), &layout_workspace.interact);
			ImGui::SameLine();
			ImGui::Checkbox(tool_label("Snap instruments").c_str(), &layout_workspace.snap);
			for (const auto &[name, w, h] :
			     {std::tuple{"Desktop", 1280, 720}, std::tuple{"Tablet", 1024, 768}, std::tuple{"Phone", 390, 844}})
			{
				if (tool_button(tool_label(name).c_str()))
				{
					layout_workspace.preview.width = w;
					layout_workspace.preview.height = h;
				}
				ImGui::SameLine();
			}
			if (tool_button(tool_label("Rotate").c_str()))
				std::swap(layout_workspace.preview.width, layout_workspace.preview.height);
			ImGui::SliderInt(tool_label("Preview width").c_str(), &layout_workspace.preview.width, 240, 1920);
			ImGui::SliderInt(tool_label("Preview height").c_str(), &layout_workspace.preview.height, 240, 1920);
			ImGui::SliderInt(tool_label("Preview text size").c_str(), &layout_workspace.preview.text_scale, 100, 200);
			catalog = document.preview_ui();
			static constexpr const char *states[] = {"normal", "engineering", "conversations",
			                                         "faults", "empty",       "long text"};
			ImGui::Combo(tool_label("Sample state").c_str(), &layout_workspace.preview.sample_state,
			             "Normal\0Engineering\0Conversations\0Faults\0Empty\0Long text\0");
			auto sample = console;
			auto preview_definition = definition;
			if (layout_workspace.compact && definition.contains("compact"))
				preview_definition["root"] = definition.at("compact");
			preview_definition.erase("compact");
			sample[layout_field] = preview_definition;
			sample.erase(override_field);
			if (layout_workspace.hud)
			{
				sample["layout"] = preview_definition;
				if (console.contains("compact"))
					sample["widgets"] = console.at("compact").at("widgets");
				sample.erase("layout_overrides");
			}
			ImGui::SliderInt(tool_label("Preview complexity stars").c_str(), &layout_workspace.preview.sample_stars, 1,
			                 3);
			sample["_preview_header"] = layout_workspace.preview.header;
			sample["_preview_stars"] = layout_workspace.preview.sample_stars;
			sample["_preview_widget_context"] = layout_workspace.widget_context;
			sample["_preview_state"] = states[layout_workspace.preview.sample_state];
			sample["_preview_pages"] = layout_workspace.preview.pages;
			sample["_preview_interact"] = layout_workspace.interact;
			for (auto &widget : sample["widgets"])
			{
				widget.erase("system");
				widget.erase("controller");
				widget.erase("channel");
				if (!widget.contains("binding"))
					widget["binding"] =
					    widget.at("kind") == "throttle" ? "engine.desired_speed" : "steering.desired_heading";
			}
			const auto origin = ImGui::GetCursorScreenPos();
			const float scale =
			    std::min({1.f, std::max(100.f, ImGui::GetContentRegionAvail().x) / layout_workspace.preview.width,
			              std::max(100.f, ImGui::GetContentRegionAvail().y - 60.f) / layout_workspace.preview.height});
			const ImVec2 size{layout_workspace.preview.width * scale, layout_workspace.preview.height * scale};
			const auto mouse = ImGui::GetIO().MousePos;
			const bool hovered = ImGui::IsWindowHovered() && mouse.x >= origin.x && mouse.x < origin.x + size.x &&
			                     mouse.y >= origin.y && mouse.y < origin.y + size.y;
			const auto texture = draw_console_preview(
			    sample, layout_workspace.preview.width, layout_workspace.preview.height,
			    {hovered && layout_workspace.interact ? (mouse.x - origin.x) / scale : -100.f,
			     hovered && layout_workspace.interact ? (mouse.y - origin.y) / scale : -100.f},
			    hovered && layout_workspace.interact && ImGui::IsMouseClicked(0), ImGui::IsMouseReleased(0),
			    hovered && layout_workspace.interact ? ImGui::GetIO().MouseWheel : 0, pseudo, catalog,
			    layout_workspace.preview.text_scale);
			const auto &resolved = console_preview_geometry();
			ImGui::Image(static_cast<ImTextureID>(texture.id), size, {0, 1}, {1, 0});
			if (!read_only && !layout_workspace.interact && ImGui::BeginDragDropTarget())
			{
				std::string target_id = root.value("id", std::string{});
				float smallest = 1e30f;
				for (const auto &[id, r] : resolved.containers)
					if (mouse.x >= origin.x + r.x * scale && mouse.y >= origin.y + (r.y + resolved.body.y) * scale &&
					    mouse.x <= origin.x + (r.x + r.width) * scale &&
					    mouse.y <= origin.y + (r.y + resolved.body.y + r.height) * scale &&
					    r.width * r.height < smallest)
					{
						smallest = r.width * r.height;
						target_id = id;
					}
				if (auto *target = layout_find_node(root, target_id); target && target->contains("children"))
				{
					const auto r = resolved.containers.find(target_id);
					layout_workspace.drop_x = layout_workspace.drop_y = 0;
					if (r != resolved.containers.end())
					{
						layout_workspace.drop_x = std::clamp(((mouse.x - origin.x) / scale - r->second.x) /
						                                         std::max(1.f, r->second.width) * 1000.f,
						                                     0.f, 700.f);
						layout_workspace.drop_y =
						    std::clamp(((mouse.y - origin.y) / scale - r->second.y - resolved.body.y) /
						                   std::max(1.f, r->second.height) * 1000.f,
						               0.f, 700.f);
					}
					if (r != resolved.containers.end())
						ImGui::GetWindowDrawList()->AddRect(
						    {origin.x + r->second.x * scale, origin.y + (r->second.y + resolved.body.y) * scale},
						    {origin.x + (r->second.x + r->second.width) * scale,
						     origin.y + (r->second.y + resolved.body.y + r->second.height) * scale},
						    IM_COL32(80, 230, 160, 255), 0, 0, 3);
					int index = 0;
					const bool horizontal = target->value("kind", "") == "row";
					for (const auto &child : target->at("children"))
					{
						const auto id = target_id + "." + child.value("id", std::string{});
						for (const auto &placement : resolved.placements)
							if (placement.id == id &&
							    (horizontal
							         ? mouse.x > origin.x + (placement.bounds.x + placement.bounds.width * .5f) * scale
							         : mouse.y > origin.y + (placement.bounds.y + resolved.body.y +
							                                 placement.bounds.height * .5f) *
							                                    scale))
								++index;
					}
					if (r != resolved.containers.end())
					{
						float coordinate = horizontal ? r->second.x + r->second.width : r->second.y + r->second.height;
						if (index < static_cast<int>(target->at("children").size()))
						{
							const auto next =
							    target_id + "." + target->at("children")[index].value("id", std::string{});
							for (const auto &placement : resolved.placements)
								if (placement.id == next)
									coordinate = horizontal ? placement.bounds.x : placement.bounds.y;
						}
						const ImVec2 a = horizontal ? ImVec2{origin.x + coordinate * scale,
						                                     origin.y + (r->second.y + resolved.body.y) * scale}
						                            : ImVec2{origin.x + r->second.x * scale,
						                                     origin.y + (coordinate + resolved.body.y) * scale};
						const ImVec2 b = horizontal ? ImVec2{a.x, a.y + r->second.height * scale}
						                            : ImVec2{a.x + r->second.width * scale, a.y};
						ImGui::GetWindowDrawList()->AddLine(a, b, IM_COL32(255, 210, 80, 255), 3);
					}
					for (const auto &[payload_type, destination] : std::vector<std::pair<const char *, std::string *>>{
					         {"PHOENIX_COMPONENT", &layout_workspace.drag_component},
					         {"PHOENIX_LAYOUT", &layout_workspace.drag_source},
					         {"PHOENIX_CONTAINER", &layout_workspace.drag_kind},
					         {"PHOENIX_GROUP", &layout_workspace.drag_group},
					         {"PHOENIX_LAYOUT_REF", &layout_workspace.drag_layout}})
						if (const auto *payload = ImGui::AcceptDragDropPayload(payload_type))
						{
							*destination = static_cast<const char *>(payload->Data);
							layout_workspace.drag_target = target_id;
							layout_workspace.drop_index = index;
						}
				}
				ImGui::EndDragDropTarget();
			}
			if (!read_only && !layout_workspace.interact && hovered && layout_workspace.canvas_drag &&
			    ImGui::IsMouseDragging(0))
				if (auto *parent = layout_find_parent(root, layout_workspace.selected);
				    parent && parent->value("kind", "") != "instruments")
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
					{
						ImGui::SetDragDropPayload("PHOENIX_LAYOUT", layout_workspace.selected.c_str(),
						                          layout_workspace.selected.size() + 1);
						ImGui::TextUnformatted(layout_workspace.selected.c_str());
						ImGui::EndDragDropSource();
					}
			if (layout_workspace.canvas_drag && (ImGui::IsKeyPressed(ImGuiKey_Escape) || !IsWindowFocused()))
			{
				root = layout_workspace.drag_root;
				layout_workspace.canvas_drag = false;
			}

			if (!ImGui::IsMouseDown(0) || (hovered && ImGui::IsMouseClicked(0)))
				layout_workspace.canvas_drag = false;
			if (!layout_workspace.interact)
				for (auto it = resolved.placements.rbegin(); it != resolved.placements.rend(); ++it)
				{
					if (!it->visible)
						continue;
					const auto r = it->bounds;
					const ImVec2 a{origin.x + r.x * scale, origin.y + (r.y + resolved.body.y) * scale},
					    b{a.x + r.width * scale, a.y + r.height * scale};
					ImGui::GetWindowDrawList()->AddRect(a, b,
					                                    it->id == layout_workspace.selected
					                                        ? IM_COL32(255, 210, 80, 255)
					                                        : IM_COL32(100, 180, 220, 120));
					if (it->id == layout_workspace.selected)
						ImGui::GetWindowDrawList()->AddRectFilled({b.x - 10, b.y - 10}, b, IM_COL32(255, 210, 80, 255));
					if (hovered && ImGui::IsMouseClicked(0) && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y &&
					    mouse.y < b.y)
					{
						layout_workspace.selected = it->id;
						layout_workspace.canvas_drag = true;
						layout_workspace.drag_root = root;
						layout_workspace.drag_start_x = mouse.x;
						layout_workspace.drag_start_y = mouse.y;
						if (auto *node = layout_find_node(root, it->id))
							layout_workspace.drag_rect =
							    node->value("rect", Json{{"x", 0}, {"y", 0}, {"width", 500}, {"height", 500}});
						layout_workspace.resizing = mouse.x > b.x - 14 && mouse.y > b.y - 14;
						break;
					}
				}
			if (!read_only && !layout_workspace.interact && layout_workspace.canvas_drag && hovered &&
			    ImGui::IsMouseDragging(0))
				if (auto *moving = layout_find_node(root, layout_workspace.selected))
				{
					const auto split = layout_workspace.selected.find_last_of('.');
					if (split != std::string::npos)
					{
						const auto parent_id = layout_workspace.selected.substr(0, split);
						auto *parent = layout_find_node(root, parent_id);
						if (parent && parent->value("kind", "") == "instruments" &&
						    resolved.containers.contains(parent_id))
						{
							const auto bounds = resolved.containers.at(parent_id);
							const auto dx =
							    (mouse.x - layout_workspace.drag_start_x) / scale * 1000 / std::max(1.f, bounds.width);
							const auto dy =
							    (mouse.y - layout_workspace.drag_start_y) / scale * 1000 / std::max(1.f, bounds.height);
							(void)workshop_custom_action(
							    *moving, "layout.rect.gesture",
							    {{"rect", layout_workspace.drag_rect},
							     {"dx", dx},
							     {"dy", dy},
							     {"resize", ImGui::GetIO().KeyShift || layout_workspace.resizing},
							     {"snap", layout_workspace.snap}});
						}
					}
				}
			for (const auto &tab : resolved.pages)
			{
				ImGui::PushID(tab.id.c_str());
				if (tool_button(localise(tab.label).c_str()))
					layout_workspace.preview.pages[resolved.page_groups.at(tab.id)] = tab.id;
				ImGui::PopID();
				ImGui::SameLine();
			}
			ImGui::NewLine();
			ImGui::TextWrapped("%s",
			                   localise("Drag instruments to move. Shift-drag resizes. Use hierarchy drag/drop to "
			                            "move panels between containers.")
			                       .c_str());
			for (const auto &diagnostic : resolved.diagnostics)
				ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		ImGui::End();
	}
	catch (const std::exception &error)
	{
		layout_workspace.last_error = error.what();
		ImGui::TextWrapped("%s", error.what());
		ImGui::End();
	}
	if (!layout_workspace.rename_from.empty())
	{
		try
		{
			document.rename_ui_definition(layout_workspace.rename_kind, layout_workspace.rename_from,
			                              layout_workspace.asset_name);
		}
		catch (const std::exception &exception)
		{
			status = exception.what();
		}
		layout_workspace.rename_from.clear();
	}
}

} // namespace phoenix
