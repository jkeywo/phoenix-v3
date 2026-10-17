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
Json step_tree(const Json &definition, TreeState &state)
{
	if (state.state.empty())
		state.state = definition.at("initial").get<std::string>();
	const auto &current = definition.at("states").at(state.state);
	Json outputs = Json::object();
	if (!state.entered)
	{
		outputs = current;
		outputs.erase("ticks");
		outputs.erase("next");
		state.entered = true;
	}
	if (++state.elapsed >= current.at("ticks").get<std::uint64_t>())
	{
		state.state = current.at("next").get<std::string>();
		state.elapsed = 0;
		state.entered = false;
	}
	return outputs;
}
Json save_tree(const TreeState &state)
{
	return {{"state", state.state}, {"elapsed", state.elapsed}, {"entered", state.entered}};
}
TreeState load_tree(const Json &definition, const Json &saved)
{
	TreeState result{saved.at("state").get<std::string>(), saved.at("elapsed").get<std::uint64_t>(),
	                 saved.at("entered").get<bool>()};
	if (result.state.empty())
	{
		if (result.elapsed || result.entered)
			throw std::runtime_error("saved tree: invalid initial state");
		return result;
	}
	if ((!result.entered && result.elapsed != 0) || (result.entered && result.elapsed == 0))
		throw std::runtime_error("saved tree: invalid entry/elapsed combination");
	if (!definition.at("states").contains(result.state) ||
	    result.elapsed >= definition.at("states").at(result.state).at("ticks").get<std::uint64_t>())
		throw std::runtime_error("saved tree: invalid continuation");
	return result;
}
} // namespace phoenix
