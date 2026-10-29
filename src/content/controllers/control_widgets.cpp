module;
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.content.controllers;

namespace phoenix
{
namespace
{
void widget_require(bool valid, const std::string &where, const std::string &reason)
{
	if (!valid)
		throw std::runtime_error("control_widgets." + where + ": " + reason);
}
std::string widget_name(const Json &value, const std::string &where)
{
	widget_require(value.is_string(), where, "expected identifier");
	const auto name = value.get<std::string>();
	widget_require(!name.empty() && name.size() <= 128 &&
	                   name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
	                       std::string::npos,
	               where, "invalid identifier");
	return name;
}
WidgetDataDestination destination(const Json &value, const std::string &where)
{
	widget_require(value.is_string(), where, "expected main, details or hidden");
	const auto name = value.get<std::string>();
	widget_require(name == "main" || name == "details" || name == "hidden", where, "expected main, details or hidden");
	return name == "main"      ? WidgetDataDestination::Main
	       : name == "details" ? WidgetDataDestination::Details
	                           : WidgetDataDestination::Hidden;
}
} // namespace

Json bind_widget_roles(const Json &source, const Json &bindings, const std::string &where)
{
	if (source.is_string())
	{
		const auto text = source.get<std::string>();
		if (text.starts_with("$"))
		{
			widget_require(bindings.contains(text.substr(1)), where, "unbound equipment role " + text);
			return bindings.at(text.substr(1));
		}
		return source;
	}
	if (!source.is_structured())
		return source;
	Json result = source.is_array() ? Json::array() : Json::object();
	for (auto it = source.begin(); it != source.end(); ++it)
	{
		auto value = bind_widget_roles(it.value(), bindings, where);
		if (source.is_array())
		{
			if (it.value().is_string() && it.value().get<std::string>().starts_with("$") && value.is_array())
				for (const auto &element : value)
					result.push_back(element);
			else
				result.push_back(std::move(value));
		}
		else
			result[it.key()] = std::move(value);
	}
	return result;
}

std::map<std::string, WidgetDataDestination> control_widget_data_placement(const ControlWidgetDefinition &definition,
                                                                           const std::string &level, int stars,
                                                                           const Json &overrides)
{
	const auto selected = std::find_if(definition.levels.begin(), definition.levels.end(),
	                                   [&](const auto &candidate)
	                                   {
		                                   return candidate.id == level;
	                                   });
	widget_require(selected != definition.levels.end(), definition.id, "unknown responsibility level " + level);
	widget_require(stars >= 1 && stars <= 3, definition.id, "expected stars 1..3");
	widget_require(overrides.is_object(), definition.id, "data placement overrides must be an object");
	for (const auto &[star, items] : overrides.items())
	{
		widget_require((star == "1" || star == "2" || star == "3") && items.is_object(), definition.id,
		               "expected per-star data placement objects");
		for (const auto &[item, value] : items.items())
		{
			widget_require(definition.data.contains(item), definition.id + "." + item, "unknown data item");
			const auto target = destination(value, definition.id + "." + item);
			widget_require(!definition.data.at(item).value("required", false) || target == WidgetDataDestination::Main,
			               definition.id + "." + item, "required state indicator must remain on main presentation");
		}
	}
	std::map<std::string, WidgetDataDestination> result;
	for (const auto &[item, specification] : definition.data.items())
	{
		auto value = specification.value("placement", Json("details"));
		if (selected->data.contains(item))
			value = selected->data.at(item);
		if (overrides.contains(std::to_string(stars)) && overrides.at(std::to_string(stars)).contains(item))
			value = overrides.at(std::to_string(stars)).at(item);
		const auto target = destination(value, definition.id + "." + item);
		widget_require(!specification.value("required", false) || target == WidgetDataDestination::Main,
		               definition.id + "." + item, "required state indicator must remain on main presentation");
		result[item] = target;
	}
	return result;
}

ControlWidgetDefinition compile_control_widget(const Catalog &catalog, const std::string &id)
{
	widget_require(catalog.control_widgets.contains(id), id, "unresolved widget definition");
	const auto &source = catalog.control_widgets.at(id);
	widget_require(!source.contains("policy") || (source.at("policy").is_object() && !source.contains("tree") &&
	                                              !source.contains("repeat") && !source.contains("member_policy")),
	               id, "policy must be an object instead of tree or repeat");
	widget_require(
	    !source.contains("member_policy") ||
	        (source.contains("repeat") && source.at("member_policy").is_object() && !source.contains("tree")),
	    id, "member_policy requires repeat instead of tree");
	widget_require(!source.contains("repeat") || (source.at("repeat").is_string() && source.contains("member_policy")),
	               id, "repeat requires a role and member_policy");

	widget_require(source.value("schema", 0) == 1, id, "expected widget schema 1");
	ControlWidgetDefinition result;
	result.id = id;
	result.tree = source.value("tree", std::string{});
	widget_require(result.tree.empty() || catalog.trees.contains(result.tree), id, "unresolved state tree");
	result.commands = source.value("commands", Json::array());
	result.equipment = source.value("equipment", Json::object());
	result.data = source.value("data", Json::object());
	result.operations = source.value("operations", Json::object());
	widget_require(result.commands.is_array() && result.commands.size() <= 256 && result.equipment.is_object() &&
	                   result.equipment.size() <= 64 && result.data.is_object() && result.data.size() <= 256,
	               id, "invalid or oversized command, equipment or data declarations");
	std::set<std::string> commands;
	for (const auto &command : result.commands)
	{
		const auto name = widget_name(command.at("id"), id + ".commands");
		widget_require(commands.insert(name).second, id, "duplicate command " + name);
		widget_require(!command.contains("individual_stars"), id,
		               "shared widget command responsibility belongs in levels, not individual_stars");
		widget_require(operating_action(command.at("action").get<std::string>()) != nullptr, id,
		               "unsupported command action");
		const auto consumers = command.value("consumers", Json::array());
		widget_require(consumers.is_array(), id, "command consumers must name equipment roles");
		for (const auto &consumer : consumers)
		{
			widget_require(consumer.is_string(), id, "command consumer must name an equipment role");
			const auto role = consumer.get<std::string>();
			widget_require(role.starts_with("$") && result.equipment.contains(role.substr(1)), id,
			               "shared command consumers must use declared equipment roles");
		}
	}
	for (const auto &[role, specification] : result.equipment.items())
	{
		(void)widget_name(Json(role), id + ".equipment");
		widget_require(specification.is_object(), id + ".equipment." + role, "expected equipment role object");
	}
	widget_require(result.operations.is_object() && result.operations.size() <= 64, id, "invalid native operations");
	std::set<std::string> responsibilities = commands;
	for (const auto &[operation, roles] : result.operations.items())
	{
		(void)widget_name(Json(operation), id + ".operations");
		static const std::set<std::string> supported_operations{
		    "team_assignment", "team_work", "medical",        "personnel",        "craft",
		    "craft_return",    "transport", "team_selection", "logistics",        "cargo_authorisation",
		    "conversation",    "mission",   "mission_send",   "mission_quantity", "mission_staffing",
		    "contribution",    "departure", "docking",        "coordination",     "alert",
		    "stance",          "objectives"};
		widget_require(supported_operations.contains(operation), id, "unsupported native operation " + operation);

		widget_require(responsibilities.insert(operation).second && roles.is_array(), id,
		               "operation must have a distinct name and equipment role array");
		std::set<std::string> unique;
		for (const auto &role : roles)
		{
			widget_require(role.is_string(), id, "operation equipment must name roles");
			const auto name = role.get<std::string>();
			widget_require(name.starts_with("$") && result.equipment.contains(name.substr(1)) &&
			                   unique.insert(name).second,
			               id, "operation equipment must use distinct declared roles");
		}
	}
	for (const auto &[item, specification] : result.data.items())
	{
		(void)widget_name(Json(item), id + ".data");
		widget_require(specification.is_object() && specification.contains("binding") &&
		                   specification.at("binding").is_string() &&
		                   !specification.at("binding").get<std::string>().empty() &&
		                   specification.at("binding").get<std::string>().size() <= 256 &&
		                   specification.contains("label") && specification.at("label").is_string(),
		               id + ".data." + item, "data item needs a bounded binding and localisable label");
		const auto binding = specification.at("binding").get<std::string>();
		bool resolved = binding.starts_with("command.") && commands.contains(binding.substr(8));
		if (binding.starts_with("board:/"))
		{
			try
			{
				(void)Json::json_pointer(binding.substr(6));
				resolved = true;
			}
			catch (const Json::exception &)
			{
				widget_require(false, id + ".data." + item, "invalid permitted-board JSON pointer");
			}
		}
		for (const auto &[role, equipment] : result.equipment.items())
			for (const auto *source_kind : {"system", "weapon", "team", "ftl", "shield"})
			{
				const auto prefix = std::string(source_kind) + "." + role + ".";
				if (binding.starts_with(prefix))
				{
					(void)widget_name(Json(binding.substr(prefix.size())), id + ".data." + item);
					resolved = true;
				}
			}
		widget_require(resolved, id + ".data." + item, "unresolved command or equipment data binding " + binding);
	}
	const auto levels = source.at("levels");
	widget_require(levels.is_array() && !levels.empty() && levels.size() <= 32, id, "expected 1..32 ordered levels");
	std::set<std::string> level_ids, previous;
	for (const auto &level : levels)
	{
		ControlWidgetLevel compiled;
		compiled.id = widget_name(level.at("id"), id + ".levels");
		widget_require(level_ids.insert(compiled.id).second, id, "duplicate level");
		const auto human = level.value("human", Json::array());
		widget_require(human.is_array(), id, "human command set must be an array");
		for (const auto &channel : human)
		{
			const auto name = widget_name(channel, id + ".levels." + compiled.id);
			widget_require(responsibilities.contains(name) && compiled.human.insert(name).second, id,
			               "unknown or duplicate human command " + name);
		}
		const auto replacements = level.value("replaces", Json::object());
		widget_require(replacements.is_object(), id, "replaces must map old commands to detailed commands");
		for (const auto &[old, targets] : replacements.items())
		{
			widget_require(previous.contains(old) && !compiled.human.contains(old) && targets.is_array() &&
			                   !targets.empty(),
			               id, "replacement must retire a previous human command");
			std::set<std::string> unique;
			for (const auto &target : targets)
			{
				const auto name = widget_name(target, id + ".replaces");
				widget_require(compiled.human.contains(name) && unique.insert(name).second, id,
				               "replacement must name distinct human commands at this level");
				compiled.replacements[old].push_back(name);
			}
		}
		for (const auto &old : previous)
			widget_require(compiled.human.contains(old) || compiled.replacements.contains(old), id,
			               "responsibility removed without explicit replacement: " + old);
		compiled.data = level.value("data", Json::object());
		widget_require(compiled.data.is_object(), id, "level data placement must be an object");
		for (const auto &[item, value] : compiled.data.items())
		{
			widget_require(result.data.contains(item), id, "unknown data item " + item);
			(void)destination(value, id + "." + item);
		}
		previous = compiled.human;
		result.levels.push_back(std::move(compiled));
	}
	widget_require(result.levels.front().id == "automated" && result.levels.front().human.empty(), id,
	               "first level must be automated with no human commands");
	const auto parts = source.value("parts", Json::array({"main"}));
	widget_require(parts.is_array() && !parts.empty() && parts.size() <= 64, id, "expected 1..64 named parts");
	for (const auto &part : parts)
		widget_require(result.parts.insert(widget_name(part, id + ".parts")).second, id, "duplicate part");
	const auto bindings = source.value("part_bindings", Json::object());
	widget_require(bindings.is_object(), id, "part_bindings must be an object");
	unsigned detail_entries = 0;
	for (const auto &[part, binding] : bindings.items())
	{
		widget_require(result.parts.contains(part) && binding.is_object(), id,
		               "unknown or invalid part binding " + part);
		if (binding.contains("renderer"))
		{
			const auto renderer = widget_name(binding.at("renderer"), id + "." + part);
			widget_require(renderer == "personnel" || renderer == "away" || renderer == "craft" ||
			                   renderer == "operations" || renderer == "docking" || renderer == "teams" ||
			                   renderer == "objectives",
			               id, "unsupported native widget renderer " + renderer);
			const auto operation = renderer == "operations" ? "logistics" : renderer == "away" ? "mission" : renderer;
			widget_require(
			    result.operations.contains(operation) ||
			        (renderer == "away" &&
			         (result.operations.contains("transport") || result.operations.contains("mission_send") ||
			          result.operations.contains("mission_quantity") ||
			          result.operations.contains("mission_staffing") || result.operations.contains("contribution") ||
			          result.operations.contains("team_selection"))) ||
			        (renderer == "teams" &&
			         (result.operations.contains("team_assignment") || result.operations.contains("team_work") ||
			          result.operations.contains("medical") || result.operations.contains("team_selection"))) ||
			        (renderer == "operations" && result.operations.contains("cargo_authorisation")),
			    id, "native renderer needs its declared operation scope");
		}
		for (const auto *kind : {"commands", "data", "operations", "ranges"})
		{
			const auto names = binding.value(kind, Json::array());
			widget_require(names.is_array(), id + "." + part, "part bindings require identifier arrays");
			std::set<std::string> unique;
			for (const auto &name : names)
			{
				const auto key = widget_name(name, id + "." + part);
				widget_require(((std::string(kind) == "commands" || std::string(kind) == "ranges")
				                    ? commands.contains(key)
				                : std::string(kind) == "operations" ? result.operations.contains(key)
				                                                    : result.data.contains(key)) &&
				                   unique.insert(key).second,
				               id, "unknown or duplicate part member " + key);
			}
		}
		for (const auto &channel : binding.value("ranges", Json::array()))
		{
			const auto members = binding.value("commands", Json::array());
			widget_require(std::find(members.begin(), members.end(), channel) != members.end(), id,
			               "range must bind a command displayed by the same part");
			const auto command = std::find_if(result.commands.begin(), result.commands.end(),
			                                  [&](const auto &field)
			                                  {
				                                  return field.at("id") == channel;
			                                  });
			const auto *action = operating_action(command->at("action").template get<std::string>());
			widget_require(action && !action->boolean && action->semantics == CommandSemantics::Setting &&
			                   operating_arguments(action->action).empty() &&
			                   command->value("minimum", action->minimum) < command->value("maximum", action->maximum),
			               id, "ranges require numeric settings without command arguments");
			widget_require(!binding.value("values", Json::object()).contains(channel.get<std::string>()), id,
			               "a command cannot combine a range with a fixed value filter");
		}
		const auto values = binding.value("values", Json::object());
		widget_require(values.is_object(), id, "part values must map command names to value arrays");
		for (const auto &[command, options] : values.items())
		{
			const auto members = binding.value("commands", Json::array());
			widget_require(std::find(members.begin(), members.end(), Json(command)) != members.end(), id,
			               "part values must filter a command displayed by the same part");
			widget_require(commands.contains(command) && options.is_array() && !options.empty() && options.size() <= 64,
			               id, "invalid command values in part " + part);
			for (const auto &option : options)
				widget_require(option.is_number_integer() || option.is_boolean(), id,
				               "part values must be scalar settings");
		}
		widget_require(!binding.contains("details") || binding.at("details").is_boolean(), id,
		               "part details flag must be boolean");
		detail_entries += binding.value("details", false);
	}
	widget_require(detail_entries <= 1, id, "named parts may declare one shared detail entry point");
	for (const auto &level : result.levels)
		(void)control_widget_data_placement(result, level.id, 1);
	return result;
}

Json bind_control_widget(const ControlWidgetDefinition &definition, const Json &instance)
{
	const auto id = widget_name(instance.at("widget"), "instance.widget");
	widget_require(id == definition.id, id, "instance refers to a different widget definition");
	const auto bindings = instance.value("equipment", Json::object());
	widget_require(bindings.is_object() && bindings.size() == definition.equipment.size(), id,
	               "instance must bind every declared equipment role exactly once");
	for (const auto &[role, specification] : definition.equipment.items())
	{
		widget_require(bindings.contains(role), id, "unbound equipment role " + role);
		const auto &binding = bindings.at(role);
		if (specification.value("multiple", false))
		{
			widget_require(binding.is_array() && !binding.empty() && binding.size() <= 256, id,
			               "equipment set must contain 1..256 identities");
			std::set<std::string> unique;
			for (const auto &name : binding)
				widget_require(unique.insert(widget_name(name, id + "." + role)).second, id,
				               "duplicate attached equipment");
		}
		else
			(void)widget_name(binding, id + "." + role);
	}
	Json result = instance;
	widget_require(!instance.contains("commands") && !instance.contains("tree") && !instance.contains("fallback") &&
	                   !instance.contains("policy") && !instance.contains("ai") && !instance.contains("operations"),
	               id, "instance cannot override shared commands or tree; derive a widget definition");
	result["commands"] = bind_widget_roles(definition.commands, bindings, id);
	result["operations"] = bind_widget_roles(definition.operations, bindings, id);
	if (definition.tree.empty())
		result["fallback"] = "hold";
	else
		result["tree"] = definition.tree;
	return result;
}
} // namespace phoenix
