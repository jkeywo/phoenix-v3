module;
#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
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
	for (const auto &spec : layout.controllers)
	{
		ConsoleControllerRuntime controller;
		controller.definition = spec;
		for (const auto &command : spec.commands)
			controller.commands.push_back({command.initial, 1, 0, 0, true, command.initial_arguments});
		if (!spec.widget.empty())
			controller.human_commands.resize(spec.commands.size(), false);
		if (!spec.tree.empty())
		{
			if (content.tree_programs.contains(spec.tree))
			{
				controller.brain = ControllerBrain{content.tree_programs.at(spec.tree), spec.tree};
				auto &brain = *controller.brain;
				brain.model_context = resolve_tree_definition(content.trees.at(spec.tree), content)
				                          .value("decision_context", std::string{});
				controller.actions =
				    compile_controller_actions(content.trees.at(spec.tree), content, brain.program.registry);
				controller.candidates =
				    compile_controller_candidates(content.trees.at(spec.tree), content, brain.program.registry);
				const auto &bindings = content.controller_bindings.at(spec.tree);
				brain.decision_base = bindings.decision_base;
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
				}
				brain.ftl_system = bindings.ftl_system;
				brain.inbox = bindings.inbox;
				brain.result = bindings.result;
				brain.telemetry_reads = bindings.telemetry;
				brain.abilities = bindings.abilities;
				brain.processes = bindings.processes;
				for (const auto &read : bindings.command_reads)
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
					brain.command_reads.push_back({static_cast<std::size_t>(target - layout.controllers.begin()),
					                               static_cast<std::size_t>(channel - target->commands.begin()),
					                               read.destination});
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
					read.priority = binding.subject_parameter == "priority_or_latest";
					read.latest = binding.subject_parameter == "latest" || read.priority;
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
