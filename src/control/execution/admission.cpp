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
	if (controller.definition.console != proposal.address.console ||
	    controller.definition.id != proposal.address.controller || controller.generation != proposal.generation ||
	    controller.human != proposal.human || proposal.channel >= controller.commands.size())
		return false;
	if (!checks.channel_valid(proposal))
		return false;
	const auto &spec = controller.definition.commands[proposal.channel];
	auto &state = controller.commands[proposal.channel];
	if (proposal.value < spec.minimum || proposal.value > spec.maximum ||
	    (!spec.choices.empty() && !spec.choices.contains(proposal.value)) ||
	    !checks.arguments_valid(proposal.controller, proposal.channel, spec.action, proposal.value, proposal.arguments))
		return false;
	if (spec.semantics == CommandSemantics::Request)
	{
		if (!proposal.request_id || proposal.request_id < state.request_id ||
		    (state.request_id && state.consumed < state.revision && proposal.request_id != state.request_id))
			return false;
		if (proposal.request_id == state.request_id)
			return proposal.value == state.value && proposal.arguments == state.arguments;
	}
	else if (state.value == proposal.value && state.arguments == proposal.arguments && state.accepted)
	{
		const bool same_effect = checks.same_effect(spec, proposal);
		if (same_effect)
		{
			if (proposal.human && !spec.shared_target_controller.empty())
				state.target_override = true;
			return true;
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
	return true;
}

} // namespace phoenix
