module;
#include <cstdint>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.control.handover;

namespace phoenix
{
void refresh_controller_operator(ConsoleControllerRuntime &controller, std::uint64_t claimant, bool human,
                                 bool validate, const ControllerHandoverPorts &ports, std::vector<bool> human_commands,
                                 std::set<std::string> human_operations)
{
	const bool present = claimant != 0;
	if (validate && (human != controller.human || human_commands != controller.human_commands ||
	                 human_operations != controller.human_operations))
		throw std::runtime_error("Saved Controller operator differs from profile/claims");
	if (validate)
		controller.operator_claimant = claimant;
	if (human == controller.human && human_commands == controller.human_commands &&
	    human_operations == controller.human_operations && (!human || claimant == controller.operator_claimant))
	{
		controller.operator_claimant = claimant;
		ports.control(present, human);
		return;
	}
	if (controller_ai_running(controller) && controller.brain)
	{
		const auto observations = ports.observations();
		ports.cancel(observations);
		// Policy continuity is independent of who owns its command channels.
		// Selected actions and commitments still belong to the outgoing operator.
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
	std::vector<bool> reset_commands;
	for (std::size_t j = 0; j < controller.commands.size(); ++j)
		reset_commands.push_back(controller.human_commands[j] != human_commands[j] ||
		                         (human_commands[j] && controller.operator_claimant != claimant));
	controller.human = human;
	controller.human_commands = std::move(human_commands);
	controller.human_operations = std::move(human_operations);
	controller.operator_claimant = claimant;
	ports.control(present, human);
	for (std::size_t j = 0; j < controller.commands.size(); ++j)
	{
		const auto action = controller.definition.commands[j].action;
		if (reset_commands[j] && (action == OperatingAction::Longitudinal || action == OperatingAction::Lateral ||
		                          action == OperatingAction::ManualSteering || action == OperatingAction::Boost))
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
