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

namespace phoenix::process_projection
{
void validate_control(const Json &control)
{
	if (control.contains("process_label") && projection::text(control.at("process_label"), 512).empty())
		throw std::runtime_error("Invalid process control label");
}

Json board(const Json &source)
{
	if (!source.is_array() || source.size() > 4096)
		throw std::runtime_error("Invalid process projection roster");
	Json result = Json::array();
	std::set<std::string> identities;
	for (const auto &entry : source)
	{
		Json row;
		for (const auto *field :
		     {"id", "control", "process", "slot", "label", "source", "owner", "phase", "blocker", "work_unit"})
			row[field] = projection::text(entry.at(field), std::string_view(field) == "id" ? 1024 : 512);
		const auto identity = row.at("id").get<std::string>();
		if (identity.empty() || !identities.insert(identity).second)
			throw std::runtime_error("Duplicate or empty process projection identity");
		if ((row.at("source") != "entity" && row.at("source") != "equipment" && row.at("source") != "officer" &&
		     row.at("source") != "compartment") ||
		    (row.at("phase") != "off" && row.at("phase") != "running" && row.at("phase") != "paused" &&
		     row.at("phase") != "completed"))
			throw std::runtime_error("Invalid process projection state");
		if (!entry.at("requested").is_boolean())
			throw std::runtime_error("Invalid process requested intent");
		row["requested"] = entry.at("requested");
		row["generation"] = projection::number(entry.at("generation"), 1, 1000000000);
		row["run"] = projection::number(entry.at("run"), 0, 9007199254740991LL);
		row["output"] = projection::number(entry.at("output"), 0, 1000);
		row["upkeep_output"] = projection::number(entry.at("upkeep_output"), 0, 1000);
		row["work"] = projection::number(entry.at("work"), 0, 1000000000000LL);
		row["display_scale"] = projection::number(entry.value("display_scale", Json(1)), 1, 1000000000);
		for (const auto *field :
		     {"finite", "driver", "details_available", "completed", "ready", "completion_delivered"})
		{
			if (!entry.at(field).is_boolean())
				throw std::runtime_error("Invalid finite process observation");
			row[field] = entry.at(field);
		}
		row["target"] = projection::text(entry.at("target"), 1024);
		row["parent"] = projection::text(entry.at("parent"), 1024);
		row["progress"] = projection::number(entry.at("progress"), 0, 1000000000000LL);
		row["required_work"] = projection::number(entry.at("required_work"), 0, 1000000000000LL);
		if (!row.at("details_available").get<bool>() &&
		    (!row.at("target").get<std::string>().empty() || row.at("progress") != 0 || row.at("required_work") != 0 ||
		     row.at("completed") != false || row.at("ready") != false || row.at("completion_delivered") != false))
			throw std::runtime_error("Unavailable finite process details must remain withheld");
		row["properties"] = property_projection::observations(entry.value("properties", Json::array()));
		result.push_back(std::move(row));
	}
	return result;
}
} // namespace phoenix::process_projection
