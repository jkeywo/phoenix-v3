module;
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
module phoenix.workshop.authoring_core;
import phoenix.content.ui;

namespace phoenix
{
Json workshop_preview_tree(const WorkshopDocument &document, Json source)
{
	if (!source.contains("context") && !source.contains("extends"))
		source["context"] = "controller";
	return document.preview_tree(source);
}
Json workshop_tree_state(const std::string &id)
{
	return {{"id", id},
	        {"entry", Json::array()},
	        {"running", Json::array()},
	        {"exit", Json::array()},
	        {"transitions", Json::array()}};
}
Json workshop_definition_default(const std::string &kind, const std::string &context)
{
	if (kind == "entities")
	{
		WorkshopDocument draft;
		const auto id = draft.create_entity();
		auto value = draft.definitions().at("entities")[0];
		value.erase("id");
		return value;
	}
	if (kind == "trees")
	{
		if (context != "controller" && context != "scenario" && context != "conversation" && context != "away" &&
		    context != "team" && context != "team_workflow" && context != "system")
			throw std::runtime_error("Unknown tree context: " + context);
		if (context == "team_workflow")
			return {{"context", context},
			        {"order_ids", Json::array({"treat", "recover"})},
			        {"initial", "operating"},
			        {"states", Json::array({workshop_tree_state("operating")})}};
		return {{"context", context},
		        {"initial", "operating"},
		        {"states", Json::array({workshop_tree_state("operating")})}};
	}
	if (kind == "maps")
		return {{"name", "New map"}, {"entities", Json::array()}, {"ship_slots", Json::array()}};
	if (kind == "consoles")
	{
		Json value{{"widgets", Json::array()}};
		value["layout"] = convert_console_layout(value);
		return value;
	}
	if (kind == "layouts" || kind == "ui_groups")
		return {{"schema", 1}, {"root", {{"id", "root"}, {"kind", "column"}, {"children", Json::array()}}}};
	if (kind == "themes")
		return resolved_ui_theme(Catalog{}, "", Json::object());
	static const std::map<std::string, std::string> records{
	    {"tasks", "gameplay-editors.scenarios.tasks"},
	    {"equipment", "gameplay-editors.effects.equipment"},
	    {"assemblies", "gameplay-editors.effects.assemblies"},
	    {"effects", "gameplay-editors.effects.effects"},
	    {"properties", "gameplay-editors.effects.properties"},
	    {"resources", "gameplay-editors.effects.resources"},
	    {"recipes", "gameplay-editors.effects.recipes"},
	    {"control_widgets", "gameplay-editors.effects.control_widgets"},
	    {"messages", "gameplay-editors.knowledge.messages"}};
	if (records.contains(kind))
	{
		auto value = workshop_record(records.at(kind));
		value.erase("id");
		return value;
	}
	return Json::object();
}
Json workshop_tree_localize(WorkshopDocument &document, const Json &binding, const std::string &context, bool copy)
{
	auto value = copy ? (binding.is_string() ? document.preview_tree(binding.get<std::string>())
	                                         : document.preview_tree(binding))
	                  : workshop_definition_default("trees", context);
	value.erase("id");
	return value;
}
Json workshop_tree_promote(WorkshopDocument &document, Json &binding, const std::string &id, bool policy)
{
	if (!binding.is_object())
		throw std::runtime_error("Promote an owner-local tree object");
	if (!valid_identifier(id))
		throw std::runtime_error("Invalid reusable tree identity");
	const auto choices = document.definition_choices(WorkshopDefinitionKind::Tree);
	if (std::find(choices.begin(), choices.end(), id) != choices.end())
		throw std::runtime_error("Tree identity already exists");
	auto copy = binding;
	copy["id"] = id;
	if (!copy.contains("context") && !copy.contains("extends"))
		copy["context"] = "controller";
	// Replace the owner before appending: it may itself live inside the trees array.
	binding = policy ? Json{{"extends", id}} : Json(id);
	document.definitions()["trees"].push_back(std::move(copy));
	return id;
}
Json workshop_tree_add_state(Json &tree, const std::string &requested)
{
	auto &states = tree.at("states");
	std::string id = requested;
	auto used = [&](const std::string &name)
	{
		return std::any_of(states.begin(), states.end(),
		                   [&](const Json &row)
		                   {
			                   return row.at("id") == name;
		                   });
	};
	if (id.empty())
	{
		unsigned serial = 1;
		do
		{
			id = "state" + std::to_string(serial++);
		} while (used(id));
	}
	if (used(id))
		throw std::runtime_error("State identity already exists");
	states.push_back(workshop_tree_state(id));
	return id;
}
std::string workshop_tree_removal_error(const Json &tree, const std::string &id)
{
	if (tree.value("initial", Json()) == id)
		return "Resolve the root initial reference before deleting this state";
	bool found = false;
	for (const auto &row : tree.at("states"))
	{
		found = found || row.at("id") == id;
		if (row.value("parent", Json()) == id || row.value("initial", Json()) == id)
			return "Resolve child/initial references before deleting this state";
		for (const auto &edge : row.value("transitions", Json::array()))
			if (edge.at("target") == id)
				return "Resolve transition references before deleting this state";
	}
	if (!found)
		return "Unknown state";
	return {};
}
void workshop_tree_remove_state(Json &tree, const std::string &id)
{
	const auto error = workshop_tree_removal_error(tree, id);
	if (!error.empty())
		throw std::runtime_error(error);
	auto &states = tree["states"];
	states.erase(std::remove_if(states.begin(), states.end(),
	                            [&](const Json &row)
	                            {
		                            return row.at("id") == id;
	                            }),
	             states.end());
}
Json workshop_tree_task(const std::string &kind)
{
	if (kind == "action")
		return {{"action", ""}, {"arguments", Json::array()}, {"once", false}};
	if (kind == "call")
		return {{"call", ""}, {"parameters", Json::object()}};
	if (kind == "attempt")
		return {{"attempt", Json::array({Json{{"tasks", Json::array()}}})}};
	if (kind == "intent" || kind == "memory")
		return {{kind, ""}, {"value", 0}};
	throw std::runtime_error("Unknown task kind");
}
Json workshop_action_arguments(const Json &signature)
{
	Json values = Json::array();
	for (const auto &argument : signature.at("arguments"))
	{
		const auto type = argument.at("type").get<std::string>();
		values.push_back(type == "boolean"   ? Json(false)
		                 : type == "text"    ? Json("")
		                 : type == "integer" ? Json(0)
		                                     : Json{{"type", type}, {"id", 1}});
	}
	return values;
}
Bytes workshop_source_archive(const WorkshopDocument &document)
{
	document.validate_draft();
	return write_pack(document.source_draft(), PackEncoding::Zip);
}
} // namespace phoenix
