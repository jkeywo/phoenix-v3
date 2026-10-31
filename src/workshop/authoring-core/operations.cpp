module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;
import phoenix.content.assets;
import phoenix.content.ui;

namespace phoenix
{
namespace
{
const std::map<std::string, WorkshopDefinitionKind> kinds{{"entities", WorkshopDefinitionKind::Entity},
                                                          {"maps", WorkshopDefinitionKind::Map},
                                                          {"consoles", WorkshopDefinitionKind::Console},
                                                          {"trees", WorkshopDefinitionKind::Tree},
                                                          {"equipment", WorkshopDefinitionKind::Equipment},
                                                          {"assemblies", WorkshopDefinitionKind::Assembly},
                                                          {"messages", WorkshopDefinitionKind::Message},
                                                          {"effects", WorkshopDefinitionKind::Effect},
                                                          {"resources", WorkshopDefinitionKind::Resource},
                                                          {"properties", WorkshopDefinitionKind::Property},
                                                          {"recipes", WorkshopDefinitionKind::Recipe},
                                                          {"layouts", WorkshopDefinitionKind::Layout},
                                                          {"ui_groups", WorkshopDefinitionKind::UiGroup},
                                                          {"themes", WorkshopDefinitionKind::Theme},
                                                          {"control_widgets", WorkshopDefinitionKind::ControlWidget}};
std::string kind_of(const Json &a)
{
	const auto kind = a.at("kind").get<std::string>();
	if (!kinds.contains(kind))
		throw std::runtime_error("Unknown definition kind: " + kind);
	return kind;
}
Json &local(WorkshopDocument &d, const Json &a)
{
	auto &rows = d.definitions().at(kind_of(a));
	for (auto &row : rows)
		if (row.at("id") == a.at("id"))
			return row;
	throw std::runtime_error("Definition is missing or belongs to a dependency; create an explicit override first");
}
Json &target(WorkshopDocument &d, const Json &a)
{
	Json *root = nullptr;
	const auto scope = a.value("scope", std::string("definitions"));
	if (a.contains("kind"))
		root = &local(d, a);
	else if (scope == "manifest")
		root = &d.manifest();
	else if (scope == "definitions")
		root = &d.definitions();
	else if (scope == "map")
		root = &d.map();
	else
		throw std::runtime_error("Unknown target scope");
	if (a.contains("segments"))
	{
		WorkshopTarget stable;
		for (const auto &s : a.at("segments"))
			stable.segments.push_back({s.at("key").get<std::string>(), s.value("identity", Json())});
		root = stable.resolve(*root);
		if (!root)
			throw std::runtime_error("Target no longer exists");
	}
	return root->at(Json::json_pointer(a.value("path", std::string{})));
}
void replace_reference(Json &value, const std::string &from, const std::string &to)
{
	if (value.is_string() && value == from)
		value = to;
	else if (value.is_object() || value.is_array())
		for (auto &child : value)
			replace_reference(child, from, to);
}
struct Operation
{
	const char *id, *help;
	Json arguments;
	std::function<Json(WorkshopDocument &, const Json &)> apply;
};
Json params(std::initializer_list<const char *> required, std::initializer_list<const char *> optional = {})
{
	Json properties = Json::object();
	properties["verify"] = {{"type", "boolean"}, {"default", false}};
	for (const auto group : {required, optional})
		for (const auto *name : group)
		{
			const std::string key = name;
			std::string type = "string";
			if (key == "target" || key == "fields" || key == "overrides" || key == "sidecar" || key == "images")
				type = "object";
			else if (key == "ids" || key == "revert" || key == "requests" || key == "patch" || key == "bytes")
				type = "array";
			else if (key == "x" || key == "y" || key == "dx" || key == "dy" || key == "index")
				type = "integer";
			else if (key == "resolved" || key == "fitted" || key == "records" || key == "assets" ||
			         key == "revert_all" || key == "scenario_only" || key == "engine_effect")
				type = "boolean";
			if (key == "value")
				properties[key] = Json::object();
			else
				properties[key] = {{"type", type}};
		}
	return {{"type", "object"}, {"required", required}, {"properties", properties}};
}
const std::vector<Operation> &operations()
{
	static const std::vector<Operation> all{
	    {"get", "Read authored content; resolved=true reads a definition without changing its source.",
	     params({}, {"target", "resolved", "fitted"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto t = a.value("target", Json::object());
		     if (a.value("resolved", false) || a.value("fitted", false))
		     {
			     auto resolved = a.value("fitted", false)
			                         ? d.preview_equipment().entities.at(t.at("id").get<std::string>())
			                         : d.preview_definition(kinds.at(kind_of(t)), t.at("id"));
			     if (t.contains("segments"))
			     {
				     WorkshopTarget stable;
				     for (const auto &s : t.at("segments"))
					     stable.segments.push_back({s.at("key"), s.value("identity", Json())});
				     auto *found = stable.resolve(resolved);
				     if (!found)
					     throw std::runtime_error("Resolved target no longer exists");
				     return found->at(Json::json_pointer(t.value("path", std::string{})));
			     }
			     return resolved.at(Json::json_pointer(t.value("path", std::string{})));
		     }
		     return target(d, t);
	     }},
	    {"list", "List local and dependency definitions with provenance, or list assets.",
	     params({}, {"kind", "assets"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     Json result = Json::array();
		     if (a.value("assets", false))
		     {
			     for (const auto &[path, bytes] : d.pack().files)
				     result.push_back({{"path", path}, {"bytes", bytes.size()}, {"origin", "local"}});
			     for (const auto &[ref, bytes] : d.libraries().assets)
				     result.push_back({{"path", ref}, {"bytes", bytes.size()}, {"origin", "dependency"}});
			     return result;
		     }
		     for (const auto &[name, kind] : kinds)
			     if (!a.contains("kind") || a.at("kind") == name)
				     for (const auto &id : d.definition_choices(kind))
				     {
					     bool own = false;
					     for (const auto &row : d.definitions().value(name, Json::array()))
						     own = own || row.at("id") == id;
					     result.push_back({{"kind", name}, {"id", id}, {"origin", own ? "local" : "dependency"}});
				     }
		     return result;
	     }},
	    {"choices", "Return current definition choices using the GUI's production resolution.", params({"kind"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return d.definition_choices(kinds.at(kind_of(a)));
	     }},
	    {"target", "Capture an identity-based target from an existing path; reuse its segments after collection edits.",
	     params({"target"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto t = a.at("target");
		     const auto path = t.value("path", std::string{});
		     t.erase("path");
		     const auto stable = WorkshopTarget::capture(target(d, t), path);
		     t["segments"] = Json::array();
		     for (const auto &s : stable.segments)
			     t["segments"].push_back({{"key", s.key}, {"identity", s.identity}});
		     return t;
	     }},
	    {"set", "Set a field on an authored object. A descriptor supplies shared GUI type/choice checks.",
	     params({"target", "field", "value"}, {"descriptor"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto &object = target(d, a.at("target"));
		     const auto field = a.at("field").get<std::string>();
		     if (!object.is_object())
			     throw std::runtime_error("set target: expected object");
		     if (field == "id" || field == "schema")
			     throw std::runtime_error("Use rename or structural editing for identity/schema changes");
		     if (a.contains("descriptor"))
		     {
			     const auto &spec = workshop_field(a.at("descriptor"));
			     if (spec.at("key") != field)
				     throw std::runtime_error("Descriptor does not describe this field");
			     workshop_set_field(object, spec, a.at("value"));
		     }
		     else
			     object[field] = a.at("value");
		     return object.at(field);
	     }},
	    {"unset", "Remove a local field, restoring inherited/default behavior.", params({"target", "field"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return target(d, a.at("target")).erase(a.at("field").get<std::string>());
	     }},
	    {"patch", "Apply RFC 6902 JSON Patch to an authored target. Use test operations for preconditions.",
	     params({"target", "patch"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto &v = target(d, a.at("target"));
		     v = v.patch(a.at("patch"));
		     return v;
	     }},
	    {"merge", "Apply RFC 7396 merge patch to an authored target; arrays replace as a family.",
	     params({"target", "value"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto &v = target(d, a.at("target"));
		     v.merge_patch(a.at("value"));
		     return v;
	     }},
	    {"append", "Append explicit data or a shared GUI record prototype, preserving authored order.",
	     params({"target"}, {"value", "prototype"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto &v = target(d, a.at("target"));
		     if (!v.is_array())
			     throw std::runtime_error("append target: expected array");
		     return workshop_append_record(v, a.contains("value") ? a.at("value") : workshop_record(a.at("prototype")),
		                                   !a.contains("value"));
	     }},
	    {"create", "Create a definition using explicit authored data; entity defaults match Add entity.",
	     params({"kind"}, {"id", "value", "parent"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto kind = kind_of(a);
		     if (kind == "entities" && !a.contains("value"))
		     {
			     const auto id = d.create_entity(a.value("parent", std::string{}));
			     if (a.contains("id"))
				     d.rename_entity(id, a.at("id"));
			     return a.value("id", id);
		     }
		     const auto id = a.at("id").get<std::string>();
		     const auto choices = d.definition_choices(kinds.at(kind));
		     if (std::find(choices.begin(), choices.end(), id) != choices.end())
			     throw std::runtime_error("ID already exists; use override explicitly");
		     Json defaults = Json::object();
		     for (const auto &record : workshop_records())
			     if (record.at("key") == kind && record.at("value").is_object() && record.at("value").contains("id"))
			     {
				     defaults = record.at("value");
				     break;
			     }
		     if (kind == "consoles")
		     {
			     defaults = {{"id", id}, {"widgets", Json::array()}};
			     defaults["layout"] = convert_console_layout(defaults);
		     }
		     auto value = a.value("value", defaults);
		     if (!value.is_object())
			     throw std::runtime_error("value: expected object");
		     value["id"] = id;
		     if (a.contains("parent"))
			     value["extends"] = a.at("parent");
		     d.definitions()[kind].push_back(value);
		     return id;
	     }},
	    {"flatten", "Resolve an inherited local definition into editable fields.", params({"kind", "id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto kind = kind_of(a);
		     auto &value = local(d, a);
		     auto resolved = kind == "entities" ? d.verify().entities.at(a.at("id").get<std::string>())
		                                        : d.preview_definition(kinds.at(kind), a.at("id"));
		     resolved.erase("extends");
		     value = std::move(resolved);
		     return value;
	     }},
	    {"copy", "Copy a resolved definition to a new local identity.", params({"kind", "id", "to"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto value = d.preview_definition(kinds.at(kind_of(a)), a.at("id"));
		     value.erase("extends");
		     return workshop_apply(d, "create", {{"kind", a.at("kind")}, {"id", a.at("to")}, {"value", value}});
	     }},
	    {"override", "Create an explicit local override of a dependency definition.", params({"kind", "id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto kind = kind_of(a);
		     for (const auto &r : d.definitions()[kind])
			     if (r.at("id") == a.at("id"))
				     throw std::runtime_error("Local definition already exists");
		     auto value = d.preview_definition(kinds.at(kind), a.at("id"));
		     d.definitions()[kind].push_back(value);
		     auto &overrides = d.manifest()["overrides"];
		     if (overrides.is_null())
			     overrides = Json::array();
		     if (std::find(overrides.begin(), overrides.end(), a.at("id")) == overrides.end())
			     overrides.push_back(a.at("id"));
		     return value;
	     }},
	    {"rename", "Rename a definition and repair references using existing Workshop policies.",
	     params({"kind", "id", "to"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto kind = kind_of(a), from = a.at("id").get<std::string>(), to = a.at("to").get<std::string>();
		     (void)local(d, a);
		     if (!valid_identifier(to))
			     throw std::runtime_error("Invalid destination identity");
		     const auto choices = d.definition_choices(kinds.at(kind));
		     if (std::find(choices.begin(), choices.end(), to) != choices.end())
			     throw std::runtime_error("Destination identity already exists");
		     if (kind == "entities")
			     d.rename_entity(from, to);
		     else if (kind == "consoles")
			     d.rename_console(from, to);
		     else if (kind == "layouts" || kind == "themes" || kind == "ui_groups" || kind == "control_widgets")
			     d.rename_ui_definition(kinds.at(kind), from, to);
		     else
		     {
			     replace_reference(d.definitions(), from, to);
			     replace_reference(d.manifest(), from, to);
		     }
		     return to;
	     }},
	    {"delete", "Delete a local definition. Verification diagnoses remaining references.", params({"kind", "id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     (void)local(d, a);
		     auto &rows = d.definitions()[kind_of(a)];
		     rows.erase(std::remove_if(rows.begin(), rows.end(),
		                               [&](const Json &r)
		                               {
			                               return r.at("id") == a.at("id");
		                               }),
		                rows.end());
		     return true;
	     }},
	    {"pack.rename", "Rename the editing pack and its owned asset references.", params({"id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     d.rename_pack(a.at("id"));
		     return a.at("id");
	     }},
	    {"map.create", "Create and select a map using Workshop defaults.", params({"id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     d.new_map(a.at("id"));
		     return a.at("id");
	     }},
	    {"map.select", "Select a map by stable ID.", params({"id"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto &maps = d.definitions().at("maps");
		     for (std::size_t i = 0; i < maps.size(); ++i)
			     if (maps[i].at("id") == a.at("id"))
			     {
				     d.select_map(i);
				     return a.at("id");
			     }
		     throw std::runtime_error("Unknown map ID");
	     }},
	    {"instance.place", "Place an entity on the selected map; coordinates use millimetres.",
	     params({"definition", "x", "y"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return d.place(a.at("definition"), a.at("x"), a.at("y"));
	     }},
	    {"instance.delete", "Delete selected instance IDs.", params({"ids"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     for (const auto &id : a.at("ids"))
			     d.erase(id);
		     return true;
	     }},
	    {"instance.duplicate", "Duplicate instances and their mission overrides; returns new IDs.", params({"ids"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return d.duplicate_instances(a.at("ids").get<std::set<std::uint64_t>>());
	     }},
	    {"instance.translate", "Translate a group while preserving relative positions.", params({"ids", "dx", "dy"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     d.translate_instances(a.at("ids").get<std::set<std::uint64_t>>(), a.at("dx"), a.at("dy"));
		     return true;
	     }},
	    {"instance.edit", "Apply shared bulk fields/overrides or revert overrides through production verification.",
	     params({"ids"}, {"fields", "overrides", "revert", "revert_all"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     d.edit_instances(a.at("ids").get<std::set<std::uint64_t>>(), a.value("fields", Json::object()),
		                      a.value("overrides", Json::object()), a.value("revert", std::vector<std::string>{}),
		                      a.value("revert_all", false));
		     return true;
	     }},
	    {"vocabulary", "Describe current action/observation signatures for a reusable or owner-local tree.",
	     params({"target"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return workshop_tree_vocabulary(d, target(d, a.at("target")));
	     }},
	    {"ship.add", "Create a draft ship item using the GUI's default fitting records.",
	     params({"target", "id", "kind"}, {"definition", "selection"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return workshop_ship_add(target(d, a.at("target")), a);
	     }},
	    {"tree.rename", "Rename a state and repair parent/initial/transition references.",
	     params({"target", "from", "to"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     workshop_tree_rename(target(d, a.at("target")), a.at("from"), a.at("to"));
		     return true;
	     }},
	    {"layout.drop", "Insert/move/wrap a panel through the same operation as canvas drag/drop.",
	     params({"target", "destination"}, {"source", "component", "group", "kind", "index", "x", "y"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     return workshop_layout_drop(target(d, a.at("target")), a, *d.preview_ui());
	     }},
	    {"sprite.apply", "Apply an imported material sidecar with explicit image references.",
	     params({"target", "sidecar", "images"}, {"engine_effect"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     auto catalog = d.libraries();
		     const auto prefix = d.manifest().at("id").get<std::string>() + ":";
		     for (const auto &[path, bytes] : d.pack().files)
			     if (path.starts_with("assets/"))
				     catalog.assets[prefix + path] = bytes;
		     apply_sprite_sidecar(target(d, a.at("target")), a.at("sidecar"),
		                          a.at("images").get<std::map<std::string, std::string>>(), catalog,
		                          a.value("engine_effect", false));
		     d.definitions()["schema"] = 3;
		     return true;
	     }},

	    {"tree.reparent", "Reparent a state, refusing cycles and repairing direct initial-child references.",
	     params({"target", "state", "parent"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     WorkshopDocument::reparent_tree_state(target(d, a.at("target")), a.at("state"), a.at("parent"));
		     return true;
	     }},
	    {"import.bytes", "Import bounded binary data. Types: image, font, model, asset, map, dependency or pack.",
	     params({"type", "filename", "bytes"}),
	     [](WorkshopDocument &d, const Json &a) -> Json
	     {
		     const auto type = a.at("type").get<std::string>(), filename = a.at("filename").get<std::string>();
		     auto bytes = a.at("bytes").get<Bytes>();
		     if (type == "image")
			     return d.import_image(filename, std::move(bytes));
		     if (type == "font")
			     return d.import_font(filename, std::move(bytes));
		     if (type == "model")
			     return d.import_model(filename, std::move(bytes));
		     if (type == "asset")
			     d.import_asset(filename, std::move(bytes));
		     else if (type == "map")
			     d.load_map(bytes);
		     else if (type == "dependency")
			     d.load_dependency(bytes);
		     else if (type == "pack")
			     d.load_pack(bytes);
		     else
			     throw std::runtime_error("Unknown import type");
		     return filename;
	     }},
	};
	return all;
}
} // namespace
Json workshop_operations()
{
	Json result = Json::array();
	for (const auto &op : operations())
		result.push_back({{"id", op.id}, {"help", op.help}, {"arguments", op.arguments}});
	for (const auto &row : Json::array(
	         {Json{{"id", "help"}, {"help", "List operations and protocol version."}},
	          Json{{"id", "capabilities"}, {"help", "List operations and supported definition kinds."}},
	          Json{{"id", "describe"},
	               {"help", "Read shared GUI field descriptions or one operation."},
	               {"arguments", params({}, {"field", "operation", "record", "records"})}},
	          Json{{"id", "verify"}, {"help", "Verify the draft with production dependency resolution."}},
	          Json{{"id", "undo"}, {"help", "Undo the previous transaction."}},
	          Json{{"id", "redo"}, {"help", "Redo the previous undone transaction."}},
	          Json{{"id", "batch"},
	               {"help", "Apply document requests atomically as one undo step."},
	               {"arguments", params({"requests"})}},
	          Json{{"id", "open"},
	               {"help", "Open a pack or source draft with source=true; dependencies must already be loaded."},
	               {"arguments", params({"path"}, {"source"})}},
	          Json{{"id", "import"},
	               {"help", "Import a local file; types match import.bytes."},
	               {"arguments", params({"path", "type"}, {"filename"})}},
	          Json{{"id", "save"},
	               {"help", "Save an unfinished source draft to a directory."},
	               {"arguments", params({"path"})}},
	          Json{{"id", "export"},
	               {"help", "Verify and export ZIP/DAT and client companions."},
	               {"arguments", params({"path"}, {"scenario_only"})}}}))
	{
		auto description = row;
		if (row.at("id") == "open")
			description["arguments"]["properties"]["source"] = {{"type", "boolean"}};
		result.push_back(std::move(description));
	}
	return result;
}
Json workshop_apply(WorkshopDocument &document, const std::string &operation, const Json &arguments)
{
	if (!arguments.is_object())
		throw std::runtime_error("arguments: expected object");
	for (const auto &op : operations())
		if (operation == op.id)
		{
			for (const auto &key : op.arguments.at("required"))
				if (!arguments.contains(key.get<std::string>()))
					throw std::runtime_error("Missing argument: " + key.get<std::string>());
			for (const auto &[key, spec] : op.arguments.at("properties").items())
				if (arguments.contains(key) && spec.contains("type"))
				{
					const auto &v = arguments.at(key);
					const auto type = spec.at("type").get<std::string>();
					if ((type == "string" && !v.is_string()) || (type == "object" && !v.is_object()) ||
					    (type == "array" && !v.is_array()) || (type == "integer" && !v.is_number_integer()) ||
					    (type == "boolean" && !v.is_boolean()))
						throw std::runtime_error(key + ": expected " + type);
				}
			return op.apply(document, arguments);
		}
	throw std::runtime_error("Unknown document operation: " + operation);
}
} // namespace phoenix
