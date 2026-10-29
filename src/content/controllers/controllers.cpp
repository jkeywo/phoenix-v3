module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>

module phoenix.content.controllers;
import phoenix.flight;

import phoenix.content.catalog;
import phoenix.content.combat;
import phoenix.content.combat.types;
import phoenix.content.controllers.types;
import phoenix.content.effects;
import phoenix.content.effects.types;
import phoenix.content.equipment;
import phoenix.content.knowledge;
import phoenix.content.resources;
import phoenix.content.resources.types;

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
void control_layout_targets(const Catalog &catalog, Json node, std::set<std::string> &targets, int stars = 0,
                            unsigned depth = 0)
{
	control_require(depth <= 24, "control widget layout", "reusable group nesting exceeds 24 levels");
	if (stars && node.contains("when"))
	{
		const auto &condition = node.at("when");
		if (stars < condition.value("min_stars", 1) || stars > condition.value("max_stars", 3))
			return;
		// Optional runtime capabilities cannot establish guaranteed access to a
		// human command or required main/detail data. An unconditional fallback can.
		for (const auto &[key, value] : condition.items())
			if (key != "min_stars" && key != "max_stars")
				return;
	}
	if (node.contains("group"))
	{
		const auto &group = catalog.ui_groups.at(node.at("group").get<std::string>());
		auto expanded = group.at("root");
		if (node.contains("overrides"))
			expanded.merge_patch(node.at("overrides"));
		control_layout_targets(catalog, std::move(expanded), targets, stars, depth + 1);
		return;
	}
	const auto component = node.value("component", std::string{});
	if (component.starts_with("control/"))
		targets.insert(component);
	for (const auto &child : node.value("children", Json::array()))
		control_layout_targets(catalog, child, targets, stars, depth + 1);
}
void verify_widget_part_coverage(const Catalog &catalog, const ConsoleControllerSpec &controller,
                                 const std::set<std::string> &parts, int stars, const std::string &path)
{
	if (parts.contains("main") || parts.contains("complete"))
		return;
	std::set<std::string> commands, data, operations;
	bool details = false;
	const auto bindings = catalog.control_widgets.at(controller.widget).value("part_bindings", Json::object());
	for (const auto &part : parts)
	{
		const auto &binding = bindings.at(part);
		for (const auto &name : binding.value("commands", Json::array()))
			commands.insert(name.get<std::string>());
		for (const auto &name : binding.value("data", Json::array()))
			data.insert(name.get<std::string>());
		for (const auto &name : binding.value("operations", Json::array()))
			operations.insert(name.get<std::string>());
		details |= binding.value("details", false);
	}
	for (const auto &channel : controller.human_channels.at(stars))
		control_require(commands.contains(channel), path, "human command has no placed part: " + channel);
	for (const auto &operation : controller.human_operations.at(stars))
		control_require(operations.contains(operation), path, "human operation has no placed part: " + operation);
	for (const auto &[item, destination] : controller.data_placement.at(stars))
	{
		control_require(destination != WidgetDataDestination::Main || data.contains(item), path,
		                "main data item has no placed part: " + item);
		control_require(destination != WidgetDataDestination::Details || details, path,
		                "detail data has no placed entry point: " + item);
	}
}
bool ability_endpoint_owned(const Json &entity, const std::string &endpoint, const std::string &controller,
                            const std::string &console)
{
	if (entity.contains("ability_controls") && entity.at("ability_controls").is_object() &&
	    entity.at("ability_controls").contains(endpoint))
	{
		const auto &binding = entity.at("ability_controls").at(endpoint);
		return binding.value("console", std::string{}) == console &&
		       (binding.value("controller", std::string{}) == controller ||
		        binding.value("planner", std::string{}) == controller);
	}
	for (const auto &[id, system] : entity.at("systems").items())
		if (system.value("kind", id) == "psychic" && system.contains("seats"))
			for (const auto &seat : system.at("seats"))
				if (choir_ability_control(id, seat.value("duty", std::string{})) == endpoint &&
				    seat.value("console", std::string{}) == console)
					return seat.value("controller", std::string{}) == controller ||
					       system.value("planning_controller", std::string{}) == controller;
	return false;
}
using CommandBounds = std::map<OperatingAction, std::pair<std::int64_t, std::int64_t>>;
CommandBounds fitted_commands(const FittedSystemSpec &fit, Json spec, const ShipResourcesDefinition *resources,
                              std::size_t index, bool advanced)
{
	CommandBounds result;
	const auto add = [&](OperatingAction action)
	{
		const auto &contract = operating_action(action);
		result[action] = {contract.minimum, contract.maximum};
	};
	const auto kind = fit.kind;
	if (spec.contains("directed"))
	{
		const auto &directed = spec.at("directed");
		add(OperatingAction::DirectedTarget);
		if (directed.contains("modes") && !directed.at("modes").empty())
			result[OperatingAction::DirectedOperatingMode] = {
			    0, static_cast<std::int64_t>(directed.at("modes").size() - 1)};
		if (directed.contains("targeting") && !directed.at("targeting").empty())
			result[OperatingAction::DirectedTargetingMode] = {
			    0, static_cast<std::int64_t>(directed.at("targeting").size() - 1)};
	}
	if (kind == OperatingSystem::Weapon || kind == OperatingSystem::Turret || kind == OperatingSystem::Magazine ||
	    kind == OperatingSystem::Team || kind == OperatingSystem::Shield || kind == OperatingSystem::Comms ||
	    kind == OperatingSystem::Displacement || kind == OperatingSystem::Dispatch || kind == OperatingSystem::Ftl ||
	    kind == OperatingSystem::ShuttleBay || kind == OperatingSystem::Psychic)
	{
		add(OperatingAction::SystemEnabled);
		if (kind == OperatingSystem::Weapon)
		{
			add(OperatingAction::WeaponTarget);
			add(OperatingAction::MagazineSelect);
			add(OperatingAction::WeaponMode);
		}
		if (kind == OperatingSystem::Shield)
			add(OperatingAction::ShieldDischarge);
		if (kind == OperatingSystem::Team &&
		    (spec.value("capabilities", std::set<std::string>{"repair"})).contains("repair"))
		{
			add(OperatingAction::RepairCompartment);
			add(OperatingAction::RepairSystem);
		}
		return result;
	}
	if (!advanced)
	{
		if (kind == OperatingSystem::Engine)
		{
			add(OperatingAction::Altitude);
			result[OperatingAction::Speed] = {0, fit.maximum};
			add(OperatingAction::EngineEnabled);
			add(OperatingAction::SystemEnabled);
		}
		if (kind == OperatingSystem::Steering)
			add(OperatingAction::Heading);
		if (kind == OperatingSystem::Radar)
		{
			add(OperatingAction::SensorTarget);
			add(OperatingAction::RadarEnabled);
			add(OperatingAction::SystemEnabled);
		}
		return result;
	}
	if (kind == OperatingSystem::Engine || kind == OperatingSystem::Steering || kind == OperatingSystem::Lateral ||
	    kind == OperatingSystem::Boost || kind == OperatingSystem::Impulse)
	{
		spec["kind"] = std::string(operating_system_name(kind));
		const auto flight = compile_flight(spec, "System " + fit.id);
		for (auto action : {OperatingAction::SystemEnabled, OperatingAction::EngineEnabled, OperatingAction::Assisted,
		                    OperatingAction::SelectContact})
			add(action);
		if (kind == OperatingSystem::Engine || kind == OperatingSystem::Steering)
		{
			add(OperatingAction::Destination);
			add(OperatingAction::FollowContact);
		}
		if (kind == OperatingSystem::Engine)
		{
			add(OperatingAction::Altitude);
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
		add(OperatingAction::SensorTarget);
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
	if (action == OperatingAction::WeaponTarget || action == OperatingAction::SensorTarget)
		return OperatingAction::DirectedTarget;
	if (action == OperatingAction::WeaponMode)
		return OperatingAction::DirectedTargetingMode;
	return action;
}
} // namespace

ControllableLayout compile_controllable_layout(const Catalog &catalog, const std::string &entity_id,
                                               SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
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
	control_require(!entity.contains("controller") && !entity.contains("controllers") && !entity.contains("presets"),
	                path, "legacy Controller ownership retired; author schema 2 shared widgets");
	if (!entity.contains("control_layout"))
	{
		control_require(catalog.presentation || std::none_of(consoles.begin(), consoles.end(),
		                                                     [](const auto &console)
		                                                     {
			                                                     return !console.definition.empty();
		                                                     }),
		                path, "player Consoles require schema 2 shared widgets");
		result.profiles.push_back({"direct", {}, 3});
		return result;
	}
	const auto resources = compile_ship_resources(catalog, entity_id);
	const bool advanced = uses_advanced_machinery(catalog, entity_id);
	std::optional<std::map<std::string, SystemObservationSchema>> observation_schemas;
	std::vector<CommandBounds> capabilities;
	for (std::size_t i = 0; i < systems.size(); ++i)
		capabilities.push_back(fitted_commands(systems[i], entity.at("systems").at(systems[i].id),
		                                       resources ? &*resources : nullptr, i, advanced));
	const auto &layout = entity.at("control_layout");
	const auto grouped = layout.value("station_complexity", Json(false));
	control_require(grouped.is_boolean() || grouped.is_array(), path,
	                "station_complexity expects boolean or Station IDs");
	if (grouped.is_array())
	{
		std::set<std::string> unique;
		for (const auto &station : grouped)
			control_require(station.is_string() && unique.insert(station.get<std::string>()).second &&
			                    std::any_of(consoles.begin(), consoles.end(),
			                                [&](const auto &c)
			                                {
				                                return Json(c.station) == station;
			                                }),
			                path, "unknown or duplicate complexity Station");
	}
	for (const auto &console : consoles)
		if ((grouped.is_boolean() && grouped.get<bool>()) ||
		    (grouped.is_array() && std::find(grouped.begin(), grouped.end(), Json(console.station)) != grouped.end()))
			result.complexity_groups.emplace(console.id, console.station);
	control_require(layout.is_object() && layout.value("schema", 0) == 2, path,
	                "unsupported layout schema; schema 2 shared widgets required");
	control_require(!layout.contains("manual_workloads"), path, "responsibility belongs in shared widget levels");
	control_require(!entity.contains("controller") && !entity.contains("controllers") && !entity.contains("presets"),
	                path, "explicit layout cannot mix legacy command AI or presets");
	control_require(layout.contains("controllers") && layout.at("controllers").is_array() &&
	                    layout.at("controllers").size() <= 256,
	                path, "expected bounded controllers array");
	std::map<std::pair<std::string, std::string>, std::size_t> controller_ids;
	std::set<std::pair<std::size_t, OperatingAction>> inputs;
	for (const auto &authored_instance : layout.at("controllers"))
	{
		const auto &instance = compiled_widget_instance(catalog, entity_id, authored_instance);
		control_require(instance.contains("widget"), path, "schema 2 Controller requires a shared widget definition");
		const auto widget =
		    instance.contains("widget")
		        ? std::optional{compile_control_widget(catalog, instance.at("widget").get<std::string>())}
		        : std::nullopt;
		auto row = widget ? bind_control_widget(*widget, instance) : instance;
		if (const auto policies = catalog.controller_policies.find(entity_id);
		    policies != catalog.controller_policies.end())
			if (const auto policy = policies->second.find({row.at("console"), row.at("id")});
			    policy != policies->second.end())
			{
				row["tree"] = policy->second;
				row.erase("fallback");
			}
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
			control_require(catalog.tree_programs.contains(controller.tree), where, "Controller tree is not compiled");
			control_require(catalog.tree_programs.at(controller.tree).context == state_tree::Context::Controller, where,
			                "Controller requires a controller-context tree");
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
			command.semantics = action->semantics;
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
			command.initial_heading = command.action == OperatingAction::Heading && !field.contains("initial");
			command.initial = number("initial", command.initial_heading ? command.minimum : 0);
			const auto arguments = field.value("arguments", Json::array());
			control_require(arguments.is_array() && arguments.size() <= 3, where, "invalid command arguments");
			for (const auto &argument : arguments)
			{
				control_require(argument.is_number_integer() && argument >= -1000000000 && argument <= 1000000000,
				                where, "invalid command argument");
				command.initial_arguments.push_back(argument.get<std::int64_t>());
			}
			control_require(operating_arguments_valid(command.action, command.initial, command.initial_arguments),
			                where, "command arguments differ from action contract");
			control_require(command.minimum <= command.initial && command.initial <= command.maximum, where + "." + id,
			                "initial outside command range");
			const bool intent =
			    command.action == OperatingAction::IntentInteger || command.action == OperatingAction::IntentBoolean ||
			    command.action == OperatingAction::AbilityActivate || command.action == OperatingAction::Waypoint;
			control_require(!field.contains("arrival_radius_mm") || field.at("arrival_radius_mm").is_number_integer(),
			                where, "arrival radius must be an integer");
			command.arrival_radius = field.value("arrival_radius_mm", std::int64_t{5000});
			control_require(command.arrival_radius > 0 && command.arrival_radius <= 1000000000, where,
			                "invalid waypoint arrival radius");
			control_require(
			    field.contains("consumers") && field.at("consumers").is_array() &&
			        field.at("consumers").size() <= 256 &&
			        (intent ? field.at("consumers").empty() : !field.at("consumers").empty()),
			    where, "intent channels require no physical consumers; actuator channels require physical consumers");
			std::optional<std::int64_t> directed_initial;
			for (const auto &consumer : field.at("consumers"))
			{
				control_require(consumer.is_string() && system_ids.contains(consumer.get<std::string>()), where,
				                "unknown physical consumer");
				const auto index = system_ids.at(consumer.get<std::string>());
				control_require(capabilities[index].contains(command.action), where, "incompatible physical input");
				if (!field.contains("initial") && (command.action == OperatingAction::DirectedOperatingMode ||
				                                   command.action == OperatingAction::DirectedTargetingMode))
				{
					const auto &directed = entity.at("systems").at(consumer.get<std::string>()).at("directed");
					const bool operation = command.action == OperatingAction::DirectedOperatingMode;
					const auto &choices = directed.at(operation ? "modes" : "targeting");
					control_require(choices.is_array() && !choices.empty(), where, "directed setting requires choices");
					const auto selected =
					    directed.value(operation ? "initial_mode" : "initial_targeting", choices.front());
					const auto position = std::find(choices.begin(), choices.end(), selected);
					control_require(position != choices.end(), where, "unknown initial directed selection");
					const auto value = static_cast<std::int64_t>(position - choices.begin());
					control_require(
					    !directed_initial || *directed_initial == value, where,
					    "grouped directed settings require matching initial selections or explicit initial");
					directed_initial = value;
					command.initial = value;
				}
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
			control_require(command.semantics != CommandSemantics::Request ||
			                    command.action == OperatingAction::AbilityActivate || command.consumers.size() == 1,
			                where, "one-shot request requires a single executor");
			const bool held =
			    command.action == OperatingAction::Longitudinal || command.action == OperatingAction::Lateral ||
			    command.action == OperatingAction::ManualSteering || command.action == OperatingAction::Boost;
			control_require(!held || (command.minimum <= 0 && command.maximum >= 0), where,
			                "held input range must include neutral zero for handover");
			control_require(command.semantics != CommandSemantics::Request || command.initial == 0, where,
			                "request channels must start idle");

			command.ui_system = field.value("ui_system", std::string{});
			control_require(command.ui_system.empty() ||
			                    (command.action == OperatingAction::IntentInteger &&
			                     system_ids.contains(command.ui_system) && !field.contains("choices")),
			                where, "ui_system requires a numeric intent and a fitted presentation subject");
			command.label_key = field.value("label_key", std::string{});
			if (field.contains("shared_target") || field.contains("shared_command"))
			{
				control_require(command.semantics == CommandSemantics::Setting &&
				                    (!field.contains("shared_target") ||
				                     input_key(command.action) == OperatingAction::DirectedTarget),
				                where, "shared commands require settings; shared targets require directed targeting");
				const auto &shared = field.at(field.contains("shared_target") ? "shared_target" : "shared_command");
				command.shared_target_controller = control_name(shared, "controller", where);
				command.shared_target_channel = control_name(shared, "channel", where);
			}
			control_require(command.label_key.size() <= 256, where, "command label too long");
			command.hull_priority = field.value("hull_priority", false);
			control_require(!command.hull_priority || command.action == OperatingAction::RepairSystem, where,
			                "hull_priority requires repair_system");
			command.coordinated_group = field.value("coordinated_group", std::string{});
			control_require(command.coordinated_group.size() <= 64 &&
			                    (command.coordinated_group.empty() || command.action == OperatingAction::Impulse),
			                where, "invalid coordinated flight command");
			if (field.contains("choices"))
			{
				const auto &choices = field.at("choices");
				control_require(intent && choices.is_array() && !choices.empty() && choices.size() <= 64, where,
				                "choices require a bounded intent channel list");
				for (const auto &choice : choices)
				{
					const auto &value = choice.at("value");
					const auto label = choice.at("label_key").get<std::string>();
					control_require(value.is_number_integer() && value >= command.minimum && value <= command.maximum &&
					                    !label.empty() && label.size() <= 256,
					                where, "invalid preference choice");
					control_require(command.choices.emplace(value.get<std::int64_t>(), label).second, where,
					                "duplicate preference choice");
				}
				control_require(command.choices.contains(command.initial), where, "initial must be an authored choice");
			}
			controller.commands.push_back(std::move(command));
		}
		if (!controller.tree.empty())
			for (const auto &endpoint :
			     controller_ability_bindings(catalog.trees.at(controller.tree), catalog, properties))
				control_require(ability_endpoint_owned(entity, endpoint, controller.id, controller.console), where,
				                "ability observation must name an owned execution/planning endpoint on this Console");
		if (!controller.tree.empty())
			for (const auto &endpoint :
			     controller_process_bindings(catalog.trees.at(controller.tree), catalog, properties))
			{
				const auto controls = entity.value("process_controls", Json::object());
				bool owned = false;
				if (controls.is_object() && controls.contains(endpoint))
				{
					const auto owner = controls.at(endpoint).value("controller", std::string{});
					owned = owner == controller.id;
					if (!owned && entity.contains("control_layout"))
						for (const auto &other : entity.at("control_layout").at("controllers"))
							owned = owned || (other.at("id") == owner &&
							                  other.value("console", std::string{}) == controller.console);
				}
				control_require(owned, where, "process observation must name an owned endpoint on this Console");
			}
		if (!controller.tree.empty() && catalog.tree_programs.contains(controller.tree))
		{
			const auto seat = std::find_if(consoles.begin(), consoles.end(),
			                               [&](const auto &console)
			                               {
				                               return console.id == controller.console;
			                               });
			if (!observation_schemas)
				observation_schemas = compile_system_observations(systems, resources ? &*resources : nullptr, advanced);
			const auto &observations = *observation_schemas;
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
				if (action.name.starts_with("select.") || action.name.starts_with("replace.") ||
				    action.name.starts_with("action.") || action.name.starts_with("coordination."))
					continue;
				if (action.name.starts_with("away.") || action.name.starts_with("craft.") ||
				    action.name.starts_with("team.") || action.name.starts_with("logistics.") ||
				    action.name.starts_with("dock.") || action.name == "alert.set" || action.name == "stance.set" ||
				    action.name == "objective.priority")
					continue;
				if (action.name == "acknowledge_message" || action.name == "stop_publication" ||
				    action.name == "withdraw_request")
					continue;

				if (action.name == "ftl.depart")
				{
					control_require(
					    std::any_of(controller.commands.begin(), controller.commands.end(),
					                [&](const auto &c)
					                {
						                return std::any_of(
						                    c.consumers.begin(), c.consumers.end(),
						                    [&](auto i)
						                    {
							                    return systems.at(i).kind == OperatingSystem::Ftl &&
							                           systems.at(i).id ==
							                               catalog.controller_bindings.at(controller.tree).ftl_system;
						                    });
					                }),
					    where, "FTL departure requires an owned FTL fitting");
					continue;
				}
				if (action.name.starts_with("publish."))
				{
					const auto publications = entity.value("publications", Json::array());
					control_require(std::any_of(publications.begin(), publications.end(),
					                            [&](const auto &p)
					                            {
						                            return p.value("id", std::string{}) == action.name.substr(8) &&
						                                   p.value("source_console", std::string{}) ==
						                                       controller.console;
					                            }),
					                where, "tree publication template is not owned by Console");
					continue;
				}
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
				control_require(action.arguments.size() == 1 + operating_arguments(found->action).size() &&
				                    std::all_of(action.arguments.begin() + 1, action.arguments.end(),
				                                [](auto type)
				                                {
					                                return type == state_tree::Type::Integer;
				                                }) &&
				                    action.arguments.front() == (found->type == KnowledgeType::Boolean
				                                                     ? state_tree::Type::Boolean
				                                                     : state_tree::Type::Integer) &&
				                    action.effect == (found->semantics == CommandSemantics::Request),
				                where, "tree command type/semantics differs from owned channel");
			}
		}
		if (row.contains("widget"))
		{
			controller.widget = row.at("widget").get<std::string>();
			for (const auto &[role, specification] : widget->equipment.items())
			{
				const auto &binding = row.at("equipment").at(role);
				const auto identities = binding.is_array() ? binding : Json::array({binding});
				const auto kinds = specification.value("kinds", Json::array());
				control_require(kinds.is_array(), where, "equipment kinds must be an array");
				for (const auto &identity : identities)
				{
					const auto name = identity.get<std::string>();
					controller.equipment[role].push_back(name);
					control_require(system_ids.contains(name), where, "unresolved attached equipment " + name);
					const auto kind = std::string(operating_system_name(systems[system_ids.at(name)].kind));
					control_require(kinds.empty() || std::find(kinds.begin(), kinds.end(), Json(kind)) != kinds.end(),
					                where, "incompatible equipment for role " + role + ": " + name);
				}
			}
			for (const auto &[operation, equipment] : row.at("operations").items())
			{
				auto &attached = controller.operations[operation];
				for (const auto &identity : equipment)
					attached.push_back(identity.get<std::string>());
			}
			const auto &mapping = row.at("star_levels");
			control_require(mapping.is_object() && mapping.size() == 3, where,
			                "widget needs responsibility mappings for stars 1, 2 and 3");
			for (int stars = 1; stars <= 3; ++stars)
			{
				const auto key = std::to_string(stars);
				control_require(mapping.contains(key) && mapping.at(key).is_string(), where,
				                "missing widget star mapping");
				const auto level = mapping.at(key).get<std::string>();
				const auto found = std::find_if(widget->levels.begin(), widget->levels.end(),
				                                [&](const auto &candidate)
				                                {
					                                return candidate.id == level;
				                                });
				control_require(found != widget->levels.end(), where, "unknown widget responsibility level " + level);
				controller.star_levels[stars] = level;
				for (const auto &responsibility : found->human)
					if (controller.operations.contains(responsibility))
						controller.human_operations[stars].insert(responsibility);
					else
						controller.human_channels[stars].insert(responsibility);
				controller.human_channels.try_emplace(stars);
				controller.human_operations.try_emplace(stars);
				controller.data_placement[stars] =
				    control_widget_data_placement(*widget, level, stars, row.value("data_placement", Json::object()));
			}
		}
		result.controllers.push_back(std::move(controller));
	}
	for (std::size_t i = 0; i < result.controllers.size(); ++i)
		for (std::size_t j = i + 1; j < result.controllers.size(); ++j)
		{
			const auto &left = result.controllers[i];
			const auto &right = result.controllers[j];
			if (left.console != right.console)
				continue;
			for (const auto &[operation, equipment] : left.operations)
				if (const auto other = right.operations.find(operation); other != right.operations.end())
					control_require(operation != "personnel" && operation != "mission_staffing" &&
					                    operation != "contribution" && operation != "alert" && operation != "stance" &&
					                    operation != "objectives" &&
					                    (equipment.empty() || other->second.empty() ||
					                     std::none_of(equipment.begin(), equipment.end(),
					                                  [&](const auto &identity)
					                                  {
						                                  return std::find(other->second.begin(), other->second.end(),
						                                                   identity) != other->second.end();
					                                  })),
					                path, "native operation has overlapping widget owners: " + operation);
		}
	for (const auto &controller : result.controllers)
		for (const auto &command : controller.commands)
			if (!command.shared_target_controller.empty())
			{
				bool valid = false;
				for (const auto &source : result.controllers)
					if (source.console == controller.console && source.id == command.shared_target_controller)
						for (const auto &channel : source.commands)
							valid |= channel.id == command.shared_target_channel &&
							         channel.shared_target_controller.empty() &&
							         (channel.action == command.action ||
							          (input_key(channel.action) == OperatingAction::DirectedTarget &&
							           input_key(command.action) == OperatingAction::DirectedTarget));
				control_require(valid, path,
				                "shared target must name an independent target channel on the same Console");
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
	for (const auto &controller : result.controllers)
		if (!controller.tree.empty() && catalog.tree_programs.contains(controller.tree))
			for (const auto &binding : controller_contact_bindings(catalog.trees.at(controller.tree), catalog))
			{
				const auto target = std::find_if(result.controllers.begin(), result.controllers.end(),
				                                 [&](const auto &candidate)
				                                 {
					                                 return candidate.id == binding.controller &&
					                                        candidate.console == controller.console;
				                                 });
				control_require(target != result.controllers.end(), path, "contact selector is outside owning Console");
				const auto channel = std::find_if(target->commands.begin(), target->commands.end(),
				                                  [&](const auto &candidate)
				                                  {
					                                  return candidate.id == binding.channel;
				                                  });
				control_require(channel != target->commands.end() &&
				                    (channel->action == OperatingAction::SelectContact ||
				                     channel->action == OperatingAction::FollowContact),
				                path, "contact selector requires a selected-contact or follow channel");
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
		profile.stars = row.value("stars", 0);
		control_require(profile.stars >= 0 && profile.stars <= 3, path, "stars must be between zero and three");
		control_require(!row.contains("consoles") && profile.stars >= 1, path,
		                "widget profiles name stars, not separate Controller allocations");
		for (std::size_t i = 0; i < result.controllers.size(); ++i)
			profile.human[i] = !result.controllers[i].human_channels.at(profile.stars).empty() ||
			                   !result.controllers[i].human_operations.at(profile.stars).empty();
		if (profile.id == layout.value("default_profile", std::string{}))
			result.default_profile = result.profiles.size();
		result.profiles.push_back(std::move(profile));
	}
	control_require(profiles.contains(layout.value("default_profile", std::string{})), path, "unknown default profile");
	const auto defaults = layout.value("default_profiles", Json::object());
	control_require(defaults.is_object() && defaults.size() <= consoles.size(), path, "invalid default profiles");
	for (const auto &[console, profile] : defaults.items())
	{
		control_require(console_ids.contains(console) && profile.is_string() &&
		                    profiles.contains(profile.get<std::string>()),
		                path, "unknown per-Console default profile");
		for (std::size_t i = 0; i < result.profiles.size(); ++i)
			if (result.profiles[i].id == profile.get<std::string>())
				result.default_profiles[console] = i;
	}
	const auto fixed = layout.value("fixed_profiles", Json::object());
	control_require(fixed.is_object() && fixed.size() <= consoles.size(), path, "invalid fixed profiles");
	for (const auto &[console, profile] : fixed.items())
	{
		control_require(console_ids.contains(console) && profile.is_string() &&
		                    profiles.contains(profile.get<std::string>()),
		                path, "unknown fixed Console profile");
		for (std::size_t i = 0; i < result.profiles.size(); ++i)
			if (result.profiles[i].id == profile.get<std::string>())
			{
				result.fixed_profiles[console] = i;
				result.default_profiles[console] = i;
			}
	}
	for (const auto &console : consoles)
		if (!console.definition.empty())
			for (const auto &widget : catalog.consoles.at(console.definition).at("widgets"))
				if (widget.contains("controller"))
				{
					const auto owner =
					    std::find_if(result.controllers.begin(), result.controllers.end(),
					                 [&](const auto &controller)
					                 {
						                 return controller.console == console.id &&
						                        controller.id == widget.at("controller").get<std::string>();
					                 });
					control_require(owner != result.controllers.end(), path,
					                "widget names a foreign or unknown Controller");
					const auto command = std::find_if(owner->commands.begin(), owner->commands.end(),
					                                  [&](const auto &channel)
					                                  {
						                                  return channel.id == widget.value("channel", std::string{});
					                                  });
					control_require(command != owner->commands.end(), path, "widget names an unknown command channel");
					control_require(command->semantics == CommandSemantics::Setting &&
					                    command->action ==
					                        (widget.at("kind") == "navigation" ? OperatingAction::Waypoint
					                         : widget.at("kind") == "throttle" ? OperatingAction::Speed
					                                                           : OperatingAction::Heading),
					                path, "widget action differs from command channel");
				}
	for (const auto &console : consoles)
	{
		if (console.definition.empty())
			continue;
		const auto &definition = catalog.consoles.at(console.definition);
		if (!definition.contains("layout"))
			continue;
		auto layout = definition.at("layout");
		if (layout.is_string())
			layout = catalog.layouts.at(layout.get<std::string>());
		if (definition.contains("layout_overrides"))
			layout.merge_patch(definition.at("layout_overrides"));
		for (const auto *variant : {"root", "compact"})
		{
			if (!layout.contains(variant))
				continue;
			std::set<std::size_t> declared;
			for (std::size_t index = 0; index < result.controllers.size(); ++index)
				if (result.controllers[index].console == console.id)
					declared.insert(index);
			for (const int stars : {0, 1, 2, 3})
			{
				std::set<std::string> targets;
				control_layout_targets(catalog, layout.at(variant), targets, stars);
				std::map<std::size_t, std::set<std::string>> parts;
				for (const auto &target : targets)
				{
					const auto last = target.rfind('/');
					control_require(last > 8 && last + 1 < target.size(), path, "invalid widget placement " + target);
					auto instance = target.substr(8, last - 8);
					const auto part = target.substr(last + 1);
					auto owner = console.id;
					if (const auto scope = instance.find('/'); scope != std::string::npos)
					{
						owner = instance.substr(0, scope);
						instance = instance.substr(scope + 1);
					}
					const auto controller =
					    std::find_if(result.controllers.begin(), result.controllers.end(),
					                 [&](const auto &candidate)
					                 {
						                 return candidate.console == owner && candidate.id == instance;
					                 });
					control_require(controller != result.controllers.end() && !controller->widget.empty(), path,
					                "layout names an unbound control widget " + target);
					const auto widget = compile_control_widget(catalog, controller->widget);
					control_require(part == "complete" || part == "main" || widget.parts.contains(part), path,
					                "layout names an unknown widget part " + target);
					if (part != "complete" && part != "main")
						control_require(catalog.control_widgets.at(controller->widget)
						                    .value("part_bindings", Json::object())
						                    .contains(part),
						                path, "named widget part has no data/control bindings " + target);
					parts[static_cast<std::size_t>(controller - result.controllers.begin())].insert(part);
				}
				if (!stars)
				{
					for (const auto &[index, placed] : parts)
						if (result.controllers.at(index).console == console.id)
							declared.insert(index);
				}
				else
					for (const auto index : declared)
						verify_widget_part_coverage(catalog, result.controllers.at(index), parts[index], stars,
						                            path + "." + variant + ".stars" + std::to_string(stars));
			}
		}
	}
	std::size_t waypoints = 0;
	for (const auto &controller : result.controllers)
		for (const auto &command : controller.commands)
			waypoints += command.action == OperatingAction::Waypoint;
	control_require(waypoints <= 1, path, "one ship waypoint requires a single owning Controller channel");
	return result;
}

} // namespace phoenix
