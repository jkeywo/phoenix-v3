module;
#include <algorithm>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;
import phoenix.content.ui;

namespace phoenix
{
namespace
{
std::string fresh(const Json &rows, const std::string &prefix)
{
	for (unsigned serial = 1;; ++serial)
	{
		const auto id = prefix + std::to_string(serial);
		if (std::none_of(rows.begin(), rows.end(),
		                 [&](const Json &row)
		                 {
			                 return row.value("id", Json()) == id;
		                 }))
			return id;
	}
}
Json &parent_node(Json &root, const std::string &id)
{
	const auto split = id.find_last_of('.');
	if (split == std::string::npos)
		throw std::runtime_error("The layout root cannot be removed or duplicated");
	auto *parent = workshop_layout_node(root, id.substr(0, split));
	if (!parent || !parent->contains("children"))
		throw std::runtime_error("Unknown layout parent");
	return *parent;
}
Json &console_source(WorkshopDocument &document, const std::string &id)
{
	for (auto &row : document.definitions().at("consoles"))
		if (row.at("id") == id)
			return row;
	throw std::runtime_error("Create an explicit local Console override first");
}
std::pair<std::string, std::string> fields(const Json &a)
{
	const auto field = a.value("field", std::string("layout"));
	if (field != "layout" && field != "header" && field != "hud_layout")
		throw std::runtime_error("Unknown Console layout field");
	return {field, field == "layout"   ? "layout_overrides"
	               : field == "header" ? "header_overrides"
	                                   : "hud_layout_overrides"};
}
} // namespace
Json workshop_layout_action(WorkshopDocument &document, Json &value, const std::string &action, const Json &a)
{
	if (action == "convert" || action == "detach" || action == "override" || action == "reference" ||
	    action == "revert")
	{
		const auto [field, overrides] = fields(a);
		if (action == "convert")
		{
			if (field == "header")
				value[field] = console_ui_header_template();
			else
			{
				auto source = value;
				if (field == "hud_layout" && value.contains("compact"))
					source["widgets"] = value.at("compact").at("widgets");
				value[field] = convert_console_layout(source);
			}
			return value.at(field);
		}
		if (action == "reference")
		{
			const auto id = a.at("id").get<std::string>();
			if (!document.preview_ui()->layouts.contains(id))
				throw std::runtime_error("Unknown layout template");
			value[field] = id;
			value.erase(overrides);
			return id;
		}
		if (action == "revert")
		{
			value.erase(overrides);
			return true;
		}
		if (!value.at(field).is_string())
			throw std::runtime_error("Select a referenced layout first");
		auto resolved = document.preview_ui()->layouts.at(value.at(field).get<std::string>());
		if (action == "override")
		{
			value[overrides] = resolved;
			return value.at(overrides);
		}
		if (value.contains(overrides))
			resolved.merge_patch(value.at(overrides));
		resolved.erase("id");
		resolved.erase("extends");
		value[field] = std::move(resolved);
		value.erase(overrides);
		return value.at(field);
	}
	if (action == "compact")
	{
		if (a.value("enabled", true))
		{
			value["compact"] = value.at("root");
			value["compact"]["kind"] = "tabs";
		}
		else
			value.erase("compact");
		return true;
	}
	if (action == "save-template" || action == "group.save")
	{
		auto authored = action == "group.save" ? Json{{"schema", 1}, {"root", value}} : value;
		return workshop_apply(
		    document, "create",
		    {{"kind", action == "group.save" ? "ui_groups" : "layouts"}, {"id", a.at("id")}, {"value", authored}});
	}
	if (action == "group.detach")
	{
		value = expand_ui_groups(*document.preview_ui(), value);
		return value;
	}
	if (action == "theme.create")
	{
		const auto choices = document.definition_choices(WorkshopDefinitionKind::Theme);
		Json identities = Json::array();
		for (const auto &id : choices)
			identities.push_back({{"id", id}});
		const auto id =
		    a.value("id", fresh(identities, document.pack().manifest.at("id").get<std::string>() + ".theme"));
		auto theme = resolved_ui_theme(*document.preview_ui(), value.value("theme", std::string{}),
		                               value.value("theme_overrides", Json::object()));
		(void)workshop_apply(document, "create", {{"kind", "themes"}, {"id", id}, {"value", theme}});
		value["theme"] = id;
		value.erase("theme_overrides");
		return id;
	}
	if (action == "theme.pack")
	{
		document.manifest()["theme"] = value.at("id");
		return value.at("id");
	}
	if (action == "widget.add")
	{
		const auto kind = a.at("kind").get<std::string>();
		std::string binding;
		bool found = false;
		for (const auto &[name, target] : widget_bindings())
			if (kind == name)
			{
				binding = target;
				found = true;
			}
		if (!found)
			throw std::runtime_error("Unsupported widget kind");
		auto widgets = value.value("widgets", Json::array());
		const auto id = a.value("id", fresh(widgets, kind));
		if (std::any_of(widgets.begin(), widgets.end(),
		                [&](const Json &row)
		                {
			                return row.at("id") == id;
		                }))
			throw std::runtime_error("Widget identity already exists");
		Json *parent = nullptr;
		if (a.contains("layout"))
		{
			auto &root = value.at(Json::json_pointer(a.at("layout").get<std::string>()));
			parent = workshop_layout_node(root, a.value("parent", root.at("id").get<std::string>()));
			if (!parent || !parent->contains("children"))
				throw std::runtime_error("Widget destination must be a layout container");
			for (const auto &child : parent->at("children"))
				if (child.at("id") == id)
					throw std::runtime_error("Layout child identity already exists");
		}
		widgets.push_back({{"id", id},
		                   {"kind", kind},
		                   {"binding", binding},
		                   {"label", kind},
		                   {"x", 0},
		                   {"y", 0},
		                   {"width", 500},
		                   {"height", 500}});
		if (parent)
			(*parent)["children"].push_back({{"id", id},
			                                 {"kind", "component"},
			                                 {"component", id},
			                                 {"label", kind},
			                                 {"rect", {{"x", 0}, {"y", 0}, {"width", 500}, {"height", 500}}}});
		value["widgets"] = std::move(widgets);
		return id;
	}
	if (action == "node.add")
	{
		const auto parent_id = a.value("parent", value.at("id").get<std::string>());
		auto *parent = workshop_layout_node(value, parent_id);
		if (!parent || !parent->contains("children"))
			throw std::runtime_error("Layout destination must be a container");
		auto &children = (*parent)["children"];
		const auto kind = a.value("kind", std::string("component"));
		static const std::set<std::string> kinds{"component", "row",         "column", "wrap",
		                                         "tabs",      "instruments", "cutout", "overlay"};
		if (!kinds.contains(kind))
			throw std::runtime_error("Unknown layout kind");
		const auto id = a.value("id", fresh(children, a.contains("layout_ref") ? "layout"
		                                              : a.contains("group")    ? "group"
		                                                                       : "panel"));
		for (const auto &child : children)
			if (child.at("id") == id)
				throw std::runtime_error("Layout identity already exists");
		Json node{{"id", id}};
		if (a.contains("layout_ref"))
		{
			if (a.contains("group") || !document.preview_ui()->layouts.contains(a.at("layout_ref")))
				throw std::runtime_error("Invalid reusable layout reference");
			node["layout_ref"] = a.at("layout_ref");
		}
		else if (a.contains("group"))
		{
			if (!document.preview_ui()->ui_groups.contains(a.at("group")))
				throw std::runtime_error("Unknown reusable group");
			node["group"] = a.at("group");
		}
		else
		{
			node["kind"] = kind;
			node["label"] = "Panel";
			if (kind == "component")
				node["component"] = a.at("component");
			else
				node["children"] = Json::array();
		}
		children.push_back(std::move(node));
		return parent_id + "." + id;
	}
	const auto id = a.at("node").get<std::string>();
	auto *selected = workshop_layout_node(value, id);
	if (!selected)
		throw std::runtime_error("Unknown layout node");
	if (action == "node.delete")
	{
		if (!workshop_layout_remove(value, id))
			throw std::runtime_error("Cannot remove the layout root");
		return true;
	}
	auto &parent = parent_node(value, id);
	auto &children = parent["children"];
	auto found = std::find_if(children.begin(), children.end(),
	                          [&](const Json &row)
	                          {
		                          return row.at("id") == selected->at("id");
	                          });
	if (action == "node.reorder")
	{
		const auto direction = a.at("direction").get<int>();
		if (direction != -1 && direction != 1)
			throw std::runtime_error("direction must be -1 or 1");
		const auto index = found - children.begin();
		if (index + direction < 0 || index + direction >= static_cast<std::ptrdiff_t>(children.size()))
			throw std::runtime_error("Node is already at the collection boundary");
		std::iter_swap(found, found + direction);
		return id;
	}
	if (action != "node.duplicate")
		throw std::runtime_error("Unknown layout operation");
	auto &console = console_source(document, a.at("console"));
	auto widgets = console.at("widgets");
	auto copy = expand_ui_groups(*document.preview_ui(), *selected);
	std::function<void(Json &, unsigned)> duplicate = [&](Json &node, unsigned depth)
	{
		if (depth > 24)
			throw std::runtime_error("Layout duplication exceeds the nesting limit");
		if (node.contains("component"))
		{
			auto original = std::find_if(console.at("widgets").begin(), console.at("widgets").end(),
			                             [&](const Json &widget)
			                             {
				                             return widget.at("id") == node.at("component");
			                             });
			if (original == console.at("widgets").end() || original->at("kind") == "conversations" ||
			    original->at("kind") == "navigation")
				throw std::runtime_error(
				    "Built-in panels have one live instance per Console; reuse their group on another Console");
			auto widget = *original;
			const auto next = fresh(widgets, "widget-copy");
			widget["id"] = next;
			widgets.push_back(widget);
			node["component"] = next;
		}
		if (node.contains("children"))
			for (auto &child : node["children"])
				duplicate(child, depth + 1);
	};
	duplicate(copy, 0);
	const auto next = fresh(children, "copy");
	copy["id"] = next;
	children.push_back(copy);
	console["widgets"] = std::move(widgets);
	return id.substr(0, id.find_last_of('.')) + "." + next;
}
} // namespace phoenix
