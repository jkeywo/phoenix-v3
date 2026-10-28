module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <charconv>
#include <cmath>
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
	Json drag_rect;
	bool canvas_drag = false;
	std::string rename_from;
	WorkshopDefinitionKind rename_kind = WorkshopDefinitionKind::Layout;
	std::string last_error;
	std::string selected, component = "gameplay", asset_name = "", asset_id = "button", theme_name;
	std::string drag_source, drag_target, drag_component;
	int serial = 1;
	LayoutPreviewSettings preview;
	bool interact = false, compact = false, snap = true, resizing = false, focus_canvas = false, hud = false;
};

Json *layout_find_node(Json &node, const std::string &identity, const std::string &prefix = {})
{
	const auto id = prefix + node.value("id", std::string{});
	if (identity == id)
		return &node;
	if (node.contains("children"))
		for (auto &child : node["children"])
			if (auto *found = layout_find_node(child, identity, id + "."))
				return found;
	return nullptr;
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
	const auto id = prefix + node.value("id", std::string{});
	if (!node.contains("children"))
		return false;
	auto &children = node["children"];
	for (std::size_t i = 0; i < children.size(); ++i)
	{
		if (id + "." + children[i].value("id", std::string{}) == identity)
		{
			children.erase(children.begin() + i);
			return true;
		}
		if (layout_remove_node(children[i], identity, id + "."))
			return true;
	}
	return false;
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
	tool_window(tool_label("Layout hierarchy").c_str());
	if (console.value("viewscreen", false))
		ImGui::Checkbox(tool_label("Edit Viewscreen HUD").c_str(), &layout_workspace.hud);
	else
		layout_workspace.hud = false;
	ImGui::Checkbox(tool_label("Edit station header").c_str(), &layout_workspace.preview.header);
	if (layout_workspace.preview.header && !console.contains("header"))
		console["header"] = console_ui_header_template();
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
			auto source = console;
			if (layout_workspace.hud && console.contains("compact"))
				source["widgets"] = console.at("compact").at("widgets");
			console[layout_field] = convert_console_layout(source);
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
					console[layout_field] = id;
					console.erase(override_field);
				}
			ImGui::EndCombo();
		}
		ImGui::InputText(tool_label("Reusable asset ID").c_str(), &layout_workspace.asset_name);
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
				auto value = catalog->layouts.at(console[layout_field].get<std::string>());
				if (console.contains(override_field))
					value.merge_patch(console.at(override_field));
				value.erase("id");
				value.erase("extends");
				console[layout_field] = value;
				console.erase(override_field);
			}
			if (!console.contains(override_field) && tool_button(tool_label("Override layout locally").c_str()))
				console[override_field] = catalog->layouts.at(console[layout_field].get<std::string>());
			if (console.contains(override_field) && tool_button(tool_label("Revert layout override").c_str()))
				console.erase(override_field);
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
				definition["compact"] = definition.at("root");
				definition["compact"]["kind"] = "tabs";
			}
			else
				ImGui::TextWrapped("%s", localise("Automatic compact pages use named groups.").c_str());
		}
		if (layout_workspace.compact && definition.contains("compact") &&
		    tool_button(tool_label("Restore automatic compact layout").c_str()))
			definition.erase("compact");
		auto &root = definition[layout_workspace.compact && definition.contains("compact") ? "compact" : "root"];
		layout_hierarchy(layout_workspace, root);
		if (!layout_workspace.drag_component.empty())
		{
			if (auto *target = layout_find_node(root, layout_workspace.drag_target))
				(*target)["children"].push_back({{"id", "panel" + std::to_string(layout_workspace.serial++)},
				                                 {"kind", "component"},
				                                 {"label", layout_workspace.drag_component},
				                                 {"component", layout_workspace.drag_component}});
			layout_workspace.drag_component.clear();
		}
		if (!layout_workspace.drag_source.empty())
		{
			const auto from = layout_workspace.drag_source, to = layout_workspace.drag_target;
			if (from != to && !to.starts_with(from + "."))
				if (auto *node = layout_find_node(root, from))
				{
					const auto copy = *node;
					if (layout_remove_node(root, from))
						if (auto *target = layout_find_node(root, to))
							target->at("children").push_back(copy);
				}
			layout_workspace.drag_source.clear();
		}
		if (tool_button(tool_label("Save layout template").c_str()) && !layout_workspace.asset_name.empty())
		{
			auto value = definition;
			value["id"] = layout_workspace.asset_name;
			auto &rows = document.definitions()["layouts"];
			if (!rows.is_array())
				rows = Json::array();
			if (std::none_of(rows.begin(), rows.end(),
			                 [&](const auto &entry)
			                 {
				                 return entry.at("id") == layout_workspace.asset_name;
			                 }))
				rows.push_back(value);
		}
		ImGui::EndDisabled();
		ImGui::End();
		tool_window(tool_label("Layout palette").c_str());
		ImGui::BeginDisabled(read_only);
		ImGui::InputText(tool_label("Component target").c_str(), &layout_workspace.component);
		std::vector<std::string> targets;
		for (const auto &component : console_ui_components())
			if (layout_workspace.preview.header ==
			    (component.id == "settings" || component.id == "console-name" || component.id == "tabs"))
				targets.push_back(component.id);
		if (!layout_workspace.preview.header)
			for (const auto &widget : console.at("widgets"))
				targets.push_back(widget.at("id"));
		if (ImGui::BeginCombo(tool_label("Available components").c_str(), layout_workspace.component.c_str()))
		{
			for (const auto &target : targets)
				if (ImGui::Selectable(target.c_str()))
					layout_workspace.component = target;
			ImGui::EndCombo();
		}
		auto *selected = layout_find_node(root, layout_workspace.selected);
		for (const auto &target : targets)
		{
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
		for (const auto &[kind, binding] : widget_bindings())
			if (tool_button(tool_label(std::string("Create ") + kind + " widget").c_str()))
			{
				const auto id = std::string(kind) + std::to_string(layout_workspace.serial++);
				console["widgets"].push_back({{"id", id},
				                              {"kind", kind},
				                              {"binding", binding},
				                              {"label", kind},
				                              {"x", 0},
				                              {"y", 0},
				                              {"width", 500},
				                              {"height", 500}});
				auto *parent = selected && selected->contains("children") ? selected : &root;
				if (parent->contains("children"))
					(*parent)["children"].push_back({{"id", id},
					                                 {"kind", "component"},
					                                 {"component", id},
					                                 {"label", kind},
					                                 {"rect", {{"x", 0}, {"y", 0}, {"width", 500}, {"height", 500}}}});
			}

		for (const char *kind : {"component", "row", "column", "wrap", "tabs", "instruments", "cutout"})
			if (tool_button(tool_label(std::string("Add ") + kind).c_str()))
			{
				auto *parent = selected && selected->contains("children") ? selected : &root;
				if (parent->contains("children"))
				{
					Json node{{"id", "panel" + std::to_string(layout_workspace.serial++)},
					          {"kind", kind},
					          {"label", "Panel"}};
					if (std::string(kind) == "component")
						node["component"] = layout_workspace.component;
					else
						node["children"] = Json::array();
					(*parent)["children"].push_back(node);
				}
			}
		if (selected)
		{
			if (tool_button(tool_label("Save reusable group").c_str()) && !layout_workspace.asset_name.empty())
			{
				auto &rows = document.definitions()["ui_groups"];
				if (!rows.is_array())
					rows = Json::array();
				if (std::none_of(rows.begin(), rows.end(),
				                 [&](const auto &entry)
				                 {
					                 return entry.at("id") == layout_workspace.asset_name;
				                 }))
					rows.push_back({{"id", layout_workspace.asset_name}, {"schema", 1}, {"root", *selected}});
			}
			for (const auto &[id, group] : catalog->ui_groups)
				if (selected->contains("children") && tool_button((localise("Insert group") + " " + id).c_str()))
					(*selected)["children"].push_back(
					    {{"id", "group" + std::to_string(layout_workspace.serial++)}, {"group", id}});
		}
		ImGui::EndDisabled();
		ImGui::End();
		tool_window(tool_label("Layout properties").c_str());
		bool show_tab = console.value("tab", true);
		if (ImGui::Checkbox(tool_label("Show as station tab").c_str(), &show_tab))
			console["tab"] = show_tab;
		ImGui::BeginDisabled(read_only);
		selected = layout_find_node(root, layout_workspace.selected);
		if (selected)
		{
			layout_string_field("Panel name", *selected, "label");
			if (tool_button(tool_label("Duplicate panel").c_str()))
			{
				if (auto *parent = layout_find_parent(root, layout_workspace.selected))
				{
					auto copy = expand_ui_groups(*catalog, *selected);
					auto widgets = console.at("widgets");
					std::function<bool(Json &, unsigned)> duplicate_bindings;
					duplicate_bindings = [&](Json &node, unsigned depth)
					{
						if (depth > 24)
							return false;
						if (node.contains("component"))
						{
							auto original = std::find_if(console.at("widgets").begin(), console.at("widgets").end(),
							                             [&](const auto &w)
							                             {
								                             return w.at("id") == node.at("component");
							                             });
							if (original == console.at("widgets").end() || original->at("kind") == "conversations" ||
							    original->at("kind") == "navigation")
								return false;
							std::string id;
							do
							{
								id = "widget-copy" + std::to_string(layout_workspace.serial++);
							} while (std::any_of(widgets.begin(), widgets.end(),
							                     [&](const auto &w)
							                     {
								                     return w.at("id") == id;
							                     }));
							auto widget = *original;
							widget["id"] = id;
							widgets.push_back(widget);
							node["component"] = id;
						}
						if (node.contains("children"))
							for (auto &child : node["children"])
								if (!duplicate_bindings(child, depth + 1))
									return false;
						return true;
					};
					if (duplicate_bindings(copy, 0))
					{
						copy["id"] = "copy" + std::to_string(layout_workspace.serial++);
						console["widgets"] = std::move(widgets);
						(*parent)["children"].push_back(copy);
					}
					else
						status = "Built-in panels have one live instance per Console; reuse their group on "
						         "another Console.";
					selected = layout_find_node(root, layout_workspace.selected);
				}
			}
			for (int direction : {-1, 1})
				if (tool_button(tool_label(direction < 0 ? "Move earlier" : "Move later").c_str()))
				{
					if (auto *parent = layout_find_parent(root, layout_workspace.selected))
					{
						auto &children = (*parent)["children"];
						for (int i = 0; i < static_cast<int>(children.size()); ++i)
							if (children[i]["id"] == selected->at("id") && i + direction >= 0 &&
							    i + direction < static_cast<int>(children.size()))
							{
								std::swap(children[i], children[i + direction]);
								break;
							}
						selected = layout_find_node(root, layout_workspace.selected);
					}
				}
			if (ImGui::BeginCombo(tool_label("Alignment").c_str(),
			                      selected->value("align", std::string("stretch")).c_str()))
			{
				for (const char *align : {"stretch", "start", "center", "end"})
					if (ImGui::Selectable(align))
						(*selected)["align"] = align;
				ImGui::EndCombo();
			}
			if (selected->value("kind", "") == "cutout")
				for (const auto &[key, fallback] :
				     {std::pair{"x", .5f}, std::pair{"y", .5f}, std::pair{"radius", .35f}})
				{
					float v = selected->value("circle", Json::object()).value(key, fallback);
					if (ImGui::SliderFloat(tool_label(std::string("Circle ") + key).c_str(), &v, 0, 1))
						(*selected)["circle"][key] = v;
				}
			if (ImGui::BeginCombo(tool_label("Cutout region").c_str(),
			                      selected->value("region", std::string{}).c_str()))
			{
				for (const char *region : {"", "centre", "top-left", "top-right", "bottom-left", "bottom-right"})
					if (ImGui::Selectable(region))
						(*selected)["region"] = region;
				ImGui::EndCombo();
			}
			for (const char *field : {"x", "y", "width", "height"})
			{
				int v = selected->value("rect", Json::object())
				            .value(field, std::string(field) == "width" || std::string(field) == "height" ? 500 : 0);
				if (ImGui::SliderInt(tool_label(std::string("Instrument ") + field).c_str(), &v, 0, 1000))
					(*selected)["rect"][field] = v;
			}
			for (const char *field :
			     {"min_width", "min_height", "width", "height", "gap", "padding", "stretch", "aspect"})
			{
				float value = selected->value(field, std::string(field).starts_with("min_") ? 44.f
				                                     : std::string(field) == "gap"          ? 12.f
				                                     : std::string(field) == "padding"      ? 8.f
				                                     : std::string(field) == "stretch"      ? 1.f
				                                                                            : 0.f);
				if (ImGui::DragFloat(tool_label(field).c_str(), &value, 1, 0, 16384))
					(*selected)[field] = value;
			}
			for (const char *field : {"min_stars", "max_stars"})
			{
				int stars = selected->value("when", Json::object()).value(field, 0);
				if (ImGui::SliderInt(tool_label(field).c_str(), &stars, 0, 3, stars ? "%d stars" : "Any"))
				{
					if (stars)
						(*selected)["when"][field] = stars;
					else if (selected->contains("when"))
						(*selected)["when"].erase(field);
				}
			}
			for (const auto &component : console_ui_components())
				if (selected->value("component", "") == component.id && !component.feature.empty())
					ImGui::TextWrapped("Requires available %s capability on this station.", component.feature.c_str());
			bool styled = selected->contains("style");
			if (ImGui::Checkbox(tool_label("Override component appearance").c_str(), &styled))
			{
				if (styled)
					(*selected)["style"] = {{"text", "#fff0d4"}, {"font_size", 22}};
				else
					selected->erase("style");
			}
			if (styled)
			{
				layout_string_field("Component text colour", (*selected)["style"], "text");
				float size = selected->at("style").value("font_size", 22.f);
				if (ImGui::DragFloat(tool_label("Component font size").c_str(), &size, 1, 12, 128))
					(*selected)["style"]["font_size"] = size;
			}
			if (selected->value("kind", "") == "component")
				layout_string_field("Binding target", *selected, "component");
			if (selected->contains("group") && tool_button(tool_label("Rename reusable group").c_str()))
			{
				layout_workspace.rename_from = selected->at("group").get<std::string>();
				layout_workspace.rename_kind = WorkshopDefinitionKind::UiGroup;
			}
			if (tool_button(tool_label("Detach reusable group").c_str()) && selected->contains("group"))
			{
				*selected = expand_ui_groups(*catalog, *selected);
			}
			if (tool_button(tool_label("Delete layout node").c_str()))
			{
				layout_remove_node(root, layout_workspace.selected);
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
		ImGui::InputText(tool_label("New reusable ID").c_str(), &layout_workspace.asset_name);
		if (!current_theme.empty() && tool_button(tool_label("Rename theme").c_str()))
		{
			layout_workspace.rename_from = current_theme;
			layout_workspace.rename_kind = WorkshopDefinitionKind::Theme;
		}
		if (tool_button(tool_label("Create theme").c_str()))
		{
			layout_workspace.theme_name =
			    document.manifest().at("id").get<std::string>() + ".theme" + std::to_string(layout_workspace.serial++);
			auto theme = resolved_ui_theme(*catalog, current_theme, console.value("theme_overrides", Json::object()));
			theme["id"] = layout_workspace.theme_name;
			auto &rows = document.definitions()["themes"];
			if (!rows.is_array())
				rows = Json::array();
			rows.push_back(theme);
			console["theme"] = layout_workspace.theme_name;
			console.erase("theme_overrides");
		}
		for (auto &theme : document.definitions()["themes"])
			if (theme.at("id") == console.value("theme", std::string{}))
			{
				for (auto &token : theme["tokens"].items())
				{
					if (token.value().is_number())
					{
						float n = token.value();
						if (ImGui::DragFloat(tool_label(token.key()).c_str(), &n, .5f, 0, 128))
							token.value() = n;
					}
					else
					{
						auto value = token.value().get<std::string>();
						if (!value.empty() && value.front() == '#')
						{
							std::uint32_t packed = 0;
							const auto parsed =
							    std::from_chars(value.data() + 1, value.data() + value.size(), packed, 16);
							if ((value.size() == 7 || value.size() == 9) && parsed.ec == std::errc{} &&
							    parsed.ptr == value.data() + value.size())
							{
								if (value.size() == 7)
									packed = (packed << 8) | 0xff;
								const auto colour = GetColor(packed);
								float rgba[] = {colour.r / 255.f, colour.g / 255.f, colour.b / 255.f, colour.a / 255.f};
								if (ImGui::ColorEdit4(tool_label(token.key() + " colour").c_str(), rgba))
								{
									static constexpr char hex[] = "0123456789abcdef";
									std::string next = "#";
									for (float channel : rgba)
									{
										const int n = std::clamp(static_cast<int>(std::round(channel * 255)), 0, 255);
										next += hex[n / 16];
										next += hex[n % 16];
									}
									value = next;
									token.value() = value;
								}
							}
							else
								ImGui::TextWrapped(
								    "%s",
								    localise("Colour must be #RRGGBB or #RRGGBBAA using hexadecimal digits.").c_str());
						}
						if (ImGui::InputText(tool_label(token.key()).c_str(), &value))
							token.value() = value;
					}
				}
				if (tool_button(tool_label("Use theme for pack and menus").c_str()))
					document.manifest()["theme"] = theme.at("id");
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
						float scale = theme.value("asset_scale", Json::object()).value(slot, 1.f);
						if (ImGui::DragFloat(tool_label(slot + " artwork scale").c_str(), &scale, .005f, .001f, 8.f))
							theme["asset_scale"][slot] = scale;
						bool sliced = theme.contains("slices") && theme.at("slices").contains(slot);
						if (ImGui::Checkbox(tool_label(std::string("Nine-slice ") + slot).c_str(), &sliced))
						{
							if (sliced)
								theme["slices"][slot] = {0, 0, 0, 0};
							else
								theme["slices"].erase(slot);
						}
						if (sliced)
							for (int edge = 0; edge < 4; ++edge)
							{
								int v = theme["slices"][slot][edge];
								if (ImGui::DragInt(
								        tool_label(std::string(slot) + " inset " + std::to_string(edge)).c_str(), &v, 1,
								        0, 16384))
									theme["slices"][slot][edge] = v;
							}
					}
				if (ImGui::BeginCombo(tool_label("Artwork asset").c_str(), "Choose imported asset"))
				{
					for (const auto &[id, bytes] : catalog->assets)
						if (ImGui::Selectable(id.c_str()) && !layout_workspace.asset_id.empty())
							theme["assets"][layout_workspace.asset_id] = id;
					ImGui::EndCombo();
				}
			}
		ImGui::EndDisabled();
		ImGui::End();
		if (layout_workspace.focus_canvas)
		{
			ImGui::SetNextWindowFocus();
			layout_workspace.focus_canvas = false;
		}
		tool_window(tool_label("Layout canvas").c_str());
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
		if (layout_workspace.hud)
		{
			sample["layout"] = console[layout_field];
			if (console.contains("compact"))
				sample["widgets"] = console.at("compact").at("widgets");
			sample.erase("layout_overrides");
		}
		ImGui::SliderInt(tool_label("Preview complexity stars").c_str(), &layout_workspace.preview.sample_stars, 1, 3);
		sample["_preview_header"] = layout_workspace.preview.header;
		sample["_preview_stars"] = layout_workspace.preview.sample_stars;
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
				                                    it->id == layout_workspace.selected ? IM_COL32(255, 210, 80, 255)
				                                                                        : IM_COL32(100, 180, 220, 120));
				if (it->id == layout_workspace.selected)
					ImGui::GetWindowDrawList()->AddRectFilled({b.x - 10, b.y - 10}, b, IM_COL32(255, 210, 80, 255));
				if (hovered && ImGui::IsMouseClicked(0) && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y &&
				    mouse.y < b.y)
				{
					layout_workspace.selected = it->id;
					layout_workspace.canvas_drag = true;
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
					if (parent && parent->value("kind", "") == "instruments" && resolved.containers.contains(parent_id))
					{
						const auto bounds = resolved.containers.at(parent_id);
						auto rect = layout_workspace.drag_rect;
						const auto step = layout_workspace.snap ? 10.f : 1.f;
						const auto dx =
						    (mouse.x - layout_workspace.drag_start_x) / scale * 1000 / std::max(1.f, bounds.width);
						const auto dy =
						    (mouse.y - layout_workspace.drag_start_y) / scale * 1000 / std::max(1.f, bounds.height);
						if (ImGui::GetIO().KeyShift || layout_workspace.resizing)
						{
							rect["width"] = std::clamp(
							    static_cast<int>(std::round((rect.at("width").get<float>() + dx) / step) * step), 1,
							    1000 - rect.at("x").get<int>());
							rect["height"] = std::clamp(
							    static_cast<int>(std::round((rect.at("height").get<float>() + dy) / step) * step), 1,
							    1000 - rect.at("y").get<int>());
						}
						else
						{
							rect["x"] =
							    std::clamp(static_cast<int>(std::round((rect.at("x").get<float>() + dx) / step) * step),
							               0, 1000 - rect.at("width").get<int>());
							rect["y"] =
							    std::clamp(static_cast<int>(std::round((rect.at("y").get<float>() + dy) / step) * step),
							               0, 1000 - rect.at("height").get<int>());
						}
						(*moving)["rect"] = rect;
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
		ImGui::TextWrapped("%s", localise("Drag instruments to move. Shift-drag resizes. Use hierarchy drag/drop to "
		                                  "move panels between containers.")
		                             .c_str());
		for (const auto &diagnostic : resolved.diagnostics)
			ImGui::TextWrapped("%s", diagnostic.c_str());
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
