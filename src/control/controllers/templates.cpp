module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

module phoenix.control.templates;
import phoenix.content.controllers;
import phoenix.content.knowledge;

namespace phoenix
{
std::vector<ConsoleControllerRuntime> compile_controller_templates(const Catalog &content,
                                                                   const ControllableLayout &layout)
{
	std::vector<ConsoleControllerRuntime> templates;
	std::map<std::string, std::shared_ptr<const ControllerObservationLayout>> observation_layouts;
	for (const auto &spec : layout.controllers)
	{
		ConsoleControllerRuntime controller;
		controller.definition = spec;
		for (const auto &command : spec.commands)
			controller.commands.push_back({command.initial, 1, 0, 0, true, command.initial_arguments});
		controller.human_commands.resize(spec.commands.size(), false);
		if (!spec.tree.empty())
		{
			if (content.tree_programs.contains(spec.tree))
			{
				controller.brain = ControllerBrain{content.tree_programs.at(spec.tree), spec.tree};
				auto &brain = *controller.brain;
				auto &observation_layout = observation_layouts[spec.tree];
				if (!observation_layout)
				{
					auto compiled = std::make_shared<ControllerObservationLayout>();
					for (const auto &schema : content.knowledge_definition.schemas)
						if (schema.name == "phoenix.objective")
						{
							compiled->objective_schema = schema.key;
							for (const auto &entry : schema.fields)
								if (const auto semantic = compile_objective_field(entry.name))
									compiled->objective_keys[static_cast<std::size_t>(*semantic)] = entry.key;
						}
					for (std::size_t field = 0; field < brain.program.registry.observations.size(); ++field)
					{
						const auto &name = brain.program.registry.observations[field].name;
						compiled->fields.emplace(name, field);
						constexpr std::array<const char *, 3> action_inputs{"input.destination_x",
						                                                    "input.destination_y", "input.contact"};
						for (std::size_t i = 0; i < action_inputs.size(); ++i)
							if (name == action_inputs[i])
								compiled->action_inputs[i] = field;
						if (name == "bay.accepted_target")
							compiled->bay_accepted_target = field;
						if (name == "drive.approach_speed")
							compiled->manoeuvre[0] = field;
						if (name == "navigation.blocked")
							compiled->manoeuvre[1] = field;
						constexpr std::array<const char *, 5> docking_names{
						    "docking.available", "docking.follower", "docking.assisted", "docking.connection_target",
						    "docking.follower_phase"};
						for (std::size_t i = 0; i < docking_names.size(); ++i)
							if (name == docking_names[i])
								compiled->docking[i] = field;
						if (name == "summary.level")
							compiled->summary_level = field;
						if (name == "objective.status")
							compiled->objective_status = field;
						constexpr std::array<const char *, 4> travel_names{"travel.active", "travel.accepted",
						                                                   "travel.x", "travel.y"};
						for (std::size_t i = 0; i < travel_names.size(); ++i)
							if (name == travel_names[i])
								compiled->travel[i] = field;
						if (name.starts_with("objective.record."))
						{
							if (const auto semantic = compile_objective_field(name.substr(17)))
								compiled->compiled_objective_fields.push_back(
								    {field, *semantic, compiled->objective_keys[static_cast<std::size_t>(*semantic)],
								     brain.program.registry.observations[field].type});
						}
					}
					for (std::size_t candidate = 0; candidate < candidate_field_specs.size(); ++candidate)
						if (const auto field = compiled->fields.find(candidate_field_specs[candidate].name);
						    field != compiled->fields.end())
							compiled->candidate_slots[candidate] = field->second;
					observation_layout = std::move(compiled);
				}
				brain.observation_layout = observation_layout;
				// Actions and their private referenced trees compile against this same registry.
				// Resolve supported names through the completed index rather than scanning every field per command.
				for (std::size_t channel = 0; channel < spec.commands.size(); ++channel)
				{
					const auto prefix = "command." + spec.commands[channel].id;
					std::map<state_tree::Index, StagedCommandRead> reads;
					const auto bind =
					    [&](const std::string &name, ControllerCommandReadField kind, std::size_t argument = 0)
					{
						if (const auto field = observation_layout->fields.find(name);
						    field != observation_layout->fields.end())
							reads[static_cast<state_tree::Index>(field->second)] = {channel, argument, kind};
					};
					using F = ControllerCommandReadField;
					bind(prefix, F::Value);
					constexpr std::array<std::pair<const char *, F>, 5> suffixes{{{".accepted", F::Accepted},
					                                                              {".revision", F::Revision},
					                                                              {".consumed", F::Consumed},
					                                                              {".request_id", F::RequestId},
					                                                              {".pending", F::Pending}}};
					for (const auto &[suffix, kind] : suffixes)
						bind(prefix + suffix, kind);
					const auto arguments = operating_arguments(spec.commands[channel].action);
					for (std::size_t argument = 0; argument < arguments.size(); ++argument)
						bind(prefix + "." + std::string(arguments[argument].name), F::Argument, argument);
					// Preserve declared command precedence and argument-over-metadata precedence within each command.
					brain.staged_command_reads.insert(reads.begin(), reads.end());
				}

				brain.model_context = resolve_tree_definition(content.trees.at(spec.tree), content)
				                          .value("decision_context", std::string{});
				controller.actions =
				    compile_controller_actions(content.trees.at(spec.tree), content, brain.program.registry);
				controller.candidates =
				    compile_controller_candidates(content.trees.at(spec.tree), content, brain.program.registry);
				// Candidate outputs retain evaluator ordinals, with endpoint meaning compiled once.
				for (const auto &candidate : controller.candidates)
				{
					auto &operations = brain.candidate_operations.emplace_back();
					for (const auto &task : candidate.program.states.back().running)
					{
						const auto &name = candidate.program.registry.actions.at(task.target).name;
						RuntimeCandidateOperationBinding operation;
						operation.name = name;
						if (name.starts_with("select.") || name.starts_with("replace."))
						{
							const bool replace = name.starts_with("replace.");
							operation.kind = replace ? ControllerCandidateOperationKind::Replace
							                         : ControllerCandidateOperationKind::Select;
							const auto action = name.substr(replace ? 8 : 7);
							for (std::size_t i = 0; i < controller.actions.size(); ++i)
								if (controller.actions[i].id == action)
									operation.action = i;
							if (!operation.action)
								throw std::runtime_error("Controller candidate action endpoint is unavailable: " +
								                         name);
						}
						else if (name == "action.cancel")
							operation.kind = ControllerCandidateOperationKind::Cancel;
						else if (name == "acknowledge_message")
							operation.kind = ControllerCandidateOperationKind::Acknowledge;
						operations.emplace(task.target, std::move(operation));
					}
				}
				const auto &bindings = content.controller_bindings.at(spec.tree);
				brain.objective_scoring = bindings.objective_scoring;
				brain.default_objective_scoring = bindings.objective_scoring.at("default");
				brain.objective_bindings =
				    compile_controller_objectives(content.trees.at(spec.tree), content, brain.program.registry);
				// Resolve candidate adapters, objective fields and action endpoints once at loading.
				for (const auto &binding : brain.objective_bindings)
				{
					RuntimeObjectiveBinding runtime;
					runtime.source = compile_candidate_source(binding.candidate_source);
					for (std::size_t i = 0; i < controller.actions.size(); ++i)
						if (controller.actions[i].id == binding.action)
							runtime.action = i;
					if (!binding.navigation_distance_parameter.empty())
						runtime.distance_parameter = content.knowledge_definition.field(
						    brain.observation_layout->objective_schema, binding.navigation_distance_parameter);
					if (!binding.report_schema.empty())
					{
						runtime.report_schema = content.knowledge_definition.schema(binding.report_schema);
						const auto &schema = content.knowledge_definition.schemas.at(runtime.report_schema - 1);
						runtime.report_stale_after = schema.stale_after;
						constexpr std::array<const char *, 4> scope{"ship", "system", "compartment", "mission"};
						for (const auto &field : schema.fields)
						{
							for (std::size_t i = 0; i < scope.size(); ++i)
								if (field.name == scope[i])
									runtime.report_scope[i] = field.key;
							CompiledReportField compiled;
							compiled.key = field.key;
							compiled.type = field.type;
							compiled.required = field.required;
							compiled.selected = binding.report_fields.contains(field.name);
							const auto name = "report_payload." + binding.id + "." + field.name;
							compiled.value_slot = brain.observation_index(name);
							compiled.available_slot = brain.observation_index(name + ".available");
							runtime.report_fields.push_back(compiled);
						}
					}
					brain.compiled_objectives.push_back(std::move(runtime));
				}
				// Coordination support is a compiled policy contract, independent of current operator allocation.
				for (std::size_t i = 0; i < brain.objective_bindings.size(); ++i)
				{
					const auto &binding = brain.objective_bindings[i];
					const auto source = brain.compiled_objectives[i].source;
					brain.request_support[3] =
					    brain.request_support[3] ||
					    (spec.operations.contains("team_assignment") && binding.behavior == "repair" &&
					     (source == CandidateSource::repair || source == CandidateSource::requests));
					brain.request_support[4] =
					    brain.request_support[4] || (spec.operations.contains("contribution") &&
					                                 source == CandidateSource::requests && binding.behavior == "away");
					brain.request_support[5] = brain.request_support[5] ||
					                           (source == CandidateSource::scans &&
					                            std::any_of(spec.commands.begin(), spec.commands.end(),
					                                        [](const auto &command)
					                                        {
						                                        return command.action == OperatingAction::SensorTarget;
					                                        }));
					constexpr std::array<const char *, 3> operations{"power", "cooling", "manoeuvre"};
					for (std::size_t op = 0; op < operations.size(); ++op)
						if (source == CandidateSource::requests && binding.request_operation == operations[op])
							for (const auto &command : spec.commands)
								brain.request_support[op] =
								    brain.request_support[op] ||
								    (op == 0 && command.action == OperatingAction::GeneratorOutput) ||
								    (op == 1 && command.action == OperatingAction::CoolingAllocation) ||
								    (op == 2 && (command.action == OperatingAction::FollowContact ||
								                 command.action == OperatingAction::Heading ||
								                 command.action == OperatingAction::Speed));
				}
				for (const auto &action : controller.actions)
				{
					std::set<std::size_t> channels;
					std::function<void(const state_tree::Program &)> collect;
					collect = [&](const state_tree::Program &program)
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
										for (std::size_t i = 0; i < spec.commands.size(); ++i)
											if (spec.commands[i].id == name.substr(6))
												channels.insert(i);
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
							collect(reference);
					};
					collect(action.program);
					brain.action_write_channels.emplace_back(channels.begin(), channels.end());
				}
				brain.decision_base = bindings.decision_base;
				brain.report_observations = bindings.report_observations;
				brain.action_base = bindings.action_base;
				brain.coordination_base = bindings.coordination_base;
				brain.routine_stance =
				    content.trees.at(spec.tree).value("officer_routine_stance", std::string("routine"));
				brain.own = bindings.own;
				for (std::size_t field = 0; field < brain.program.registry.observations.size(); ++field)
				{
					const auto &name = brain.program.registry.observations[field].name;
					if (name == "logistics.revision")
						brain.logistics_revision = field;
					if (name == "docking.revision")
						brain.docking_revision = field;
					if (name == "console.stars")
						brain.complexity_stars = field;
					if (name == "medical.team")
						brain.medical_base = field;
					if (name == "workflow.mission")
						brain.workflow_base = field;
				}
				brain.ftl_system = bindings.ftl_system;
				brain.inbox = bindings.inbox;
				brain.result = bindings.result;
				for (const auto &read : bindings.telemetry)
				{
					const auto field = telemetry_field(read.source.field);
					if (!field)
						throw std::runtime_error("Controller telemetry has no compiled field ID: " + read.source.field);
					brain.telemetry_reads.push_back({read.source, read.destination, field});
				}
				for (const auto &binding : bindings.abilities)
				{
					RuntimeAbilityBinding runtime;
					static_cast<ControllerAbilityBinding &>(runtime) = binding;
					brain.abilities.push_back(std::move(runtime));
				}
				for (const auto &binding : bindings.processes)
				{
					RuntimeProcessBinding runtime;
					static_cast<ControllerProcessBinding &>(runtime) = binding;
					brain.processes.push_back(std::move(runtime));
				}
				for (const auto &read : bindings.command_reads)
				{
					const auto &binding = read.source;
					const auto target = std::find_if(
					    layout.controllers.begin(), layout.controllers.end(),
					    [&](const auto &candidate)
					    {
						    return candidate.id == binding.controller &&
						           candidate.console == (binding.console.empty() ? spec.console : binding.console);
					    });
					const auto channel = std::find_if(target->commands.begin(), target->commands.end(),
					                                  [&](const auto &candidate)
					                                  {
						                                  return candidate.id == binding.channel;
					                                  });
					brain.command_reads.push_back({static_cast<std::size_t>(target - layout.controllers.begin()),
					                               static_cast<std::size_t>(channel - target->commands.begin()),
					                               read.destination,
					                               *controller_command_read_source(channel->action, binding.field)});
				}
				for (const auto &read : bindings.contacts)
				{
					const auto &binding = read.source;
					const auto target =
					    std::find_if(layout.controllers.begin(), layout.controllers.end(),
					                 [&](const auto &candidate)
					                 {
						                 return candidate.id == binding.controller && candidate.console == spec.console;
					                 });
					const auto channel = std::find_if(target->commands.begin(), target->commands.end(),
					                                  [&](const auto &candidate)
					                                  {
						                                  return candidate.id == binding.channel;
					                                  });
					brain.contact_reads.push_back({static_cast<std::size_t>(target - layout.controllers.begin()),
					                               static_cast<std::size_t>(channel - target->commands.begin()),
					                               read.fields});
				}
				for (const auto &binding : bindings.commands)
					brain.commands.push_back(
					    {binding, static_cast<std::size_t>(std::find_if(spec.commands.begin(), spec.commands.end(),
					                                                    [&](const auto &command)
					                                                    {
						                                                    return command.id == binding.channel;
					                                                    }) -
					                                       spec.commands.begin())});
				for (const auto &binding : bindings.messages)
				{
					RuntimeRecordRead read;
					read.destination = binding.destination;
					read.schema = content.knowledge_definition.schema(binding.source.schema);
					read.field = content.knowledge_definition.field(read.schema, binding.source.field);
					brain.message_reads.push_back(read);
				}
				for (const auto &compiled : bindings.records)
				{
					const auto &binding = compiled.source;
					RuntimeRecordRead read;
					read.destination = compiled.destination;
					read.metadata = compiled.metadata;
					read.shared = binding.scope == GameplayTreeRecordScope::Ship;
					read.schema = content.knowledge_definition.schema(binding.schema);
					read.field = content.knowledge_definition.field(read.schema, binding.field);
					read.latest = binding.subject_parameter == "latest";
					if (!binding.subject_parameter.empty() && !read.latest)
						read.subject = std::stoull(binding.subject_parameter);
					controller.brain->reads.push_back(read);
				}
			}
		}
		templates.push_back(std::move(controller));
	}
	return templates;
}
} // namespace phoenix
