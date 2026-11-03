module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <map>
#include <limits>
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
const std::map<std::string, WorkshopDefinitionKind> kinds{
    {"entities", WorkshopDefinitionKind::Entity},     {"maps", WorkshopDefinitionKind::Map},
    {"consoles", WorkshopDefinitionKind::Console},    {"trees", WorkshopDefinitionKind::Tree},
    {"tasks", WorkshopDefinitionKind::Task},          {"equipment", WorkshopDefinitionKind::Equipment},
    {"assemblies", WorkshopDefinitionKind::Assembly}, {"messages", WorkshopDefinitionKind::Message},
    {"effects", WorkshopDefinitionKind::Effect},      {"resources", WorkshopDefinitionKind::Resource},
    {"properties", WorkshopDefinitionKind::Property}, {"recipes", WorkshopDefinitionKind::Recipe},
    {"layouts", WorkshopDefinitionKind::Layout},      {"ui_groups", WorkshopDefinitionKind::UiGroup},
    {"themes", WorkshopDefinitionKind::Theme},        {"control_widgets", WorkshopDefinitionKind::ControlWidget}};
std::string kind_of(const Json &a)
{
	const auto kind = a.at("kind").get<std::string>();
	if (!kinds.contains(kind))
		throw std::runtime_error("Unknown definition kind: " + kind);
	return kind;
}
template <typename Document> auto &local(Document &d, const Json &a)
{
	auto &rows = d.definitions().at(kind_of(a));
	for (auto &row : rows)
		if (row.at("id") == a.at("id"))
			return row;
	throw std::runtime_error("Definition is missing or belongs to a dependency; create an explicit override first");
}
template <typename Document> auto &target(Document &d, const Json &a)
{
	std::remove_reference_t<decltype(d.definitions())> *root = nullptr;
	const auto scope = a.value("scope", std::string("definitions"));
	if (a.contains("kind"))
		root = &local(d, a);
	else if (scope == "manifest")
	{
		if constexpr (std::is_const_v<Document>)
			root = &d.pack().manifest;
		else
			root = &d.manifest();
	}
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
void rename_tree_references(Json &value, const std::string &from, const std::string &to, bool tree = false)
{
	if (value.is_array())
	{
		for (auto &child : value)
			rename_tree_references(child, from, to, tree);
		return;
	}
	if (!value.is_object())
		return;
	for (auto &[key, child] : value.items())
	{
		const bool binding = key == "tree" || key == "dialogue" || key == "default_conversation" ||
		                     key == "hail_tree" || key == "policy" || key == "member_policy" || key == "call";
		if ((binding || (key == "extends" && tree)) && child.is_string() && child == from)
			child = to;
		else
			rename_tree_references(child, from, to, binding || (tree && key == "extends"));
	}
}
struct Operation
{
	const char *id, *help;
	Json arguments;
	std::function<Json(WorkshopDocument &, const Json &)> apply;
	std::function<Json(const WorkshopDocument &, const Json &)> query;
	const char *execution;

	template <typename Handler>
	Operation(const char *id, const char *help, Json arguments, Handler handler, const char *execution = "edit")
	    : id(id), help(help), arguments(std::move(arguments)), execution(execution)
	{
		if constexpr (std::is_invocable_v<Handler, const WorkshopDocument &, const Json &>)
		{
			query = std::move(handler);
			this->execution = "query";
		}
		else
			apply = std::move(handler);
	}
};
Json params(std::initializer_list<std::pair<const char *, const char *>> required,
            std::initializer_list<std::pair<const char *, const char *>> optional = {})
{
	Json properties{{"verify", {{"type", "boolean"}, {"default", false}}}};
	Json names = Json::array();
	for (const auto &entry : required)
		names.push_back(entry.first);
	for (const auto group : {required, optional})
		for (const auto &[name, type] : group)
		{
			const std::string kind = type;
			if (kind == "bytes" || kind == "identities")
				properties[name] = {
				    {"type", "array"},
				    {"items",
				     {{"type", "integer"},
				      {"minimum", 0},
				      {"maximum", kind == "bytes" ? Json(255) : Json(std::numeric_limits<std::uint64_t>::max())}}}};
			else
				properties[name] = kind == "any" ? Json::object() : Json{{"type", type}};
		}
	return {{"type", "object"}, {"required", names}, {"properties", properties}};
}
const std::vector<Operation> &operations()
{
	static const std::vector<Operation> all = []
	{
		std::vector<Operation> result{
		    {"get", "Read authored content; resolved=true reads a definition without changing its source.",
		     params({}, {{"target", "object"}, {"resolved", "boolean"}, {"fitted", "boolean"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
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
			     if (t.contains("kind"))
			     {
				     const auto kind = kind_of(t);
				     bool own = false;
				     for (const auto &row : d.definitions().value(kind, Json::array()))
					     own = own || row.at("id") == t.at("id");
				     if (!own)
				     {
					     auto value = d.preview_definition(kinds.at(kind), t.at("id"));
					     if (t.contains("segments"))
					     {
						     WorkshopTarget stable;
						     for (const auto &segment : t.at("segments"))
							     stable.segments.push_back({segment.at("key"), segment.value("identity", Json())});
						     auto *found = stable.resolve(value);
						     if (!found)
							     throw std::runtime_error("Dependency target no longer exists");
						     return found->at(Json::json_pointer(t.value("path", std::string{})));
					     }
					     return value.at(Json::json_pointer(t.value("path", std::string{})));
				     }
			     }
			     return target(d, t);
		     }},
		    {"list", "List local and dependency definitions with provenance, or list assets.",
		     params({}, {{"kind", "string"}, {"assets", "boolean"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
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
		    {"choices", "Return current definition choices using the GUI's production resolution.",
		     params({{"kind", "string"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
		     {
			     return d.definition_choices(kinds.at(kind_of(a)));
		     }},
		    {"target",
		     "Capture an identity-based target from an existing path; reuse its segments after collection edits.",
		     params({{"target", "object"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
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
		     params({{"target", "object"}, {"field", "string"}, {"value", "any"}},
		            {{"descriptor", "string"}, {"context", "object"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto &object = target(d, a.at("target"));
			     const auto field = a.at("field").get<std::string>();
			     if (!object.is_object())
				     throw std::runtime_error("set target: expected object");
			     if (a.contains("descriptor"))
			     {
				     const auto &spec = workshop_field(a.at("descriptor"));
				     if (spec.at("key") != field)
					     throw std::runtime_error("Descriptor does not describe this field");
				     return workshop_set_control(object, a.at("descriptor"), a.at("value"),
				                                 a.value("context", Json::object()));
			     }
			     else
				     object[field] = a.at("value");
			     return object.contains(field) ? object.at(field) : Json();
		     }},
		    {"unset", "Remove a local field, restoring inherited/default behavior.",
		     params({{"target", "object"}, {"field", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return target(d, a.at("target")).erase(a.at("field").get<std::string>());
		     }},
		    {"patch", "Apply RFC 6902 JSON Patch to an authored target. Use test operations for preconditions.",
		     params({{"target", "object"}, {"patch", "array"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto &v = target(d, a.at("target"));
			     v = v.patch(a.at("patch"));
			     return v;
		     }},
		    {"merge", "Apply RFC 7396 merge patch to an authored target; arrays replace as a family.",
		     params({{"target", "object"}, {"value", "any"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto &v = target(d, a.at("target"));
			     v.merge_patch(a.at("value"));
			     return v;
		     }},
		    {"append", "Append explicit data or a shared GUI record prototype, preserving authored order.",
		     params({{"target", "object"}}, {{"value", "any"}, {"prototype", "string"}, {"context", "object"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto &v = target(d, a.at("target"));
			     if (!v.is_array())
				     throw std::runtime_error("append target: expected array");
			     return workshop_append_record(
			         v,
			         a.contains("value") ? a.at("value")
			                             : workshop_record_value(a.at("prototype"), a.value("context", Json::object())),
			         !a.contains("value"));
		     }},
		    {"create", "Create a definition using explicit authored data; entity defaults match Add entity.",
		     params({{"kind", "string"}},
		            {{"id", "string"}, {"value", "any"}, {"parent", "string"}, {"context", "string"}}),
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
			     const auto choices = d.definition_choices(kinds.at(kind));
			     std::string id = a.value("id", std::string{});
			     if (id.empty())
			     {
				     auto prefix = kind == "trees"      ? std::string("tree")
				                   : kind == "maps"     ? d.pack().manifest.at("id").get<std::string>() + ".map"
				                   : kind == "consoles" ? d.pack().manifest.at("id").get<std::string>() + ".console"
				                                        : kind.substr(0, kind.size() - 1);
				     unsigned serial = 1;
				     do
				     {
					     id = prefix + std::to_string(serial++);
				     } while (std::find(choices.begin(), choices.end(), id) != choices.end());
			     }
			     if (std::find(choices.begin(), choices.end(), id) != choices.end())
				     throw std::runtime_error("ID already exists; use override explicitly");
			     const auto defaults =
			         a.contains("parent")
			             ? Json::object()
			             : workshop_definition_default(kind, a.value("context", std::string("controller")));
			     auto value = a.value("value", defaults);
			     if (!value.is_object())
				     throw std::runtime_error("value: expected object");
			     value["id"] = id;
			     if (a.contains("parent"))
				     value["extends"] = a.at("parent");
			     d.definitions()[kind].push_back(value);
			     if (kind == "maps")
				     d.select_map(d.definitions()[kind].size() - 1);
			     return id;
		     }},
		    {"flatten", "Resolve an inherited local definition into editable fields.",
		     params({{"kind", "string"}, {"id", "string"}}),
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
		    {"copy", "Copy a resolved definition to a new local identity.",
		     params({{"kind", "string"}, {"id", "string"}, {"to", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto value = d.preview_definition(kinds.at(kind_of(a)), a.at("id"));
			     value.erase("extends");
			     return workshop_apply(d, "create", {{"kind", a.at("kind")}, {"id", a.at("to")}, {"value", value}});
		     }},
		    {"override", "Create an explicit local override of a dependency definition.",
		     params({{"kind", "string"}, {"id", "string"}}),
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
		    {"rename",
		     "Rename a definition using namespace-specific Workshop policies; verify remaining external references.",
		     params({{"kind", "string"}, {"id", "string"}, {"to", "string"}}),
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
			     else if (kind == "trees")
			     {
				     local(d, a)["id"] = to;
				     for (auto &[collection, rows] : d.definitions().items())
					     rename_tree_references(rows, from, to, collection == "trees");
			     }
			     else
			     {
				     // Generic database inspectors author identities directly. Repair
				     // inheritance in this namespace; never rewrite arbitrary text or
				     // coincidentally equal identifiers from another namespace.
				     local(d, a)["id"] = to;
				     for (auto &row : d.definitions().at(kind))
					     if (row.value("extends", Json()) == from)
						     row["extends"] = to;
			     }
			     return to;
		     }},
		    {"delete", "Delete a local definition. Verification diagnoses remaining references.",
		     params({{"kind", "string"}, {"id", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     (void)local(d, a);
			     const auto selected = kind_of(a) == "maps" ? d.map().at("id") : Json();
			     auto &rows = d.definitions()[kind_of(a)];
			     rows.erase(std::remove_if(rows.begin(), rows.end(),
			                               [&](const Json &r)
			                               {
				                               return r.at("id") == a.at("id");
			                               }),
			                rows.end());
			     if (kind_of(a) == "maps" && !rows.empty())
			     {
				     std::size_t next = 0;
				     for (std::size_t i = 0; i < rows.size(); ++i)
					     if (rows[i].at("id") == selected)
						     next = i;
				     d.select_map(next);
			     }
			     return true;
		     }},
		    {"pack.rename", "Rename the editing pack and its owned asset references.", params({{"id", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     d.rename_pack(a.at("id"));
			     return a.at("id");
		     }},
		    {"map.create", "Create and select a map using Workshop defaults.", params({{"id", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return workshop_apply(d, "create", {{"kind", "maps"}, {"id", a.at("id")}});
		     }},
		    {"map.select", "Select a map by stable ID.", params({{"id", "string"}}),
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
		     params({{"definition", "string"}, {"x", "integer"}, {"y", "integer"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return d.place(a.at("definition"), a.at("x"), a.at("y"));
		     }},
		    {"instance.delete", "Delete selected instance IDs.", params({{"ids", "identities"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     for (const auto &id : a.at("ids"))
				     d.erase(id);
			     return true;
		     }},
		    {"instance.duplicate", "Duplicate instances and their mission overrides; returns new IDs.",
		     params({{"ids", "identities"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return d.duplicate_instances(a.at("ids").get<std::set<std::uint64_t>>());
		     }},
		    {"instance.translate", "Translate a group while preserving relative positions.",
		     params({{"ids", "identities"}, {"dx", "integer"}, {"dy", "integer"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     d.translate_instances(a.at("ids").get<std::set<std::uint64_t>>(), a.at("dx"), a.at("dy"));
			     return true;
		     }},
		    {"instance.edit", "Apply shared bulk fields/overrides or revert overrides through production verification.",
		     params({{"ids", "identities"}},
		            {{"fields", "object"}, {"overrides", "object"}, {"revert", "array"}, {"revert_all", "boolean"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     d.edit_instances(a.at("ids").get<std::set<std::uint64_t>>(), a.value("fields", Json::object()),
			                      a.value("overrides", Json::object()), a.value("revert", std::vector<std::string>{}),
			                      a.value("revert_all", false));
			     return true;
		     }},
		    {"vocabulary", "Describe current action/observation signatures for a reusable or owner-local tree.",
		     params({{"target", "object"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
		     {
			     return workshop_tree_vocabulary(d, target(d, a.at("target")));
		     }},
		    {"ship.add", "Create a draft ship item using the GUI's default fitting records.",
		     params({{"target", "object"}, {"id", "string"}, {"kind", "string"}},
		            {{"definition", "string"}, {"selection", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return workshop_ship_add(target(d, a.at("target")), a);
		     }},
		    {"tree.rename", "Rename a state and repair parent/initial/transition references.",
		     params({{"target", "object"}, {"from", "string"}, {"to", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     workshop_tree_rename(target(d, a.at("target")), a.at("from"), a.at("to"));
			     return true;
		     }},
		    {"layout.drop", "Insert/move/wrap a panel through the same operation as canvas drag/drop.",
		     params({{"target", "object"}, {"destination", "string"}}, {{"source", "string"},
		                                                                {"component", "string"},
		                                                                {"group", "string"},
		                                                                {"kind", "string"},
		                                                                {"index", "integer"},
		                                                                {"x", "integer"},
		                                                                {"y", "integer"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     return workshop_layout_drop(target(d, a.at("target")), a, *d.preview_ui());
		     }},
		    {"sprite.apply", "Apply an imported material sidecar with explicit image references.",
		     params({{"target", "object"}, {"sidecar", "object"}, {"images", "object"}},
		            {{"engine_effect", "boolean"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     workshop_sprite_apply(d, target(d, a.at("target")), a.at("sidecar"), a.at("images"),
			                           a.value("engine_effect", false));
			     return true;
		     }},

		    {"tree.reparent", "Reparent a state, refusing cycles and repairing direct initial-child references.",
		     params({{"target", "object"}, {"state", "string"}, {"parent", "string"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     WorkshopDocument::reparent_tree_state(target(d, a.at("target")), a.at("state"), a.at("parent"));
			     return true;
		     }},
		    {"import.bytes", "Import bounded binary data. Types: image, font, model, asset, map, dependency or pack.",
		     params({{"type", "string"}, {"filename", "string"}, {"bytes", "bytes"}}),
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
		result.push_back(
		    {"defaults", "Read the same definition factories as the GUI.",
		     params({{"kind", "string"}}, {{"context", "string"}}), [](const WorkshopDocument &, const Json &a)
		     {
			     return workshop_definition_default(a.at("kind"), a.value("context", std::string("controller")));
		     }});
		result.push_back({"seed", "Apply the same optional-block defaults as a GUI Add/Enable action.",
		                  params({{"target", "object"}, {"seed", "string"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  const auto &seed = workshop_seeds().at(a.at("seed").get<std::string>());
			                  auto &owner = target(d, a.at("target"));
			                  owner[seed.at("field").get<std::string>()] = seed.at("value");
			                  return seed.at("value");
		                  }});
		result.push_back({"new", "Start a new empty authoring pack.", params({}, {{"id", "string"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  d.reset();
			                  if (a.contains("id"))
				                  d.rename_pack(a.at("id"));
			                  return d.manifest();
		                  },
		                  "load"});
		result.push_back(
		    {"pack.parent",
		     "Set a parent from loaded base ancestry, refusing cycles and dropping unrelated dependencies.",
		     params({{"id", "string"}}), [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     workshop_set_parent(d, a.at("id"));
			     return a.at("id");
		     }});
		result.push_back(
		    {"tree.localize", "Create/copy an owner-local tree binding without serializing private compiled IDs.",
		     params({{"target", "object"}, {"field", "string"}}, {{"context", "string"}, {"copy", "boolean"}}),
		     [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     auto &owner = target(d, a.at("target"));
			     const auto key = a.at("field").get<std::string>();
			     owner[key] =
			         workshop_tree_localize(d, owner.value(key, Json()), a.value("context", std::string("controller")),
			                                a.value("copy", false));
			     return owner.at(key);
		     }});
		result.push_back({"tree.promote",
		                  "Promote a local tree to a reusable definition, retaining policy reference encoding.",
		                  params({{"target", "object"}, {"id", "string"}}, {{"policy", "boolean"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  const auto path = a.at("target").value("path", std::string{});
			                  return workshop_tree_promote(
			                      d, target(d, a.at("target")), a.at("id"),
			                      a.value("policy", path.ends_with("/policy") || path.ends_with("/member_policy")));
		                  }});
		result.push_back({"tree.state.add", "Add a state with shared phase defaults and a collision-free identity.",
		                  params({{"target", "object"}}, {{"id", "string"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  return workshop_tree_add_state(target(d, a.at("target")), a.value("id", std::string{}));
		                  }});
		result.push_back(
		    {"tree.state.delete", "Remove a state only after initial, child and transition references are resolved.",
		     params({{"target", "object"}, {"id", "string"}}), [](WorkshopDocument &d, const Json &a) -> Json
		     {
			     workshop_tree_remove_state(target(d, a.at("target")), a.at("id"));
			     return true;
		     }});
		result.push_back({"tree.task.add", "Append a supported task to a state's ordered phase.",
		                  params({{"target", "object"}, {"state", "string"}, {"phase", "string"}, {"kind", "string"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  auto &tree = target(d, a.at("target"));
			                  const auto phase = a.at("phase").get<std::string>();
			                  if (phase != "entry" && phase != "running" && phase != "exit")
				                  throw std::runtime_error("Unknown tree phase");
			                  if (a.at("kind") == "attempt" &&
			                      d.preview_tree(tree).value("context", std::string{}) != "controller")
				                  throw std::runtime_error("Attempt tasks require a Controller context");
			                  for (auto &state : tree.at("states"))
				                  if (state.at("id") == a.at("state"))
				                  {
					                  if (!state.contains(phase))
						                  state[phase] = Json::array();
					                  state[phase].push_back(workshop_tree_task(a.at("kind")));
					                  return state[phase].size() - 1;
				                  }
			                  throw std::runtime_error("Unknown state");
		                  }});
		result.push_back({"tree.task.signature", "Reset action arguments to the current production signature.",
		                  params({{"target", "object"}, {"tree", "object"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  auto &task = target(d, a.at("target"));
			                  const auto vocabulary = workshop_tree_vocabulary(d, target(d, a.at("tree")));
			                  for (const auto &action : vocabulary.at("actions"))
				                  if (action.at("id") == task.at("action"))
				                  {
					                  task["arguments"] = workshop_action_arguments(action);
					                  return task.at("arguments");
				                  }
			                  throw std::runtime_error("Action is absent from the current production vocabulary");
		                  }});
		const auto layout = [&](const char *id, const char *action, const char *help, Json schema)
		{
			result.push_back({id, help, std::move(schema), [action](WorkshopDocument &d, const Json &a)
			                  {
				                  return workshop_layout_action(d, target(d, a.at("target")), action, a);
			                  }});
		};
		layout("layout.convert", "convert", "Convert legacy Console widgets/header/HUD to an explicit layout.",
		       params({{"target", "object"}}, {{"field", "string"}}));
		layout("layout.detach", "detach", "Detach a referenced layout, merging its authored overrides.",
		       params({{"target", "object"}}, {{"field", "string"}}));
		layout("layout.override", "override", "Make an explicit local override of a referenced layout.",
		       params({{"target", "object"}}, {{"field", "string"}}));
		layout("layout.revert", "revert", "Revert local overrides of a referenced layout.",
		       params({{"target", "object"}}, {{"field", "string"}}));
		layout("layout.reference", "reference", "Select a layout template and clear the previous override.",
		       params({{"target", "object"}, {"id", "string"}}, {{"field", "string"}}));
		layout("layout.compact", "compact", "Create or revert the compact arrangement.",
		       params({{"target", "object"}}, {{"enabled", "boolean"}}));
		layout("layout.save-template", "save-template", "Save a reusable layout template.",
		       params({{"target", "object"}, {"id", "string"}}));
		layout("layout.group.save", "group.save", "Save a selected node as a reusable group.",
		       params({{"target", "object"}, {"id", "string"}}));
		layout("layout.group.detach", "group.detach", "Expand a referenced group and its overrides locally.",
		       params({{"target", "object"}}));
		layout("layout.node.add", "node.add", "Insert a draft node without wrapping an existing panel.",
		       params({{"target", "object"}}, {{"parent", "string"},
		                                       {"id", "string"},
		                                       {"kind", "string"},
		                                       {"component", "string"},
		                                       {"group", "string"}}));
		layout("layout.node.delete", "node.delete", "Delete a layout child; the root cannot be deleted.",
		       params({{"target", "object"}, {"node", "string"}}));
		layout("layout.node.reorder", "node.reorder", "Move a node one place earlier/later.",
		       params({{"target", "object"}, {"node", "string"}, {"direction", "integer"}}));
		layout("layout.node.duplicate", "node.duplicate",
		       "Duplicate a panel and its widget bindings, refusing singleton built-ins.",
		       params({{"target", "object"}, {"node", "string"}, {"console", "string"}}));
		layout("widget.add", "widget.add", "Create a supported widget, optionally placing it in a Console layout.",
		       params({{"target", "object"}, {"kind", "string"}},
		              {{"id", "string"}, {"layout", "string"}, {"parent", "string"}}));
		layout("theme.create", "theme.create", "Create a local theme from the resolved current theme and overrides.",
		       params({{"target", "object"}}, {{"id", "string"}}));
		layout("theme.pack", "theme.pack", "Use the selected theme for pack menus.", params({{"target", "object"}}));
		const auto graph =
		    [&](const char *id, const char *action, const char *help, Json schema, const char *execution = "edit")
		{
			result.push_back({id, help, std::move(schema),
			                  [action](WorkshopDocument &d, const Json &a)
			                  {
				                  return workshop_ship_graph(d, a.at("entity"), action, a);
			                  },
			                  execution});
		};
		result.emplace_back("ship.inspect", "Read the GUI's fitted nodes, binding choices and assembly edit addresses.",
		                    params({{"entity", "string"}}),
		                    [](const WorkshopDocument &document, const Json &args)
		                    {
			                    return workshop_inspect_ship(document, args.at("entity"));
		                    });
		graph("ship.override", "override",
		      "Make the explicit local/inherited assembly override used by the graph editor.",
		      params({{"entity", "string"}, {"node", "string"}}));
		graph("ship.connect", "connect",
		      "Connect compatible ship nodes and verify the candidate through production paths.",
		      params({{"entity", "string"}, {"node", "string"}, {"field", "string"}, {"destination", "string"}}));
		graph("ship.disconnect", "disconnect", "Remove a scalar/selected array binding and verify the candidate.",
		      params({{"entity", "string"}, {"node", "string"}, {"field", "string"}}, {{"destination", "string"}}));
		graph("ship.remove", "remove", "Remove a local ship item only if the production candidate remains coherent.",
		      params({{"entity", "string"}, {"node", "string"}}));

		result.push_back(
		    {"controls",
		     "Describe registered authoring controls, including resolved values/defaults/ranges for the current target "
		     "and context.",
		     params({}, {{"target", "object"}, {"control", "string"}, {"group", "string"}, {"context", "object"}}),
		     [](const WorkshopDocument &d, const Json &a) -> Json
		     {
			     if (!a.contains("control") && !a.contains("group"))
				     return workshop_fields();
			     const auto &owner = target(d, a.at("target"));
			     const auto context = a.value("context", Json::object());
			     if (a.contains("control"))
				     return workshop_control(a.at("control"), owner, context);
			     Json result = Json::array();
			     for (const auto &[id, spec] : workshop_fields().items())
				     if (spec.value("group", Json()) == a.at("group"))
					     result.push_back(workshop_control(id, owner, context));
			     return result;
		     }});
		result.push_back(
		    {"control.set", "Apply a registered control's constraints and coupled authoring changes.",
		     params({{"target", "object"}, {"control", "string"}, {"value", "any"}}, {{"context", "object"}}),
		     [](WorkshopDocument &d, const Json &a)
		     {
			     return workshop_set_control(target(d, a.at("target")), a.at("control"), a.at("value"),
			                                 a.value("context", Json::object()));
		     }});
		result.push_back({"authoring.actions", "Describe custom authoring transformations shared by the GUI and CLI.",
		                  params({}), [](const WorkshopDocument &, const Json &)
		                  {
			                  return workshop_custom_actions();
		                  }});
		result.push_back({"authoring.apply", "Apply a custom authoring transformation from the shared registry.",
		                  params({{"target", "object"}, {"action", "string"}}, {{"arguments", "object"}}),
		                  [](WorkshopDocument &d, const Json &a)
		                  {
			                  return workshop_custom_action(target(d, a.at("target")), a.at("action"),
			                                                a.value("arguments", Json::object()));
		                  }});
		result.push_back({"asset.choices",
		                  "List imported image assets using the same compatibility filters as the GUI picker.",
		                  params({}, {{"skybox", "boolean"}}), [](const WorkshopDocument &d, const Json &a)
		                  {
			                  return workshop_asset_choices(d, a.value("skybox", false));
		                  }});
		result.push_back({"layout.palette", "Describe the GUI component palette and valid ship/Console contexts.",
		                  params({{"target", "object"}}, {{"header", "boolean"}, {"context", "string"}}),
		                  [](const WorkshopDocument &d, const Json &a)
		                  {
			                  return workshop_layout_palette(d, target(d, a.at("target")), a.value("header", false),
			                                                 a.value("context", std::string{}));
		                  }});
		result.push_back(
		    {"instance.fields",
		     "Describe common bulk-editable fields, mixed values and sprite choices for the selected instances.",
		     params({{"ids", "identities"}}), [](const WorkshopDocument &d, const Json &a)
		     {
			     return workshop_instance_fields(d, a.at("ids").get<std::set<std::uint64_t>>());
		     }});
		result.push_back({"instance.field", "Edit a field from the shared bulk inspector description.",
		                  params({{"ids", "identities"}, {"path", "string"}, {"value", "any"}}),
		                  [](WorkshopDocument &d, const Json &a) -> Json
		                  {
			                  workshop_instance_field(d, a.at("ids").get<std::set<std::uint64_t>>(), a.at("path"),
			                                          a.at("value"));
			                  return true;
		                  }});
		result.push_back({"record.defaults", "Resolve a shared record factory with its declared context.",
		                  params({{"prototype", "string"}}, {{"context", "object"}}),
		                  [](const WorkshopDocument &, const Json &a)
		                  {
			                  return workshop_record_value(a.at("prototype"), a.value("context", Json::object()));
		                  }});
		return result;
	}();

	return all;
}
} // namespace
Json workshop_definition_kinds()
{
	Json result = Json::array();
	for (const auto &[name, kind] : kinds)
		result.push_back(name);
	return result;
}
Json workshop_operations()
{
	Json result = Json::array();
	for (const auto &op : operations())
		result.push_back({{"id", op.id}, {"help", op.help}, {"arguments", op.arguments}, {"execution", op.execution}});
	for (const auto &row : Json::array(
	         {Json{{"id", "help"}, {"execution", "query"}, {"help", "List operations and protocol version."}},
	          Json{{"id", "capabilities"},
	               {"execution", "query"},
	               {"help", "List operations and supported definition kinds."}},
	          Json{{"id", "describe"},
	               {"execution", "query"},
	               {"help", "Read shared GUI field descriptions or one operation."},
	               {"arguments", params({}, {{"field", "string"},
	                                         {"operation", "string"},
	                                         {"record", "string"},
	                                         {"records", "boolean"},
	                                         {"seeds", "boolean"}})}},
	          Json{{"id", "verify"},
	               {"execution", "query"},
	               {"help", "Verify the draft with production dependency resolution."}},
	          Json{{"id", "undo"}, {"execution", "history"}, {"help", "Undo the previous transaction."}},
	          Json{{"id", "redo"}, {"execution", "history"}, {"help", "Redo the previous undone transaction."}},
	          Json{{"id", "batch"},
	               {"execution", "edit"},
	               {"help", "Apply document requests atomically as one undo step."},
	               {"arguments", params({{"requests", "array"}})}},
	          Json{{"id", "open"},
	               {"execution", "load"},
	               {"help", "Open a pack or source draft with source=true; dependencies must already be loaded."},
	               {"arguments", params({{"path", "string"}}, {{"source", "boolean"}})}},
	          Json{{"id", "import"},
	               {"execution", "edit"},
	               {"help", "Import a local file; types match import.bytes."},
	               {"arguments", params({{"path", "string"}, {"type", "string"}}, {{"filename", "string"}})}},
	          Json{{"id", "save"},
	               {"execution", "write"},
	               {"help", "Save an unfinished source draft to a directory."},
	               {"arguments", params({{"path", "string"}})}},
	          Json{{"id", "map.export"},
	               {"execution", "write"},
	               {"help", "Save the selected map JSON through ordinary Workshop verification."},
	               {"arguments", params({{"path", "string"}})}},
	          Json{{"id", "source.export"},
	               {"execution", "write"},
	               {"help", "Export an unfinished editable source ZIP."},
	               {"arguments", params({{"path", "string"}})}},
	          Json{{"id", "export"},
	               {"execution", "write"},
	               {"help", "Verify and export ZIP/DAT and client companions."},
	               {"arguments", params({{"path", "string"}}, {{"scenario_only", "boolean"}})}}}))
	{
		auto description = row;
		result.push_back(std::move(description));
	}
	for (auto &row : result)
	{
		if (!row.contains("arguments"))
			row["arguments"] = params({});
		const auto id = row.at("id").get<std::string>();
		if (row.at("execution") != "edit")
			row["arguments"]["properties"].erase("verify");
		row["available"] = true;
#ifdef __EMSCRIPTEN__
		if (id == "open" || id == "import" || row.at("execution") == "write")
			row["available"] = false;
#endif
	}

	return result;
}
Json workshop_operation(const std::string &id)
{
	static const auto all = workshop_operations();
	for (const auto &row : all)
		if (row.at("id") == id)
			return row;
	throw std::runtime_error("Unknown operation: " + id);
}
void workshop_validate_arguments(const std::string &operation, const Json &arguments)
{
	const auto descriptor = workshop_operation(operation);
	if (!descriptor.value("available", true))
		throw std::runtime_error("Operation is unavailable on this platform: " + operation);
	if (!arguments.is_object())
		throw std::runtime_error("arguments: expected object");
	const auto schema = descriptor.at("arguments");
	for (const auto &name : schema.at("required"))
		if (!arguments.contains(name.get<std::string>()))
			throw std::runtime_error("Missing argument: " + name.get<std::string>());
	for (const auto &[name, value] : arguments.items())
	{
		if (!schema.at("properties").contains(name))
			throw std::runtime_error("Unknown argument: " + name);
		const auto &spec = schema.at("properties").at(name);
		if (!spec.contains("type"))
			continue;
		const auto type = spec.at("type").get<std::string>();
		if ((type == "string" && !value.is_string()) || (type == "object" && !value.is_object()) ||
		    (type == "array" && !value.is_array()) || (type == "integer" && !value.is_number_integer()) ||
		    (type == "boolean" && !value.is_boolean()))
			throw std::runtime_error(name + ": expected " + type);
		if (type == "integer")
			(void)checked_integer(value, std::numeric_limits<std::int64_t>::min(),
			                      std::numeric_limits<std::int64_t>::max(), name);
		if (spec.contains("items"))
			for (const auto &item : value)
				(void)checked_unsigned(item, spec.at("items").at("minimum"), spec.at("items").at("maximum"),
				                       name + " item");
	}
}
Json workshop_query(const WorkshopDocument &document, const std::string &operation, const Json &arguments)
{
	workshop_validate_arguments(operation, arguments);
	for (const auto &op : operations())
		if (operation == op.id && op.query)
			return op.query(document, arguments);
	throw std::runtime_error("Not a document query: " + operation);
}
Json workshop_apply(WorkshopDocument &document, const std::string &operation, const Json &arguments)
{
	workshop_validate_arguments(operation, arguments);
	for (const auto &op : operations())
		if (operation == op.id)
			return op.query ? op.query(document, arguments) : op.apply(document, arguments);
	throw std::runtime_error("Not a document operation: " + operation);
}
} // namespace phoenix
