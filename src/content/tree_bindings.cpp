module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.content;

namespace phoenix
{
namespace
{
void binding_require(bool condition, const std::string &path, const std::string &reason)
{
	if (!condition)
		throw std::runtime_error(path + ": " + reason);
}
struct InheritedRecordBindings
{
	std::optional<state_tree::Context> context;
	std::vector<Json> bindings;
};
InheritedRecordBindings inherited_record_bindings(const Json &tree, const Catalog &catalog,
                                                  std::set<std::string> &active, std::size_t depth)
{
	const auto id = tree.value("id", std::string("<inline>"));
	const auto path = "trees." + id;
	binding_require(tree.is_object(), path, "record bindings require a tree object");
	binding_require(depth <= 64, path, "record-binding inheritance exceeds 64 levels");
	InheritedRecordBindings result;
	if (tree.contains("extends"))
	{
		binding_require(tree.at("extends").is_string(), path + ".extends", "expected a tree identity");
		const auto parent = tree.at("extends").get<std::string>();
		binding_require(catalog.trees.contains(parent), path + ".extends", "unknown parent tree '" + parent + "'");
		binding_require(active.insert(parent).second, path + ".extends",
		                "cyclic tree inheritance through '" + parent + "'");
		result = inherited_record_bindings(catalog.trees.at(parent), catalog, active, depth + 1);
		active.erase(parent);
	}
	if (tree.contains("context"))
	{
		binding_require(tree.at("context").is_string(), path + ".context", "expected system or scenario");
		const auto name = tree.at("context").get<std::string>();
		binding_require(name == "system" || name == "scenario", path + ".context", "expected system or scenario");
		const auto context = name == "system" ? state_tree::Context::System : state_tree::Context::Scenario;
		binding_require(!result.context || *result.context == context, path + ".context",
		                "inherited tree context cannot change");
		result.context = context;
	}
	if (!tree.contains("record_bindings"))
		return result;
	const auto &bindings = tree.at("record_bindings");
	binding_require(bindings.is_array() && bindings.size() <= 4096, path + ".record_bindings",
	                "expected at most 4096 named bindings");
	std::set<std::string> names;
	for (const auto &binding : bindings)
	{
		binding_require(binding.is_object() && binding.contains("name") && binding.at("name").is_string(),
		                path + ".record_bindings", "binding requires a name");
		const auto name = binding.at("name").get<std::string>();
		binding_require(valid_identifier(name) && names.insert(name).second, path + ".record_bindings",
		                "invalid or duplicate local binding '" + name + "'");
		const auto existing = std::find_if(result.bindings.begin(), result.bindings.end(),
		                                   [&](const auto &value)
		                                   {
			                                   return value.at("name") == name;
		                                   });
		if (existing == result.bindings.end())
			result.bindings.push_back(binding);
		else
			*existing = binding;
	}
	binding_require(result.bindings.size() <= 4096, path + ".record_bindings", "resolved binding limit exceeded");
	return result;
}
InheritedRecordBindings resolve_record_bindings(const Json &tree, const Catalog &catalog)
{
	std::set<std::string> active;
	if (tree.contains("id") && tree.at("id").is_string())
		active.insert(tree.at("id").get<std::string>());
	return inherited_record_bindings(tree, catalog, active, 0);
}
std::string binding_subject(const Json &binding, const std::string &path)
{
	binding_require(!(binding.contains("subject") && binding.contains("subject_parameter")), path,
	                "choose subject or subject_parameter, not both");
	if (!binding.contains("subject") && !binding.contains("subject_parameter"))
		return {};
	const auto &value = binding.at(binding.contains("subject") ? "subject" : "subject_parameter");
	if (value.is_string())
	{
		const auto name = value.get<std::string>();
		binding_require(name.empty() || name == "self" || name == "selected_contact", path + ".subject",
		                "supported names are self and selected_contact");
		return name == "selected_contact" ? name : std::string{};
	}
	binding_require(value.is_number_integer(), path + ".subject",
	                "expected self, selected_contact, or a positive integer entity identity");
	std::uint64_t id = 0;
	if (value.is_number_unsigned())
		id = value.get<std::uint64_t>();
	else
	{
		const auto number = value.get<std::int64_t>();
		binding_require(number > 0, path + ".subject", "entity identity must be positive");
		id = static_cast<std::uint64_t>(number);
	}
	binding_require(id > 0 && id <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
	                path + ".subject", "entity identity is outside the supported reference range");
	return std::to_string(id);
}
} // namespace

std::vector<GameplayTreeRecordBinding> gameplay_record_bindings(const Json &tree, const Catalog &catalog)
{
	const auto inherited = resolve_record_bindings(tree, catalog);
	const auto path = "trees." + tree.value("id", std::string("<inline>")) + ".record_bindings";
	binding_require(inherited.bindings.empty() || inherited.context.has_value(), path,
	                "record bindings require a contextual System or scenario tree");
	std::vector<GameplayTreeRecordBinding> result;
	for (const auto &value : inherited.bindings)
	{
		GameplayTreeRecordBinding binding;
		binding.name = value.at("name").get<std::string>();
		const auto field_path = path + "." + binding.name;
		binding.schema = value.at("schema").get<std::string>();
		binding.field = value.at("field").get<std::string>();
		const auto schema_key = catalog.knowledge_definition.schema(binding.schema);
		const auto field_key = catalog.knowledge_definition.field(schema_key, binding.field);
		const auto &schema = catalog.knowledge_definition.schemas.at(schema_key - 1);
		const auto &field = schema.fields.at(field_key - 1);
		const auto scope = value.value("scope", std::string("console"));
		binding_require(scope == "console" || scope == "ship", field_path + ".scope",
		                "supported scopes are console and ship");
		binding.scope = scope == "ship" ? GameplayTreeRecordScope::Ship : GameplayTreeRecordScope::Console;
		if (binding.scope == GameplayTreeRecordScope::Ship)
			binding_require(schema.ship_publishable && field.publishable, field_path + ".scope",
			                "ship binding selects a field that cannot be published");
		binding.type = field.type == KnowledgeType::Boolean ? state_tree::Type::Boolean
		               : field.type == KnowledgeType::Text  ? state_tree::Type::Text
		                                                    : state_tree::Type::Integer;
		if (value.contains("type"))
		{
			const auto expected = field.type == KnowledgeType::Boolean ? "boolean"
			                      : field.type == KnowledgeType::Text  ? "text"
			                                                           : "integer";
			binding_require(value.at("type") == expected, field_path + ".type",
			                "binding type is derived from the schema field");
		}
		binding.subject_parameter = binding_subject(value, field_path);
		binding.contexts = static_cast<std::uint8_t>(*inherited.context);
		binding_require(binding.subject_parameter != "selected_contact" ||
		                    inherited.context == state_tree::Context::System,
		                field_path + ".subject", "selected_contact requires a System Console context");
		result.push_back(std::move(binding));
	}
	// Use the production vocabulary's limits and name checks, including collisions
	// with built-in observations. This reads schema metadata only.
	if (inherited.context)
		(void)gameplay_tree_registry(*inherited.context, result);
	return result;
}
state_tree::Registry gameplay_registry(const Json &tree, const Catalog &catalog, state_tree::Context context)
{
	const auto inherited = resolve_record_bindings(tree, catalog);
	binding_require(!inherited.context || inherited.context == context,
	                "trees." + tree.value("id", std::string("<inline>")) + ".context",
	                "registry context differs from authored tree");
	return gameplay_tree_registry(context, gameplay_record_bindings(tree, catalog));
}
} // namespace phoenix
