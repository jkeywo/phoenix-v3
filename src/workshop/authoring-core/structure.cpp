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
import phoenix.content.sensing;
import phoenix.conversations;
import phoenix.gameplay_vocabulary;
namespace phoenix
{
Json workshop_prepare_vocabulary(const Json &vocabulary)
{
	if (vocabulary.contains("controller") || vocabulary.contains("system") || vocabulary.contains("scenario"))
	{
		return vocabulary;
	}
	Json contexts = Json::object();
	for (const auto &[context, mask] :
	     std::vector<std::pair<std::string, unsigned>>{{"system", 1}, {"scenario", 2}, {"controller", 4}})
	{
		contexts[context] = {{"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
		for (const char *kind : {"actions", "observations", "intents"})
			if (vocabulary.contains(kind))
				for (auto descriptor : vocabulary.at(kind))
					if ((descriptor.value("contexts", 3u) & mask) != 0)
					{
						descriptor["id"] = descriptor.at("name");
						contexts[context][kind].push_back(std::move(descriptor));
					}
	}
	const auto dialogue = conversation_registry();
	contexts["conversation"] = workshop_seed("gameplay-editors.gameplay_editor.conversation");
	for (const auto &f : dialogue.observations)
		contexts["conversation"]["observations"].push_back(
		    {{"id", f.name},
		     {"name", f.name},
		     {"type", f.type == state_tree::Type::Text      ? "text"
		              : f.type == state_tree::Type::Boolean ? "boolean"
		                                                    : "integer"}});
	for (const auto &a : dialogue.actions)
		contexts["conversation"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "definition"}, {"type", "text"}}})},
		     {"effect", true}});
	const auto teams = team_registry();
	contexts["team"] = workshop_seed("gameplay-editors.gameplay_editor.team");
	for (const auto &f : teams.observations)
		contexts["team"]["observations"].push_back(
		    {{"id", f.name}, {"type", f.type == state_tree::Type::Boolean ? "boolean" : "integer"}});
	for (const auto &a : teams.actions)
		contexts["team"]["actions"].push_back({{"id", a.name}, {"arguments", Json::array()}});
	const auto away = away_registry(Json{{"boarding", true}});
	contexts["away"] = workshop_seed("gameplay-editors.gameplay_editor.away");
	for (const auto &f : away.observations)
		contexts["away"]["observations"].push_back({{"id", f.name},
		                                            {"name", f.name},
		                                            {"type", f.type == state_tree::Type::Text      ? "text"
		                                                     : f.type == state_tree::Type::Boolean ? "boolean"
		                                                                                           : "integer"}});
	for (const auto &a : away.actions)
		contexts["away"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", a.name == "work" ? "integer" : "text"}}})},
		     {"effect", true}});
	const auto encounter = interdiction_registry();
	contexts["interdiction"] = workshop_seed("gameplay-editors.gameplay_editor.interdiction");
	for (const auto &f : encounter.observations)
		contexts["interdiction"]["observations"].push_back(
		    {{"id", f.name}, {"name", f.name}, {"type", f.type == state_tree::Type::Text ? "text" : "boolean"}});
	for (const auto &a : encounter.actions)
		contexts["interdiction"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", "text"}}})},
		     {"effect", true}});
	return contexts;
}
Json workshop_tree_vocabulary(const WorkshopDocument &document, const Json &source)
{
	const auto tree = workshop_preview_tree(document, source);
	const auto context = tree.value("context", std::string("system"));
	const std::map<std::string, state_tree::Context> contexts{{"system", state_tree::Context::System},
	                                                          {"scenario", state_tree::Context::Scenario},
	                                                          {"controller", state_tree::Context::Controller},
	                                                          {"conversation", state_tree::Context::Conversation},
	                                                          {"away", state_tree::Context::Away},
	                                                          {"team", state_tree::Context::Team},
	                                                          {"team_workflow", state_tree::Context::TeamWorkflow}};
	const auto kind = contexts.at(context);
	const auto registry = context == "controller"      ? document.preview_controller_registry(source)
	                      : context == "conversation"  ? conversation_registry()
	                      : context == "away"          ? away_registry(tree)
	                      : context == "team_workflow" ? team_workflow_registry(tree)
	                      : context == "team"          ? team_registry()
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
		const auto component = a.value("component", std::string{});
		if (component.starts_with("equipment/control/"))
		{
			const auto leaf = component.rfind('/');
			const auto role = component.rfind('/', leaf - 1);
			if (role == std::string::npos || role <= 18 || leaf <= role + 1 || leaf + 1 == component.size())
				throw std::runtime_error("Equipment drop needs a Controller, role and meter");
			const auto owner = component.substr(18, role - 18);
			bool has_owner = false;
			for (auto ancestor = to; !ancestor.empty();)
			{
				const auto *parent = workshop_layout_node(candidate, ancestor);
				if (parent && parent->contains("controller_scope"))
				{
					if (parent->at("controller_scope") != owner)
						throw std::runtime_error("Drop equipment into its owning Controller scope");
					has_owner = true;
					break;
				}
				const auto end = ancestor.rfind('.');
				ancestor = end == std::string::npos ? std::string{} : ancestor.substr(0, end);
			}
			added["kind"] = "column";
			added["equipment_scope"] = component.substr(role + 1, leaf - role - 1);
			added["children"] = Json::array({{{"id", "meter"}, {"kind", "component"},
			                                   {"component", "meter/" + component.substr(leaf + 1)}}});
			if (!has_owner)
			{
				auto equipment = std::move(added);
				equipment["id"] = "equipment";
				added = {{"id", id}, {"kind", "column"}, {"controller_scope", owner},
				         {"children", Json::array({std::move(equipment)})}};
			}
		}
		else if (component.starts_with("scope/control/"))
		{
			const auto separator = component.rfind('/');
			if (separator <= 14 || separator + 1 == component.size())
				throw std::runtime_error("Controller scope needs a named part");
			added["kind"] = "column";
			added["controller_scope"] = component.substr(14, separator - 14);
			added["children"] = Json::array({{{"id", "part"}, {"kind", "component"},
			                                   {"component", "part/" + component.substr(separator + 1)}}});
		}
		else if (!component.empty())
		{
			added["kind"] = "component";
			added["component"] = component;
			for (auto ancestor = to; component.starts_with("control/") && !ancestor.empty();)
			{
				const auto *parent = workshop_layout_node(candidate, ancestor);
				if (parent && parent->contains("controller_scope"))
				{
					const auto owner = "control/" + parent->at("controller_scope").get<std::string>() + "/";
					if (!component.starts_with(owner))
						throw std::runtime_error("Controller part belongs to a different Controller");
					added["component"] = "part/" + component.substr(owner.size());
					break;
				}
				const auto end = ancestor.rfind('.');
				ancestor = end == std::string::npos ? std::string{} : ancestor.substr(0, end);
			}
		}
		else if (!a.value("group", std::string{}).empty())
			added["group"] = a.value("group", std::string{});
		else if (!a.value("layout_ref", std::string{}).empty())
			added["layout_ref"] = a.at("layout_ref");
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
