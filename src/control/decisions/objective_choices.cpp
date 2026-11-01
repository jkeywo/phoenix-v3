module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.decisions;
import phoenix.gameplay.ordnance.contacts;
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
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
	std::map<std::size_t, Json> report_evidence;
	std::set<KnowledgeId> seen;
	CandidateCache cache{controller, input, source};
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
			const auto &adapter = candidate_adapter(binding.candidate_source);
			auto options = adapter.enumerate(cache, controller, input, binding, *record, source);
			if (options.empty())
				options.emplace_back();
			const auto before_binding = candidates.size();
			for (const auto &option : options)
			{
				auto values = option.complete && !option.values.empty()
				                  ? option.values
				                  : std::vector<Value>(source.begin(), source.end());
				const auto fact = [&](const char *name) -> const Value &
				{
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
						if (brain.program.registry.observations[i].name == name)
							return values[i];
					static const Value unavailable;
					return unavailable;
				};
				CandidateContext context{controller, input, binding, &*record, brain, behavior, values, option.values};
				if (!adapter.prepare(context))
					continue;
				ObjectiveCandidate candidate;
				candidate.objective = record->value.subject;
				candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
				candidate.marked = input.priority_objectives.contains(candidate.objective);
				candidate.action = binding.id + "/" + binding.action;
				candidate.reason = record->value.withdrawn                   ? "withdrawn"
				                   : input.stale(*record, input.tick)        ? "stale_evidence"
				                   : text(*record, "status", "") != "active" ? "objective_inactive"
				                                                             : "binding_not_applicable";
				adapter.describe(context, candidate);
				ControllerObjectiveChoice choice;
				choice.objective = candidate.objective;
				choice.action = binding.action;
				choice.binding = binding.id;
				for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
				{
					const auto &name = brain.program.registry.observations[i].name;
					if (!name.starts_with("objective.record."))
						continue;
					const auto key = name.substr(17);
					if (key == "marked")
						values[i] = Value::boolean(candidate.marked);
					else if (key == "id")
						values[i] = Value::integer(candidate.objective);
					else if (key == "revision")
						values[i] = Value::integer(record->value.provenance.revision);
					else if (key == "behavior" || key == "tier" || key == "operation" || key == "compartment" ||
					         key == "system" || key == "mission")
						values[i] = Value::string(text(*record, key.c_str(), key == "operation" ? behavior : ""));
					else if (key == "directive")
					{
						const auto value = field(*record, "directive");
						values[i] = Value::boolean(value && std::get_if<bool>(value) && std::get<bool>(*value));
					}
					else if (key == "age")
						values[i] = Value::integer(
						    std::min<std::uint64_t>(1000000, input.tick >= record->value.provenance.observed_tick
						                                         ? input.tick - record->value.provenance.observed_tick
						                                         : 0));
					// An omitted authored speed constraint means no limit, rather than
					// missing physical evidence. Unknown destinations still stay Missing.
					else if (key == "target" || key == "urgency" || key == "cruise_speed")
						values[i] = Value::integer(number(*record, key.c_str()));
					else if (const auto value = field(*record, key.c_str()); value && std::get_if<std::int64_t>(value))
						values[i] = Value::integer(std::get<std::int64_t>(*value));
					else if (const auto value = field(*record, key.c_str());
					         value && std::get_if<std::string>(value) &&
					         brain.program.registry.observations[i].type == state_tree::Type::Text)
						values[i] = Value::string(std::get<std::string>(*value));
					else
						values[i] = {};
				}
				if (!binding.navigation_mode.empty())
				{
					// Pure authored geometry over the frozen permitted candidate. No World lookup,
					// destination selection or hidden position extrapolation occurs here.
					const bool contact = adapter.contact_geometry;
					fixed::Vec point{contact ? fact("contact_candidate.x").number : number(*record, "destination_x"),
					                 contact ? fact("contact_candidate.y").number : number(*record, "destination_y")};
					const auto own = fixed::Vec{input.observation.own.x, input.observation.own.y};
					const auto bounded = [](fixed::Vec p)
					{
						return p.x >= -1000000000 && p.x <= 1000000000 && p.y >= -1000000000 && p.y <= 1000000000;
					};
					bool valid = bounded(point) && bounded(own) &&
					             (contact ? fact("contact_candidate.available").number != 0
					                      : field(*record, "destination_x") && field(*record, "destination_y"));
					if (binding.navigation_mode == "avoid")
						valid = bounded(own);
					auto stand_off = binding.navigation_distance;
					if (!binding.navigation_distance_parameter.empty())
						if (const auto parameter = field(*record, binding.navigation_distance_parameter.c_str()))
						{
							if (const auto number = std::get_if<std::int64_t>(parameter);
							    number && *number >= 0 && *number <= 1000000000)
								stand_off = *number;
							else
								valid = false;
						}
					std::int64_t distance = 0;
					if (valid)
					{
						const fixed::Vec delta{point.x - own.x, point.y - own.y};
						distance = fixed::length(delta);
						if (binding.navigation_mode == "avoid")
						{
							point =
							    own + fixed::scale(fixed::direction(fact("navigation.left_heading").number), stand_off);
							distance = stand_off;
						}
						else if (binding.navigation_mode == "relative")
						{
							const auto direction = fixed::direction(fact("contact_candidate.heading").number);
							point = {point.x + binding.navigation_forward * direction.x / fixed::unit -
							             binding.navigation_lateral * direction.y / fixed::unit,
							         point.y + binding.navigation_forward * direction.y / fixed::unit +
							             binding.navigation_lateral * direction.x / fixed::unit};
						}
						else if (binding.navigation_mode != "destination")
						{
							valid = distance > 0 || stand_off == 0;
							if (valid)
							{
								const fixed::Vec offset{distance ? delta.x * stand_off / distance : 0,
								                        distance ? delta.y * stand_off / distance : 0};
								const auto anchor = binding.navigation_mode == "withdraw" ? own : point;
								point = {anchor.x - offset.x, anchor.y - offset.y};
							}
						}
						valid = valid && bounded(point);
					}
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
					{
						const auto &name = brain.program.registry.observations[i].name;
						if (name == "navigation.x")
							values[i] = Value::integer(valid ? point.x : 0);
						else if (name == "navigation.y")
							values[i] = Value::integer(valid ? point.y : 0);
						else if (name == "navigation.distance")
							values[i] = Value::integer(distance);
						else if (name == "navigation.valid")
							values[i] = Value::boolean(valid);
					}
					if (!valid && candidate.reason == "binding_not_applicable")
						candidate.reason = "permitted_navigation_geometry_unavailable";
				}
				if (candidate.reason == "binding_not_applicable")
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
						choice.arguments.assign(arguments.begin() + 7, arguments.end());
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
						                    std::to_string(choice.target) + "/" + binding.id + "/" + choice.action +
						                    "/" + std::to_string(hash);
						choice.commitment = choice.invocation;
						choice.execution = "execution/" + choice.action + "/" + std::to_string(choice.target) + "/" +
						                   std::to_string(hash);
						choice.retry_ticks = binding.retry_ticks;
						std::uint64_t attempt = 0;
						if (const auto retry = controller.objective_retries.find(choice.execution);
						    retry != controller.objective_retries.end() && retry->second.action == choice.action &&
						    retry->second.arguments == choice.arguments)
						{
							attempt = retry->second.attempt;
							if (retry->second.not_before > input.tick)
							{
								candidate.feasible = false;
								candidate.reason = "execution_backoff";
							}
						}
						choice.invocation += "/attempt/" + std::to_string(attempt);
						candidate.action += "/" + details;
						candidate.committed =
						    arguments[6].number != 0 ||
						    (controller.objective_commitment == choice.commitment &&
						     controller.action.status == "running" && controller.action.id == choice.action &&
						     controller.action.inputs == choice.arguments);
					}
					else
						adapter.failed(context, candidate);
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
				if (auto evidence = adapter.evidence(context); !evidence.is_null())
					report_evidence[candidates.size()] = std::move(evidence);
				candidates.push_back(std::move(candidate));
				choices.push_back(std::move(choice));
			}
			if (candidates.size() == before_binding)
			{
				ObjectiveCandidate candidate;
				candidate.objective = record->value.subject;
				candidate.action = binding.id;
				candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
				candidate.marked = input.priority_objectives.contains(candidate.objective);
				candidate.reason = adapter.scope_reason;
				candidates.push_back(std::move(candidate));
				choices.emplace_back();
			}
		}
		if (!supported)
		{
			ObjectiveCandidate candidate;
			candidate.objective = record->value.subject;
			candidate.action = behavior;
			candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
			candidate.marked = input.priority_objectives.contains(candidate.objective);
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
		const auto retry = controller.objective_retries.find(choices[i].execution);
		const auto not_before = retry == controller.objective_retries.end() ? 0 : retry->second.not_before;
		const auto attempt = retry == controller.objective_retries.end() ? 0 : retry->second.attempt;
		diagnostics.push_back({{"objective", candidate.objective},
		                       {"target", candidate.target},
		                       {"action", choices[i].action.empty() ? candidate.action : choices[i].action},
		                       {"binding", choices[i].binding},
		                       {"tier", objective_tier_name(candidate.tier)},
		                       {"marked", candidate.marked},
		                       {"feasible", candidate.feasible},
		                       {"reason", candidate.reason},
		                       {"score", score.total},
		                       {"terms", score.terms},
		                       {"commitment", choices[i].commitment},
		                       {"retry_not_before", not_before},
		                       {"attempt", attempt},
		                       {"selected", selected && *selected == i}});
		if (const auto evidence = report_evidence.find(i); evidence != report_evidence.end())
			diagnostics.back()["evidence"] = evidence->second;
	}
	return selected ? std::optional<ControllerObjectiveChoice>{choices[*selected]} : std::nullopt;
}
} // namespace phoenix
