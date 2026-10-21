module;
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
std::vector<ControllerActionSpec> compile_controller_actions(const Json &source, const Catalog &catalog, const state_tree::Registry &registry)
{
	const auto tree = resolve_tree_definition(source, catalog);
	std::vector<ControllerActionSpec> result;
	std::set<std::string> names;
	for (const auto &binding : tree.value("action_bindings", Json::array()))
	{
		ControllerActionSpec spec;
		spec.id = binding.at("id"); spec.tree = binding.at("tree");
		if (!names.insert(spec.id).second || result.size() >= 64) throw std::runtime_error("Duplicate/excessive Controller actions");
		const auto definition = resolve_tree_definition(catalog.trees.at(spec.tree), catalog);
		const auto &metadata = definition.at("controller_action");
		spec.label = metadata.at("label");
		if (spec.label.empty() || spec.label.size() > 256) throw std::runtime_error("Controller action needs a bounded label");
		std::set<std::string> inputs;
		for (const auto &input : metadata.value("inputs", Json::array()))
		{
			const auto name = input.at("name").get<std::string>();
			if (!inputs.insert(name).second || inputs.size() > 32) throw std::runtime_error("Duplicate/excessive action inputs");
			const auto found = std::find_if(registry.observations.begin(), registry.observations.end(), [&](const auto &f) { return f.name == "input." + name; });
			if (found == registry.observations.end()) throw std::runtime_error("Unbound action input");
			spec.inputs.push_back(found - registry.observations.begin());
		}
		spec.program = state_tree::compile(definition, registry, catalog.trees);
		std::function<void(const state_tree::Program &)> validate = [&](const auto &program)
		{
			for (const auto &state : program.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &task : *tasks)
						if (task.kind == state_tree::TaskKind::Action &&
						    (registry.actions.at(task.target).name.starts_with("select.") || registry.actions.at(task.target).name == "action.cancel"))
							throw std::runtime_error("Action execution cannot invoke its scheduler");
			for (const auto &child : program.references) validate(child);
		};
		validate(spec.program);
		Json available{{"context", "controller"}, {"initial", "check"}, {"states", Json::array({
			{{"id", "check"}, {"transitions", Json::array({{{"target", "available"}, {"conditions", metadata.value("available", Json::array())}}})}},
			{{"id", "available"}}
		})}};
		spec.availability = state_tree::compile(available, registry);
		result.push_back(std::move(spec));
	}
	if (!result.empty())
	{
		const auto policy = state_tree::compile(tree, registry, catalog.trees);
		std::function<void(const state_tree::Program &)> check = [&](const auto &p)
		{
			for (const auto &s : p.states) for (const auto *tasks : {&s.entry, &s.running, &s.exit}) for (const auto &t : *tasks)
				if (t.kind == state_tree::TaskKind::Action && (registry.actions.at(t.target).name.starts_with("write.") || registry.actions.at(t.target).name == "action.succeed" || registry.actions.at(t.target).name == "action.fail"))
					throw std::runtime_error("Action policy cannot write commands or complete its executor");
			for (const auto &child : p.references) check(child);
		};
		check(policy);
	}
	return result;
}
}
