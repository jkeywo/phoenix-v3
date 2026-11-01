module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.control.execution;
import phoenix.world.state;

namespace phoenix
{
namespace
{
class ControllerTreeEvaluation final : public state_tree::EvaluationPorts
{
	const ConsoleControllerRuntime &controller;
	const ControllerActionPorts &ports;
	mutable std::vector<ControllerChannelState> staged;

public:
	ControllerTreeEvaluation(const ConsoleControllerRuntime &owner, const ControllerActionPorts &bindings)
	    : controller(owner), ports(bindings), staged(owner.commands)
	{
	}
	bool admits_commands() const override
	{
		return static_cast<bool>(ports.command);
	}
	state_tree::CommandAttempt command(const state_tree::Program &program,
	                                   const state_tree::Output &output) const override
	{
		return ports.command(staged, program, output);
	}
	state_tree::Value observation(const state_tree::Program &current, state_tree::Index index,
	                              const state_tree::Value &frozen) const override
	{
		const auto &name = current.registry.observations.at(index).name;
		for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
		{
			const auto prefix = "command." + controller.definition.commands[i].id;
			if (name == prefix)
				return state_tree::Value{frozen.type, staged[i].value, {}};
			if (name == prefix + ".accepted")
				return state_tree::Value::boolean(staged[i].accepted);
			if (name == prefix + ".revision")
				return state_tree::Value::integer(static_cast<std::int64_t>(staged[i].revision));
			if (name == prefix + ".consumed")
				return state_tree::Value::integer(static_cast<std::int64_t>(staged[i].consumed));
			if (name == prefix + ".request_id")
				return state_tree::Value::integer(static_cast<std::int64_t>(staged[i].request_id));
			if (name == prefix + ".pending")
				return state_tree::Value::boolean(staged[i].consumed < staged[i].revision);
			const auto arguments = operating_arguments(controller.definition.commands[i].action);
			for (std::size_t argument = 0; argument < arguments.size(); ++argument)
				if (name == prefix + "." + std::string(arguments[argument].name))
					return state_tree::Value::integer(staged[i].arguments.at(argument));
		}
		return frozen;
	}
};
state_tree::Result evaluate_controller_tree(ConsoleControllerRuntime &controller, const state_tree::Program &program,
                                            state_tree::Instance &continuation,
                                            std::span<const state_tree::Value> observations,
                                            const ControllerActionPorts &ports)
{
	const ControllerTreeEvaluation evaluation{controller, ports};
	// step already protects the continuation transactionally. Only resuming an
	// interrupted owner needs a separate copy so failure also rolls back resume.
	if (!continuation.interrupted)
		return state_tree::step(program, continuation, observations, 1, evaluation);
	auto next = continuation;
	state_tree::resume(next);
	auto result = state_tree::step(program, next, observations, 1, evaluation);
	continuation = std::move(next);
	return result;
}
} // namespace
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
	if (!controller_ai_running(controller) || generation != controller.generation || !controller.brain ||
	    invocation.empty() || invocation.size() > 1024)
		return false;
	if (!controller.brain->objective_bindings.empty() &&
	    (!controller.objective_choice || controller.objective_choice->action != name ||
	     controller.objective_choice->invocation != invocation ||
	     controller.objective_choice->arguments != std::vector<state_tree::Value>(inputs.begin(), inputs.end())))
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
	const auto result =
	    evaluate_controller_tree(controller, spec.program, controller.action.continuation, observations, ports);
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
	if (controller.effective_provider == "tree" || !brain.objective_bindings.empty())
	{
		const auto result =
		    evaluate_controller_tree(controller, brain.program, brain.continuation, observations, ports);
		brain.debug = result.debug;
		for (const auto &output : result.outputs)
			ports.output(output, observations, nullptr);
	}
	if (!brain.objective_bindings.empty())
	{
		if (controller.objective_choice)
		{
			const auto &choice = *controller.objective_choice;
			if (select_controller_action(controller, choice.action, choice.invocation, choice.arguments, observations,
			                             controller.generation, ports))
				controller.objective_commitment = choice.commitment;
		}
		else if (controller.action.invocation.starts_with("objective/"))
			cancel_controller_action(controller, "no_feasible_objective", observations, ports);
	}
	run_controller_action(controller, observations, ports);
	if (controller.objective_choice && controller.action.status == "failed" &&
	    controller.action.invocation == controller.objective_choice->invocation)
	{
		const auto &choice = *controller.objective_choice;
		auto &retry = controller.objective_retries[choice.execution];
		if (retry.failed_serial != controller.action.serial)
		{
			if (retry.attempt >= 9007199254740991ULL)
				throw std::runtime_error("Objective retry sequence exhausted");
			retry.action = choice.action;
			retry.arguments = choice.arguments;
			retry.failed_serial = controller.action.serial;
			++retry.attempt;
			const auto tick = static_cast<std::uint64_t>(observations[brain.own[ControllerOwnField::Tick]].number);
			if (tick > 9007199254740991ULL - choice.retry_ticks)
				throw std::runtime_error("Objective retry tick exhausted");
			retry.not_before = tick + choice.retry_ticks;
		}
		// Retain bounded private failure evidence. Eviction is stable by failure
		// serial and identity, independent of map/evaluation traversal order.
		const auto bytes = [&]
		{
			std::size_t total = 0;
			for (const auto &[key, receipt] : controller.objective_retries)
			{
				total += key.size() + receipt.action.size() + 64;
				for (const auto &argument : receipt.arguments)
					total += argument.text.size() + 32;
			}
			return total;
		};
		while (controller.objective_retries.size() > 128 || bytes() > 1048576)
		{
			const auto oldest =
			    std::min_element(controller.objective_retries.begin(), controller.objective_retries.end(),
			                     [](const auto &left, const auto &right)
			                     {
				                     return std::pair{left.second.failed_serial, left.first} <
				                            std::pair{right.second.failed_serial, right.first};
			                     });
			controller.objective_retries.erase(oldest);
		}
	}
}

} // namespace phoenix
