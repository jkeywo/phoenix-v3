module;
#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.control.controllers;

namespace phoenix
{
bool controller_command_human(const ConsoleControllerRuntime &controller, std::size_t channel)
{
	return controller.human_commands.empty() ? controller.human : controller.human_commands.at(channel);
}
const ControllerActionSpec &controller_action_spec(const ConsoleControllerRuntime &controller)
{
	for (const auto &spec : controller.actions)
		if (spec.id == controller.action.id)
			return spec;
	throw std::runtime_error("Unknown active Controller action");
}
Json save_controller_action(const ConsoleControllerRuntime &controller)
{
	const auto &a = controller.action;
	Json result{{"id", a.id},         {"invocation", a.invocation}, {"status", a.status},
	            {"reason", a.reason}, {"serial", a.serial},         {"inputs", Json::array()}};
	for (const auto &v : a.inputs)
		result["inputs"].push_back({{"type", static_cast<int>(v.type)}, {"number", v.number}, {"text", v.text}});
	if (!a.id.empty())
		result["continuation"] = state_tree::save(controller_action_spec(controller).program, a.continuation);
	return result;
}
void restore_controller_action(ConsoleControllerRuntime &controller, const Json &saved)
{
	auto &a = controller.action;
	a.id = saved.at("id");
	a.invocation = saved.at("invocation");
	a.status = saved.at("status");
	a.reason = saved.at("reason");
	a.serial = saved.at("serial");
	if (a.reason.size() > 4096 || a.invocation.size() > 1024 ||
	    (a.status != "idle" && a.status != "running" && a.status != "succeeded" && a.status != "failed" &&
	     a.status != "cancelled"))
		throw std::runtime_error("Invalid action status");
	if (a.id.empty())
	{
		if (a.status != "idle" || a.serial || !saved.at("inputs").empty() || saved.contains("continuation"))
			throw std::runtime_error("Invalid idle action");
		return;
	}
	if (!controller.brain || !a.serial || a.invocation.empty() || a.status == "idle" ||
	    (controller.human && controller.definition.widget.empty() && a.status == "running"))
		throw std::runtime_error("Invalid action ownership");
	const auto &spec = controller_action_spec(controller);
	if (!saved.at("inputs").is_array() || saved.at("inputs").size() != spec.inputs.size())
		throw std::runtime_error("Invalid saved action inputs");
	a.inputs.clear();
	for (std::size_t i = 0; i < spec.inputs.size(); ++i)
	{
		const auto &v = saved.at("inputs").at(i);
		const auto type = spec.program.registry.observations.at(spec.inputs[i]).type;
		if (v.at("type") != static_cast<int>(type) || !v.at("number").is_number_integer() ||
		    !v.at("text").is_string() || v.at("text").get_ref<const std::string &>().size() > 4096)
			throw std::runtime_error("Invalid saved action input type");
		if (type == state_tree::Type::Boolean && v.at("number") != 0 && v.at("number") != 1)
			throw std::runtime_error("Invalid saved boolean action input");
		a.inputs.push_back({type, v.at("number"), v.at("text")});
	}
	a.continuation = state_tree::load(spec.program, saved.at("continuation"));
}

void set_console_complexity(EntityControls &controls, const std::string &console, std::size_t profile)
{
	const auto group = controls.layout.complexity_groups.find(console);
	for (auto &[id, selected] : controls.profiles)
		if (id == console ||
		    (group != controls.layout.complexity_groups.end() && controls.layout.complexity_groups.contains(id) &&
		     controls.layout.complexity_groups.at(id) == group->second))
			selected = controls.layout.fixed_profiles.contains(id) ? controls.layout.fixed_profiles.at(id) : profile;
}

} // namespace phoenix
