module;
#include <optional>
#include <array>
#include <utility>
#include <initializer_list>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.decisions;
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
void append_controller_docking_facts(const ConsoleControllerRuntime &controller,
                                     const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	using state_tree::Value;
	std::array<Value, 5> facts{Value::boolean(false), Value::boolean(false), Value::boolean(false), Value::integer(0),
	                           Value::string("unknown")};
	const auto &board = input.observation.docking;
	if (board.observed && board.complete)
	{
		facts[0] = Value::boolean(true);
		facts[4] = Value::string("free");
		for (const auto &port : board.ports)
			if (port.follower)
			{
				facts[1] = Value::boolean(true);
				facts[2] = Value::boolean(port.assisted);
				facts[3] = Value::integer(port.target);
				facts[4] = Value::string(port.phase);
			}
	}
	for (std::size_t i = 0; i < facts.size(); ++i)
		if (const auto slot = controller.brain->observation_layout->docking[i])
			values.at(*slot) = facts[i];
}

std::vector<CandidateFacts> controller_docking_candidates(const ConsoleControllerRuntime &controller,
                                                          const ControllerObservationSources &input,
                                                          const ControllerObjectiveBinding &binding,
                                                          const KnowledgeRecord &goal)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	std::vector<Facts> result;
	const auto target = objective_number(controller, input, goal, ObjectiveField::target);
	const auto &board = input.observation.docking;
	const auto offer = [&](const DockingPortDecision &port, const DockingTargetDecision *remote, std::size_t index)
	{
		const auto id = port.id;
		const auto phase = port.phase;
		const bool initiating = binding.docking_mode == "assist" || binding.docking_mode == "connect";
		const auto other = remote ? static_cast<std::int64_t>(remote->entity) : static_cast<std::int64_t>(port.target);
		bool current = false;
		std::int64_t distance = 0, travel = 0;
		for (const auto &contact : input.observation.contacts)
			if (contact.id == other)
			{
				current = true;
				distance = fixed::length({contact.x - input.observation.own.x, contact.y - input.observation.own.y});
				travel = distance / 1000;
				if (remote)
				{
					const auto rotate = [](const auto &p, std::int64_t heading)
					{
						const auto direction = fixed::direction(heading);
						const auto x = p.x_mm, y = p.y_mm;
						return fixed::Vec{x * direction.x / fixed::unit - y * direction.y / fixed::unit,
						                  x * direction.y / fixed::unit + y * direction.x / fixed::unit};
					};
					const auto heading = (contact.heading + remote->heading_mdeg + 180000 - port.heading_mdeg) % 360000;
					const auto destination =
					    fixed::Vec{contact.x, contact.y} + rotate(*remote, contact.heading) - rotate(port, heading);
					travel = fixed::length(destination - fixed::Vec{input.observation.own.x, input.observation.own.y}) /
					         1000;
				}
			}
		const bool delegated = port.controller == controller.definition.id &&
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
				for (const auto &local : remote->local_ports)
					compatible |= local == id;
			if (!current)
			{
				available = false;
				reason = "no_current_docking_contact";
			}
			else if (!compatible || phase != "free" || !port.working)
			{
				available = false;
				reason = "docking_port_unavailable";
			}
		}
		else
		{
			const auto &mode = binding.docking_mode;
			available = phase != "free" && phase != "unknown" && (mode != "cancel" || phase == "approach") &&
			            (mode != "undock" || phase != "separating") && (mode != "emergency" || port.emergency);
			if (!available)
				reason = "docking_phase_unavailable";
		}
		const auto remote_port = remote ? remote->port : std::string{};
		const auto identity = !initiating && binding.docking_mode != "continue" && target ? target : other;
		result.push_back(
		    {{CandidateField::docking_candidate_available, Value::boolean(available)},
		     {CandidateField::docking_candidate_current, Value::boolean(current)},
		     {CandidateField::docking_candidate_follower, Value::boolean(port.follower)},
		     {CandidateField::docking_candidate_working, Value::boolean(port.working)},
		     {CandidateField::docking_candidate_emergency, Value::boolean(port.emergency)},
		     {CandidateField::docking_candidate_target, Value::integer(identity)},
		     {CandidateField::docking_candidate_connection_target,
		      Value::integer(static_cast<std::int64_t>(port.target))},
		     {CandidateField::docking_candidate_distance, Value::integer(distance)},
		     {CandidateField::docking_candidate_travel, Value::integer(travel)},
		     {CandidateField::docking_candidate_local_index, Value::integer(index + 1)},
		     {CandidateField::docking_candidate_revision, Value::integer(board.observed ? board.revision : 0)},
		     {CandidateField::docking_candidate_port, Value::string(id)},
		     {CandidateField::docking_candidate_remote_port, Value::string(remote_port)},
		     {CandidateField::docking_candidate_remote,
		      Value::string(remote ? std::to_string(other) + "/" + remote_port : "")},
		     {CandidateField::docking_candidate_phase, Value::string(phase)},
		     {CandidateField::docking_candidate_mode, Value::string(binding.docking_mode)},
		     {CandidateField::docking_candidate_reason, Value::string(reason)}});
	};
	if (board.observed)
		for (std::size_t i = 0; i < board.ports.size(); ++i)
		{
			const auto &port = board.ports[i];
			if (port.controller != controller.definition.id)
				continue;
			if (binding.docking_mode == "assist" || binding.docking_mode == "connect")
				for (const auto &remote : board.targets)
					offer(port, &remote, i);
			else
				offer(port, nullptr, i);
		}
	if (result.empty())
	{
		offer(DockingPortDecision{}, nullptr, 0);
		result.back()[CandidateField::docking_candidate_target] = Value::integer(target);
		result.back()[CandidateField::docking_candidate_reason] = Value::string("no_owned_docking_evidence");
	}
	return result;
}
CandidateAdapter docks_adapter()
{
	return {"docks",
	        "objective_parameter_outside_known_docking_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(controller_docking_candidates(controller, input, binding, record), controller,
		                                source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::docks &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::docking_candidate_port).text) ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::docking_candidate_target).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::docks && candidate.reason == "binding_not_applicable" &&
		            !context.fact(CandidateField::docking_candidate_available).number)
			        candidate.reason = context.fact(CandidateField::docking_candidate_reason).text;
		        if (context.source() == CandidateSource::docks)
			        candidate.target = context.fact(CandidateField::docking_candidate_target).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.source() == CandidateSource::docks)
		        {
			        Json evidence;
			        for (const auto &[name, field] : std::initializer_list<std::pair<const char *, CandidateField>>{
			                 {"port", CandidateField::docking_candidate_port},
			                 {"remote", CandidateField::docking_candidate_remote},
			                 {"phase", CandidateField::docking_candidate_phase},
			                 {"mode", CandidateField::docking_candidate_mode},
			                 {"connection_target", CandidateField::docking_candidate_connection_target},
			                 {"follower", CandidateField::docking_candidate_follower},
			                 {"current", CandidateField::docking_candidate_current},
			                 {"distance", CandidateField::docking_candidate_distance},
			                 {"travel", CandidateField::docking_candidate_travel}})
			        {
				        const auto &value = context.fact(field);
				        if (value.type == state_tree::Type::Text)
					        evidence[name] = value.text;
				        else if (value.type == state_tree::Type::Boolean)
					        evidence[name] = value.number != 0;
				        else
					        evidence[name] = value.number;
			        }
			        return evidence;
		        }
		        return {};
	        }};
}
} // namespace phoenix
