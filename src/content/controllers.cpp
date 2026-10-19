module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.content;

namespace phoenix
{
namespace
{
void control_require(bool condition, const std::string &path, const std::string &reason)
{
	if (!condition)
		throw std::runtime_error(path + ": " + reason);
}
std::string control_name(const Json &value, const char *field, const std::string &path)
{
	control_require(value.contains(field) && value.at(field).is_string(), path, std::string("missing ") + field);
	auto name = value.at(field).get<std::string>();
	control_require(!name.empty() && name.size() <= 128 &&
	                    name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
	                        std::string::npos,
	                path, std::string("invalid ") + field);
	return name;
}
bool accepts(const FittedSystemSpec &fit, const Json &spec, OperatingAction action)
{
	const auto kind = fit.kind;
	const bool flight = kind == OperatingSystem::Engine || kind == OperatingSystem::Steering ||
	                    kind == OperatingSystem::Lateral || kind == OperatingSystem::Boost ||
	                    kind == OperatingSystem::Impulse;
	switch (action)
	{
	case OperatingAction::SystemEnabled:
		return true;
	case OperatingAction::EngineEnabled:
		return flight;
	case OperatingAction::RadarEnabled:
		return kind == OperatingSystem::Radar;
	case OperatingAction::Speed:
	case OperatingAction::Longitudinal:
	case OperatingAction::MatchSpeed:
		return kind == OperatingSystem::Engine;
	case OperatingAction::Heading:
	case OperatingAction::ManualSteering:
		return flight && spec.value("turn_mdeg_s", std::int64_t{0}) > 0;
	case OperatingAction::Assisted:
	case OperatingAction::SelectContact:
		return flight;
	case OperatingAction::Lateral:
		return flight &&
		       (spec.value("lateral_acceleration_mm_s2", std::int64_t{0}) > 0 || kind == OperatingSystem::Lateral);
	case OperatingAction::Boost:
		return flight && (spec.contains("boost") || kind == OperatingSystem::Boost);
	case OperatingAction::Impulse:
		return flight && (spec.contains("impulse") || kind == OperatingSystem::Impulse);
	case OperatingAction::GeneratorOutput:
		return spec.contains("generator");
	case OperatingAction::ChargeLimit:
	case OperatingAction::DischargeLimit:
	case OperatingAction::ThermalOverride:
	case OperatingAction::Overdrive:
		return spec.contains("power_store");
	case OperatingAction::GroupAllocation:
		return spec.contains("power_group");
	case OperatingAction::LinkIsolation:
		return spec.contains("conduit");
	case OperatingAction::CoolingAllocation:
		return spec.contains("cooling_link");
	}
	return false;
}
using CommandBounds = std::map<OperatingAction, std::pair<std::int64_t, std::int64_t>>;
CommandBounds fitted_commands(const FittedSystemSpec &fit, Json spec, const ShipResourcesDefinition *resources,
                              std::size_t index)
{
	CommandBounds result;
	const auto add = [&](OperatingAction action)
	{
		const auto &contract = operating_action(action);
		result[action] = {contract.minimum, contract.maximum};
	};
	const auto kind = fit.kind;
	if (kind == OperatingSystem::Engine || kind == OperatingSystem::Steering || kind == OperatingSystem::Lateral ||
	    kind == OperatingSystem::Boost || kind == OperatingSystem::Impulse)
	{
		spec["kind"] = std::string(operating_system_name(kind));
		const auto flight = compile_flight(spec, "System " + fit.id);
		for (auto action : {OperatingAction::SystemEnabled, OperatingAction::EngineEnabled, OperatingAction::Assisted,
		                    OperatingAction::SelectContact})
			add(action);
		if (kind == OperatingSystem::Engine)
		{
			add(OperatingAction::Longitudinal);
			add(OperatingAction::MatchSpeed);
			result[OperatingAction::Speed] = {-flight.reverse_speed, flight.maximum_speed};
		}
		if (kind == OperatingSystem::Steering || flight.turn_rate)
			add(OperatingAction::Heading);
		if (flight.turn_rate)
			add(OperatingAction::ManualSteering);
		if (flight.lateral_acceleration)
			add(OperatingAction::Lateral);
		if (flight.boost_available)
			add(OperatingAction::Boost);
		if (flight.impulse_available)
			add(OperatingAction::Impulse);
	}
	if (kind == OperatingSystem::Radar)
	{
		add(OperatingAction::SystemEnabled);
		add(OperatingAction::RadarEnabled);
	}
	if (kind == OperatingSystem::Coolant)
		add(OperatingAction::SystemEnabled);
	if (!resources)
		return result;
	const auto &binding = resources->systems.at(index);
	const auto &network = resources->network;
	if (kind == OperatingSystem::Generator && binding.generator != no_resource)
	{
		add(OperatingAction::SystemEnabled);
		result[OperatingAction::GeneratorOutput] = {0, network.generators[binding.generator].maximum_rate};
	}
	if (kind == OperatingSystem::Storage && binding.store != no_resource)
	{
		add(OperatingAction::SystemEnabled);
		result[OperatingAction::ChargeLimit] = {0, network.stores[binding.store].charge_rate};
		result[OperatingAction::DischargeLimit] = {0, network.stores[binding.store].discharge_rate};
	}
	if (kind == OperatingSystem::Distribution)
	{
		if (binding.group != no_resource)
			add(OperatingAction::GroupAllocation);
		if (binding.conduit != no_resource)
			add(OperatingAction::LinkIsolation);
	}
	if (kind == OperatingSystem::Coolant && binding.cooling != no_resource)
	{
		const auto &link = network.cooling_links[binding.cooling];
		result[OperatingAction::CoolingAllocation] = {
		    0, std::min(link.capacity_rate, network.coolant_sources[link.source].removal_rate)};
	}
	if (binding.store != no_resource && binding.store_modes)
	{
		add(OperatingAction::Overdrive);
		if (network.stores[binding.store].allows_override)
			add(OperatingAction::ThermalOverride);
	}
	return result;
}
OperatingAction input_key(OperatingAction action)
{
	if (action == OperatingAction::EngineEnabled || action == OperatingAction::RadarEnabled)
		return OperatingAction::SystemEnabled;
	return action;
}
ControllerCommandSpec command_spec(const FittedSystemSpec &fit, const OperatingActionSpec &action, std::size_t index)
{
	ControllerCommandSpec result;
	result.id = action.name;
	result.action = action.action;
	result.physical_input = input_key(action.action);
	result.type = action.boolean ? KnowledgeType::Boolean : KnowledgeType::Integer;
	result.minimum = action.minimum;
	result.maximum = action.maximum;
	result.semantics =
	    action.action == OperatingAction::Impulse ? CommandSemantics::Request : CommandSemantics::Setting;
	if (action.action == OperatingAction::Speed)
		result.maximum = fit.maximum;
	result.consumers.push_back(index);
	return result;
}
} // namespace

ControllableLayout compile_controllable_layout(const Catalog &catalog, const std::string &entity_id)
{
	const auto &entity = catalog.entities.at(entity_id);
	const auto path = "entities." + entity_id + ".control_layout";
	const auto systems = resolve_systems(catalog, entity_id);
	const auto consoles = resolve_consoles(catalog, entity_id);
	ControllableLayout result;
	std::map<std::string, std::size_t> system_ids;
	std::set<std::string> console_ids;
	for (std::size_t i = 0; i < systems.size(); ++i)
		system_ids.emplace(systems[i].id, i);
	for (const auto &console : consoles)
		console_ids.insert(console.id);
	if (!entity.contains("control_layout"))
	{
		result.converted = true;
		for (const auto &console : consoles)
			for (const auto &name : console.controlled_systems)
			{
				const auto i = system_ids.at(name);
				const auto &fit = systems[i];
				ConsoleControllerSpec controller{fit.id, console.id, fit.controller};
				if (controller.tree.empty() && entity.contains("controller"))
					controller.tree = entity.at("controller").get<std::string>();
				for (const auto &action : operating_actions)
					if (accepts(fit, entity.at("systems").at(fit.id), action.action) &&
					    action.action != OperatingAction::SystemEnabled)
						controller.commands.push_back(command_spec(fit, action, i));
				if (std::none_of(controller.commands.begin(), controller.commands.end(),
				                 [](const auto &c)
				                 {
					                 return input_key(c.action) == OperatingAction::SystemEnabled;
				                 }))
					controller.commands.push_back(
					    command_spec(fit, operating_action(OperatingAction::SystemEnabled), i));
				result.controllers.push_back(std::move(controller));
			}
		if (entity.contains("controller") && !result.controllers.empty())
		{
			// A legacy whole-entity program executes once; never copy it onto every actuator.
			ConsoleControllerSpec combined{"legacy", result.controllers.front().console,
			                               entity.at("controller").get<std::string>()};
			for (const auto &part : result.controllers)
				for (auto command : part.commands)
				{
					command.id = part.id + "." + command.id;
					combined.commands.push_back(std::move(command));
				}
			const auto program = compile_tree(catalog.trees.at(combined.tree));
			for (const auto &output : program.outputs)
				control_require(std::count_if(combined.commands.begin(), combined.commands.end(),
				                              [&](const auto &c)
				                              {
					                              return c.action == output.action;
				                              }) == 1,
				                path, "ambiguous legacy entity policy output");
			result.controllers = {std::move(combined)};
		}
		for (const auto &preset : entity.value("presets", Json::array()))
		{
			ConsoleComplexitySpec profile{preset.at("id").get<std::string>(),
			                              std::vector<bool>(result.controllers.size(), true)};
			for (auto row = preset.at("consoles").begin(); row != preset.at("consoles").end(); ++row)
				for (const auto &delegated : row.value().value("delegated", Json::array()))
					for (std::size_t i = 0; i < result.controllers.size(); ++i)
						if (result.controllers[i].console == row.key() &&
						    result.controllers[i].id == delegated.get<std::string>())
							profile.human[i] = false;
			if (profile.id == entity.value("default_preset", std::string{}))
				result.default_profile = result.profiles.size();
			result.profiles.push_back(std::move(profile));
		}
		if (result.profiles.empty())
			result.profiles.push_back({"direct", std::vector<bool>(result.controllers.size(), true)});
		return result;
	}
	const auto resources = compile_ship_resources(catalog, entity_id);
	std::vector<CommandBounds> capabilities;
	for (std::size_t i = 0; i < systems.size(); ++i)
		capabilities.push_back(
		    fitted_commands(systems[i], entity.at("systems").at(systems[i].id), resources ? &*resources : nullptr, i));
	const auto &layout = entity.at("control_layout");
	control_require(layout.is_object() && layout.value("schema", 0) == 1, path,
	                "unsupported layout schema (expected 1)");
	control_require(!entity.contains("controller") && !entity.contains("controllers") && !entity.contains("presets"),
	                path, "explicit layout cannot mix legacy command AI or presets");
	control_require(layout.contains("controllers") && layout.at("controllers").is_array() &&
	                    layout.at("controllers").size() <= 256,
	                path, "expected bounded controllers array");
	std::map<std::pair<std::string, std::string>, std::size_t> controller_ids;
	std::set<std::pair<std::size_t, OperatingAction>> inputs;
	for (const auto &row : layout.at("controllers"))
	{
		ConsoleControllerSpec controller;
		controller.id = control_name(row, "id", path);
		controller.console = control_name(row, "console", path);
		const auto where = path + "." + controller.console + "." + controller.id;
		control_require(console_ids.contains(controller.console), where, "unknown Console");
		control_require(
		    controller_ids.emplace(std::pair{controller.console, controller.id}, result.controllers.size()).second,
		    where, "duplicate Controller identity");
		if (row.contains("tree"))
		{
			controller.tree = control_name(row, "tree", where);
			control_require(catalog.trees.contains(controller.tree), where, "unknown Controller tree");
			if (catalog.tree_programs.contains(controller.tree))
				control_require(catalog.tree_programs.at(controller.tree).context == state_tree::Context::Controller,
				                where, "Controller requires a controller-context tree");
		}
		else
			control_require(row.value("fallback", std::string{}) == "hold", where,
			                "Controller requires a tree or explicit hold fallback");
		control_require(row.contains("commands") && row.at("commands").is_array() && row.at("commands").size() <= 256,
		                where, "expected bounded commands array");
		std::set<std::string> command_ids;
		for (const auto &field : row.at("commands"))
		{
			const auto id = control_name(field, "id", where);
			const auto action_name = control_name(field, "action", where);
			const auto *action = operating_action(action_name);
			control_require(action != nullptr, where, "unsupported command action");
			control_require(command_ids.insert(id).second, where, "duplicate command channel");
			ControllerCommandSpec command;
			command.id = id;
			command.action = action->action;
			command.physical_input = input_key(action->action);
			command.type = action->boolean ? KnowledgeType::Boolean : KnowledgeType::Integer;
			command.semantics =
			    command.action == OperatingAction::Impulse ? CommandSemantics::Request : CommandSemantics::Setting;
			auto number = [&](const char *key, std::int64_t fallback)
			{
				if (!field.contains(key))
					return fallback;
				const auto &v = field.at(key);
				control_require(v.is_number_integer() && v >= action->minimum && v <= action->maximum, where + "." + id,
				                "invalid command range/default");
				return v.get<std::int64_t>();
			};
			command.minimum = number("minimum", action->minimum);
			command.maximum = number("maximum", action->maximum);
			command.initial = number("initial", 0);
			control_require(command.minimum <= command.initial && command.initial <= command.maximum, where + "." + id,
			                "initial outside command range");
			control_require(field.contains("consumers") && field.at("consumers").is_array() &&
			                    !field.at("consumers").empty() && field.at("consumers").size() <= 256,
			                where, "expected physical consumers");
			for (const auto &consumer : field.at("consumers"))
			{
				control_require(consumer.is_string() && system_ids.contains(consumer.get<std::string>()), where,
				                "unknown physical consumer");
				const auto index = system_ids.at(consumer.get<std::string>());
				control_require(capabilities[index].contains(command.action), where, "incompatible physical input");
				control_require(inputs.emplace(index, input_key(command.action)).second, where,
				                "physical input has multiple command sources");
				const auto [low, high] = capabilities[index].at(command.action);
				control_require(!field.contains("minimum") || command.minimum >= low, where,
				                "range exceeds physical input");
				control_require(!field.contains("maximum") || command.maximum <= high, where,
				                "range exceeds physical input");
				command.minimum = std::max(command.minimum, low);
				command.maximum = std::min(command.maximum, high);
				command.consumers.push_back(index);
			}
			control_require(command.minimum <= command.initial && command.initial <= command.maximum, where,
			                "initial exceeds physical input");
			control_require(command.semantics != CommandSemantics::Request || command.consumers.size() == 1, where,
			                "one-shot request requires a single executor");
			const bool held =
			    command.action == OperatingAction::Longitudinal || command.action == OperatingAction::Lateral ||
			    command.action == OperatingAction::ManualSteering || command.action == OperatingAction::Boost;
			control_require(!held || (command.minimum <= 0 && command.maximum >= 0), where,
			                "held input range must include neutral zero for handover");
			control_require(command.semantics != CommandSemantics::Request || command.initial == 0, where,
			                "request channels must start idle");
			controller.commands.push_back(std::move(command));
		}
		if (!controller.tree.empty() && catalog.tree_programs.contains(controller.tree))
		{
			const auto seat = std::find_if(consoles.begin(), consoles.end(),
			                               [&](const auto &console)
			                               {
				                               return console.id == controller.console;
			                               });
			const auto observations = compile_system_observations(catalog, entity_id);
			for (const auto &binding : controller_telemetry_bindings(catalog.trees.at(controller.tree), catalog))
			{
				control_require(std::find(seat->feed_systems.begin(), seat->feed_systems.end(), binding.system) !=
				                    seat->feed_systems.end(),
				                where, "System observation is outside Console feeds");
				control_require(observations.contains(binding.system) &&
				                    observations.at(binding.system).contains(binding.field),
				                where, "System observation is unavailable on fitted mechanism");
				control_require(observations.at(binding.system).at(binding.field) == binding.type, where,
				                "System observation type differs from fitted producer");
			}
			for (const auto &action : catalog.tree_programs.at(controller.tree).registry.actions)
			{
				if (action.name == "acknowledge_message")
					continue;
				if (action.name.starts_with("send."))
				{
					const auto templates = entity.value("message_templates", Json::array());
					control_require(std::find(templates.begin(), templates.end(), action.name.substr(5)) !=
					                    templates.end(),
					                where, "tree message template is not permitted by entity");
					continue;
				}
				const auto channel = action.name.substr(6);
				const auto found = std::find_if(controller.commands.begin(), controller.commands.end(),
				                                [&](const auto &command)
				                                {
					                                return command.id == channel;
				                                });
				control_require(action.name.starts_with("write.") && found != controller.commands.end(), where,
				                "tree writes an unowned command channel");
				control_require(action.arguments.size() == 1 &&
				                    action.arguments.front() == (found->type == KnowledgeType::Boolean
				                                                     ? state_tree::Type::Boolean
				                                                     : state_tree::Type::Integer) &&
				                    action.effect == (found->semantics == CommandSemantics::Request),
				                where, "tree command type/semantics differs from owned channel");
			}
		}
		result.controllers.push_back(std::move(controller));
	}
	for (const auto &controller : result.controllers)
		if (!controller.tree.empty() && catalog.tree_programs.contains(controller.tree))
			for (const auto &binding : controller_command_reads(catalog.trees.at(controller.tree), catalog))
			{
				const auto target = std::find_if(result.controllers.begin(), result.controllers.end(),
				                                 [&](const auto &candidate)
				                                 {
					                                 return candidate.id == binding.controller &&
					                                        candidate.console == controller.console;
				                                 });
				control_require(target != result.controllers.end(), path, "command read is outside owning Console");
				const auto channel = std::find_if(target->commands.begin(), target->commands.end(),
				                                  [&](const auto &candidate)
				                                  {
					                                  return candidate.id == binding.channel;
				                                  });
				control_require(channel != target->commands.end(), path, "command read names unknown channel");
				control_require(binding.type == (channel->type == KnowledgeType::Boolean ? state_tree::Type::Boolean
				                                                                         : state_tree::Type::Integer),
				                path, "command read type differs from channel");
			}
	control_require(layout.contains("profiles") && layout.at("profiles").is_array() && !layout.at("profiles").empty() &&
	                    layout.at("profiles").size() <= 32,
	                path, "expected 1..32 complexity profiles");
	std::set<std::string> profiles;
	for (const auto &row : layout.at("profiles"))
	{
		ConsoleComplexitySpec profile{control_name(row, "id", path),
		                              std::vector<bool>(result.controllers.size(), false)};
		control_require(profiles.insert(profile.id).second, path, "duplicate profile");
		control_require(row.contains("consoles") && row.at("consoles").is_object(), path,
		                "profile requires Console allocations");
		std::set<std::size_t> assigned;
		for (auto console = row.at("consoles").begin(); console != row.at("consoles").end(); ++console)
		{
			control_require(console_ids.contains(console.key()) && console.value().is_object(), path,
			                "unknown profile Console");
			for (const auto *operator_kind : {"human", "ai"})
			{
				const auto ids = console.value().value(operator_kind, Json::array());
				control_require(ids.is_array(), path, "profile allocation must be an array");
				for (const auto &id : ids)
				{
					control_require(id.is_string(), path, "invalid profile Controller");
					const auto key = std::pair{console.key(), id.get<std::string>()};
					control_require(controller_ids.contains(key), path, "foreign or unknown profile Controller");
					const auto i = controller_ids.at(key);
					control_require(assigned.insert(i).second, path, "Controller allocated twice");
					profile.human[i] = std::string(operator_kind) == "human";
				}
			}
		}
		control_require(assigned.size() == result.controllers.size(), path, "profile must allocate every Controller");
		if (profile.id == layout.value("default_profile", std::string{}))
			result.default_profile = result.profiles.size();
		result.profiles.push_back(std::move(profile));
	}
	control_require(profiles.contains(layout.value("default_profile", std::string{})), path, "unknown default profile");
	return result;
}
} // namespace phoenix
