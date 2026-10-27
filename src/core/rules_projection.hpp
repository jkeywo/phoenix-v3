#pragma once
#include "body_projection.hpp"
namespace phoenix::rules_projection
{
inline Json board(const Json &source)
{
	if (!source.is_array() || source.size() > 4096)
		throw std::runtime_error("Invalid rule projection roster");
	Json result = Json::array();
	std::vector<std::string> seen;
	for (const auto &entry : source)
	{
		const auto id = projection::text(entry.at("id"), 1024);
		if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end())
			throw std::runtime_error("Duplicate or empty rule projection identity");
		seen.push_back(id);
		Json row{{"id", id}};
		for (const auto *key : {"rule", "label", "source_kind", "equipment", "phase", "blocker"})
			row[key] = projection::text(entry.at(key), 512);
		if (row.at("rule").get<std::string>().empty() || row.at("label").get<std::string>().empty())
			throw std::runtime_error("Invalid rule projection metadata");
		const auto &phase = row.at("phase");
		if (phase != "ready" && phase != "active" && phase != "paused" && phase != "stopped" && phase != "unavailable")
			throw std::runtime_error("Invalid rule projection phase");
		const auto &kind = row.at("source_kind");
		if (kind != "equipment" && kind != "officer" && kind != "effect" && kind != "region" && kind != "shield" &&
		    kind != "process")
			throw std::runtime_error("Invalid rule projection source kind");
		const auto &blocker = row.at("blocker");
		if (blocker != "" && blocker != "conditions" && blocker != "source" && blocker != "funding" &&
		    blocker != "expression" && blocker != "target" && blocker != "stopped")
			throw std::runtime_error("Invalid public rule blocker code");
		row["output"] = projection::number(entry.at("output"), 0, 1000);
		result.push_back(std::move(row));
	}
	return result;
}
} // namespace phoenix::rules_projection
