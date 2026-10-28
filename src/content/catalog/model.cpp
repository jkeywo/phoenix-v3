module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

module phoenix.content.catalog;

namespace phoenix
{
namespace
{
bool legacy_phoenix_root(const Json &manifest)
{
	return manifest.value("id", std::string{}) == "phoenix-base" && !manifest.contains("base") &&
	       !manifest.contains("parent") && pack_dependencies(manifest).empty() && !manifest.contains("format");
}

[[noreturn]] void invalid(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}

std::string text(const Json &object, const char *key, const std::string &path)
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_string() ||
	    object[key].get<std::string>().empty())
		invalid(path + "." + key, "expected nonempty string");
	return object[key].get<std::string>();
}
} // namespace
std::vector<std::string> pack_dependencies(const Json &manifest)
{
	if (manifest.contains("parent"))
		return {manifest.at("parent").get<std::string>()};
	return manifest.value("dependencies", std::vector<std::string>{});
}

BaseConfiguration base_configuration(const Pack &pack)
{
	const auto &manifest = pack.manifest();
	if ((!manifest.contains("base") && !legacy_phoenix_root(manifest)) || manifest.contains("parent") ||
	    !pack_dependencies(manifest).empty())
		throw std::runtime_error("Launch base must be an independent root pack");
	const auto config = manifest.value("base", Json::object());
	return {manifest.at("id"), pack.hash(), config.value("render_mode", std::string("sprites")),
	        config.value("interface_style", std::string("current"))};
}

void inherit_definitions(std::map<std::string, Json> &definitions, const std::string &kind, const std::string &root)
{
	std::set<std::string> done, active;
	std::function<void(const std::string &)> visit = [&](const std::string &id)
	{
		if (done.contains(id))
			return;
		if (!active.insert(id).second)
			invalid(kind + "." + id, "inheritance cycle");
		auto &definition = definitions.at(id);
		if (definition.contains("extends"))
		{
			auto parent = text(definition, "extends", kind + "." + id);
			if (!definitions.contains(parent))
				invalid(kind + "." + id + ".extends", "unknown parent '" + parent + "'");
			visit(parent);
			auto resolved = definitions.at(parent);
			resolved.merge_patch(definition);
			resolved.erase("extends");
			definition = std::move(resolved);
		}
		active.erase(id);
		done.insert(id);
	};
	if (!root.empty())
		visit(root);
	else
		for (const auto &[id, value] : definitions)
			visit(id);
}

void overlay_knowledge(Json &target, const Json &source, const std::string &path)
{
	if (!source.is_object())
		throw std::runtime_error(path + ": expected object");
	if (!target.is_object())
		target = Json::object();
	for (const auto *field : {"roles", "schemas"})
	{
		if (source.contains(field) && !source.at(field).is_array())
			throw std::runtime_error(path + "." + field + ": expected array");
		if (!target.contains(field))
			target[field] = Json::array();
	}
	for (const auto &role : source.value("roles", Json::array()))
		if (std::find(target["roles"].begin(), target["roles"].end(), role) == target["roles"].end())
			target["roles"].push_back(role);
	std::set<std::string> local;
	for (const auto &schema : source.value("schemas", Json::array()))
	{
		if (!schema.is_object() || !schema.contains("id") || !schema.at("id").is_string())
			throw std::runtime_error(path + ".schemas: expected schema identity");
		const auto id = schema.at("id").get<std::string>();
		if (!local.insert(id).second)
			throw std::runtime_error(path + ".schemas." + id + ": duplicate schema");
		auto &schemas = target["schemas"];
		const auto existing = std::find_if(schemas.begin(), schemas.end(),
		                                   [&](const auto &entry)
		                                   {
			                                   return entry.at("id") == id;
		                                   });
		if (existing == schemas.end())
			schemas.push_back(schema);
		else
			*existing = schema;
	}
	if (source.contains("history_limit"))
		target["history_limit"] = source.at("history_limit");
}

} // namespace phoenix
