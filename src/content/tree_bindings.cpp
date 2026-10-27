module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
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
	std::vector<Json> summaries;
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
		binding_require(name == "system" || name == "scenario" || name == "controller" || name == "conversation" ||
		                    name == "away",
		                path + ".context", "expected system, controller or scenario");
		const auto context = name == "away"           ? state_tree::Context::Away
		                     : name == "conversation" ? state_tree::Context::Conversation
		                     : name == "controller"   ? state_tree::Context::Controller
		                     : name == "system"       ? state_tree::Context::System
		                                              : state_tree::Context::Scenario;
		binding_require(!result.context || *result.context == context, path + ".context",
		                "inherited tree context cannot change");
		result.context = context;
	}
	auto merge = [&](const char *field, std::vector<Json> &resolved)
	{
		if (!tree.contains(field))
			return;
		const auto field_path = path + "." + field;
		const auto &bindings = tree.at(field);
		binding_require(bindings.is_array() && bindings.size() <= 4096, field_path,
		                "expected at most 4096 named bindings");
		std::set<std::string> names;
		for (const auto &binding : bindings)
		{
			binding_require(binding.is_object() && binding.contains("name") && binding.at("name").is_string(),
			                field_path, "binding requires a name");
			const auto name = binding.at("name").get<std::string>();
			binding_require(valid_identifier(name) && names.insert(name).second, field_path,
			                "invalid or duplicate local binding '" + name + "'");
			const auto existing = std::find_if(resolved.begin(), resolved.end(),
			                                   [&](const auto &value)
			                                   {
				                                   return value.at("name") == name;
			                                   });
			if (existing == resolved.end())
				resolved.push_back(binding);
			else
				*existing = binding;
		}
		binding_require(resolved.size() <= 4096, field_path, "resolved binding limit exceeded");
	};
	merge("record_bindings", result.bindings);
	merge("summary_bindings", result.summaries);
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
		binding_require(name.empty() || name == "self" || name == "selected_contact" || name == "latest" ||
		                    name == "priority_or_latest",
		                path + ".subject", "supported names are self, selected_contact, latest and priority_or_latest");
		return name == "selected_contact" || name == "latest" || name == "priority_or_latest" ? name : std::string{};
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

Json resolve_tree_definition(const Json &tree, const Catalog &catalog)
{
	const auto inherited = resolve_record_bindings(tree, catalog);
	if (!inherited.context)
	{
		auto trees = catalog.trees;
		const auto id = tree.value("id", std::string("<inline>"));
		trees[id] = tree;
		inherit_definitions(trees, "trees", id);
		return trees.at(id);
	}
	auto resolved = state_tree::resolve_definition(tree, catalog.trees);
	if (!inherited.bindings.empty() || resolved.contains("record_bindings"))
		resolved["record_bindings"] = inherited.bindings;
	if (!inherited.summaries.empty() || resolved.contains("summary_bindings"))
		resolved["summary_bindings"] = inherited.summaries;
	return resolved;
}
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
		binding_require(binding.subject_parameter != "priority_or_latest" ||
		                    (inherited.context == state_tree::Context::Controller &&
		                     binding.schema == "phoenix.objective" && binding.scope == GameplayTreeRecordScope::Ship),
		                field_path + ".subject", "priority_or_latest requires a ship objective Controller read");
		binding_require(binding.subject_parameter != "latest" || inherited.context == state_tree::Context::Controller,
		                field_path + ".subject", "latest requires a Controller context");
		binding_require(binding.subject_parameter != "selected_contact" ||
		                    inherited.context == state_tree::Context::System,
		                field_path + ".subject", "selected_contact requires a System Console context");
		result.push_back(std::move(binding));
	}
	// Use the production vocabulary's limits and name checks, including collisions
	// with built-in observations. This reads schema metadata only.
	if (inherited.context && inherited.context != state_tree::Context::Controller)
		(void)gameplay_tree_registry(*inherited.context, result);
	return result;
}
namespace
{
std::vector<GameplayTreeSummaryBinding> summary_declarations(const Json &tree, const Catalog &catalog)
{
	const auto inherited = resolve_record_bindings(tree, catalog);
	const auto path = "trees." + tree.value("id", std::string("<inline>")) + ".summary_bindings";
	binding_require(inherited.summaries.empty() || inherited.context == state_tree::Context::System, path,
	                "summary bindings require a System tree context");
	if (inherited.summaries.empty())
		return {};
	std::vector<GameplayTreeSummaryBinding> result;
	for (const auto &value : inherited.summaries)
	{
		const auto name = value.at("name").get<std::string>();
		const auto field_path = path + "." + name;
		binding_require(value.contains("channel") && value.at("channel").is_string(), field_path,
		                "channel must be an authored identifier");
		const auto channel = value.at("channel").get<std::string>();
		binding_require(valid_identifier(channel) && channel != "destination.x" && channel != "destination.y",
		                field_path + ".channel", "invalid or reserved summary intent channel");
		if (value.contains("type"))
			binding_require(value.at("type") == "integer", field_path + ".type",
			                "summary intent bindings have integer type");
		result.push_back({name, channel});
	}
	return result;
}
} // namespace
std::vector<GameplayTreeSummaryBinding> gameplay_summary_bindings(const Json &tree, const Catalog &catalog)
{
	auto result = summary_declarations(tree, catalog);
	if (result.empty())
		return result;
	const auto registry = gameplay_tree_registry(state_tree::Context::System, gameplay_record_bindings(tree, catalog));
	const auto path = "trees." + tree.value("id", std::string("<inline>")) + ".summary_bindings";
	std::set<std::string> names;
	for (const auto &observation : registry.observations)
		names.insert(observation.name);
	for (const auto &binding : result)
		binding_require(names.insert(binding.name).second, path + "." + binding.name,
		                "name collides with another observation");
	binding_require(registry.observations.size() + result.size() <= 4096, path,
	                "combined observation layout exceeds 4096 fields");
	return result;
}
std::vector<std::string> gameplay_summary_channels(const Catalog &catalog, const std::string &entity,
                                                   const std::string &system)
{
	const auto fitted = resolve_systems(catalog, entity);
	const auto found = std::find_if(fitted.begin(), fitted.end(),
	                                [&](const auto &candidate)
	                                {
		                                return candidate.id == system;
	                                });
	if (found == fitted.end())
		throw std::runtime_error("entities." + entity + ".systems." + system + ": unknown fitted System");
	std::vector<std::string> result;
	switch (found->kind)
	{
	case OperatingSystem::Generator:
		result = {"output"};
		break;
	case OperatingSystem::Distribution:
		result = {"power", "allocation"};
		break;
	case OperatingSystem::Coolant:
		result = {"coolant", "cooling"};
		break;
	case OperatingSystem::Storage:
		result = {"charge", "discharge"};
		break;
	default:
		break;
	}
	if (!found->controller.empty())
		for (const auto &binding : summary_declarations(catalog.trees.at(found->controller), catalog))
			if (std::find(result.begin(), result.end(), binding.channel) == result.end())
				result.push_back(binding.channel);
	return result;
}
std::vector<ControllerTelemetryBinding> controller_telemetry_bindings(const Json &tree, const Catalog &catalog)
{
	const auto resolved = resolve_tree_definition(tree, catalog);
	const auto bindings = resolved.value("system_bindings", Json::array());
	binding_require(bindings.is_array() && bindings.size() <= 256, "Controller tree",
	                "expected bounded system_bindings");
	std::vector<ControllerTelemetryBinding> result;
	for (const auto &binding : bindings)
	{
		ControllerTelemetryBinding value{binding.at("name").get<std::string>(), binding.at("system").get<std::string>(),
		                                 binding.at("field").get<std::string>()};
		binding_require(valid_identifier(value.name) && valid_identifier(value.system) &&
		                    supported_system_observation(value.field),
		                "Controller tree", "invalid System observation binding");
		value.type = system_observation_type(value.field);
		const std::string expected = value.type == state_tree::Type::Boolean ? "boolean" : "integer";
		binding_require(binding.value("type", expected) == expected, "Controller tree",
		                "System observation type differs from producer");
		result.push_back(std::move(value));
	}
	return result;
}
std::vector<ControllerCommandReadBinding> controller_command_reads(const Json &tree, const Catalog &catalog)
{
	const auto bindings = resolve_tree_definition(tree, catalog).value("command_reads", Json::array());
	binding_require(bindings.is_array() && bindings.size() <= 256, "Controller tree", "expected bounded command_reads");
	std::vector<ControllerCommandReadBinding> result;
	for (const auto &binding : bindings)
	{
		ControllerCommandReadBinding value{binding.at("name").get<std::string>(),
		                                   binding.at("controller").get<std::string>(),
		                                   binding.at("channel").get<std::string>()};
		binding_require(valid_identifier(value.name) && valid_identifier(value.controller) &&
		                    valid_identifier(value.channel),
		                "Controller tree", "invalid accepted command read");
		const auto type = binding.value("type", std::string("integer"));
		binding_require(type == "integer" || type == "boolean", "Controller tree", "invalid accepted command type");
		value.type = type == "boolean" ? state_tree::Type::Boolean : state_tree::Type::Integer;
		result.push_back(std::move(value));
	}
	return result;
}
static std::vector<std::string> controller_execution_endpoints(const Json &tree, const Catalog &catalog,
                                                               SharedPropertyCatalog compiled,
                                                               const char *endpoint_field, const char *property_field)
{
	const auto bindings = resolve_tree_definition(tree, catalog).value(endpoint_field, Json::array());
	binding_require(bindings.is_array() && bindings.size() <= 256, "Controller tree",
	                std::string("expected bounded ") + endpoint_field);
	std::vector<std::string> result;
	std::set<std::string> seen;
	for (const auto &binding : bindings)
	{
		binding_require(binding.is_string(), "Controller tree", "execution binding must name an endpoint");
		const auto endpoint = binding.get<std::string>();
		binding_require(valid_identifier(endpoint) && seen.insert(endpoint).second, "Controller tree",
		                "invalid or duplicate execution endpoint binding");
		result.push_back(endpoint);
	}
	const auto property_bindings = resolve_tree_definition(tree, catalog).value(property_field, Json::array());
	binding_require(property_bindings.is_array() && property_bindings.size() <= 256, "Controller tree",
	                std::string("expected bounded ") + property_field);
	const auto definitions = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	std::set<std::pair<std::string, std::string>> seen_properties;
	for (const auto &binding : property_bindings)
	{
		binding_require(binding.is_object() && binding.size() == 2 && binding.contains("endpoint") &&
		                    binding.contains("property") && binding.at("endpoint").is_string() &&
		                    binding.at("property").is_string(),
		                "Controller tree", "expected endpoint and property");
		const auto endpoint = binding.at("endpoint").get<std::string>(),
		           property = binding.at("property").get<std::string>();
		binding_require(valid_identifier(endpoint) && definitions->contains(property) &&
		                    seen_properties.emplace(endpoint, property).second,
		                "Controller tree", "invalid or duplicate observed execution property binding");
		if (seen.insert(endpoint).second)
			result.push_back(endpoint);
	}
	return result;
}
std::vector<std::string> controller_ability_bindings(const Json &tree, const Catalog &catalog,
                                                     SharedPropertyCatalog properties)
{
	return controller_execution_endpoints(tree, catalog, std::move(properties), "ability_bindings",
	                                      "ability_property_bindings");
}
std::vector<std::string> controller_process_bindings(const Json &tree, const Catalog &catalog,
                                                     SharedPropertyCatalog properties)
{
	return controller_execution_endpoints(tree, catalog, std::move(properties), "process_bindings",
	                                      "process_property_bindings");
}
std::vector<ControllerContactBinding> controller_contact_bindings(const Json &tree, const Catalog &catalog)
{
	const auto bindings = resolve_tree_definition(tree, catalog).value("contact_bindings", Json::array());
	binding_require(bindings.is_array() && bindings.size() <= 64, "Controller tree",
	                "expected bounded contact_bindings");
	std::vector<ControllerContactBinding> result;
	for (const auto &binding : bindings)
	{
		ControllerContactBinding value{binding.at("name").get<std::string>(),
		                               binding.at("controller").get<std::string>(),
		                               binding.at("channel").get<std::string>()};
		binding_require(valid_identifier(value.name) && valid_identifier(value.controller) &&
		                    valid_identifier(value.channel),
		                "Controller tree", "invalid contact binding");
		result.push_back(std::move(value));
	}
	return result;
}
std::vector<std::string> controller_message_templates(const Json &tree, const Catalog &catalog)
{
	const auto names = resolve_tree_definition(tree, catalog).value("message_templates", Json::array());
	binding_require(names.is_array() && names.size() <= 128, "Controller tree", "expected bounded message_templates");
	std::vector<std::string> result;
	for (const auto &value : names)
	{
		const auto name = value.get<std::string>();
		binding_require(valid_identifier(name) && catalog.messages.contains(name) &&
		                    std::find(result.begin(), result.end(), name) == result.end(),
		                "Controller tree", "unknown or duplicate message template");
		result.push_back(name);
	}
	return result;
}
std::vector<GameplayTreeRecordBinding> controller_message_reads(const Json &tree, const Catalog &catalog)
{
	const auto resolved = resolve_tree_definition(tree, catalog);
	Json source{{"context", "controller"}, {"record_bindings", resolved.value("message_bindings", Json::array())}};
	for (const auto &binding : source.at("record_bindings"))
		binding_require(!binding.contains("scope") && !binding.contains("subject") &&
		                    !binding.contains("subject_parameter"),
		                "Controller tree", "message bindings select fields of the addressed inbox message");
	return gameplay_record_bindings(source, catalog);
}
ControllerTreeBindings compile_controller_bindings(const Json &tree, const Catalog &catalog,
                                                   SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto inherited = resolve_record_bindings(tree, catalog);
	binding_require(!inherited.context || inherited.context == state_tree::Context::Controller,
	                "trees." + tree.value("id", std::string("<inline>")) + ".context",
	                "registry context differs from authored tree");
	using namespace state_tree;
	ControllerTreeBindings result;
	auto &registry = result.registry;
	auto add = [&](FieldSpec field)
	{
		const auto index = registry.observations.size();
		registry.observations.push_back(std::move(field));
		return index;
	};
	const std::vector<FieldSpec> own{{"self_entity", Type::Entity, 4, {}},
	                                 {"self_console", Type::Console, 4, {}},
	                                 {"tick", Type::Integer, 4, {}},
	                                 {"own.x", Type::Integer, 4, {}},
	                                 {"own.y", Type::Integer, 4, {}},
	                                 {"own.vx", Type::Integer, 4, {}},
	                                 {"own.vy", Type::Integer, 4, {}},
	                                 {"own.heading", Type::Integer, 4, {}},
	                                 {"navigation.blocked", Type::Boolean, 4, {}},
	                                 {"navigation.left_heading", Type::Integer, 4, {}},
	                                 {"navigation.lateral_thrust", Type::Integer, 4, {}},
	                                 {"navigation.lateral_safe", Type::Boolean, 4, {}},
	                                 {"alert.id", Type::Text, 4, {}},
	                                 {"alert.severity", Type::Integer, 4, {}},
	                                 {"console.stance", Type::Text, 4, {}}};
	for (std::size_t i = 0; i < own.size(); ++i)
		result.own.values.at(i) = add(own[i]);
	result.own[ControllerOwnField::CraftX] = add({"craft.x", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftY] = add({"craft.y", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftAltitude] = add({"craft.altitude", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftSpeed] = add({"craft.speed", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftTarget] = add({"craft.target", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftNavigate] = add({"craft.navigate", Type::Boolean, 4, {}});
	result.own[ControllerOwnField::CraftHeading] = add({"craft.heading", Type::Integer, 4, {}});
	result.own[ControllerOwnField::CraftOrder] = add({"craft.order", Type::Text, 4, {}});
	result.own[ControllerOwnField::CraftReserveLow] = add({"craft.reserve_low", Type::Boolean, 4, {}});
	result.own[ControllerOwnField::CraftArmed] = add({"craft.armed", Type::Boolean, 4, {}});
	result.own[ControllerOwnField::CraftTargetPresent] = add({"craft.target_present", Type::Boolean, 4, {}});
	result.own[ControllerOwnField::ObjectiveTarget] = add({"objective.target", Type::Integer, 4, {}});
	result.own[ControllerOwnField::PriorityObjective] = add({"objective.priority", Type::Integer, 4, {}});
	result.own[ControllerOwnField::OwnIdentity] = add({"own.id", Type::Integer, 4, {}});
	const auto resolved = resolve_tree_definition(tree, catalog);
	if (resolved.value("complexity_observation", false))
		add({"console.stars", Type::Integer, 4, {}});
	if (resolved.value("logistics_commands", false))
		add({"logistics.revision", Type::Integer, 4, {}});
	if (resolved.value("docking_commands", false))
		add({"docking.revision", Type::Integer, 4, {}});
	if (resolved.contains("ftl_system"))
	{
		result.ftl_system = resolved.at("ftl_system").get<std::string>();
		result.own[ControllerOwnField::FtlReady] = add({"ftl.ready", Type::Boolean, 4, {}});
		result.own[ControllerOwnField::Visitors] = add({"local.visitors", Type::Integer, 4, {}});
		result.own[ControllerOwnField::LocalWorkComplete] = add({"local.work_completed", Type::Boolean, 4, {}});
	}
	binding_require(!resolved.contains("summary_bindings"), "Controller tree",
	                "use owned command channels instead of System summary intents");
	const auto commands = resolved.value("command_bindings", Json::array());
	binding_require(commands.is_array() && commands.size() <= 256, "Controller tree",
	                "expected bounded command_bindings");
	for (const auto &command : commands)
	{
		const auto name = command.at("channel").get<std::string>();
		binding_require(valid_identifier(name), "Controller tree", "invalid command channel");
		const auto type = command.value("type", std::string("integer"));
		binding_require(type == "integer" || type == "boolean", "Controller tree", "unsupported command type");
		const auto kind = type == "boolean" ? Type::Boolean : Type::Integer;
		const OperatingActionSpec *contract = nullptr;
		if (command.contains("action"))
		{
			contract = operating_action(command.at("action").get<std::string>());
			binding_require(contract != nullptr, "Controller tree", "unknown command action");
		}
		const auto intrinsic = contract && contract->semantics == CommandSemantics::Request ? "request" : "setting";
		const auto semantics = command.value("semantics", std::string(intrinsic));
		binding_require(semantics == "setting" || semantics == "request", "Controller tree",
		                "unsupported command semantics");
		binding_require(!contract || semantics == intrinsic, "Controller tree",
		                "command semantics differ from intrinsic action");
		const auto index = static_cast<Index>(registry.intents.size());
		result.commands.push_back({name, add({"command." + name, kind, 4, {}})});
		registry.intents.push_back({name, kind, 4, {}});
		registry.actions.push_back({"write." + name, 4, {kind}, semantics == "request", index, none, index, 0});
		if (contract)
		{
			for (const auto &field : operating_arguments(contract->action))
				registry.actions.back().arguments.push_back(Type::Integer);
		}
	}
	for (const auto &binding : gameplay_record_bindings(tree, catalog))
		result.records.push_back({{binding, add({binding.name, binding.type, 4, {}})}, {}});
	for (const auto &binding : controller_telemetry_bindings(tree, catalog))
		result.telemetry.push_back({binding, add({binding.name, binding.type, 4, {}})});
	for (const auto &binding : controller_command_reads(tree, catalog))
		result.command_reads.push_back({binding, add({binding.name, binding.type, 4, {}})});
	const auto property_bindings =
	    resolve_tree_definition(tree, catalog).value("ability_property_bindings", Json::array());
	for (const auto &endpoint : controller_ability_bindings(tree, catalog, properties))
	{
		ControllerAbilityBinding binding;
		binding.endpoint = endpoint;
		const auto prefix = "ability." + endpoint + ".";
		binding.fields[ControllerAbilityField::Phase] = add({prefix + "phase", Type::Text, 4, {}});
		binding.fields[ControllerAbilityField::Ready] = add({prefix + "ready", Type::Boolean, 4, {}});
		binding.fields[ControllerAbilityField::Preparation] = add({prefix + "preparation", Type::Integer, 4, {}});
		binding.fields[ControllerAbilityField::Recovery] = add({prefix + "recovery", Type::Integer, 4, {}});
		binding.fields[ControllerAbilityField::Output] = add({prefix + "output", Type::Integer, 4, {}});
		binding.fields[ControllerAbilityField::Blocked] = add({prefix + "blocked", Type::Boolean, 4, {}});
		binding.fields[ControllerAbilityField::Requested] = add({prefix + "requested", Type::Boolean, 4, {}});
		for (const auto &property_binding : property_bindings)
			if (property_binding.at("endpoint") == endpoint)
			{
				const auto property = property_binding.at("property").get<std::string>();
				const auto kind = properties->at(property).kind;
				const auto type = kind == CustomPropertyKind::Flag   ? Type::Boolean
				                  : kind == CustomPropertyKind::Enum ? Type::Text
				                                                     : Type::Integer;
				binding.properties.push_back({property, add({prefix + "property." + property, type, 4, {}}), type});
			}
		result.abilities.push_back(std::move(binding));
	}
	const auto process_properties =
	    resolve_tree_definition(tree, catalog).value("process_property_bindings", Json::array());
	for (const auto &endpoint : controller_process_bindings(tree, catalog, properties))
	{
		ControllerProcessBinding binding;
		binding.endpoint = endpoint;
		const auto prefix = "process." + endpoint + ".";
		binding.fields[ControllerProcessField::Phase] = add({prefix + "phase", Type::Text, 4, {}});
		binding.fields[ControllerProcessField::Requested] = add({prefix + "requested", Type::Boolean, 4, {}});
		binding.fields[ControllerProcessField::Running] = add({prefix + "running", Type::Boolean, 4, {}});
		binding.fields[ControllerProcessField::Paused] = add({prefix + "paused", Type::Boolean, 4, {}});
		binding.fields[ControllerProcessField::Blocked] = add({prefix + "blocked", Type::Boolean, 4, {}});
		binding.fields[ControllerProcessField::Activity] = add({prefix + "activity", Type::Integer, 4, {}});
		binding.fields[ControllerProcessField::Work] = add({prefix + "work", Type::Integer, 4, {}});
		for (const auto &property_binding : process_properties)
			if (property_binding.at("endpoint") == endpoint)
			{
				const auto property = property_binding.at("property").get<std::string>();
				const auto kind = properties->at(property).kind;
				const auto type = kind == CustomPropertyKind::Flag   ? Type::Boolean
				                  : kind == CustomPropertyKind::Enum ? Type::Text
				                                                     : Type::Integer;
				binding.properties.push_back({property, add({prefix + "property." + property, type, 4, {}}), type});
			}
		result.processes.push_back(std::move(binding));
	}
	const std::vector<FieldSpec> inbox{
	    {"message.available", Type::Boolean, 4, {}},     {"message.id", Type::Message, 4, {}},
	    {"message.source", Type::Integer, 4, {}},        {"message.source_console", Type::Console, 4, {}},
	    {"message.schema", Type::Text, 4, {}},           {"message.subject", Type::Entity, 4, {}},
	    {"message.observed_tick", Type::Integer, 4, {}}, {"message.delivered_tick", Type::Integer, 4, {}},
	    {"message.uncertainty", Type::Integer, 4, {}},   {"message.kind", Type::Integer, 4, {}},
	    {"message.urgency", Type::Integer, 4, {}},       {"message.request_key", Type::Text, 4, {}}};
	for (std::size_t i = 0; i < inbox.size(); ++i)
		result.inbox.values.at(i) = add(inbox[i]);
	for (const auto &binding : controller_message_reads(tree, catalog))
		result.messages.push_back({binding, add({binding.name, binding.type, 4, {}})});

	const auto publications = resolved.value("publication_templates", Json::array());
	binding_require(publications.is_array() && publications.size() <= 128, "Controller tree",
	                "expected bounded publication_templates");
	std::set<std::string> publication_names;
	for (const auto &entry : publications)
	{
		const auto name = entry.get<std::string>();
		binding_require(valid_identifier(name) && publication_names.insert(name).second, "Controller tree",
		                "invalid or duplicate publication template");
		registry.actions.push_back({"publish." + name, 4, {}, true});
	}
	// Preserve legacy Controller fingerprints when the new command vocabulary is unused.
	const auto team_source = resolved.dump();
	if (resolved.value("team_commands", false) || team_source.find("\"team.order\"") != std::string::npos ||
	    team_source.find("\"team.cancel\"") != std::string::npos)
	{
		registry.actions.push_back({"team.order", 4, {Type::Text, Type::Text, Type::Integer}, true});
		registry.actions.push_back({"team.cancel", 4, {Type::Text, Type::Text, Type::Integer}, true});
	}
	if (resolved.value("logistics_commands", false))
		for (const auto *name :
		     {"target", "hold", "release", "cancel", "route", "transfer", "deliver", "craft_allow", "cargo_cancel"})
			registry.actions.push_back(
			    {std::string("logistics.") + name, 4, {Type::Text, Type::Text, Type::Integer}, true});
	if (resolved.value("docking_commands", false))
		for (const auto *name : {"accept", "reject", "supply", "connect", "assist", "cancel", "undock", "emergency"})
			registry.actions.push_back({std::string("dock.") + name, 4, {Type::Text, Type::Text, Type::Integer}, true});
	for (const auto &name : {"open", "assign", "withdraw", "transport", "send", "recall", "cancel"})
		registry.actions.push_back({std::string("away.") + name, 4, {Type::Text, Type::Text, Type::Integer}, true});
	for (const auto &name : {"defend", "escort", "bomb", "recall"})
		registry.actions.push_back({std::string("craft.") + name, 4, {Type::Text, Type::Text, Type::Integer}, true});
	registry.actions.push_back({"craft.return_home", 4, {}, true});
	if (resolved.contains("ftl_system"))
	{
		binding_require(resolved.at("ftl_system").is_string() &&
		                    valid_identifier(resolved.at("ftl_system").get<std::string>()),
		                "Controller tree", "invalid FTL binding");
		registry.actions.push_back({"ftl.depart", 4, {Type::Text}, true});
	}
	registry.actions.push_back({"stop_publication", 4, {Type::Integer}, true});
	registry.actions.push_back({"withdraw_request", 4, {Type::Text}, true});
	registry.actions.push_back({"acknowledge_message", 4, {Type::Message}, true});
	for (const auto &name : controller_message_templates(tree, catalog))
	{
		ActionSpec action{"send." + name, 4, {Type::Integer, Type::Boolean, Type::Text}, true};
		action.capture_context = true;
		registry.actions.push_back(std::move(action));
	}
	for (std::size_t i = 0; i < commands.size(); ++i)
		if (commands[i].contains("action"))
			for (const auto &field :
			     operating_arguments(operating_action(commands[i].at("action").get<std::string>())->action))
				result.commands[i].arguments.push_back(add(
				    {"command." + result.commands[i].channel + "." + std::string(field.name), Type::Integer, 4, {}}));
	for (auto &binding : result.records)
	{
		std::size_t i = 0;
		for (const auto *field : {"available", "observed_tick", "received_tick", "source", "uncertainty", "kind",
		                          "withdrawn", "stale", "revision", "subject"})
			binding.metadata.values.at(i++) =
			    add({binding.source.name + "." + field,
			         std::string_view(field) == "available" || std::string_view(field) == "withdrawn" ||
			                 std::string_view(field) == "stale"
			             ? Type::Boolean
			             : Type::Integer,
			         4,
			         {}});
	}
	for (auto &command : result.commands)
	{
		std::size_t i = 0;
		for (const auto *field : {"accepted", "revision", "consumed", "request_id", "pending"})
			command.metadata.values.at(i++) =
			    add({"command." + command.channel + "." + field,
			         std::string_view(field) == "accepted" || std::string_view(field) == "pending" ? Type::Boolean
			                                                                                       : Type::Integer,
			         4,
			         {}});
	}
	for (const auto &binding : controller_contact_bindings(tree, catalog))
	{
		ControllerContactRead read{binding};
		std::size_t i = 0;
		for (const auto *field : {"available", "id", "x", "y", "vx", "vy", "heading", "speed", "observed_tick",
		                          "uncertainty", "bearing_available", "bearing_mdeg"})
			read.fields.values.at(i++) =
			    add({binding.name + "." + field,
			         (std::string_view(field) == "available" || std::string_view(field) == "bearing_available")
			             ? Type::Boolean
			             : Type::Integer,
			         4,
			         {}});
		result.contacts.push_back(std::move(read));
	}
	std::size_t result_index = 0;
	for (const auto &field : std::vector<FieldSpec>{{"result.available", Type::Boolean, 4, {}},
	                                                {"result.success", Type::Boolean, 4, {}},
	                                                {"result.id", Type::Integer, 4, {}},
	                                                {"result.publication", Type::Integer, 4, {}},
	                                                {"result.message", Type::Message, 4, {}},
	                                                {"result.reason", Type::Text, 4, {}}})
		result.result.values.at(result_index++) = add(field);
	const auto add_inputs = [&](const Json &action)
	{
		for (const auto &input : action.value("inputs", Json::array()))
		{
			const auto name = input.at("name").get<std::string>();
			binding_require(valid_identifier(name), "Controller action", "invalid input name");
			const auto type = input.at("type").get<std::string>();
			binding_require(type == "integer" || type == "boolean" || type == "text", "Controller action",
			                "invalid input type");
			const auto kind = type == "integer" ? Type::Integer : type == "boolean" ? Type::Boolean : Type::Text;
			const auto found = std::find_if(registry.observations.begin(), registry.observations.end(),
			                                [&](const auto &f)
			                                {
				                                return f.name == "input." + name;
			                                });
			if (found == registry.observations.end())
				add({"input." + name, kind, 4, {}});
			else
				binding_require(found->type == kind, "Controller action", "conflicting input types");
		}
	};
	if (resolved.contains("controller_action"))
		add_inputs(resolved.at("controller_action"));
	for (const auto &binding : resolved.value("action_bindings", Json::array()))
	{
		const auto name = binding.at("id").get<std::string>(), target = binding.at("tree").get<std::string>();
		binding_require(valid_identifier(name) && catalog.trees.contains(target), "Controller action",
		                "invalid action binding");
		const auto definition = resolve_tree_definition(catalog.trees.at(target), catalog);
		binding_require(definition.contains("controller_action") && !definition.contains("action_bindings"),
		                "Controller action", "execution tree required; recursive selection forbidden");
		const auto &metadata = definition.at("controller_action");
		add_inputs(metadata);
		ActionSpec select{"select." + name, 4, {Type::Text}, false, 100000};
		for (const auto &input : metadata.value("inputs", Json::array()))
		{
			const auto type = input.at("type").get<std::string>();
			select.arguments.push_back(type == "integer"   ? Type::Integer
			                           : type == "boolean" ? Type::Boolean
			                                               : Type::Text);
		}
		registry.actions.push_back(select);
		select.name = "replace." + name;
		registry.actions.push_back(std::move(select));
	}
	registry.actions.push_back({"action.cancel", 4, {Type::Text}, false, 100000});
	registry.actions.push_back({"action.succeed", 4, {Type::Text}, true});
	registry.actions.push_back({"action.fail", 4, {Type::Text}, true});
	result.action_base = registry.observations.size();
	add({"action.status", Type::Text, 4, {}});
	add({"action.reason", Type::Text, 4, {}});
	add({"action.id", Type::Text, 4, {}});
	add({"action.goal_distance", Type::Integer, 4, {}});
	add({"action.actual_speed", Type::Integer, 4, {}});
	add({"action.contact_available", Type::Boolean, 4, {}});
	result.decision_base = registry.observations.size();
	for (const auto *name : {"tactical.target", "tactical.range", "tactical.bearing", "tactical.magazine",
	                         "repair.compartment", "repair.system", "shield.fraction", "shield.lowest",
	                         "shield.difference", "drive.cruise_speed", "drive.approach_speed", "support.power",
	                         "support.cooling", "tactical.ship_target", "tactical.intercept_target",
	                         "tactical.pursuit_heading", "tactical.pursuit_speed", "tactical.pursuit_altitude"})
		add({name, Type::Integer, 4, {}});
	add({"tactical.point_defence", Type::Boolean, 4, {}});
	result.coordination_base = registry.observations.size();
	add({"coordination.pending", Type::Boolean, 4, {}});
	for (const auto *name : {"operation", "status", "reason", "invocation"})
		add({std::string("coordination.") + name, Type::Text, 4, {}});
	for (const auto *name : {"request", "target", "amount", "priority", "expires"})
		add({std::string("coordination.") + name, Type::Integer, 4, {}});
	for (const auto *name : {"feasible", "done", "blocked"})
		add({std::string("coordination.") + name, Type::Boolean, 4, {}});
	for (const auto *name : {"bearing", "range", "limit"})
		add({std::string("coordination.") + name, Type::Integer, 4, {}});
	for (const auto *op : {"power", "cooling", "manoeuvre", "repair"})
	{
		add({std::string("coordination.reply.") + op + ".status", Type::Text, 4, {}});
		add({std::string("coordination.reply.") + op + ".amount", Type::Integer, 4, {}});
	}
	registry.actions.push_back(
	    {"coordination.request",
	     4,
	     {Type::Text, Type::Text, Type::Text, Type::Integer, Type::Integer, Type::Integer, Type::Integer},
	     true});
	registry.actions.push_back({"coordination.respond", 4, {Type::Text, Type::Integer, Type::Text}, true});
	registry.actions.push_back({"coordination.withdraw", 4, {Type::Text}, true});
	registry.actions.push_back({"coordination.withdraw_from", 4, {Type::Text, Type::Text, Type::Text}, true});
	registry.actions.push_back({"coordination.maintain",
	                            4,
	                            {Type::Text, Type::Text, Type::Text, Type::Integer, Type::Integer, Type::Integer,
	                             Type::Integer, Type::Integer},
	                            false});
	return result;
}
state_tree::Registry gameplay_registry(const Json &tree, const Catalog &catalog, state_tree::Context context)
{
	if (context == state_tree::Context::Controller)
		return compile_controller_bindings(tree, catalog).registry;
	const auto inherited = resolve_record_bindings(tree, catalog);
	binding_require(!inherited.context || inherited.context == context,
	                "trees." + tree.value("id", std::string("<inline>")) + ".context",
	                "registry context differs from authored tree");
	auto registry = gameplay_tree_registry(context, gameplay_record_bindings(tree, catalog));
	for (const auto &binding : gameplay_summary_bindings(tree, catalog))
		registry.observations.push_back(
		    {binding.name, state_tree::Type::Integer, static_cast<std::uint8_t>(state_tree::Context::System), {}});
	return registry;
}
} // namespace phoenix
