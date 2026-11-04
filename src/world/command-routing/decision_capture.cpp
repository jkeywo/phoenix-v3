module;
#include "native_headers.hpp"
module phoenix.simulation;
import :command_routing;
namespace phoenix::world_detail
{
auto Runtime::capture_controller_decision(const ControllerAddress &recipient, const ConsoleAddress &address,
                                          const DecisionObservation *projected,
                                          const GameplayKnowledgeProjection *projected_board)
    -> ControllerDecisionCapture
{
	ControllerDecisionCapture result;
	if (projected)
		result.borrowed_observation = projected;
	else if (const auto found = decision_frame.observations().find(address);
	         found != decision_frame.observations().end())
		result.borrowed_observation = &found->second;
	else
		result.owned_observation = capture_decision_observation(address);
	if (projected_board)
		result.borrowed_board = projected_board;
	else if (const auto found = decision_frame.knowledge().find(address); found != decision_frame.knowledge().end())
		result.borrowed_board = &found->second;
	else
		result.owned_board = knowledge.project(address);
	if (auto restricted = knowledge.restrict_projection(result.board(), knowledge.controller_readers(recipient)))
		result.owned_board = std::move(restricted);
	if (auto inspected = controller_inspection_observation(result.observation(), result.board(), knowledge.definition(),
	                                                       ecs.get<Clock>().tick))
		result.owned_observation = std::move(inspected);
	return result;
}
} // namespace phoenix::world_detail
