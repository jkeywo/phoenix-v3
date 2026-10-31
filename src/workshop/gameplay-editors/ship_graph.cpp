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
		auto resolved = document.preview_equipment();
		auto ship = resolved.entities.contains(graph_entity) ? resolved.entities.at(graph_entity) : *authored;
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
		std::vector<Node> nodes;
		std::function<void(const Json &, std::string, std::string, std::string)> collect;
		collect = [&](const Json &value, std::string path, std::string category, std::string parent)
		{
			if (value.is_array())
				for (std::size_t i = 0; i < value.size(); ++i)
					collect(value[i], path + "/" + std::to_string(i), category, parent);
			else if (value.is_object())
			{
				if (value.contains("id"))
				{
					nodes.push_back({path, value.at("id"), category, parent, value, {}});
					parent = path;
				}
				for (const auto &[key, child] : value.items())
					if (child.is_array() || child.is_object())
					{
						if (path == "/systems" && child.is_object())
						{
							auto named = child;
							named["id"] = key;
							collect(named, path + "/" + key, "systems", parent);
						}
						else
							collect(child, path + "/" + key, child.is_array() ? key : category, parent);
					}
			}
		};
		for (const auto *key :
		     {"combat", "systems", "assemblies", "stations", "control_layout", "resources", "logistics"})
			if (ship.contains(key))
				collect(ship.at(key), std::string("/") + key, key, {});
		struct Port
		{
			std::string field, category;
			bool multiple = false;
		};
		auto ports = [&](const Node &node)
		{
			std::vector<Port> result;
			static const std::map<std::string, std::string> kinds{{"compartment", "compartments"},
			                                                      {"console", "consoles"},
			                                                      {"controller", "controllers"},
			                                                      {"system", "systems"},
			                                                      {"systems", "systems"},
			                                                      {"feeds", "systems"},
			                                                      {"consumers", "systems"},
			                                                      {"power_store", "stores"},
			                                                      {"store", "stores"},
			                                                      {"generator", "generators"},
			                                                      {"power_group", "groups"},
			                                                      {"coolant_source", "coolant_sources"},
			                                                      {"cooling_link", "cooling_links"},
			                                                      {"turret", "systems"},
			                                                      {"conduit", "links"}};
			for (const auto &[key, value] : node.value.items())
			{
				if (kinds.contains(key) && (value.is_string() || value.is_array()))
					result.push_back({"/" + key, kinds.at(key), value.is_array()});
				if (key == "equipment" && value.is_object())
					for (const auto &[role, binding] : value.items())
						result.push_back({"/equipment/" + role, "systems", binding.is_array()});
				if ((key == "from" || key == "to") && node.category == "links")
					result.push_back({"/" + key, "stores"});
			}
			if (node.category == "systems")
				for (const auto *key : {"compartment", "power_store", "coolant_source"})
					if (!node.value.contains(key))
						result.push_back({std::string("/") + key, kinds.at(key)});
			return result;
		};
		std::map<std::string, std::vector<Port>> node_ports;
		for (const auto &node : nodes)
			node_ports[node.path] = ports(node);
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
					std::string edit_path = graph_node;
					Json assembly_value;
					const auto &shown = ship.at(Json::json_pointer(graph_node));
					if (shown.contains("_assembly_origin"))
					{
						const auto &origin = shown.at("_assembly_origin");
						const auto placements = ship.value("assemblies", Json::array());
						for (std::size_t i = 0; i < placements.size(); ++i)
							if (placements[i].at("id") == origin.at("placement"))
							{
								auto source = document.preview_definition(WorkshopDefinitionKind::Assembly,
								                                          origin.at("assembly"));
								source.merge_patch(placements[i].value("overrides", Json::object()));
								if (graph_node.starts_with("/systems/"))
								{
									edit_path = "/assemblies/" + std::to_string(i) + "/overrides/systems/" +
									            origin.at("local_id").get<std::string>();
									assembly_value = source.at("systems").at(origin.at("local_id").get<std::string>());
								}
								else
								{
									// Resource arrays replace as a family; inspect the owning placement instead of copying expanded identities.
									edit_path = "/assemblies/" + std::to_string(i);
								}
								break;
							}
					}
					const auto pointer = Json::json_pointer(edit_path);
					const bool inherited = !authored->contains(pointer);
					if (inherited)
					{
						text("Inherited item");
						if (button("Override graph section locally"))
						{
							const auto top = graph_node.substr(1, graph_node.find('/', 1) - 1);
							if (!assembly_value.is_null())
							{
								if (!authored->contains("assemblies"))
									(*authored)["assemblies"] = ship.at("assemblies");
								(*authored)[pointer] = assembly_value;
							}
							else
								(*authored)[top] = ship.value("_equipment_source", ship).value(top, ship.at(top));
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
								auto candidate = document;
								for (auto &entry : candidate.definitions()["entities"])
									if (entry.at("id") == graph_entity)
									{
										auto &binding = entry[Json::json_pointer(edit_path + graph_port)];
										if (binding.is_array() && !graph_link.empty())
										{
											const auto id = ship.at(Json::json_pointer(graph_link)).at("id");
											binding.erase(std::remove(binding.begin(), binding.end(), id),
											              binding.end());
										}
										else
										{
											auto path = Json::json_pointer(edit_path + graph_port);
											const auto key = path.back();
											path.pop_back();
											entry.at(path).erase(key);
										}
									}
								candidate.verify();
								for (const auto &entry : candidate.definitions()["entities"])
									if (entry.at("id") == graph_entity)
										(*authored)[pointer] = entry.at(pointer);
								graph_port.clear();
								graph_link.clear();
							}
						}
						structure((*authored)[pointer]);
						if (button("Remove selected ship item"))
						{
							auto candidate = document;
							for (auto &entry : candidate.definitions()["entities"])
								if (entry.at("id") == graph_entity)
								{
									const auto split = edit_path.find_last_of('/');
									const auto key = edit_path.substr(split + 1);
									auto &parent = entry[Json::json_pointer(edit_path.substr(0, split))];
									if (parent.is_array())
										parent.erase(parent.begin() + std::stoull(key));
									else
										parent.erase(key);
								}
							candidate.verify();
							for (const auto &entry : candidate.definitions()["entities"])
								if (entry.at("id") == graph_entity)
									*authored = entry;
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
							const auto target_id = ship.at(Json::json_pointer(graph_link)).at("id");
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
										auto candidate = document;
										for (auto &entry : candidate.definitions()["entities"])
											if (entry.at("id") == graph_entity)
											{
												auto &binding = entry[Json::json_pointer(edit_path + port.field)];
												if (port.multiple)
												{
													if (!binding.is_array())
														binding = Json::array();
													if (std::find(binding.begin(), binding.end(), target_id) ==
													    binding.end())
														binding.push_back(target_id);
												}
												else
													binding = target_id;
											}
										candidate.verify();
										for (const auto &entry : candidate.definitions()["entities"])
											if (entry.at("id") == graph_entity)
												(*authored)[pointer] = entry.at(pointer);
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
