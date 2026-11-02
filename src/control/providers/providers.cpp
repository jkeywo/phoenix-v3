module;
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
#include <utility>

module phoenix.control.providers;
import phoenix.control.coordination;

namespace phoenix
{
static Json model_value(const state_tree::Value &v)
{
	return {{"type", static_cast<int>(v.type)}, {"number", v.number}, {"text", v.text}};
}
bool model_action_available(const ConsoleControllerRuntime &controller, std::size_t action,
                            std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> observed)
{
	if (action >= controller.actions.size())
		return false;
	const auto &spec = controller.actions[action];
	if (spec.inputs.size() != inputs.size())
		return false;
	std::vector<state_tree::Value> values(observed.begin(), observed.end());
	for (std::size_t i = 0; i < inputs.size(); ++i)
		values.at(spec.inputs[i]) = inputs[i];
	state_tree::Instance check;
	const auto result = state_tree::step(spec.availability, check, values);
	return !result.debug.active.empty() && result.debug.active.back() == "available";
}
std::vector<ModelCandidate> evaluate_model_candidates(const ConsoleControllerRuntime &controller,
                                                      std::span<const state_tree::Value> observed)
{
	std::vector<ModelCandidate> result;
	// Provider advice cannot replace the fixed objective arbitration contract.
	if (!controller.brain || !controller.brain->objective_bindings.empty())
		return result;
	for (std::size_t i = 0; i < controller.candidates.size(); ++i)
	{
		const auto &candidate = controller.candidates[i];
		state_tree::Instance instance;
		const auto checked = state_tree::step(candidate.program, instance, observed);
		if (checked.debug.active.empty() || checked.debug.active.back() != "offer")
			continue;
		const auto evaluated = state_tree::step(candidate.program, instance, observed);
		if (evaluated.outputs.empty())
			continue;
		ModelCandidate prepared{candidate.id, candidate.description, candidate.deviation};
		bool available = true;
		for (const auto &output : evaluated.outputs)
		{
			const auto &binding = controller.brain->candidate_operations.at(i).at(output.action);
			if (binding.kind == ControllerCandidateOperationKind::Select ||
			    binding.kind == ControllerCandidateOperationKind::Replace)
				available = available && binding.action &&
				            model_action_available(controller, *binding.action, std::span(output.arguments).subspan(1),
				                                   observed);
			prepared.operations.push_back({binding.kind, binding.name, binding.action, output.arguments});
		}
		if (available)
			result.push_back(std::move(prepared));
	}
	return result;
}
Json model_candidate_json(const ConsoleControllerRuntime &controller, const ModelCandidate &candidate)
{
	Json operations = Json::array();
	for (const auto &operation : candidate.operations)
	{
		Json arguments = Json::array();
		for (const auto &value : operation.arguments)
			arguments.push_back(model_value(value));
		Json row{{"name", operation.name}, {"arguments", arguments}};
		if (operation.action)
		{
			const auto &spec = controller.actions.at(*operation.action);
			row["label"] = spec.label;
			Json readable = Json::object();
			for (std::size_t i = 0; i < spec.inputs.size(); ++i)
			{
				const auto &value = operation.arguments.at(i + 1);
				readable[spec.program.registry.observations.at(spec.inputs[i]).name.substr(6)] =
				    value.type == state_tree::Type::Text      ? Json(value.text)
				    : value.type == state_tree::Type::Boolean ? Json(value.number != 0)
				                                              : Json(value.number);
			}
			row["inputs"] = std::move(readable);
		}
		operations.push_back(std::move(row));
	}
	return {{"id", candidate.id},
	        {"description", candidate.description},
	        {"deviation", candidate.deviation},
	        {"operations", operations}};
}
Json model_candidates(const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observed)
{
	Json result = Json::array();
	for (const auto &candidate : evaluate_model_candidates(controller, observed))
		result.push_back(model_candidate_json(controller, candidate));
	return result;
}

Json model_basis(const BridgeBlackboard &bridge, const ConsoleControllerRuntime &controller)
{
	return {{"generation", controller.generation},
	        {"provider_generation", controller.provider_generation},
	        {"action_serial", controller.action.serial},
	        {"action_status", controller.action.status},
	        {"orders",
	         {{"revision",
	           (bridge.epochs.contains(controller.definition.console) ? bridge.epochs.at(controller.definition.console)
	                                                                  : 0)},
	          {"priority_revision", bridge.priority_revision},
	          {"priorities", bridge.priority_objectives},
	          {"alert", bridge.condition}}},
	        {"coordination", save_controller_coordination(controller)}};
}
Json build_controller_snapshot(const ControllerAddress &address, KnowledgeTick tick,
                               const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observed,
                               Json board, KnowledgeId actor, const BridgeBlackboard &bridge, bool full_diagnostics)
{
	auto all = model_candidates(controller, observed);
	const auto total = all.size();
	// Candidate authors order standing commitments first. Preserve their current action
	// independently through the always-present continue option.
	if (all.size() > 24)
		all.erase(all.begin() + 24, all.end());
	all.push_back({{"id", "continue"}, {"description", "Continue the current activity and commitments."}});
	all.push_back({{"id", "defer_to_tree"}, {"description", "Ask the authored officer policy to decide."}});
	Json facts = Json::object();
	for (std::size_t i = 0; i < observed.size(); ++i)
	{
		const auto &v = observed[i];
		const auto &name = controller.brain->program.registry.observations[i].name;
		if (v.type == state_tree::Type::Missing || name.starts_with("input."))
			continue;
		facts[name] = v.type == state_tree::Type::Text      ? Json(v.text)
		              : v.type == state_tree::Type::Boolean ? Json(v.number != 0)
		                                                    : Json(v.number);
	}
	for (const auto *key : {"messages", "notifications"})
	{
		Json visible = Json::array();
		for (const auto &message : board.at(key))
			if (message.at("destination_kind") != static_cast<unsigned>(MessageDestinationKind::Controller) ||
			    message.at("destination") == actor || message.value("public", false))
				visible.push_back(message);
		board[key] = std::move(visible);
	}
	Json result{{"schema", 1},
	            {"ship", address.entity},
	            {"console", address.console},
	            {"controller", address.controller},
	            {"tick", tick},
	            {"expires", tick + 100},
	            {"sequence", controller.decision_sequence + 1},
	            {"provider", controller.selected_provider},
	            {"basis", model_basis(bridge, controller)},
	            {"facts", facts},
	            {"objective_candidates", controller.objective_diagnostics},
	            {"selected_objective", controller.selected_objective},
	            {"role", controller.definition.id},
	            {"mechanics", "Actions request work; command acceptance is not physical success. Distances are "
	                          "millimetres, headings millidegrees, time simulation ticks at 20 Hz."},
	            {"history", controller.decision_history},
	            {"candidates", all},
	            {"omitted_candidates", total > 24 ? total - 24 : 0}};
	Json omitted = Json::object();
	for (const auto &[key, limit] :
	     std::vector<std::pair<std::string, std::size_t>>{{"records", 32}, {"shared", 16}, {"messages", 16}})
	{
		auto &items = board[key];
		omitted[key] = items.size() > limit ? items.size() - limit : 0;
		if (items.size() > limit)
			items.erase(items.begin(), items.end() - limit);
	}
	for (auto &message : board["messages"])
	{
		message.erase("speaker_avatar");
		message.erase("speaker_portrait");
	}
	board.erase("notifications");
	board["omitted"] = omitted;
	result["role_context"] = controller.brain->model_context;
	result["current_action"] = {{"id", controller.action.id},
	                            {"invocation", controller.action.invocation},
	                            {"status", controller.action.status},
	                            {"reason", controller.action.reason},
	                            {"inputs", Json::array()}};
	for (const auto &input : controller.action.inputs)
		result["current_action"]["inputs"].push_back(model_value(input));
	// These are the same permitted records and messages supplied to this Console.
	result["evidence"] = board;
	// Detailed diagnostic rows are redundant with the scored choice, not authority
	// or evidence. Bound provider context without hiding that rows were omitted.
	auto &diagnostics = result["objective_candidates"];
	std::size_t omitted_diagnostics = 0;
	if (!full_diagnostics && diagnostics.size() > 16)
	{
		Json bounded = Json::array();
		for (const auto &row : diagnostics)
			if (row.value("selected", false))
				bounded.push_back(row);
		for (const auto &row : diagnostics)
			if (!row.value("selected", false) && bounded.size() < 16)
				bounded.push_back(row);
		omitted_diagnostics = diagnostics.size() - bounded.size();
		diagnostics = std::move(bounded);
	}
	result["omitted_objective_candidates"] = omitted_diagnostics;
	if (!full_diagnostics)
		while (result.dump().size() > 48000 && !diagnostics.empty() && !diagnostics.back().value("selected", false))
		{
			diagnostics.erase(diagnostics.end() - 1);
			result["omitted_objective_candidates"] = ++omitted_diagnostics;
		}
	// Essential permitted evidence, uncertainty and accepted orders are never
	// silently removed to fit a model. Tools can request the full diagnostic view.
	if (!full_diagnostics && result.dump().size() > 48000)
		return nullptr;
	return result;
}

} // namespace phoenix
