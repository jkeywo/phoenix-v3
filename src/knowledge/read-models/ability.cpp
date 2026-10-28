module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module phoenix.knowledge.read_models;

namespace phoenix::ability_projection
{
void validate_control(const Json &control)
{
	if (!control.contains("ability_label") && !control.contains("ability_target"))
		return;
	if (projection::text(control.at("ability_label"), 512).empty())
		throw std::runtime_error("Invalid ability control label");
	if (control.contains("ability_target"))
	{
		const auto target = projection::number(control.at("ability_target"), 1, 1000000000);
		if (projection::number(control.at("value"), 0, 1000000000) != target)
			throw std::runtime_error("Ability control label target disagrees with its command");
	}
}

Json board(const Json &source)
{
	if (!source.is_array() || source.size() > 4096)
		throw std::runtime_error("Invalid ability projection roster");
	Json result = Json::array();
	std::vector<std::string> seen;
	for (const auto &entry : source)
	{
		const auto id = projection::text(entry.at("id"), 1024);
		if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end())
			throw std::runtime_error("Duplicate or empty ability projection identity");
		seen.push_back(id);
		Json row{{"id", id}};
		for (const auto *key : {"grant", "control", "ability", "source", "owner", "phase", "blocker"})
			row[key] = projection::text(entry.at(key), std::string_view(key) == "grant" ? 1024 : 512);
		row["label"] = projection::text(entry.at("label"), 512);
		if (row.at("grant") != id || (row.at("source") != "officer" && row.at("source") != "equipment") ||
		    (row.at("phase") != "ready" && row.at("phase") != "preparing" && row.at("phase") != "active" &&
		     row.at("phase") != "paused" && row.at("phase") != "recovering" && row.at("phase") != "unavailable"))
			throw std::runtime_error("Invalid ability projection state");
		if (!entry.at("requested").is_boolean())
			throw std::runtime_error("Invalid ability requested intent");
		row["requested"] = entry.at("requested");
		row["target"] = projection::number(entry.at("target"), 0, 7999999999999999LL);
		row["preparation_ticks"] = projection::number(entry.at("preparation_ticks"), 0, 1000000);
		row["progress"] =
		    projection::number(entry.at("progress"), 0, row.at("preparation_ticks").get<std::int64_t>() * 1000);
		row["ready"] = projection::number(entry.at("ready"), 0, 9007199254740991LL);
		row["generation"] = projection::number(entry.at("generation"), 1, 1000000000);
		row["output"] = projection::number(entry.at("output"), 0, 1000);
		for (const auto *key : {"energy_per_second", "preparation_energy_per_second"})
			row[key] = projection::number(entry.at(key), 0, 1000000000);
		row["properties"] = property_projection::observations(entry.value("properties", Json::array()));
		result.push_back(std::move(row));
	}
	return result;
}
} // namespace phoenix::ability_projection
