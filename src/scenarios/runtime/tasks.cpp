module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <utility>

module phoenix.scenario;

namespace phoenix
{
ScenarioTaskSearch plan_scenario_task(const ScenarioDefinition &definition, const std::string &root,
                                      const ScenarioFacts &facts, std::uint64_t seed)
{
	ScenarioTaskSearch result;
	if (!seed)
		seed = definition.task_seed;
	std::function<bool(const std::string &, const std::string &, unsigned)> expand;
	expand = [&](const std::string &task, const std::string &path, unsigned depth)
	{
		if (depth >= definition.task_depth || result.nodes.size() >= definition.task_nodes)
		{
			result.exhausted = true;
			result.reason = "task depth/node budget exhausted";
			return false;
		}
		const auto found = definition.tasks.find(task);
		if (found == definition.tasks.end())
		{
			result.reason = "unknown task " + task;
			return false;
		}
		const auto &d = found->second;
		const auto checkpoint = result.nodes.size();
		std::vector<const ScenarioTaskMethod *> methods;
		for (const auto &method : d.methods)
			methods.push_back(&method);
		if (d.vary_methods)
		{
			const auto rank = [&](const ScenarioTaskMethod *method)
			{
				std::uint64_t value = 14695981039346656037ULL ^ seed;
				for (const auto byte : path + ":" + task + ":" + method->id)
				{
					value ^= static_cast<unsigned char>(byte);
					value *= 1099511628211ULL;
				}
				value ^= value >> 30;
				value *= 0xbf58476d1ce4e5b9ULL;
				value ^= value >> 27;
				value *= 0x94d049bb133111ebULL;
				return value ^ (value >> 31);
			};
			std::stable_sort(methods.begin(), methods.end(),
			                 [&](const auto *a, const auto *b)
			                 {
				                 return rank(a) < rank(b);
			                 });
		}
		std::string rejected;
		for (const auto *selected : methods)
		{
			const auto &method = *selected;
			if (result.expansions >= definition.task_expansions)
			{
				result.exhausted = true;
				result.reason = "task expansion budget exhausted";
				return false;
			}
			++result.expansions;
			if (method.prerequisites && !scenario_condition(*method.prerequisites, facts).value_or(false))
			{
				if (rejected.size() < 2048)
					rejected += " " + method.id + ":prerequisites";
				continue;
			}
			if (method.maintain && !scenario_condition(*method.maintain, facts).value_or(false))
			{
				if (rejected.size() < 2048)
					rejected += " " + method.id + ":maintenance";
				continue;
			}
			result.nodes.resize(checkpoint);
			result.nodes.push_back({path, task, method.id});
			bool feasible = true;
			for (const auto &child : method.children)
			{
				const auto start = result.nodes.size();
				if (!expand(child.task, path + "." + child.id, depth + 1))
				{
					feasible = false;
					break;
				}
				for (const auto &predecessor : child.after)
					result.nodes[start].after.push_back(path + "." + predecessor);
				result.nodes[start].parent = path;
			}
			if (feasible)
			{
				result.nodes[checkpoint].reason = rejected.empty() ? "selected" : "selected; rejected:" + rejected;
				return true;
			}
			if (rejected.size() < 2048)
				rejected += " " + method.id + ":child_infeasible";
			if (result.exhausted)
				break;
		}
		result.nodes.resize(checkpoint);
		if (!result.exhausted)
			result.reason = "no feasible method for " + task + rejected;
		return false;
	};
	result.feasible = expand(root, "root", 0);
	if (!result.feasible)
		result.nodes.clear();
	else
		result.reason.clear();
	return result;
}
} // namespace phoenix
