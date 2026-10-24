module;
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <cstdint>
#include <map>
#include <set>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.content;

#include "knowledge_overlay.hpp"

namespace phoenix
{
namespace
{
using DefinitionTable = std::map<std::string, Json>;
struct DraftTable
{
	const char *name;
	const DefinitionTable &library;
};
DraftTable draft_table(const Catalog &library, WorkshopDefinitionKind kind)
{
	switch (kind)
	{
	case WorkshopDefinitionKind::Layout:
		return {"layouts", library.layouts};
	case WorkshopDefinitionKind::UiGroup:
		return {"ui_groups", library.ui_groups};
	case WorkshopDefinitionKind::Theme:
		return {"themes", library.themes};
	case WorkshopDefinitionKind::Entity:
		return {"entities", library.entities};
	case WorkshopDefinitionKind::Map:
		return {"maps", library.maps};
	case WorkshopDefinitionKind::Console:
		return {"consoles", library.consoles};
	case WorkshopDefinitionKind::Tree:
		return {"trees", library.trees};
	case WorkshopDefinitionKind::Equipment:
		return {"equipment", library.equipment};
	case WorkshopDefinitionKind::Assembly:
		return {"assemblies", library.assemblies};
	case WorkshopDefinitionKind::Effect:
		return {"effects", library.effects};
	case WorkshopDefinitionKind::Resource:
		return {"resources", library.resources};
	case WorkshopDefinitionKind::Recipe:
		return {"recipes", library.recipes};
	case WorkshopDefinitionKind::Message:
		return {"messages", library.messages};
	}
	throw std::runtime_error("Unknown Workshop definition kind");
}
const Json &draft_rows(const Json &definitions, const char *kind)
{
	static const Json empty = Json::array();
	if (!definitions.contains(kind))
		return empty;
	const auto &rows = definitions.at(kind);
	if (!rows.is_array())
		throw std::runtime_error(std::string(kind) + ": expected array");
	return rows;
}
std::string draft_identity(const Json &entry)
{
	if (!entry.is_object() || !entry.contains("id") || !entry.at("id").is_string())
		return {};
	return entry.at("id").get<std::string>();
}
const Json &draft_definition(const Json &definitions, DraftTable table, const std::string &id)
{
	const Json *found = nullptr;
	for (const auto &entry : draft_rows(definitions, table.name))
		if (draft_identity(entry) == id)
		{
			if (found)
				throw std::runtime_error(std::string(table.name) + "." + id + ": duplicate draft definition");
			found = &entry;
		}
	if (found)
		return *found;
	if (const auto dependency = table.library.find(id); dependency != table.library.end())
		return dependency->second;
	throw std::runtime_error(std::string(table.name) + "." + id + ": unknown definition");
}
DefinitionTable draft_definitions(const Json &definitions, DraftTable table)
{
	auto result = table.library;
	std::set<std::string> seen;
	for (const auto &entry : draft_rows(definitions, table.name))
	{
		const auto id = draft_identity(entry);
		if (id.empty() || !seen.insert(id).second)
			throw std::runtime_error(std::string(table.name) + "." + id + ": missing or duplicate draft identity");
		result[id] = entry;
	}
	return result;
}
Catalog tree_preview_catalog(const Json &tree, const Catalog &library, const Json &definitions)
{
	Catalog result;
	const auto table = draft_table(library, WorkshopDefinitionKind::Tree);
	std::function<void(const Json &, std::size_t)> collect = [&](const Json &source, std::size_t depth)
	{
		if (depth > 64)
			throw std::runtime_error("trees: inheritance exceeds 64 levels");
		const auto id = source.value("id", std::string("<inline>"));
		if (result.trees.contains(id))
			return; // The production resolver diagnoses cycles.
		result.trees[id] = source;
		if (source.contains("extends"))
			collect(draft_definition(definitions, table, source.at("extends").get<std::string>()), depth + 1);
	};
	collect(tree, 0);
	return result;
}
} // namespace
Json authored_equipment_source(Json resolved);
WorkshopDocument::WorkshopDocument()
{
	reset();
}
Catalog WorkshopDocument::preview_ui() const
{
	Catalog preview = library_catalog_;
	preview.ui_theme = pack_.manifest.value("theme", preview.ui_theme);
	preview.layouts = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Layout));
	preview.ui_groups = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::UiGroup));
	preview.themes = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Theme));
	preview.consoles = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Console));
	inherit_definitions(preview.layouts, "layouts");
	inherit_definitions(preview.ui_groups, "ui_groups");
	inherit_definitions(preview.themes, "themes");
	inherit_definitions(preview.consoles, "consoles");
	const auto prefix = pack_.manifest.at("id").get<std::string>() + ":";
	for (const auto &[path, bytes] : pack_.files)
		if (path.starts_with("assets/"))
			preview.assets[prefix + path] = bytes;
	return preview;
}
Catalog WorkshopDocument::preview_equipment() const
{
	Catalog preview;
	preview.entities = library_catalog_.entities;
	for (auto &[id, entity] : preview.entities)
		entity = authored_equipment_source(std::move(entity));
	preview.layouts = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Layout));
	preview.ui_groups = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::UiGroup));
	preview.themes = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Theme));
	inherit_definitions(preview.layouts, "layouts");
	inherit_definitions(preview.ui_groups, "ui_groups");
	inherit_definitions(preview.themes, "themes");
	preview.entities = draft_definitions(definitions_, {"entities", preview.entities});
	preview.equipment =
	    draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Equipment));
	preview.effects = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Effect));
	preview.resources =
	    draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Resource));
	preview.recipes = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Recipe));
	preview.assemblies =
	    draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Assembly));
	preview.trees = draft_definitions(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Tree));
	for (const auto &[kind, table] :
	     std::vector<std::pair<const char *, std::map<std::string, Json> *>>{{"effects", &preview.effects},
	                                                                         {"resources", &preview.resources},
	                                                                         {"recipes", &preview.recipes},
	                                                                         {"entities", &preview.entities},
	                                                                         {"equipment", &preview.equipment},
	                                                                         {"assemblies", &preview.assemblies}})
	{
		inherit_definitions(*table, kind);
	}
	resolve_equipment(preview);
	return preview;
}
Json WorkshopDocument::preview_tree(const Json &tree) const
{
	const auto preview = tree_preview_catalog(tree, library_catalog_, definitions_);
	return resolve_tree_definition(tree, preview);
}
Json WorkshopDocument::preview_tree(const std::string &id) const
{
	return preview_tree(
	    draft_definition(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Tree), id));
}
Json WorkshopDocument::preview_definition(WorkshopDefinitionKind kind, const std::string &id) const
{
	if (kind == WorkshopDefinitionKind::Tree)
		return preview_tree(id);
	const auto table = draft_table(library_catalog_, kind);
	DefinitionTable closure;
	std::function<void(const std::string &, std::size_t)> collect = [&](const std::string &name, std::size_t depth)
	{
		if (depth > 64)
			throw std::runtime_error(std::string(table.name) + ": inheritance exceeds 64 levels");
		if (closure.contains(name))
			return;
		const auto &source = draft_definition(definitions_, table, name);
		closure[name] = source;
		if (source.contains("extends"))
			collect(source.at("extends").get<std::string>(), depth + 1);
	};
	collect(id, 0);
	inherit_definitions(closure, table.name, id);
	return closure.at(id);
}
std::vector<std::string> WorkshopDocument::definition_choices(WorkshopDefinitionKind kind) const
{
	const auto table = draft_table(library_catalog_, kind);
	std::vector<std::string> result;
	std::set<std::string> seen;
	for (const auto &entry : draft_rows(definitions_, table.name))
	{
		const auto id = draft_identity(entry);
		if (!id.empty() && seen.insert(id).second)
			result.push_back(id);
	}
	for (const auto &[id, entry] : table.library)
		if (seen.insert(id).second)
			result.push_back(id);
	return result;
}
Json WorkshopDocument::preview_knowledge() const
{
	auto result = library_catalog_.knowledge;
	overlay_knowledge(result, definitions_.value("knowledge", Json::object()), "knowledge");
	return result;
}
state_tree::Registry WorkshopDocument::preview_controller_registry(const Json &tree) const
{
	try
	{
		auto catalog = tree_preview_catalog(tree, library_catalog_, definitions_);
		const auto resolved = resolve_tree_definition(tree, catalog);
		std::set<std::string> schemas;
		for (const auto *kind : {"record_bindings", "message_bindings"})
			for (const auto &binding : resolved.value(kind, Json::array()))
				schemas.insert(binding.at("schema").get<std::string>());
		// Compile only the schemas needed for this vocabulary. Invalid unrelated
		// draft schemas must not disable a valid Controller's authoring palette.
		auto select = [&](const Json &knowledge)
		{
			Json selected = knowledge;
			if (!selected.is_object())
				throw std::runtime_error("knowledge: expected object");
			selected["schemas"] = Json::array();
			for (const auto &schema : draft_rows(knowledge, "schemas"))
				if (schemas.contains(draft_identity(schema)))
					selected["schemas"].push_back(schema);
			return selected;
		};
		auto knowledge = select(library_catalog_.knowledge);
		overlay_knowledge(knowledge, select(definitions_.value("knowledge", Json::object())), "knowledge");
		// Vocabulary depends on role/schema types, not retention tuning.
		knowledge.erase("history_limit");
		std::set<std::string> roles;
		for (const auto &schema : knowledge.at("schemas"))
			for (const auto *kind : {"producers", "readers"})
				for (const auto &role : schema.at(kind))
					roles.insert(role.get<std::string>());
		auto &declared = knowledge["roles"];
		declared.erase(std::remove_if(declared.begin(), declared.end(),
		                              [&](const auto &role)
		                              {
			                              return !role.is_string() || !roles.contains(role.template get<std::string>());
		                              }),
		               declared.end());
		catalog.knowledge_definition = compile_knowledge(knowledge);
		for (const auto &name : resolved.value("message_templates", Json::array()))
		{
			const auto id = name.get<std::string>();
			catalog.messages[id] =
			    draft_definition(definitions_, draft_table(library_catalog_, WorkshopDefinitionKind::Message), id);
		}
		return compile_controller_bindings(tree, catalog).registry;
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error("trees." + tree.value("id", std::string("<inline>")) + ": " + error.what());
	}
}
std::vector<Bytes> WorkshopDocument::dependency_archives() const
{
	std::vector<Bytes> result;
	for (const auto &pack : libraries_)
		result.push_back(pack.archive());
	return result;
}
void WorkshopDocument::reset()
{
	pack_ = PackDraft{};
	pack_.manifest = {{"schema", 1}, {"id", "workshop"}};
	libraries_.clear();
	library_catalog_ = Catalog{};
	definitions_ = {{"schema", 1},
	                {"entities", Json::array()},
	                {"maps", Json::array()},
	                {"consoles", Json::array()},
	                {"trees", Json::array()}};
	new_map("workshop.map");
}
void WorkshopDocument::new_map(const std::string &id)
{
	for (const auto &map : definitions_["maps"])
		if (map["id"] == id)
			throw std::runtime_error("Map id already exists: " + id);
	definitions_["maps"].push_back({{"id", id}, {"entities", Json::array()}, {"ship_slots", Json::array()}});
	map_index_ = definitions_["maps"].size() - 1;
}
void WorkshopDocument::select_map(std::size_t index)
{
	if (index >= definitions_["maps"].size())
		throw std::runtime_error("Unknown map index");
	map_index_ = index;
}
void WorkshopDocument::load_pack(std::span<const std::uint8_t> archive)
{
	auto loaded = read_pack(archive);
	// A standalone pack opens independently of the current draft's dependencies.
	// A mod retains the explicitly loaded dependency closure it declares.
	auto dependencies = pack_dependencies(loaded.manifest());
	std::set<std::string> needed;
	for (const auto &dependency : dependencies)
		needed.insert(dependency);
	for (auto it = libraries_.rbegin(); it != libraries_.rend(); ++it)
		if (needed.contains(it->manifest().at("id").get<std::string>()))
			for (const auto &dependency : pack_dependencies(it->manifest()))
				needed.insert(dependency);
	std::vector<Pack> retained;
	for (const auto &library : libraries_)
		if (needed.contains(library.manifest().at("id").get<std::string>()))
			retained.push_back(library);
	auto order = retained;
	order.push_back(loaded);
	auto resolved = resolve_packs(order);
	if (resolved.presentation)
		throw std::runtime_error("Console assets are not an editable gameplay pack");
	auto definitions = parse_json(loaded.files().at("definitions.json"), "definitions.json");
	// Resolved Controllers can refer to generated officer/combat programs. Keep
	// their authored layouts so export regenerates those programs on the next
	// load instead of serialising references to programs absent from the draft.
	DefinitionTable authored_entities;
	for (const auto &pack : order)
	{
		const auto source = parse_json(pack.files().at("definitions.json"), "definitions.json");
		for (const auto &entity : source.value("entities", Json::array()))
			authored_entities[entity.at("id").get<std::string>()] = entity;
	}
	inherit_definitions(authored_entities, "entities");
	for (auto kind : {"entities", "maps", "consoles", "trees", "layouts", "ui_groups", "themes"})
		if (!definitions.contains(kind))
			definitions[kind] = Json::array();
	auto flatten = [&](const char *kind, const auto &table)
	{
		for (auto &value : definitions[kind])
			value = table.at(value.at("id").template get<std::string>());
	};
	flatten("entities", resolved.entities);
	for (auto &entity : definitions["entities"])
	{
		const auto &authored = authored_entities.at(entity.at("id").get<std::string>());
		if (authored.contains("control_layout"))
			entity["control_layout"] = authored.at("control_layout");
	}
	for (auto &entity : definitions["entities"])
		if (entity.contains("_equipment_source"))
		{
			const auto source = entity.at("_equipment_source");
			for (const auto *field : {"systems", "resources", "assemblies", "services", "controllers"})
			{
				entity.erase(field);
				if (source.contains(field))
					entity[field] = source.at(field);
			}
			entity.erase("_equipment_source");
		}
	flatten("maps", resolved.maps);
	flatten("consoles", resolved.consoles);
	flatten("layouts", resolved.layouts);
	flatten("ui_groups", resolved.ui_groups);
	flatten("themes", resolved.themes);
	flatten("trees", resolved.trees);
	if (definitions["maps"].empty())
		definitions["maps"].push_back(
		    {{"id", "workshop.map"}, {"entities", Json::array()}, {"ship_slots", Json::array()}});
	pack_ = loaded.draft();
	libraries_ = std::move(retained);
	library_catalog_ = libraries_.empty() ? Catalog{} : resolve_packs(libraries_);
	definitions_ = std::move(definitions);
	map_index_ = 0;
}
void WorkshopDocument::load_dependency(std::span<const std::uint8_t> archive)
{
	if (pack_.manifest.contains("base"))
		throw std::runtime_error("A base pack cannot be reparented by loading dependencies");
	auto library = read_pack(archive);
	auto order = libraries_;
	order.push_back(library);
	auto catalog = resolve_packs(order);
	if (catalog.presentation)
		throw std::runtime_error("Console assets are not a gameplay dependency");
	auto id = library.manifest().at("id").get<std::string>();
	if (id == pack_.manifest.at("id").get<std::string>())
		throw std::runtime_error("Rename the authored pack before loading a dependency with the same id");
	libraries_ = std::move(order);
	library_catalog_ = std::move(catalog);
	if (!library_catalog_.base.id.empty())
	{
		pack_.manifest.erase("dependencies");
		if (!pack_.manifest.contains("parent"))
			pack_.manifest["parent"] = library_catalog_.base.id;
	}
	else
	{
		if (!pack_.manifest.contains("dependencies"))
			pack_.manifest["dependencies"] = Json::array();
		auto &required = pack_.manifest["dependencies"];
		if (std::find(required.begin(), required.end(), Json(id)) == required.end())
			required.push_back(id);
	}
}
void WorkshopDocument::rename_pack(const std::string &id)
{
	if (id.empty() || id.size() > 128 ||
	    id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
		throw std::runtime_error("Pack id uses lower-case letters, numbers, dots, underscores and hyphens");
	for (const auto &[loaded, hash] : library_catalog_.ordered_packs)
		if (loaded == id)
			throw std::runtime_error("Pack id already belongs to a dependency");
	auto old = pack_.manifest.at("id").get<std::string>();
	std::function<void(Json &)> rename = [&](Json &value)
	{
		if (value.is_string())
		{
			auto text = value.get<std::string>();
			if (text.starts_with(old + ".") || text.starts_with(old + ":"))
				value = id + text.substr(old.size());
		}
		else if (value.is_object())
		{
			for (auto it = value.begin(); it != value.end(); ++it)
				if (it.key() != "label")
					rename(it.value());
		}
		else if (value.is_array())
			for (auto &child : value)
				rename(child);
	};
	rename(definitions_);
	pack_.manifest["id"] = id;
}
Catalog WorkshopDocument::verify() const
{
	PackDraft draft;
	draft.manifest = pack_.manifest;
	draft.files = pack_.files;
	draft.files["definitions.json"] = json_bytes(definitions_);
	return resolve_draft(libraries_, draft);
}
Bytes WorkshopDocument::export_pack(PackEncoding encoding, bool scenario_only) const
{
	PackDraft draft;
	draft.manifest = pack_.manifest;
	draft.files = pack_.files;
	draft.files["definitions.json"] = json_bytes(definitions_);
	const auto catalog = resolve_draft(libraries_, draft);
	auto dependencies = libraries_;
	if (scenario_only)
	{
		// The editable pack supplies shared definitions/assets. The scenario is a
		// small dependent overlay, so it can be installed alongside that game pack.
		dependencies.push_back(read_pack(export_pack(PackEncoding::Zip, false)));
		const auto map_id = map().at("id").get<std::string>();
		const auto parent = draft.manifest.at("id").get<std::string>();
		const auto scenario_id = parent.substr(0, 100) + ".scenario-" + sha256(json_bytes(map_id)).substr(0, 16);
		const auto required = draft.manifest.value("dependencies", Json::array());
		draft = {};
		draft.manifest = {{"schema", 1},
		                  {"id", scenario_id},
		                  {"parent", parent},
		                  {"overrides", Json::array({map_id})},
		                  {"scenario", map_id}};

		draft.files["definitions.json"] = json_bytes(Json{{"schema", 3}, {"maps", Json::array({map()})}});
		const auto assets = scenario_assets(catalog, map_id);
		(void)assets; // Validate future-spawn dependencies before packaging.
	}
	auto preview = launcher_catalog(resolve_draft(dependencies, draft));
	if (scenario_only)
		std::erase_if(preview["scenarios"].get_ref<Json::array_t &>(),
		              [&](const Json &entry)
		              {
			              return entry.at("id") != map().at("id");
		              });
	draft.files["launcher.json"] = json_bytes(preview);
	return write_pack(draft, encoding);
}
std::map<std::string, Bytes> WorkshopDocument::export_companions(const std::string &filename, PackEncoding encoding,
                                                                 bool scenario_only) const
{
	auto dependencies = libraries_;
	if (scenario_only)
		dependencies.push_back(read_pack(export_pack(PackEncoding::Zip, false)));
	const auto archive = export_pack(encoding, scenario_only);
	dependencies.push_back(read_pack(archive));
	auto files = pack_companions(dependencies, filename);
	files[filename] = archive;
	return files;
}
void WorkshopDocument::load_map(std::span<const std::uint8_t> bytes)
{
	auto value = parse_json(bytes, "map.json");
	auto candidate = *this;
	bool replaced = false;
	for (std::size_t i = 0; i < candidate.definitions_["maps"].size(); ++i)
	{
		if (candidate.definitions_["maps"][i]["id"] == value.value("id", std::string{}))
		{
			candidate.definitions_["maps"][i] = value;
			candidate.map_index_ = i;
			replaced = true;
			break;
		}
	}
	if (!replaced)
	{
		candidate.definitions_["maps"].push_back(value);
		candidate.map_index_ = candidate.definitions_["maps"].size() - 1;
	}
	candidate.verify();
	*this = std::move(candidate);
}
Bytes WorkshopDocument::save_map() const
{
	verify();
	return json_bytes(map());
}
std::string WorkshopDocument::create_entity(const std::string &parent)
{
	std::size_t number = definitions_["entities"].size() + 1;
	std::string id;
	do
	{
		id = pack_.manifest.at("id").get<std::string>() + ".entity" + std::to_string(number++);
	} while (std::any_of(definitions_["entities"].begin(), definitions_["entities"].end(),
	                     [&](const auto &entity)
	                     {
		                     return entity["id"] == id;
	                     }));
	if (!parent.empty())
	{
		if (!library_catalog_.entities.contains(parent))
			throw std::runtime_error("Unknown library entity: " + parent);
		definitions_["entities"].push_back({{"id", id}, {"extends", parent}});
		return id;
	}
	if (library_catalog_.base.render_mode == "models" ||
	    pack_.manifest.value("base", Json::object()).value("render_mode", "sprites") == "models")
	{
		definitions_["entities"].push_back(
		    {{"id", id},
		     {"render", {{"scale", 1}, {"star", Json::object()}}},
		     {"systems", Json::object()},
		     {"collision", {{"shape", "circle"}, {"radius_mm", 2000}, {"mass_grams", 1000000}}}});
		return id;
	}
	std::string image;
	for (const auto &[path, bytes] : pack_.files)
		if (path.starts_with("assets/"))
		{
			image = pack_.manifest.at("id").get<std::string>() + ":" + path;
			break;
		}
	if (image.empty())
		for (const auto &[reference, bytes] : library_catalog_.assets)
			if (reference.ends_with(".png") && !reference.starts_with("console-assets:assets/radar/"))
			{
				image = reference;
				break;
			}
	definitions_["entities"].push_back(
	    {{"id", id},
	     {"render", {{"mode", "camera"}, {"images", Json::array({image})}, {"scale", 1}}},
	     {"systems",
	      {{"engine", {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 3000}}},
	       {"steering", {{"turn_mdeg_s", 45000}}},
	       {"radar", {{"range_mm", 200000}}}}},
	     {"collision", {{"shape", "circle"}, {"radius_mm", 2000}, {"mass_grams", 1000000}}}});
	return id;
}
void WorkshopDocument::rename_entity(const std::string &from, const std::string &to)
{
	if (from == to)
		return;
	if (to.empty() || to.size() > 256 ||
	    to.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos)
		throw std::runtime_error("Template ID uses letters, numbers, dots, underscores and hyphens");
	for (const auto *kind : {"entities", "maps", "consoles", "trees"})
		for (const auto &value : definitions_.at(kind))
			if (value.at("id") == to)
				throw std::runtime_error("Definition ID already exists: " + to);
	if (library_catalog_.entities.contains(to) || library_catalog_.maps.contains(to) ||
	    library_catalog_.consoles.contains(to) || library_catalog_.trees.contains(to))
		throw std::runtime_error("Definition ID already belongs to a dependency: " + to);
	auto &entities = definitions_["entities"];
	auto found = std::find_if(entities.begin(), entities.end(),
	                          [&](const Json &value)
	                          {
		                          return value.at("id") == from;
	                          });
	if (found == entities.end())
		throw std::runtime_error("Unknown local template: " + from);
	(*found)["id"] = to;
	for (auto &entity : entities)
		if (entity.value("extends", std::string{}) == from)
			entity["extends"] = to;
	for (auto &map : definitions_["maps"])
	{
		for (auto &instance : map["entities"])
			if (instance.at("definition") == from)
				instance["definition"] = to;
		for (auto &slot : map["ship_slots"])
			for (auto &ship : slot["allowed_ships"])
				if (ship == from)
					ship = to;
		auto rename_fields = [&](Json &owner)
		{
			if (!owner.contains("regions") || !owner.at("regions").is_array())
				return;
			for (auto &region : owner["regions"])
				if (region.contains("asteroids"))
					for (const auto *key : {"types", "decorative_types"})
						if (region["asteroids"].contains(key) && region["asteroids"][key].is_array())
							for (auto &type : region["asteroids"][key])
								if (type.value("definition", Json()) == from)
									type["definition"] = to;
		};
		rename_fields(map);
		if (map.contains("scenario") && map["scenario"].is_object())
			rename_fields(map["scenario"]);
		if (map.contains("scenario") && map.at("scenario").is_object() && map.at("scenario").contains("spawn_groups") &&
		    map.at("scenario").at("spawn_groups").is_array())
			for (auto &group : map["scenario"]["spawn_groups"])
				if (group.is_object() && group.contains("entities") && group.at("entities").is_array())
					for (auto &placement : group["entities"])
						if (placement.is_object() && placement.value("definition", Json()) == from)
							placement["definition"] = to;
	}
	// Only the first literal argument of spawn_entity names an entity definition.
	// Other text, handles and runtime/parameter bindings may deliberately share it.
	auto rename_spawn_tasks = [&](Json &state)
	{
		if (!state.is_object())
			return;
		for (const char *phase : {"entry", "running", "exit"})
			if (state.contains(phase) && state.at(phase).is_array())
				for (auto &task : state[phase])
					if (task.is_object() && task.value("action", Json()) == "spawn_entity" &&
					    task.contains("arguments") && task.at("arguments").is_array() &&
					    !task.at("arguments").empty() && task.at("arguments")[0] == from)
						task["arguments"][0] = to;
	};
	for (auto &tree : definitions_["trees"])
		for (const char *field : {"states", "overrides"})
			if (tree.contains(field) && tree.at(field).is_array())
				for (auto &state : tree[field])
					rename_spawn_tasks(state);
	if (pack_.manifest.contains("overrides"))
	{
		auto &overrides = pack_.manifest["overrides"];
		overrides.erase(std::remove(overrides.begin(), overrides.end(), Json(from)), overrides.end());
	}
}
void WorkshopDocument::rename_console(const std::string &from, const std::string &to)
{
	if (from == to)
		return;
	if (to.empty() || to.size() > 256 ||
	    to.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos)
		throw std::runtime_error("Console ID uses letters, numbers, dots, underscores and hyphens");
	for (const auto *kind : {"entities", "maps", "consoles", "trees"})
		for (const auto &value : definitions_.at(kind))
			if (value.at("id") == to)
				throw std::runtime_error("Definition ID already exists: " + to);
	if (library_catalog_.entities.contains(to) || library_catalog_.maps.contains(to) ||
	    library_catalog_.consoles.contains(to) || library_catalog_.trees.contains(to))
		throw std::runtime_error("Definition ID already belongs to a dependency: " + to);
	auto &consoles = definitions_["consoles"];
	auto found = std::find_if(consoles.begin(), consoles.end(),
	                          [&](const Json &value)
	                          {
		                          return value.at("id") == from;
	                          });
	if (found == consoles.end())
		throw std::runtime_error("Unknown local Console: " + from);
	(*found)["id"] = to;
	for (auto &console : consoles)
		if (console.value("extends", std::string{}) == from)
			console["extends"] = to;
	for (auto &entity : definitions_["entities"])
	{
		if (entity.value("console", Json()) == from)
			entity["console"] = to;
		if (entity.contains("stations"))
			for (auto &station : entity["stations"])
				for (auto &console : station["consoles"])
					if (console.at("definition") == from)
						console["definition"] = to;
	}
	if (pack_.manifest.contains("overrides"))
	{
		auto &overrides = pack_.manifest["overrides"];
		overrides.erase(std::remove(overrides.begin(), overrides.end(), Json(from)), overrides.end());
	}
}
std::uint64_t WorkshopDocument::place(const std::string &definition, std::int64_t x, std::int64_t y)
{
	bool exists = std::any_of(definitions_["entities"].begin(), definitions_["entities"].end(),
	                          [&](const auto &entity)
	                          {
		                          return entity["id"] == definition;
	                          });
	if (!exists && !library_catalog_.entities.contains(definition))
		throw std::runtime_error("Cannot place unknown definition: " + definition);
	std::uint64_t id = 1;
	for (const auto &entity : map()["entities"])
		id = std::max(id, entity.at("id").get<std::uint64_t>() + 1);
	map()["entities"].push_back(
	    {{"id", id}, {"definition", definition}, {"x_mm", x}, {"y_mm", y}, {"heading_mdeg", 0}});
	return id;
}
void WorkshopDocument::erase(std::uint64_t instance)
{
	auto &entities = map()["entities"];
	auto found = std::find_if(entities.begin(), entities.end(),
	                          [&](const auto &entity)
	                          {
		                          return entity["id"] == instance;
	                          });
	if (found != entities.end())
		entities.erase(found);
}
void WorkshopDocument::duplicate(std::uint64_t instance)
{
	for (const auto &entity : map()["entities"])
		if (entity["id"] == instance)
		{
			auto copy = entity;
			auto offset = [](std::int64_t position)
			{
				return std::clamp(position, static_cast<std::int64_t>(-1000000000),
				                  static_cast<std::int64_t>(999995000)) +
				       5000;
			};
			auto id = place(copy["definition"], offset(copy["x_mm"].get<std::int64_t>()),
			                offset(copy["y_mm"].get<std::int64_t>()));
			map()["entities"].back()["heading_mdeg"] = copy["heading_mdeg"];
			(void)id;
			return;
		}
}
std::string WorkshopDocument::import_model(const std::string &filename, Bytes bytes)
{
	compile_model(bytes);
	auto leaf =
	    filename.substr(filename.find_last_of("/\\") == std::string::npos ? 0 : filename.find_last_of("/\\") + 1);
	if (leaf.empty() || leaf.find(':') != std::string::npos || !leaf.ends_with(".glb"))
		throw std::runtime_error("Choose a GLB filename");
	const auto path = "assets/" + leaf;
	if (pack_.files.contains(path))
		throw std::runtime_error("Asset already exists; rename the imported model");
	pack_.files[path] = std::move(bytes);
	return pack_.manifest.at("id").get<std::string>() + ":" + path;
}
std::string WorkshopDocument::import_image(const std::string &filename, Bytes bytes)
{
	if (bytes.size() > 16 * 1024 * 1024)
		throw std::runtime_error("Image exceeds 16 MiB");
	auto leaf =
	    filename.substr(filename.find_last_of("/\\") == std::string::npos ? 0 : filename.find_last_of("/\\") + 1);
	if (leaf.empty() || leaf == "." || leaf == ".." || leaf.find(':') != std::string::npos)
		throw std::runtime_error("Invalid image filename");
	auto path = "assets/" + leaf;
	if (pack_.files.contains(path))
		throw std::runtime_error("Asset already exists; rename the imported file: " + path);
	// Verify using the same image contract as referenced entity render assets.
	Catalog probe;
	auto reference = pack_.manifest.at("id").get<std::string>() + ":" + path;
	probe.assets[reference] = bytes;
	probe.entities["import"] = {{"render", {{"mode", "camera"}, {"images", Json::array({reference})}, {"scale", 1}}},
	                            {"systems", Json::object()},
	                            {"collision", {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}}}};
	verify_catalog(probe);
	pack_.files[path] = std::move(bytes);
	return reference;
}
std::string WorkshopDocument::import_font(const std::string &filename, Bytes bytes)
{
	const auto leaf =
	    filename.substr(filename.find_last_of("/\\") == std::string::npos ? 0 : filename.find_last_of("/\\") + 1);
	if (leaf.empty() ||
	    leaf.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
	        std::string::npos ||
	    !(leaf.ends_with(".ttf") || leaf.ends_with(".otf")) || bytes.size() < 12 || bytes.size() > 16 * 1024 * 1024)
		throw std::runtime_error("Expected a bounded .ttf or .otf font");
	if (!(bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 0) &&
	    !(bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'T' && bytes[3] == 'O'))
		throw std::runtime_error("Unsupported font header");
	const auto path = "assets/fonts/" + leaf;
	if (pack_.files.contains(path))
		throw std::runtime_error("Font already exists; rename the imported file");
	pack_.files[path] = std::move(bytes);
	return pack_.manifest.at("id").get<std::string>() + ":" + path;
}

bool WorkshopHistory::same_resources(const WorkshopDocument &a, const WorkshopDocument &b)
{
	if (a.pack_.files != b.pack_.files || a.libraries_.size() != b.libraries_.size())
		return false;
	for (std::size_t i = 0; i < a.libraries_.size(); ++i)
		if (a.libraries_[i].hash() != b.libraries_[i].hash())
			return false;
	return true;
}
WorkshopHistory::Snapshot WorkshopHistory::snapshot(const WorkshopDocument &document) const
{
	Snapshot result{document.definitions_,
	                document.pack_.manifest,
	                document.map_index_,
	                document.definitions_.dump().size() + document.pack_.manifest.dump().size(),
	                0,
	                {}};
	if (!past_.empty() && same_resources(*past_.back().resources, document))
	{
		result.resources = past_.back().resources;
		result.resource_bytes = past_.back().resource_bytes;
	}
	else
	{
		auto resources = std::make_shared<WorkshopDocument>(document);
		resources->definitions_ = Json();
		for (const auto &[name, data] : document.pack_.files)
			result.resource_bytes += name.size() + data.size();
		for (const auto &pack : document.libraries_)
			result.resource_bytes += pack.archive().size();
		result.resources = std::move(resources);
	}
	return result;
}
void WorkshopHistory::restore(WorkshopDocument &document, const Snapshot &snapshot)
{
	document = *snapshot.resources;
	document.definitions_ = snapshot.definitions;
	document.pack_.manifest = snapshot.manifest;
	document.map_index_ = snapshot.map_index;
}
void WorkshopHistory::trim(std::vector<Snapshot> &history)
{
	// Charge shared assets once, excluding the latest resource set needed by the current draft.
	// Always preserve one undo step even when an individual imported asset exceeds the budget.
	while (history.size() > 2)
	{
		std::size_t bytes = 0;
		std::set<const WorkshopDocument *> seen{history.back().resources.get()};
		for (const auto &entry : history)
		{
			bytes += entry.bytes;
			if (seen.insert(entry.resources.get()).second)
				bytes += entry.resource_bytes;
		}
		if (history.size() <= 128 && bytes <= 32 * 1024 * 1024)
			break;
		history.erase(history.begin());
	}
}
void WorkshopHistory::reset(const WorkshopDocument &document)
{
	past_.clear();
	future_.clear();
	past_.push_back(snapshot(document));
}
void WorkshopHistory::checkpoint(const WorkshopDocument &document)
{
	if (!past_.empty() && past_.back().definitions == document.definitions_ &&
	    past_.back().manifest == document.pack_.manifest && same_resources(*past_.back().resources, document))
		return;
	past_.push_back(snapshot(document));
	trim(past_);
	future_.clear();
}
bool WorkshopHistory::undo(WorkshopDocument &document)
{
	checkpoint(document);
	if (!can_undo())
		return false;
	future_.push_back(std::move(past_.back()));
	past_.pop_back();
	trim(future_);
	restore(document, past_.back());
	return true;
}
bool WorkshopHistory::redo(WorkshopDocument &document)
{
	checkpoint(document);
	if (!can_redo())
		return false;
	past_.push_back(std::move(future_.back()));
	future_.pop_back();
	trim(past_);
	restore(document, past_.back());
	return true;
}
} // namespace phoenix
