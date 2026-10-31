module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
std::optional<ControllerObjectiveChoice> evaluate_controller_objectives(const ConsoleControllerRuntime &controller,
                                                                        const ControllerObservationSources &input,
                                                                        std::span<const state_tree::Value> source,
                                                                        Json &diagnostics)
{
	using state_tree::Value;
	const auto &brain = *controller.brain;
	const auto schema = input.definitions.schema("phoenix.objective");
	const auto field = [&](const KnowledgeRecord &record, const char *name) -> const KnowledgeValue *
	{
		const auto value = record.value.fields.find(input.definitions.field(schema, name));
		return value == record.value.fields.end() ? nullptr : &value->second;
	};
	const auto text = [&](const KnowledgeRecord &record, const char *name, const std::string &fallback)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : fallback;
	};
	const auto number = [&](const KnowledgeRecord &record, const char *name, std::int64_t fallback = 0)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::int64_t>(value) ? std::get<std::int64_t>(*value) : fallback;
	};
	std::vector<ObjectiveCandidate> candidates;
	std::vector<ControllerObjectiveChoice> choices;
	std::set<KnowledgeId> seen;
	for (auto record = input.board.shared.rbegin(); record != input.board.shared.rend(); ++record)
	{
		if (record->value.schema != schema || !seen.insert(record->value.subject).second)
			continue;
		const auto behavior = text(*record, "behavior", "observe");
		bool supported = false;
		for (const auto &binding : brain.objective_bindings)
		{
			if (binding.behavior != behavior)
				continue;
			supported = true;
			ObjectiveCandidate candidate;
			candidate.objective = record->value.subject;
			candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
			candidate.marked = input.priority_objectives.contains(candidate.objective);
			candidate.action = binding.id + "/" + binding.action;
			candidate.reason = record->value.withdrawn                   ? "withdrawn"
			                   : input.stale(*record, input.tick)        ? "stale_evidence"
			                   : text(*record, "status", "") != "active" ? "objective_inactive"
			                                                             : "binding_not_applicable";
			ControllerObjectiveChoice choice;
			choice.objective = candidate.objective;
			choice.action = binding.action;
			auto values = std::vector<Value>(source.begin(), source.end());
			for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
			{
				const auto &name = brain.program.registry.observations[i].name;
				if (!name.starts_with("objective.record."))
					continue;
				const auto key = name.substr(17);
				if (key == "id")
					values[i] = Value::integer(candidate.objective);
				else if (key == "behavior" || key == "tier")
					values[i] = Value::string(text(*record, key.c_str(), ""));
				else if (key == "age")
					values[i] = Value::integer(
					    std::min<std::uint64_t>(1000000, input.tick >= record->value.provenance.observed_tick
					                                         ? input.tick - record->value.provenance.observed_tick
					                                         : 0));
				else if (key == "target" || key == "urgency")
					values[i] = Value::integer(number(*record, key.c_str()));
				else if (const auto value = field(*record, key.c_str()); value && std::get_if<std::int64_t>(value))
					values[i] = Value::integer(std::get<std::int64_t>(*value));
				else
					values[i] = {};
			}
			if (!record->value.withdrawn && !input.stale(*record, input.tick) &&
			    text(*record, "status", "") == "active")
			{
				state_tree::Instance scratch;
				const auto offered = state_tree::step(binding.program, scratch, values);
				if (!offered.outputs.empty())
				{
					const auto &arguments = offered.outputs.front().arguments;
					candidate.target = arguments[0].number >= 0 ? arguments[0].number : 0;
					candidate.factors = {arguments[1].number, arguments[2].number, arguments[3].number,
					                     arguments[4].number, arguments[5].number};
					choice.target = candidate.target;
					choice.arguments.assign(arguments.begin() + 6, arguments.end());
					const auto action = std::find_if(controller.actions.begin(), controller.actions.end(),
					                                 [&](const auto &spec)
					                                 {
						                                 return spec.id == binding.action;
					                                 });
					candidate.reason = "execution_unavailable";
					if (action != controller.actions.end())
					{
						for (std::size_t i = 0; i < action->inputs.size(); ++i)
							values[action->inputs[i]] = choice.arguments[i];
						state_tree::Instance availability;
						const auto eligible = state_tree::step(action->availability, availability, values);
						candidate.feasible =
						    !eligible.debug.active.empty() && eligible.debug.active.back() == "available";
						bool writes = false, delegated = false;
						std::function<void(const state_tree::Program &)> capabilities;
						capabilities = [&](const auto &program)
						{
							std::function<void(const std::vector<state_tree::Task> &)> visit;
							visit = [&](const auto &tasks)
							{
								for (const auto &task : tasks)
								{
									if (task.kind == state_tree::TaskKind::Action)
									{
										const auto &name = program.registry.actions.at(task.target).name;
										if (name.starts_with("write."))
											for (std::size_t channel = 0;
											     channel < controller.definition.commands.size(); ++channel)
												if (controller.definition.commands[channel].id == name.substr(6))
												{
													writes = true;
													delegated |= !controller_command_human(controller, channel);
												}
									}
									for (const auto &branch : task.branches)
										visit(branch);
								}
							};
							for (const auto &state : program.states)
							{
								visit(state.entry);
								visit(state.running);
								visit(state.exit);
							}
							for (const auto &reference : program.references)
								capabilities(reference);
						};
						capabilities(action->program);
						if (writes && !delegated)
						{
							candidate.feasible = false;
							candidate.reason = "capabilities_unavailable";
						}
						if (candidate.feasible)
							candidate.reason = "permitted_action";
					}
					Json identity = Json::array();
					for (const auto &argument : choice.arguments)
						identity.push_back({static_cast<unsigned>(argument.type), argument.number, argument.text});
					const auto details = identity.dump();
					// A stable hash keeps the persisted invocation bounded. Ties
					// compare the complete argument identity, never hash order.
					std::uint64_t hash = 14695981039346656037ULL;
					for (const unsigned char byte : details)
					{
						hash ^= byte;
						hash *= 1099511628211ULL;
					}
					choice.invocation = "objective/" + std::to_string(choice.objective) + "/" +
					                    std::to_string(choice.target) + "/" + binding.id + "/" + choice.action + "/" +
					                    std::to_string(hash);
					choice.commitment = choice.invocation;
					candidate.action += "/" + details;
					candidate.committed =
					    controller.objective_commitment == choice.commitment && controller.action.status == "running" &&
					    controller.action.id == choice.action && controller.action.inputs == choice.arguments;
				}
			}
			const auto policy = brain.objective_scoring.find(text(*record, "scoring_policy", "default"));
			if (policy == brain.objective_scoring.end())
			{
				candidate.feasible = false;
				candidate.reason = "unsupported_scoring_policy";
			}
			else
			{
				const auto &w = policy->second;
				candidate.policy = {w[0], w[1], w[2], w[3], w[4], w[5]};
			}
			candidates.push_back(std::move(candidate));
			choices.push_back(std::move(choice));
		}
		if (!supported)
		{
			ObjectiveCandidate candidate;
			candidate.objective = record->value.subject;
			candidate.action = behavior;
			candidate.reason = "unsupported_behavior";
			candidates.push_back(std::move(candidate));
			choices.emplace_back();
		}
	}
	const auto selected = select_objective(candidates);
	diagnostics = Json::array();
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const auto &candidate = candidates[i];
		const auto score = score_objective(candidate);
		diagnostics.push_back({{"objective", candidate.objective},
		                       {"target", candidate.target},
		                       {"action", choices[i].action.empty() ? candidate.action : choices[i].action},
		                       {"tier", objective_tier_name(candidate.tier)},
		                       {"marked", candidate.marked},
		                       {"feasible", candidate.feasible},
		                       {"reason", candidate.reason},
		                       {"score", score.total},
		                       {"terms", score.terms},
		                       {"commitment", choices[i].commitment},
		                       {"selected", selected && *selected == i}});
	}
	return selected ? std::optional<ControllerObjectiveChoice>{choices[*selected]} : std::nullopt;
}
} // namespace phoenix
