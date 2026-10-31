module;
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
module phoenix.workshop.authoring_core;
import phoenix.content.ui;
import phoenix.content.operations;
import phoenix.conversations;
import phoenix.gameplay_vocabulary;
namespace phoenix
{
Json workshop_tree_vocabulary(WorkshopDocument &document, const Json &source)
{
	const auto tree = document.preview_tree(source);
	const auto context = tree.value("context", std::string("system"));
	const std::map<std::string, state_tree::Context> contexts{{"system", state_tree::Context::System},
	                                                          {"scenario", state_tree::Context::Scenario},
	                                                          {"controller", state_tree::Context::Controller},
	                                                          {"conversation", state_tree::Context::Conversation},
	                                                          {"away", state_tree::Context::Away},
	                                                          {"team", state_tree::Context::Team}};
	const auto kind = contexts.at(context);
	const auto registry = context == "controller"     ? document.preview_controller_registry(source)
	                      : context == "conversation" ? conversation_registry()
	                      : context == "away"         ? away_registry(tree)
	                      : context == "team"         ? team_registry()
	                                                  : gameplay_tree_registry(kind);
	auto type_name = [](state_tree::Type type)
	{
		static const char *names[]{"missing", "boolean", "integer",   "text",   "entity",
		                           "system",  "console", "objective", "region", "message"};
		return names[static_cast<unsigned>(type)];
	};
	Json result{
	    {"context", context}, {"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
	for (const auto &field : registry.observations)
		if (field.contexts & static_cast<unsigned>(kind))
			result["observations"].push_back({{"id", field.name}, {"type", type_name(field.type)}});
	for (const auto &field : registry.intents)
		if (field.contexts & static_cast<unsigned>(kind))
			result["intents"].push_back({{"id", field.name}, {"type", type_name(field.type)}});
	for (const auto &action : registry.actions)
		if (action.contexts & static_cast<unsigned>(kind))
		{
			Json arguments = Json::array();
			for (std::size_t i = 0; i < action.arguments.size(); ++i)
				arguments.push_back({{"name", std::to_string(i + 1)}, {"type", type_name(action.arguments[i])}});
			result["actions"].push_back(
			    {{"id", action.name}, {"name", action.name}, {"arguments", arguments}, {"effect", action.effect}});
		}
	return result;
}
Json *workshop_layout_node(Json &node, const std::string &identity, const std::string &prefix)
{
	const auto id = prefix + node.value("id", std::string{});
	if (identity == id)
		return &node;
	if (node.contains("children"))
		for (auto &child : node["children"])
			if (auto *found = workshop_layout_node(child, identity, id + "."))
				return found;
	return nullptr;
}
bool workshop_layout_remove(Json &node, const std::string &identity, const std::string &prefix)
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
		if (workshop_layout_remove(children[i], identity, id + "."))
			return true;
	}
	return false;
}
Json workshop_layout_drop(Json &root, const Json &a, const Catalog &catalog)
{
	auto candidate = root;
	const auto from = a.value("source", std::string{}), to = a.at("destination").get<std::string>();
	std::string selected;
	int serial = 1;
	int insertion = a.value("index", -1);
	if (!from.empty() && insertion >= 0 && from.substr(0, from.find_last_of('.')) == to)
		if (const auto *parent = workshop_layout_node(candidate, to); parent && parent->contains("children"))
			for (std::size_t i = 0; i < parent->at("children").size(); ++i)
				if (to + "." + parent->at("children")[i].value("id", std::string{}) == from &&
				    static_cast<int>(i) < insertion)
					--insertion;
	Json added;
	if (!from.empty())
	{
		if (from == to || to.starts_with(from + "."))
			throw std::runtime_error("A container cannot be moved inside itself");
		if (auto *node = workshop_layout_node(candidate, from))
			added = *node;
		if (!workshop_layout_remove(candidate, from))
			throw std::runtime_error("Cannot move the layout root");
	}
	else
	{
		std::string id;
		do
		{
			id = "panel" + std::to_string(serial++);
		} while (workshop_layout_node(candidate, to + "." + id));
		added = {{"id", id}, {"label", "Panel"}};
		if (!a.value("component", std::string{}).empty())
		{
			added["kind"] = "component";
			added["component"] = a.value("component", std::string{});
		}
		else if (!a.value("group", std::string{}).empty())
			added["group"] = a.value("group", std::string{});
		else
		{
			added["kind"] = a.value("kind", std::string{});
			added["children"] = Json::array();
		}
	}
	auto *target = workshop_layout_node(candidate, to);
	if (target && target->contains("children") && !added.is_null())
	{
		if (target->value("kind", "") == "overlay")
		{
			std::set<std::string> used;
			for (const auto &child : target->at("children"))
				used.insert(child.value("region", std::string{}));
			std::string region;
			for (const auto *r : {"background", "top-left", "top-right", "bottom-left", "bottom-right"})
				if (!used.contains(r))
				{
					region = r;
					break;
				}
			if (region.empty())
				throw std::runtime_error("All overlay regions are occupied");
			added["region"] = region;
		}
		else
			added.erase("region");
		if (target->value("kind", "") == "instruments" && !added.contains("rect"))
			added["rect"] = {{"x", static_cast<int>(a.value("x", 0))},
			                 {"y", static_cast<int>(a.value("y", 0))},
			                 {"width", 300},
			                 {"height", 300}};
		auto &children = (*target)["children"];
		auto index = insertion < 0 ? children.size() : std::min(children.size(), static_cast<std::size_t>(insertion));
		if (!a.value("kind", std::string{}).empty() && !children.empty())
		{
			index = std::min(index, children.size() - 1);
			auto wrapped = children[index];
			wrapped.erase("region");
			if (a.value("kind", std::string{}) == "overlay")
				wrapped["region"] = "background";
			if (a.value("kind", std::string{}) == "cutout")
				wrapped["region"] = "centre";
			added["children"].push_back(wrapped);
			children.erase(children.begin() + index);
		}
		children.insert(children.begin() + index, added);
		Json checked = {{"schema", 1}, {"root", candidate}};
		(void)compile_ui_tree(catalog, checked);
		root = std::move(candidate);
		selected = to + "." + added.at("id").get<std::string>();
	}
	if (selected.empty())
		throw std::runtime_error("Drop destination must be a container");
	return selected;
}
void workshop_tree_rename(Json &tree, const std::string &from, const std::string &to)
{
	if (to.empty())
		throw std::runtime_error("State identity cannot be empty");
	auto candidate = tree;
	Json *state = nullptr;
	for (auto &row : candidate.at("states"))
	{
		if (row.at("id") == to)
			throw std::runtime_error("State identity already exists");
		if (row.at("id") == from)
			state = &row;
	}
	if (!state)
		throw std::runtime_error("Unknown state");
	(*state)["id"] = to;
	if (candidate.value("initial", std::string{}) == from)
		candidate["initial"] = to;
	for (auto &row : candidate["states"])
	{
		for (const auto *key : {"parent", "initial"})
			if (row.value(key, std::string{}) == from)
				row[key] = to;
		if (row.contains("transitions"))
			for (auto &edge : row["transitions"])
				if (edge.at("target") == from)
					edge["target"] = to;
	}
	tree = std::move(candidate);
}

} // namespace phoenix
