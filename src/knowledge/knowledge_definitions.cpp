module;
#include <span>
#include <optional>
#include <string>
#include <variant>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

module phoenix.knowledge;

namespace phoenix
{
namespace
{
void require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error("knowledge: " + message);
}
std::uint64_t nonnegative(const Json &value)
{
	require(value.is_number_integer(), "expected a nonnegative integer");
	if (value.is_number_unsigned())
		return value.get<std::uint64_t>();
	const auto result = value.get<std::int64_t>();
	require(result >= 0, "expected a nonnegative integer");
	return static_cast<std::uint64_t>(result);
}
} // namespace
KnowledgeKey CompiledKnowledge::role(std::string_view name) const
{
	for (std::size_t i = 0; i < roles.size(); ++i)
		if (roles[i] == name)
			return static_cast<KnowledgeKey>(i + 1);
	throw std::runtime_error("knowledge: unknown role '" + std::string(name) + "'");
}
KnowledgeKey CompiledKnowledge::schema(std::string_view name) const
{
	for (const auto &item : schemas)
		if (item.name == name)
			return item.key;
	throw std::runtime_error("knowledge: unknown schema '" + std::string(name) + "'");
}
KnowledgeKey CompiledKnowledge::field(KnowledgeKey key, std::string_view name) const
{
	for (const auto &item : schemas)
		if (item.key == key)
			for (const auto &field : item.fields)
				if (field.name == name)
					return field.key;
	throw std::runtime_error("knowledge: unknown field '" + std::string(name) + "'");
}
CompiledKnowledge compile_knowledge(const Json &definition)
{
	require(definition.is_object(), "definition must be an object");
	CompiledKnowledge result;
	const auto history = definition.contains("history_limit") ? nonnegative(definition.at("history_limit")) : 256;
	require(history > 0 && history <= 65536, "history_limit must be 1..65536");
	result.history_limit = static_cast<std::size_t>(history);
	require(result.history_limit > 0 && result.history_limit <= 65536, "history_limit must be 1..65536");
	result.roles = definition.value("roles", std::vector<std::string>{});
	std::set<std::string> names;
	for (const auto &role : result.roles)
		require(valid_identifier(role) && names.insert(role).second, "invalid or duplicate role '" + role + "'");
	names.clear();
	for (const auto &entry : definition.value("schemas", Json::array()))
	{
		KnowledgeSchema schema;
		schema.key = static_cast<KnowledgeKey>(result.schemas.size() + 1);
		schema.name = entry.at("id").get<std::string>();
		require(valid_identifier(schema.name) && names.insert(schema.name).second,
		        "invalid or duplicate schema '" + schema.name + "'");
		schema.ship_publishable = entry.value("ship_publishable", false);
		schema.stale_after = entry.contains("stale_after_ticks") ? nonnegative(entry.at("stale_after_ticks")) : 0;
		for (const auto &role : entry.at("producers"))
			schema.producers.push_back(result.role(role.get<std::string>()));
		for (const auto &role : entry.at("readers"))
			schema.readers.push_back(result.role(role.get<std::string>()));
		require(!schema.producers.empty() && !schema.readers.empty(), schema.name + ": requires producers and readers");
		std::set<std::string> fields;
		for (const auto &item : entry.at("fields"))
		{
			KnowledgeField field;
			field.key = static_cast<KnowledgeKey>(schema.fields.size() + 1);
			field.name = item.at("id").get<std::string>();
			require(valid_identifier(field.name) && fields.insert(field.name).second,
			        schema.name + ": invalid or duplicate field '" + field.name + "'");
			const auto type = item.at("type").get<std::string>();
			require(type == "integer" || type == "boolean" || type == "text",
			        schema.name + ": unsupported field type '" + type + "'");
			field.type = type == "boolean" ? KnowledgeType::Boolean
			             : type == "text"  ? KnowledgeType::Text
			                               : KnowledgeType::Integer;
			field.required = item.value("required", true);
			field.publishable = item.value("publishable", false);
			schema.fields.push_back(std::move(field));
		}
		require(!schema.fields.empty(), schema.name + ": requires fields");
		result.schemas.push_back(std::move(schema));
	}
	return result;
}

namespace knowledge_detail
{
void knowledge_require(bool condition, const std::string &reason)
{
	if (!condition)
		throw std::runtime_error("gameplay knowledge: " + reason);
}
KnowledgeId knowledge_unsigned(const Json &value)
{
	knowledge_require(value.is_number_integer(), "expected a nonnegative integral identity/tick");
	if (value.is_number_unsigned())
		return value.get<KnowledgeId>();
	const auto number = value.get<std::int64_t>();
	knowledge_require(number >= 0, "negative identity/tick");
	return static_cast<KnowledgeId>(number);
}
bool knowledge_has(const std::vector<KnowledgeKey> &keys, KnowledgeKey key)
{
	return std::find(keys.begin(), keys.end(), key) != keys.end();
}
const KnowledgeSchema &knowledge_schema(const CompiledKnowledge &definition, KnowledgeKey key)
{
	knowledge_require(key > 0 && key <= definition.schemas.size(), "unknown schema key");
	return definition.schemas[key - 1];
}
} // namespace knowledge_detail
} // namespace phoenix
