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
bool model_action_available(const ConsoleControllerRuntime &controller, const std::string &name,
                            std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> observed)
{
	for (const auto &spec : controller.actions)
		if (spec.id == name && spec.inputs.size() == inputs.size())
		{
			std::vector<state_tree::Value> values(observed.begin(), observed.end());
			for (std::size_t i = 0; i < inputs.size(); ++i)
				values.at(spec.inputs[i]) = inputs[i];
			state_tree::Instance check;
			const auto result = state_tree::step(spec.availability, check, values);
			return !result.debug.active.empty() && result.debug.active.back() == "available";
		}
	return false;
}
Json model_candidates(const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observed)
{
	Json result = Json::array();
	if (!controller.brain)
		return result;
	for (const auto &candidate : controller.candidates)
	{
		state_tree::Instance instance;
		const auto checked = state_tree::step(candidate.program, instance, observed);
		if (checked.debug.active.empty() || checked.debug.active.back() != "offer")
			continue;
		const auto evaluated = state_tree::step(candidate.program, instance, observed);
		if (evaluated.outputs.empty())
			continue;
		Json operations = Json::array();
		bool available = true;
		for (const auto &output : evaluated.outputs)
		{
			const auto &name = candidate.program.registry.actions.at(output.action).name;
			if (name.starts_with("select.") || name.starts_with("replace."))
				available =
				    available && model_action_available(controller, name.substr(name.starts_with("select.") ? 7 : 8),
				                                        std::span(output.arguments).subspan(1), observed);
			Json arguments = Json::array();
			for (const auto &v : output.arguments)
				arguments.push_back(model_value(v));
			operations.push_back({{"name", name}, {"arguments", arguments}});
			if (name.starts_with("select.") || name.starts_with("replace."))
				for (const auto &spec : controller.actions)
					if (spec.id == name.substr(name.starts_with("select.") ? 7 : 8))
					{
						operations.back()["label"] = spec.label;
						Json readable = Json::object();
						for (std::size_t i = 0; i < spec.inputs.size(); ++i)
						{
							const auto &value = output.arguments[i + 1];
							readable[spec.program.registry.observations.at(spec.inputs[i]).name.substr(6)] =
							    value.type == state_tree::Type::Text      ? Json(value.text)
							    : value.type == state_tree::Type::Boolean ? Json(value.number != 0)
							                                              : Json(value.number);
						}
						operations.back()["inputs"] = readable;
					}
		}
		if (available)
			result.push_back({{"id", candidate.id},
			                  {"description", candidate.description},
			                  {"deviation", candidate.deviation},
			                  {"operations", operations}});
	}
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
	          {"alert", bridge.condition}}},
	        {"coordination", save_controller_coordination(controller)}};
}
Json build_controller_snapshot(const ControllerAddress &address, KnowledgeTick tick,
                               const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> observed,
                               Json board, KnowledgeId actor, const BridgeBlackboard &bridge)
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
	// Oversized context is refused, never silently stripped of uncertainty or orders.
	if (result.dump().size() > 48000)
		return nullptr;
	return result;
}

} // namespace phoenix
