module;
#include <cstdint>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.control.handover;

namespace phoenix
{
void refresh_controller_operator(ConsoleControllerRuntime &controller, std::uint64_t claimant, bool human,
                                 std::int64_t stars, bool validate, const ControllerHandoverPorts &ports)
{
	const bool present = claimant != 0;
	// A lower workload relinquishes settings whose individual controls are no longer available.
	if (human && !validate)
		for (std::size_t j = 0; j < controller.commands.size(); ++j)
		{
			const auto &spec = controller.definition.commands[j];
			auto &state = controller.commands[j];
			if (spec.semantics == CommandSemantics::Setting && stars < spec.individual_stars &&
			    (state.value != spec.initial || state.arguments != spec.initial_arguments))
			{
				state.value = spec.initial;
				state.arguments = spec.initial_arguments;
				state.target_override = false;
				++state.revision;
			}
		}
	if (validate && human != controller.human)
		throw std::runtime_error("Saved Controller operator differs from profile/claims");
	if (validate)
		controller.operator_claimant = claimant;
	if (human == controller.human && (!human || claimant == controller.operator_claimant))
	{
		controller.operator_claimant = claimant;
		ports.control(present, human);
		return;
	}
	if (!controller.human && controller.brain)
	{
		const auto observations = ports.observations();
		ports.cancel(observations);
		const auto result =
		    state_tree::interrupt(controller.brain->program, controller.brain->continuation, observations);
		for (const auto &output : result.outputs)
			ports.output(output, observations);
		ports.cancel(observations);
		for (auto &request : controller.incoming_requests)
			if (request.status == OfficerStatus::Accepted || request.status == OfficerStatus::Completed)
			{
				request.status = OfficerStatus::Failed;
				request.reason = "operator_changed";
				ports.reply(request);
			}
		for (auto &request : controller.outgoing_requests)
			if (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted ||
			    request.status == OfficerStatus::Completed)
			{
				request.status = OfficerStatus::Cancelled;
				request.reason = "operator_changed";
				ports.reply(request);
			}
	}
	if (controller.generation == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Controller operator generation exhausted");
	++controller.generation;
	ports.advance_epoch();
	controller.human = human;
	controller.operator_claimant = claimant;
	ports.control(present, human);
	for (std::size_t j = 0; j < controller.commands.size(); ++j)
	{
		const auto action = controller.definition.commands[j].action;
		if (action == OperatingAction::Longitudinal || action == OperatingAction::Lateral ||
		    action == OperatingAction::ManualSteering || action == OperatingAction::Boost)
		{
			auto &command = controller.commands[j];
			command.value = 0;
			++command.revision;
		}
	}
}
void advance_controller_stance_epoch(BridgeBlackboard &bridge, const std::string &console)
{
	if (bridge.epochs.contains(console))
	{
		auto &epoch = bridge.epochs.at(console);
		if (epoch >= 9007199254740991ULL)
			throw std::runtime_error("Console stance authority generation exhausted");
		++epoch;
	}
}
} // namespace phoenix
