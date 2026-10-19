#pragma once
// Module-private composition shared by production packs and editable drafts.
// Override authorization remains the production resolver's responsibility.
namespace phoenix
{
inline void overlay_knowledge(Json &target, const Json &source, const std::string &path)
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
