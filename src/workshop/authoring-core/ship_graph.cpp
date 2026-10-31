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
namespace phoenix
{
namespace
{
Json &ship_source(WorkshopDocument &document, const std::string &id)
{
	for (auto &ship : document.definitions().at("entities"))
		if (ship.at("id") == id)
			return ship;
	throw std::runtime_error("Create an explicit local ship override first");
}
Json graph_model(WorkshopDocument &document, const std::string &id)
{
	const auto &authored = ship_source(document, id);
	const auto catalog = document.preview_equipment();
	const auto ship = catalog.entities.at(id);
	struct Node
	{
		std::string path, id, category, parent;
		Json value;
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
				nodes.push_back({path, value.at("id"), category, parent, value});
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
	for (const auto *key : {"combat", "systems", "assemblies", "stations", "control_layout", "resources", "logistics"})
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

	Json result = Json::array();
	for (const auto &node : nodes)
	{
		std::string path = node.path;
		Json assembly_value;
		if (node.value.contains("_assembly_origin"))
		{
			const auto &origin = node.value.at("_assembly_origin");
			const auto placements = ship.value("assemblies", Json::array());
			for (std::size_t i = 0; i < placements.size(); ++i)
				if (placements[i].at("id") == origin.at("placement"))
				{
					if (node.path.starts_with("/systems/"))
					{
						auto source =
						    document.preview_definition(WorkshopDefinitionKind::Assembly, origin.at("assembly"));
						source.merge_patch(placements[i].value("overrides", Json::object()));
						const auto local = origin.at("local_id").get<std::string>();
						const auto escaped = Json::json_pointer("") / local;
						path = "/assemblies/" + std::to_string(i) + "/overrides/systems" + escaped.to_string();
						assembly_value = source.at("systems").at(local);
					}
					else
						path = "/assemblies/" + std::to_string(i);
					break;
				}
		}
		Json available = Json::array();
		for (const auto &port : ports(node))
			available.push_back({{"field", port.field}, {"category", port.category}, {"multiple", port.multiple}});
		result.push_back({{"path", node.path},
		                  {"id", node.id},
		                  {"category", node.category},
		                  {"parent", node.parent},
		                  {"value", node.value},
		                  {"ports", available},
		                  {"edit_path", path},
		                  {"inherited", !authored.contains(Json::json_pointer(path))},
		                  {"assembly_value", assembly_value}});
	}
	return {{"ship", ship}, {"nodes", result}};
}
} // namespace
Json workshop_ship_graph(WorkshopDocument &document, const std::string &entity, const std::string &action,
                         const Json &a)
{
	auto graph = graph_model(document, entity);
	if (action == "inspect")
		return graph;
	const auto from = a.at("node").get<std::string>();
	Json selected;
	for (const auto &node : graph.at("nodes"))
		if (node.at("path") == from)
			selected = node;
	if (selected.is_null())
		throw std::runtime_error("Unknown ship graph node");
	auto &authored = ship_source(document, entity);
	const auto path = selected.at("edit_path").get<std::string>();
	const auto pointer = Json::json_pointer(path);
	if (action == "override")
	{
		if (!selected.at("inherited").get<bool>())
			return path;
		if (!selected.at("assembly_value").is_null())
		{
			if (!authored.contains("assemblies"))
				authored["assemblies"] = graph.at("ship").at("assemblies");
			authored[pointer] = selected.at("assembly_value");
		}
		else
		{
			const auto top = from.substr(1, from.find('/', 1) - 1);
			authored[top] =
			    graph.at("ship").value("_equipment_source", graph.at("ship")).value(top, graph.at("ship").at(top));
		}
		return path;
	}
	if (selected.at("inherited").get<bool>())
		throw std::runtime_error("Override the inherited graph section before editing");
	auto candidate = document;
	auto &changed = ship_source(candidate, entity);
	if (action == "remove")
	{
		auto at = pointer;
		const auto key = at.back();
		at.pop_back();
		auto &parent = changed.at(at);
		if (parent.is_array())
			parent.erase(parent.begin() + std::stoull(key));
		else
			parent.erase(key);
	}
	else if (action == "connect" || action == "disconnect")
	{
		const auto field = a.at("field").get<std::string>();
		Json port;
		for (const auto &available : selected.at("ports"))
			if (available.at("field") == field)
				port = available;
		if (port.is_null())
			throw std::runtime_error("Unknown or incompatible binding field");
		Json destination;
		if (a.contains("destination"))
			for (const auto &node : graph.at("nodes"))
				if (node.at("path") == a.at("destination"))
					destination = node;
		if (action == "connect" && (destination.is_null() || port.at("category") != destination.at("category")))
			throw std::runtime_error("Incompatible connection destination");
		if (action == "connect")
		{
			auto &binding = changed[Json::json_pointer(path + field)];
			if (port.at("multiple").get<bool>())
			{
				if (!binding.is_array())
					binding = Json::array();
				if (std::find(binding.begin(), binding.end(), destination.at("id")) == binding.end())
					binding.push_back(destination.at("id"));
			}
			else
				binding = destination.at("id");
		}
		else
		{
			auto at = Json::json_pointer(path + field);
			if (!changed.contains(at))
				throw std::runtime_error("Binding does not exist");
			auto &binding = changed.at(at);
			if (binding.is_array() && !destination.is_null())
				binding.erase(std::remove(binding.begin(), binding.end(), destination.at("id")), binding.end());
			else
			{
				const auto key = at.back();
				at.pop_back();
				changed.at(at).erase(key);
			}
		}
	}
	else
		throw std::runtime_error("Unknown graph operation");
	(void)candidate.verify();
	authored = changed;
	return true;
}
void workshop_set_parent(WorkshopDocument &document, const std::string &parent)
{
	if (document.pack().manifest.contains("base"))
		throw std::runtime_error("A base pack's parent is fixed");
	const auto root = document.libraries().base.id;
	if (root.empty())
		throw std::runtime_error("Load the selected base and parent ancestry first");
	std::map<std::string, Json> manifests;
	for (const auto &bytes : document.dependency_archives())
	{
		const auto pack = read_pack(bytes);
		manifests[pack.manifest().at("id")] = pack.manifest();
	}
	std::set<std::string> seen{document.pack().manifest.at("id").get<std::string>()};
	auto current = parent;
	while (current != root)
	{
		if (!seen.insert(current).second || !manifests.contains(current))
			throw std::runtime_error("Parent ancestry is missing or cyclic");
		current = manifests.at(current).value("parent", std::string{});
	}
	auto candidate = document;
	auto source = candidate.source_draft();
	source.manifest.erase("dependencies");
	source.manifest["parent"] = parent;
	const auto map = candidate.definitions().at("maps").empty() ? Json() : candidate.map().at("id");
	candidate.load_draft(std::move(source));
	if (!map.is_null())
		for (std::size_t i = 0; i < candidate.definitions().at("maps").size(); ++i)
			if (candidate.definitions().at("maps")[i].at("id") == map)
				candidate.select_map(i);
	document = std::move(candidate);
}
} // namespace phoenix
