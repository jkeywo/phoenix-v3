module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.control.execution;
import phoenix.world.state;

namespace phoenix
{
std::vector<state_tree::Value> controller_action_observations(const ConsoleControllerRuntime &controller,
                                                              std::span<const state_tree::Value> source,
                                                              const ControllerActionPorts &ports)
{
	std::vector<state_tree::Value> result(source.begin(), source.end());
	const auto &spec = controller_action_spec(controller);
	for (std::size_t i = 0; i < spec.inputs.size(); ++i)
		result.at(spec.inputs[i]) = controller.action.inputs.at(i);
	const auto &brain = *controller.brain;
	std::optional<std::int64_t> x, y, contact;
	for (const auto index : spec.inputs)
	{
		const auto &name = brain.program.registry.observations[index].name;
		if (name == "input.destination_x")
			x = result[index].number;
		if (name == "input.destination_y")
			y = result[index].number;
		if (name == "input.contact")
			contact = result[index].number;
	}
	result.at(brain.action_base + 3) =
	    state_tree::Value::integer(x && y ? fixed::length({*x - source[brain.own[ControllerOwnField::X]].number,
	                                                       *y - source[brain.own[ControllerOwnField::Y]].number})
	                                      : 0);
	result.at(brain.action_base + 4) = state_tree::Value::integer(fixed::length(
	    {source[brain.own[ControllerOwnField::Vx]].number, source[brain.own[ControllerOwnField::Vy]].number}));
	bool visible = false;
	if (contact)
	{
		const auto &observed = ports.observation();
		visible = std::any_of(observed.contacts.begin(), observed.contacts.end(),
		                      [&](const auto &body)
		                      {
			                      return body.id == *contact;
		                      }) ||
		          std::any_of(observed.bearing_contacts.begin(), observed.bearing_contacts.end(),
		                      [&](const auto &b)
		                      {
			                      return b.id == *contact;
		                      });
	}
	result.at(brain.action_base + 5) = state_tree::Value::boolean(visible);
	return result;
}
void cancel_controller_action(ConsoleControllerRuntime &controller, const std::string &reason,
                              std::span<const state_tree::Value> source, const ControllerActionPorts &ports)
{
	if (controller.action.status != "running")
		return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = controller_action_observations(controller, source, ports);
	const auto result = state_tree::interrupt(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs)
		ports.output(output, observations, &spec.program);
	controller.action.status = "cancelled";
	controller.action.reason = reason;
}
bool select_controller_action(ConsoleControllerRuntime &controller, const std::string &name,
                              const std::string &invocation, std::span<const state_tree::Value> inputs,
                              std::span<const state_tree::Value> source, std::uint64_t generation,
                              const ControllerActionPorts &ports)
{
	if (controller.human || generation != controller.generation || !controller.brain || invocation.empty() ||
	    invocation.size() > 1024)
		return false;
	const auto found = std::find_if(controller.actions.begin(), controller.actions.end(),
	                                [&](const auto &spec)
	                                {
		                                return spec.id == name;
	                                });
	if (found == controller.actions.end() || inputs.size() != found->inputs.size())
		return false;
	for (std::size_t i = 0; i < inputs.size(); ++i)
		if (inputs[i].type != controller.brain->program.registry.observations.at(found->inputs[i]).type)
			return false;
	auto &active = controller.action;
	const std::vector<state_tree::Value> arguments(inputs.begin(), inputs.end());
	if (active.id == name && active.invocation == invocation && active.inputs == arguments &&
	    !(active.status == "cancelled" && active.reason == "operator_changed"))
		return true;
	std::vector<state_tree::Value> observations(source.begin(), source.end());
	for (std::size_t i = 0; i < inputs.size(); ++i)
		observations.at(found->inputs[i]) = inputs[i];
	state_tree::Instance check;
	const auto availability = state_tree::step(found->availability, check, observations);
	if (availability.debug.active.empty() || availability.debug.active.back() != "available")
		return false;
	cancel_controller_action(controller, "replaced", source, ports);
	if (active.serial == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Action serial exhausted");
	const auto serial = active.serial + 1;
	active = {name, invocation, "running", {}, arguments, {}, serial};
	return true;
}
void run_controller_action(ConsoleControllerRuntime &controller, std::span<const state_tree::Value> source,
                           const ControllerActionPorts &ports)
{
	controller.action_writes.clear();
	if (controller.action.status != "running")
		return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = controller_action_observations(controller, source, ports);
	const auto result = state_tree::step(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs)
	{
		ports.output(output, observations, &spec.program);
		if (controller.action.status != "running")
			break;
	}
	if (controller.action.status != "running")
	{
		const auto status = controller.action.status, reason = controller.action.reason;
		const auto cleanup = state_tree::interrupt(spec.program, controller.action.continuation, observations);
		for (const auto &output : cleanup.outputs)
			ports.output(output, observations, &spec.program);
		controller.action.status = status;
		controller.action.reason = reason;
	}
}

void step_controller_policy(ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observations,
                            const ControllerActionPorts &ports)
{
	if (!controller.brain)
		return;
	auto &brain = *controller.brain;
	if (controller.effective_provider == "tree")
	{
		const auto result = state_tree::step(brain.program, brain.continuation, observations);
		brain.debug = result.debug;
		for (const auto &output : result.outputs)
			ports.output(output, observations, nullptr);
	}
	run_controller_action(controller, observations, ports);
}

} // namespace phoenix
