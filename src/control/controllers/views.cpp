module;
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.control.views;
import phoenix.control.coordination;
import phoenix.gameplay_vocabulary;

namespace phoenix
{
Json project_controller_runtime(const ConsoleControllerRuntime &controller, const ControllerViewFacts &facts,
                                std::span<const state_tree::Value> action_observations)
{
	Json commands = Json::object();
	for (std::size_t i = 0; i < controller.commands.size(); ++i)
	{
		const auto &spec = controller.definition.commands[i];
		const auto &state = controller.commands[i];
		commands[spec.id] = {{"value", state.value},
		                     {"arguments", state.arguments},
		                     {"action", std::string(operating_action(spec.action).name)},
		                     {"minimum", spec.minimum},
		                     {"maximum", spec.maximum},
		                     {"revision", state.revision},
		                     {"consumed", state.consumed},
		                     {"pending", state.consumed < state.revision},
		                     {"semantics", spec.semantics == CommandSemantics::Setting ? "setting" : "request"},
		                     {"accepted", state.accepted},
		                     {"request_id", state.request_id}};
		commands[spec.id]["human"] = controller_command_human(controller, i);
		if (!spec.ui_system.empty())
			commands[spec.id]["ui_system"] = spec.ui_system;
		auto &consumers = commands[spec.id]["consumers"] = Json::array();
		for (const auto index : spec.consumers)
			consumers.push_back(facts.systems.at(index));
	}
	Json row{{"id", controller.definition.id},
	         {"console", controller.definition.console},
	         {"human", controller.human},
	         {"generation", controller.generation},
	         {"commands", commands}};
	if (!controller.definition.widget.empty())
	{
		row["widget"] = controller.definition.widget;
		row["equipment"] = controller.definition.equipment;
		row["star_levels"] = Json::object();
		row["data_placement"] = Json::object();
		for (const auto &[stars, level] : controller.definition.star_levels)
			row["star_levels"][std::to_string(stars)] = level;
		for (const auto &[stars, items] : controller.definition.data_placement)
		{
			auto &placement = row["data_placement"][std::to_string(stars)] = Json::object();
			for (const auto &[item, target] : items)
				placement[item] = target == WidgetDataDestination::Main      ? "main"
				                  : target == WidgetDataDestination::Details ? "details"
				                                                             : "hidden";
		}
	}
	row["conversation_authority"] = facts.conversation_authority;
	row["contribution_authority"] = facts.contribution_authority;
	row["craft_authority"] = facts.craft_authority;
	row["transport_authority"] = facts.transport_authority;
	row["action"] = {
	    {"id", controller.action.id}, {"status", controller.action.status}, {"reason", controller.action.reason}};
	auto &available = row["actions"] = Json::array();
	if (controller.brain)
		for (const auto &action : controller.actions)
		{
			Json inputs = Json::array();
			for (const auto index : action.inputs)
			{
				const auto &field = controller.brain->program.registry.observations.at(index);
				inputs.push_back({{"name", field.name.substr(6)},
				                  {"type", field.type == state_tree::Type::Text      ? "text"
				                           : field.type == state_tree::Type::Boolean ? "boolean"
				                                                                     : "integer"}});
			}
			bool usable = false;
			if (!controller.human || !controller.definition.widget.empty())
			{
				state_tree::Instance probe;
				const auto check = state_tree::step(action.availability, probe, action_observations);
				usable = !check.debug.active.empty() && check.debug.active.back() == "available";
			}
			available.push_back({{"id", action.id},
			                     {"definition", action.tree},
			                     {"label", action.label},
			                     {"inputs", inputs},
			                     {"available", usable}});
		}
	row["provider"] = {{"selected", controller.selected_provider},
	                   {"effective", controller.effective_provider},
	                   {"reason", controller.fallback_reason}};
	row["action"]["invocation"] = controller.action.invocation;
	row["coordination"] = save_controller_coordination(controller);
	return row;
}
} // namespace phoenix
