module;
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.control.execution;
import phoenix.control.coordination;
import phoenix.effects.contracts;

namespace phoenix
{
Json save_controllers(const EntityControls &controls)
{
	Json result{{"profiles", controls.profiles}, {"controllers", Json::array()}};
	for (const auto &controller : controls.controllers)
	{
		Json row{{"id", controller.definition.id},
		         {"console", controller.definition.console},
		         {"generation", controller.generation},
		         {"human", controller.human},
		         {"commands", Json::array()}};
		if (!controller.human_commands.empty())
			row["human_commands"] = controller.human_commands;
		if (!controller.definition.widget.empty())
			row["human_operations"] = controller.human_operations;
		for (const auto &command : controller.commands)
			row["commands"].push_back({{"value", command.value},
			                           {"arguments", command.arguments},
			                           {"revision", command.revision},
			                           {"consumed", command.consumed},
			                           {"request_id", command.request_id},
			                           {"accepted", command.accepted},
			                           {"target_override", command.target_override}});
		if (controller.brain)
			row["brain"] = state_tree::save(controller.brain->program, controller.brain->continuation);

		row["result"] = {{"available", controller.result.available},
		                 {"success", controller.result.success},
		                 {"id", controller.result.id},
		                 {"publication", controller.result.publication},
		                 {"message", controller.result.message},
		                 {"reason", controller.result.reason}};
		row["model"] = {{"selected", controller.selected_provider}, {"effective", controller.effective_provider},
		                {"reason", controller.fallback_reason},     {"generation", controller.provider_generation},
		                {"sequence", controller.decision_sequence}, {"next", controller.next_decision},
		                {"retries", controller.retry_count},        {"last", controller.last_decision_tick},
		                {"history", controller.decision_history}};
		row["effects"] = controller.effects;
		if (!controller.action.id.empty())
			row["action"] = save_controller_action(controller);
		if (!controller.incoming_requests.empty() || !controller.outgoing_requests.empty() ||
		    !controller.coordination_receipts.empty())
			row["coordination"] = save_controller_coordination(controller);
		result["controllers"].push_back(std::move(row));
	}
	return result;
}
void restore_controllers(EntityControls &controls, const Json &saved, const ControllerArgumentCheck &arguments_valid,
                         std::span<const KnowledgeId> known_peers)
{
	const auto fail = [](bool valid)
	{
		if (!valid)
			throw std::runtime_error("Saved Controller state differs from layout or bounds");
	};
	auto profiles = saved.at("profiles").get<std::map<std::string, std::size_t>>();
	fail(profiles.size() == controls.profiles.size());
	for (const auto &[console, profile] : profiles)
		fail(controls.profiles.contains(console) && profile < controls.layout.profiles.size());
	for (const auto &[console, profile] : controls.layout.fixed_profiles)
		fail(profiles.at(console) == profile);
	for (const auto &[console, group] : controls.layout.complexity_groups)
		for (const auto &[other, other_group] : controls.layout.complexity_groups)
			if (group == other_group)
				fail(profiles.at(console) == profiles.at(other));
	fail(saved.at("controllers").is_array() && saved.at("controllers").size() == controls.controllers.size());
	for (std::size_t i = 0; i < controls.controllers.size(); ++i)
	{
		auto &controller = controls.controllers[i];
		const auto &row = saved.at("controllers").at(i);
		fail(row.at("id") == controller.definition.id && row.at("console") == controller.definition.console);
		fail(row.at("generation").is_number_unsigned() && row.at("generation").get<std::uint64_t>() > 0);
		controller.generation = row.at("generation").get<std::uint64_t>();
		fail(row.at("human").is_boolean());
		controller.human = row.at("human").get<bool>();
		controller.human_commands.clear();
		controller.human_operations.clear();
		fail(!row.contains("human_operations") ||
		     (row.at("human_operations").is_array() &&
		      row.at("human_operations").size() <= controller.definition.operations.size()));
		for (const auto &operation : row.value("human_operations", Json::array()))
		{
			fail(operation.is_string() && controller.definition.operations.contains(operation.get<std::string>()));
			fail(controller.human_operations.insert(operation.get<std::string>()).second);
		}
		if (row.contains("human_commands"))
		{
			const auto &ownership = row.at("human_commands");
			fail(ownership.is_array() && ownership.size() == controller.commands.size());
			for (const auto &owned : ownership)
			{
				fail(owned.is_boolean());
				controller.human_commands.push_back(owned.get<bool>());
			}
		}
		fail(row.at("commands").is_array() && row.at("commands").size() == controller.commands.size());
		for (std::size_t j = 0; j < controller.commands.size(); ++j)
		{
			auto &state = controller.commands[j];
			const auto &spec = controller.definition.commands[j];
			const auto &value = row.at("commands").at(j);
			fail(value.at("value").is_number_integer() && value.at("value") >= spec.minimum &&
			     value.at("value") <= spec.maximum);
			state.value = value.at("value").get<std::int64_t>();
			fail(spec.choices.empty() || spec.choices.contains(state.value));
			const auto arguments = value.value("arguments", Json::array());
			fail(arguments.is_array() && arguments.size() <= 3);
			state.arguments.clear();
			for (const auto &argument : arguments)
			{
				fail(argument.is_number_integer() && argument >= -1000000000 && argument <= 1000000000);
				state.arguments.push_back(argument.get<std::int64_t>());
			}
			const auto controller_index = static_cast<std::size_t>(&controller - controls.controllers.data());
			const auto channel_index = static_cast<std::size_t>(&state - controller.commands.data());
			fail(arguments_valid(controller_index, channel_index, spec.action, state.value, state.arguments));
			for (const auto *key : {"revision", "consumed", "request_id"})
				fail(value.at(key).is_number_unsigned());
			state.revision = value.at("revision").get<std::uint64_t>();
			state.consumed = value.at("consumed").get<std::uint64_t>();
			state.request_id = value.at("request_id").get<std::uint64_t>();
			fail(state.revision > 0 && state.consumed <= state.revision &&
			     (spec.semantics == CommandSemantics::Request || state.request_id == 0));
			fail(value.at("accepted").is_boolean());
			state.accepted = value.at("accepted").get<bool>();
			state.target_override = value.value("target_override", false);
			fail(!state.target_override || !spec.shared_target_controller.empty());
		}
		fail(!row.contains("timed") && row.contains("brain") == controller.brain.has_value());
		if (controller.brain)
			controller.brain->continuation = state_tree::load(controller.brain->program, row.at("brain"));
		if (row.contains("model"))
		{
			const auto &m = row.at("model");
			controller.selected_provider = m.at("selected");
			controller.effective_provider = m.at("effective");
			for (const auto *p : {&controller.selected_provider, &controller.effective_provider})
				fail(*p == "tree" || *p == "anyjev" || *p == "llamacpp" || *p == "jev");
			fail(controller.effective_provider == "tree" ||
			     controller.effective_provider == controller.selected_provider);
			fail(controller.selected_provider == "tree" || !controller.candidates.empty());
			controller.provider_generation =
			    checked_unsigned(m.at("generation"), 1, 9007199254740991ULL, "model generation");
			controller.decision_sequence =
			    checked_unsigned(m.at("sequence"), 0, 9007199254740991ULL, "decision sequence");
			controller.next_decision = checked_unsigned(m.at("next"), 0, 9007199254740991ULL, "decision tick");
			controller.last_decision_tick = checked_unsigned(m.at("last"), 0, 9007199254740991ULL, "last decision");
			controller.retry_count = checked_unsigned(m.at("retries"), 0, 5, "decision retries");
			controller.fallback_reason = m.at("reason");
			fail(controller.fallback_reason.size() <= 256);
			controller.decision_history = m.at("history");
			fail(controller.decision_history.is_array() && controller.decision_history.size() <= 16 &&
			     controller.decision_history.dump().size() <= 8192);
		}
		if (row.contains("action"))
			restore_controller_action(controller, row.at("action"));
		else
			controller.action = {};
		if (row.contains("coordination"))
			restore_controller_coordination(controller, row.at("coordination"), known_peers);
		else
		{
			controller.incoming_requests.clear();
			controller.outgoing_requests.clear();
			controller.coordination_receipts.clear();
		}

		const auto &result = row.at("result");
		fail(result.at("available").is_boolean() && result.at("success").is_boolean() &&
		     result.at("id").is_number_unsigned() && result.at("publication").is_number_unsigned() &&
		     result.at("message").is_number_unsigned() && result.at("reason").is_string() &&
		     result.at("reason").get_ref<const std::string &>().size() <= 4096);
		controller.result = {result.at("available").get<bool>(),
		                     result.at("success").get<bool>(),
		                     result.at("reason").get<std::string>(),
		                     result.at("id").get<std::uint64_t>(),
		                     result.at("publication").get<std::uint64_t>(),
		                     result.at("message").get<std::uint64_t>()};
		fail((!controller.result.publication || controller.result.publication == controller.result.id) &&
		     (!controller.result.message || controller.result.message == controller.result.id) &&
		     !(controller.result.publication && controller.result.message) &&
		     controller.result.id <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
		const auto &effects = row.at("effects");
		fail(effects.is_array() && effects.size() <= effect_receipt_limit);
		for (const auto &effect : effects)
		{
			fail(effect.is_string() && !effect.get_ref<const std::string &>().empty() &&
			     effect.get_ref<const std::string &>().size() <= 2048);
			fail(controller.effects.insert(effect.get<std::string>()).second);
		}
	}
	controls.profiles = std::move(profiles);
}

} // namespace phoenix
