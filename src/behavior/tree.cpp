module;
#include <stdexcept>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.content;

namespace phoenix
{
void verify_tree(const Json &tree, const std::string &source)
{
	if (!tree.contains("initial") || !tree["initial"].is_string() || !tree.contains("states") ||
	    !tree["states"].is_object() || tree["states"].empty() || tree["states"].size() > 256 ||
	    !tree["states"].contains(tree["initial"].get<std::string>()))
		throw std::runtime_error(source + ": initial state must resolve in 1..256 states");
	for (auto it = tree["states"].begin(); it != tree["states"].end(); ++it)
	{
		const auto &state = it.value();
		if (!state.is_object() || !state.contains("ticks") || !state["ticks"].is_number_integer() ||
		    state["ticks"].get<std::int64_t>() < 1 || state["ticks"].get<std::int64_t>() > 1000000 ||
		    !state.contains("next") || !state["next"].is_string() ||
		    !tree["states"].contains(state["next"].get<std::string>()))
			throw std::runtime_error(source + ".states." + it.key() +
			                         ": positive bounded ticks and resolving next state required");
		for (auto field = state.begin(); field != state.end(); ++field)
		{
			const auto &key = field.key();
			if (key == "ticks" || key == "next")
				continue;
			if (key == "radar_enabled" || key == "engine_enabled")
			{
				if (!field.value().is_boolean())
					throw std::runtime_error(source + ": " + key + " must be boolean");
			}
			else if (key == "speed_mm_s" || key == "heading_mdeg")
			{
				if (!field.value().is_number_integer() || field.value().get<std::int64_t>() < 0 ||
				    field.value().get<std::int64_t>() > (key == "heading_mdeg" ? 359999 : 1000000000))
					throw std::runtime_error(source + ": invalid " + key);
			}
			else
				throw std::runtime_error(source + ": unsupported state output '" + key + "'");
		}
	}
}
TreeProgram compile_tree(const Json &definition)
{
	verify_tree(definition, "controller");
	std::map<std::string, std::size_t> indices;
	for (auto it = definition.at("states").begin(); it != definition.at("states").end(); ++it)
		indices.emplace(it.key(), indices.size());
	TreeProgram result{indices.at(definition.at("initial").get<std::string>()), {}, {}, {}};
	for (auto it = definition.at("states").begin(); it != definition.at("states").end(); ++it)
	{
		const auto &state = it.value();
		TreeNode node{state.at("ticks").get<std::uint64_t>(), indices.at(state.at("next").get<std::string>()),
		              result.outputs.size(), 0};
		// Retain the authored JSON object's stable output ordering at the boundary.
		for (auto field = state.begin(); field != state.end(); ++field)
		{
			if (field.key() == "ticks" || field.key() == "next")
				continue;
			auto action = field.key() == "speed_mm_s"       ? TreeAction::Speed
			              : field.key() == "heading_mdeg"   ? TreeAction::Heading
			              : field.key() == "engine_enabled" ? TreeAction::EngineEnabled
			                                                : TreeAction::RadarEnabled;
			result.outputs.push_back({action, field.value().is_boolean() ? (field.value().get<bool>() ? 1 : 0)
			                                                             : field.value().get<std::int64_t>()});
		}
		node.output_count = result.outputs.size() - node.output_begin;
		result.nodes.push_back(node);
		result.names.push_back(it.key());
	}
	return result;
}
std::span<const TreeOutput> step_tree(const TreeProgram &program, TreeCursor &cursor)
{
	if (cursor.state == TreeCursor::unset)
		cursor.state = program.initial;
	const auto &node = program.nodes[cursor.state];
	std::span<const TreeOutput> outputs;
	if (!cursor.entered)
	{
		outputs = std::span<const TreeOutput>(program.outputs).subspan(node.output_begin, node.output_count);
		cursor.entered = true;
	}
	if (++cursor.elapsed >= node.ticks)
	{
		cursor.state = node.next;
		cursor.elapsed = 0;
		cursor.entered = false;
	}
	return outputs;
}
TreeState save_tree(const TreeProgram &program, const TreeCursor &cursor)
{
	return {cursor.state == TreeCursor::unset ? std::string{} : program.names[cursor.state], cursor.elapsed,
	        cursor.entered};
}
TreeCursor load_tree(const TreeProgram &program, const TreeState &saved)
{
	if (saved.state.empty())
	{
		if (saved.elapsed || saved.entered)
			throw std::runtime_error("saved tree: invalid initial state");
		return {};
	}
	if ((!saved.entered && saved.elapsed != 0) || (saved.entered && saved.elapsed == 0))
		throw std::runtime_error("saved tree: invalid entry/elapsed combination");
	for (std::size_t index = 0; index < program.nodes.size(); ++index)
		if (program.names[index] == saved.state && saved.elapsed < program.nodes[index].ticks)
			return {index, saved.elapsed, saved.entered};
	throw std::runtime_error("saved tree: invalid continuation");
}
} // namespace phoenix
