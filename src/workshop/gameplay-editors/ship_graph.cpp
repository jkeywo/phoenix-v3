module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
module phoenix.workshop.gameplay;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::ship_graph(WorkshopDocument &document, std::string &selection)
{
	if (!ship_graph_open)
		return;
	if (ship_graph_focus)
	{
		ImGui::SetNextWindowFocus();
		ship_graph_focus = false;
	}
	if (!tool_window(label("Ship setup").c_str()))
	{
		ImGui::End();
		return;
	}
	try
	{
		Json *authored = nullptr;
		for (auto &ship : document.definitions()["entities"])
			if (ship.at("id") == graph_entity)
				authored = &ship;
		if (!authored)
		{
			ship_graph_open = false;
			ImGui::End();
			return;
		}
		text(graph_entity.c_str());
		if (ImGui::CollapsingHeader(label("Add ship item").c_str()))
		{
			Json kind{{"Item type", graph_new_kind}};
			if (edit_registered(kind, "gameplay-editors.ship_graph.Item type"))
				graph_new_kind = kind.at("Item type");
			ImGui::InputText(label("Item identity").c_str(), &graph_new_id);
			if (graph_new_kind == "Assembly" || graph_new_kind == "Controller widget" ||
			    graph_new_kind == "Console seat")
			{
				const auto type = graph_new_kind == "Assembly"            ? WorkshopDefinitionKind::Assembly
				                  : graph_new_kind == "Controller widget" ? WorkshopDefinitionKind::ControlWidget
				                                                          : WorkshopDefinitionKind::Console;
				Json selected{{"Definition", graph_new_template}};
				if (choice(selected, "Definition", document.definition_choices(type)))
					graph_new_template = selected.at("Definition");
			}
			if (button("Add item") && !graph_new_id.empty())
			{
				const auto path = workshop_ship_add(*authored, {{"id", graph_new_id},
				                                                {"kind", graph_new_kind},
				                                                {"definition", graph_new_template},
				                                                {"selection", graph_node}})
				                      .get<std::string>();
				graph_node = path;
				graph_target = WorkshopTarget::capture(*authored, path);
				selection = "Ship graph";
				graph_new_id.clear();
			}
		}

		if (button("Fit / automatic layout"))
		{
			graph_fit = true;
			graph_zoom = 1;
			graph_pan_x = graph_pan_y = 0;
		}
		ImGui::SameLine();
		ImGui::SliderFloat(label("Zoom").c_str(), &graph_zoom, .05f, 2.f);
		struct Node
		{
			std::string path, id, category, parent;
			Json value;
			ImVec2 point;
		};
		struct Port
		{
			std::string field, category;
			bool multiple = false;
		};
		const auto graph = workshop_ship_graph(document, graph_entity, "inspect", Json::object());
		auto ship = graph.at("ship");
		std::vector<Node> nodes;
		std::map<std::string, std::vector<Port>> node_ports;
		for (const auto &node : graph.at("nodes"))
		{
			node_ports[node.at("path")];
			nodes.push_back(
			    {node.at("path"), node.at("id"), node.at("category"), node.at("parent"), node.at("value"), {}});
			for (const auto &port : node.at("ports"))
				node_ports[node.at("path")].push_back({port.at("field"), port.at("category"), port.at("multiple")});
		}
		auto ports = [&](const Node &node)
		{
			return node_ports.at(node.path);
		};
		if (!graph_node.empty())
		{
			if (auto *selected = graph_target.resolve(ship))
			{
				for (const auto &node : nodes)
					if (&ship.at(Json::json_pointer(node.path)) == selected)
					{
						graph_node = node.path;
						break;
					}
			}
			else
				graph_node.clear();
		}
		std::set<std::string> categories;
		for (const auto &node : nodes)
			categories.insert(node.category);
		for (const auto &category : categories)
		{
			bool show = !graph_hidden.contains(category);
			if (ImGui::Checkbox(category.c_str(), &show))
			{
				if (show)
					graph_hidden.erase(category);
				else
					graph_hidden.insert(category);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
		const auto available = ImGui::GetContentRegionAvail();
		if (graph_fit)
		{
			std::map<std::string, std::size_t> counts;
			std::size_t maximum = 1;
			for (const auto &node : nodes)
				if (!graph_hidden.contains(node.category))
					maximum = std::max(maximum, ++counts[node.category]);
			graph_zoom = std::clamp(
			    std::min(available.x / std::max(210.f, counts.size() * 210.f), available.y / (maximum * 90.f)) * .9f,
			    .05f, 2.f);
			graph_pan_x = graph_pan_y = 0;
			graph_fit = false;
		}

		{
			ScopedChild canvas("ship-graph", {0, 0}, ImGuiWindowFlags_HorizontalScrollbar);
			const auto origin = ImGui::GetCursorScreenPos();
			if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(2))
			{
				graph_pan_x += ImGui::GetIO().MouseDelta.x;
				graph_pan_y += ImGui::GetIO().MouseDelta.y;
			}
			std::map<std::string, int> columns, rows;
			for (const auto &category : categories)
				if (!graph_hidden.contains(category))
					columns[category] = static_cast<int>(columns.size());
			for (auto &node : nodes)
				node.point = {origin.x + graph_pan_x + columns[node.category] * 210.f * graph_zoom,
				              origin.y + graph_pan_y + rows[node.category]++ * 90.f * graph_zoom};
			auto *draw = ImGui::GetWindowDrawList();
			auto edge = [&](const Node &a, const Node &b, const std::string &caption)
			{
				if (graph_hidden.contains(a.category) || graph_hidden.contains(b.category))
					return;
				ImVec2 from{a.point.x + 160 * graph_zoom, a.point.y + 20 * graph_zoom},
				    to{b.point.x, b.point.y + 20 * graph_zoom};
				draw->AddBezierCubic(from, {from.x + 40, from.y}, {to.x - 40, to.y}, to, IM_COL32(100, 180, 200, 160),
				                     1.5f);
				draw->AddText({(from.x + to.x) * .5f, (from.y + to.y) * .5f}, IM_COL32(180, 210, 220, 255),
				              caption.c_str());
				const auto mid = ImVec2{(from.x + to.x) * .5f, (from.y + to.y) * .5f};
				ImGui::SetCursorScreenPos(mid);
				ScopedId connection((a.path + "->" + b.path + caption).c_str());
				if (ImGui::InvisibleButton("connection", {std::max(16.f, ImGui::CalcTextSize(caption.c_str()).x),
				                                          ImGui::GetFontSize() + 4}))
				{
					graph_node = a.path;
					graph_target = WorkshopTarget::capture(ship, a.path);
					graph_port = caption;
					graph_link = b.path;
					selection = "Ship graph";
				}
			};
			for (const auto &node : nodes)
				for (const auto &other : nodes)
				{
					if (node.parent == other.path)
						edge(other, node, "contains");
					for (const auto &port : node_ports.at(node.path))
						if (port.category == other.category && node.value.contains(Json::json_pointer(port.field)))
						{
							const auto &value = node.value.at(Json::json_pointer(port.field));
							if ((value.is_string() && value == other.id) ||
							    (value.is_array() &&
							     std::find(value.begin(), value.end(), Json(other.id)) != value.end()))
								edge(node, other, port.field);
						}
				}
			for (const auto &node : nodes)
			{
				if (graph_hidden.contains(node.category))
					continue;
				ImGui::SetCursorScreenPos(node.point);
				ScopedId scope(node.path.c_str());
				const bool inherited = !authored->contains(Json::json_pointer(node.path));
				if (ImGui::Selectable((node.id + (inherited ? " *" : "")).c_str(), graph_node == node.path, 0,
				                      {160 * graph_zoom, 42 * graph_zoom}))
				{
					graph_node = node.path;
					graph_port.clear();
					graph_link.clear();
					graph_target = WorkshopTarget::capture(ship, node.path);
					selection = "Ship graph";
				}
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PHOENIX_SHIP_NODE", node.path.c_str(), node.path.size() + 1);
					text("Drop on another node, then choose the binding field.");
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const auto *payload = ImGui::AcceptDragDropPayload("PHOENIX_SHIP_NODE"))
					{
						graph_node = static_cast<const char *>(payload->Data);
						graph_port.clear();
						graph_target = WorkshopTarget::capture(ship, graph_node);
						graph_link = node.path;
					}
					ImGui::EndDragDropTarget();
				}
			}
			ImGui::SetCursorScreenPos(origin);
			ImGui::Dummy(
			    {std::max(1.f, columns.size() * 210.f * graph_zoom), std::max(1.f, nodes.size() * 90.f * graph_zoom)});
		}
		if (selection == "Ship graph")
			if (ScopedWindow properties(label("Properties").c_str()); properties)
			{
				if (ImGui::CollapsingHeader(label("Ship item list").c_str()))
				{
					ScopedChild items("ship-items", {0, 160}, ImGuiWindowFlags_HorizontalScrollbar);
					for (const auto &node : nodes)
						if (ImGui::Selectable((node.category + " / " + node.id + "###list" + node.path).c_str(),
						                      graph_node == node.path))
						{
							graph_node = node.path;
							graph_port.clear();
							graph_link.clear();
							graph_target = WorkshopTarget::capture(ship, node.path);
						}
				}
				if (!graph_node.empty() && ship.contains(Json::json_pointer(graph_node)))
				{
					Json edit;
					for (const auto &node : graph.at("nodes"))
						if (node.at("path") == graph_node)
							edit = node;
					const auto edit_path = edit.at("edit_path").get<std::string>();
					const auto pointer = Json::json_pointer(edit_path);
					const bool inherited = !authored->contains(pointer);
					if (inherited)
					{
						text("Inherited item");
						if (button("Override graph section locally"))
						{
							(void)workshop_ship_graph(document, graph_entity, "override", {{"node", graph_node}});
						}
					}
					else
					{
						if (!graph_port.empty() && graph_port != "contains" &&
						    (*authored)[pointer].contains(Json::json_pointer(graph_port)))
						{
							text(graph_port.c_str());
							if (button("Remove selected connection"))
							{
								Json args{{"node", graph_node}, {"field", graph_port}};
								if (!graph_link.empty())
									args["destination"] = graph_link;
								(void)workshop_ship_graph(document, graph_entity, "disconnect", args);
								graph_port.clear();
								graph_link.clear();
							}
						}
						structure((*authored)[pointer]);
						if (button("Remove selected ship item"))
						{
							(void)workshop_ship_graph(document, graph_entity, "remove", {{"node", graph_node}});
							graph_node.clear();
							graph_link.clear();
							graph_port.clear();
						}
						if (ImGui::BeginCombo(label("Connection destination").c_str(),
						                      graph_link.empty() ? localise("Choose compatible item").c_str()
						                                         : graph_link.c_str()))
						{
							const auto source = std::find_if(nodes.begin(), nodes.end(),
							                                 [&](const Node &node)
							                                 {
								                                 return node.path == graph_node;
							                                 });
							if (source != nodes.end())
								for (const auto &target : nodes)
								{
									const auto available = ports(*source);
									if (std::any_of(available.begin(), available.end(),
									                [&](const Port &port)
									                {
										                return port.category == target.category;
									                }))
										if (ImGui::Selectable((target.category + " / " + target.id).c_str(),
										                      graph_link == target.path))
											graph_link = target.path;
								}
							ImGui::EndCombo();
						}
						if (!graph_link.empty() && ship.contains(Json::json_pointer(graph_link)))
						{
							text("Choose the property to connect to the dropped target:");
							const auto source = std::find_if(nodes.begin(), nodes.end(),
							                                 [&](const Node &n)
							                                 {
								                                 return n.path == graph_node;
							                                 });
							const auto target = std::find_if(nodes.begin(), nodes.end(),
							                                 [&](const Node &n)
							                                 {
								                                 return n.path == graph_link;
							                                 });
							if (source != nodes.end() && target != nodes.end())
								for (const auto &port : node_ports.at(source->path))
									if (port.category == target->category && button(("Connect " + port.field).c_str()))
									{
										(void)workshop_ship_graph(
										    document, graph_entity, "connect",
										    {{"node", graph_node}, {"field", port.field}, {"destination", graph_link}});
										graph_link.clear();
										break;
									}
							if (button("Cancel connection"))
								graph_link.clear();
						}
					}
				}
			}
	}
	catch (const std::exception &error)
	{
		ImGui::TextWrapped("%s", error.what());
	}
	ImGui::End();
}
} // namespace phoenix
