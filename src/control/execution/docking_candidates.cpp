module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.execution;
import phoenix.control.coordination;

namespace phoenix
{
void append_controller_docking_facts(const ConsoleControllerRuntime &controller,
                                     const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	using state_tree::Value;
	std::map<std::string, Value> facts{{"docking.available", Value::boolean(false)},
	                                   {"docking.follower", Value::boolean(false)},
	                                   {"docking.assisted", Value::boolean(false)},
	                                   {"docking.connection_target", Value::integer(0)},
	                                   {"docking.follower_phase", Value::string("unknown")}};
	const auto board = input.observation.gameplay.find("docking");
	if (board != input.observation.gameplay.end() && board->value("complete", false))
	{
		facts["docking.available"] = Value::boolean(true);
		facts["docking.follower_phase"] = Value::string("free");
		for (const auto &port : board->at("ports"))
			if (port.value("follower", false))
			{
				facts["docking.follower"] = Value::boolean(true);
				facts["docking.assisted"] = Value::boolean(port.value("assisted", false));
				facts["docking.connection_target"] = Value::integer(port.at("target"));
				facts["docking.follower_phase"] = Value::string(port.at("phase"));
			}
	}
	const auto &registry = controller.brain->program.registry.observations;
	for (std::size_t i = 0; i < registry.size(); ++i)
		if (const auto at = facts.find(registry[i].name); at != facts.end())
			values[i] = at->second;
}

std::vector<std::map<std::string, state_tree::Value>>
controller_docking_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                              const ControllerObjectiveBinding &binding, const KnowledgeRecord &goal)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	std::vector<Facts> result;
	const auto schema = input.definitions.schema("phoenix.objective");
	const auto field = goal.value.fields.find(input.definitions.field(schema, "target"));
	const auto target = field != goal.value.fields.end() && std::get_if<std::int64_t>(&field->second)
	                        ? std::get<std::int64_t>(field->second)
	                        : 0;
	const auto board = input.observation.gameplay.find("docking");
	const auto offer = [&](const Json &port, const Json *remote, std::size_t index)
	{
		const auto id = port.value("id", std::string{});
		const auto phase = port.value("phase", std::string("unknown"));
		const bool initiating = binding.docking_mode == "assist" || binding.docking_mode == "connect";
		const auto other = remote ? remote->at("entity").get<std::int64_t>() : port.value("target", std::int64_t{});
		bool current = false;
		std::int64_t distance = 0;
		for (const auto &contact : input.observation.contacts)
			if (contact.id == other)
			{
				current = true;
				distance = fixed::length({contact.x - input.observation.own.x, contact.y - input.observation.own.y});
			}
		const bool delegated = port.value("controller", std::string{}) == controller.definition.id &&
		                       controller_operation_owns(controller, "docking", {}) &&
		                       !controller_operation_human(controller, "docking");
		bool available = delegated;
		std::string reason = "permitted_docking_choice";
		if (!delegated)
		{
			available = false;
			reason = "docking_not_delegated";
		}
		else if (initiating)
		{
			bool compatible = false;
			if (remote)
				for (const auto &local : remote->value("local_ports", Json::array()))
					compatible |= local == id;
			if (!current)
			{
				available = false;
				reason = "no_current_docking_contact";
			}
			else if (!compatible || phase != "free" || !port.value("working", false))
			{
				available = false;
				reason = "docking_port_unavailable";
			}
		}
		else
		{
			const auto &mode = binding.docking_mode;
			available = phase != "free" && phase != "unknown" && (mode != "cancel" || phase == "approach") &&
			            (mode != "undock" || phase != "separating") &&
			            (mode != "emergency" || port.value("emergency", false));
			if (!available)
				reason = "docking_phase_unavailable";
		}
		const auto remote_port = remote ? remote->at("port").get<std::string>() : std::string{};
		const auto identity = !initiating && binding.docking_mode != "continue" && target ? target : other;
		result.push_back(
		    {{"docking_candidate.available", Value::boolean(available)},
		     {"docking_candidate.current", Value::boolean(current)},
		     {"docking_candidate.follower", Value::boolean(port.value("follower", false))},
		     {"docking_candidate.working", Value::boolean(port.value("working", false))},
		     {"docking_candidate.emergency", Value::boolean(port.value("emergency", false))},
		     {"docking_candidate.target", Value::integer(identity)},
		     {"docking_candidate.connection_target", Value::integer(port.value("target", std::int64_t{}))},
		     {"docking_candidate.distance", Value::integer(distance)},
		     {"docking_candidate.local_index", Value::integer(index + 1)},
		     {"docking_candidate.revision",
		      Value::integer(board == input.observation.gameplay.end() ? 0 : board->value("revision", std::int64_t{}))},
		     {"docking_candidate.port", Value::string(id)},
		     {"docking_candidate.remote_port", Value::string(remote_port)},
		     {"docking_candidate.remote", Value::string(remote ? std::to_string(other) + "/" + remote_port : "")},
		     {"docking_candidate.phase", Value::string(phase)},
		     {"docking_candidate.mode", Value::string(binding.docking_mode)},
		     {"docking_candidate.reason", Value::string(reason)}});
	};
	if (board != input.observation.gameplay.end())
		for (std::size_t i = 0; i < board->at("ports").size(); ++i)
		{
			const auto &port = board->at("ports")[i];
			if (port.at("controller") != controller.definition.id)
				continue;
			if (binding.docking_mode == "assist" || binding.docking_mode == "connect")
				for (const auto &remote : board->at("targets"))
					offer(port, &remote, i);
			else
				offer(port, nullptr, i);
		}
	if (result.empty())
	{
		offer(Json::object(), nullptr, 0);
		result.back()["docking_candidate.target"] = Value::integer(target);
		result.back()["docking_candidate.reason"] = Value::string("no_owned_docking_evidence");
	}
	return result;
}
} // namespace phoenix
