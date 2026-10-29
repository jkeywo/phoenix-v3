module;
#include <nlohmann/json.hpp>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
bool commit_controller_command(EntityControls &controls, const ControllerProposal &proposal,
                               const ControllerCommandChecks &checks)
{
	if (proposal.controller >= controls.controllers.size())
		return false;
	auto &controller = controls.controllers[proposal.controller];
	return attempt_controller_command(controller, controller.commands, proposal, checks).outcome !=
	       state_tree::CommandOutcome::Refused;
}
state_tree::CommandAttempt attempt_controller_command(const ConsoleControllerRuntime &controller,
                                                      std::vector<ControllerChannelState> &commands,
                                                      const ControllerProposal &proposal,
                                                      const ControllerCommandChecks &checks)
{
	using state_tree::CommandOutcome;
	const auto refused = [](const char *reason)
	{
		return state_tree::CommandAttempt{CommandOutcome::Refused, reason};
	};
	if (controller.definition.console != proposal.address.console ||
	    controller.definition.id != proposal.address.controller || controller.generation != proposal.generation ||
	    proposal.channel >= commands.size())
		return refused("stale_or_invalid_controller");
	if (!checks.channel_valid(proposal))
		return refused("channel_unavailable");
	const auto &spec = controller.definition.commands[proposal.channel];
	auto &state = commands.at(proposal.channel);
	if (proposal.value < spec.minimum || proposal.value > spec.maximum ||
	    (!spec.choices.empty() && !spec.choices.contains(proposal.value)) ||
	    !checks.arguments_valid(proposal.controller, proposal.channel, spec.action, proposal.value, proposal.arguments))
		return refused("invalid_arguments");
	if (!proposal.operation.empty() &&
	    (proposal.operation != "coordinated_impulse" || spec.action != OperatingAction::Impulse ||
	     spec.coordinated_group.empty() || !controller.definition.operations.contains(proposal.operation)))
		return refused("invalid_native_operation");
	if (proposal.operation.empty() && !proposal.human && !spec.coordinated_group.empty() &&
	    controller_operation_human(controller, "coordinated_impulse"))
		return refused("coordinated_request_owned_by_human");
	const bool owner = proposal.operation.empty() ? controller_command_human(controller, proposal.channel)
	                                              : controller_operation_human(controller, proposal.operation);
	if (owner != proposal.human)
	{
		if (!proposal.human && spec.semantics == CommandSemantics::Setting && state.value == proposal.value &&
		    state.arguments == proposal.arguments)
			return {CommandOutcome::AlreadySatisfied, {}};
		return refused("command_owned_by_other_operator");
	}
	if (spec.semantics == CommandSemantics::Request)
	{
		if (!proposal.request_id || proposal.request_id < state.request_id ||
		    (state.request_id && state.consumed < state.revision && proposal.request_id != state.request_id))
			return refused("invalid_request_identity");
		if (proposal.request_id == state.request_id)
			return proposal.value == state.value && proposal.arguments == state.arguments
			           ? state_tree::CommandAttempt{CommandOutcome::AlreadySatisfied, {}}
			           : refused("request_identity_conflict");
	}
	else if (state.value == proposal.value && state.arguments == proposal.arguments && state.accepted)
	{
		const bool same_effect = checks.same_effect(spec, proposal);
		if (same_effect)
		{
			if (proposal.human && !spec.shared_target_controller.empty())
				state.target_override = true;
			return {CommandOutcome::AlreadySatisfied, {}};
		}
	}
	if (state.revision == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Controller command revision exhausted");
	state.value = proposal.value;
	state.arguments = proposal.arguments;
	state.request_id = proposal.request_id;
	if (proposal.human && !spec.shared_target_controller.empty())
		state.target_override = true;
	++state.revision;
	return {CommandOutcome::Applied, {}};
}

} // namespace phoenix
