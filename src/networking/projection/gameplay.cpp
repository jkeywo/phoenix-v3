module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module phoenix.networking.projection;
import phoenix.knowledge.read_models;

namespace phoenix::network
{
namespace
{
Json alert_json(const Json &source)
{
	if (source.is_object() && source.empty())
		return Json::object();
	if (!source.is_object())
		throw std::runtime_error("Invalid alert blackboard");
	Json result;
	for (const auto *field : {"id", "label", "colour", "siren", "music"})
	{
		if (!source.at(field).is_string() || source.at(field).get_ref<const std::string &>().size() > 512)
			throw std::runtime_error("Invalid alert presentation field");
		result[field] = source.at(field);
	}
	result["severity"] = display::number(source.at("severity"), 0, 63);
	result["revision"] = display::number(source.at("revision"), 0, 9007199254740991LL);
	if (!source.at("glow").is_boolean())
		throw std::runtime_error("Invalid alert glow flag");
	result["glow"] = source.at("glow");
	return result;
}
} // namespace
Json bounded_gameplay(const Json &source)
{
	if (!source.is_object())
		return Json::object();
	ship_camera_state(source);
	constexpr std::size_t budget = 1024 * 1024;
	// Controls and fresh notifications take precedence over historical reports.
	// Entries remain whole, and retained history keeps its original order.
	const char *categories[] = {"controls",   "notifications", "processes", "controllers",
	                            "objectives", "shared",        "records",   "messages"};
	Json result = source;
	if (source.contains("objective_board"))
		result["objective_board"] = objective_projection::board(source.at("objective_board"));
	if (source.contains("ftl"))
		result["ftl"] = ftl_projection::board(source.at("ftl"));
	if (source.contains("mission_report"))
		result["mission_report"] = ftl_projection::report(source.at("mission_report"));
	if (source.contains("psychic"))
		result["psychic"] = psychic_projection::board(source.at("psychic"));
	if (source.contains("processes"))
		result["processes"] = process_projection::board(source.at("processes"));
	if (source.contains("abilities"))
		result["abilities"] = ability_projection::board(source.at("abilities"));
	if (source.contains("rules"))
		result["rules"] = rules_projection::board(source.at("rules"));
	if (source.contains("sensors"))
		result["sensors"] = sensor_projection::board(source.at("sensors"));
	if (source.contains("directed"))
		result["directed"] = directed_projection::board(source.at("directed"));
	if (source.contains("away_missions"))
		result["away_missions"] = away_projection::board(source.at("away_missions"));
	if (source.contains("docking"))
		result["docking"] = docking_projection::board(source.at("docking"));
	if (source.contains("logistics"))
		result["logistics"] = logistics_projection::board(source.at("logistics"));
	if (source.contains("craft"))
		result["craft"] = craft_projection::board(source.at("craft"));
	if (source.contains("personnel"))
		result["personnel"] = personnel_projection::board(source.at("personnel"));
	if (source.contains("teams"))
		result["teams"] = team_projection::board(source.at("teams"));
	if (source.contains("conversations"))
		result["conversations"] = conversation_projection::board(source.at("conversations"));
	if (source.contains("alert"))
		result["alert"] = alert_json(source.at("alert"));
	const auto process_rows = result.value("processes", Json::array());
	Json omitted = Json::object();
	for (const auto *category : categories)
	{
		const auto entries = source.find(category);
		omitted[category] = entries != source.end() && entries->is_array() ? entries->size() : 0;
		if (entries != source.end())
			result[category] = Json::array();
	}
	result["omitted"] = omitted;
	// Reserving metadata at the original (largest) counts also covers every
	// count's digits as entries are retained. One separator byte is conservative.
	std::size_t bytes = result.dump().size();
	const auto keep = [&](const char *category, const Json &entry)
	{
		const auto size = entry.dump().size() + 1;
		if (bytes <= budget && size <= budget - bytes)
		{
			result[category].push_back(entry);
			bytes += size;
			omitted[category] = omitted[category].get<std::size_t>() - 1;
		}
	};
	if (const auto shared = source.find("shared"); shared != source.end() && shared->is_array())
		for (const auto &record : *shared)
			if (record.value("schema", "") == "phoenix.camera")
				keep("shared", record);
	// World emits Away/resume and complexity profiles before optional actuator
	// descriptors. Preserve that priority so truncation cannot strand control.
	if (const auto controls = source.find("controls"); controls != source.end() && controls->is_array())
		for (const auto &control : *controls)
		{
			ability_projection::validate_control(control);
			process_projection::validate_control(control);
			keep("controls", control);
		}
	if (const auto notifications = source.find("notifications");
	    notifications != source.end() && notifications->is_array())
		for (auto entry = notifications->rbegin(); entry != notifications->rend(); ++entry)
			keep("notifications", *entry);
	for (const auto &process : process_rows)
		keep("processes", process);
	std::size_t history = 0;
	for (const auto *category : {"controllers", "objectives", "shared", "records", "messages"})
		if (const auto entries = source.find(category); entries != source.end() && entries->is_array())
			history = std::max(history, entries->size());
	// Round-robin recent entries so an old record stream cannot starve the
	// newest message/report in another permitted stream.
	for (std::size_t depth = 0; depth < history; ++depth)
		for (const auto *category : {"controllers", "objectives", "shared", "records", "messages"})
			if (const auto entries = source.find(category);
			    entries != source.end() && entries->is_array() && depth < entries->size())
			{
				const auto &entry = (*entries)[entries->size() - 1 - depth];
				if (std::string_view(category) != "shared" || entry.value("schema", "") != "phoenix.camera")
					keep(category, entry);
			}
	for (const auto *category : categories)
		if (std::string_view(category) != "controls" && std::string_view(category) != "processes" &&
		    result.contains(category) && result.at(category).is_array())
			std::reverse(result[category].begin(), result[category].end());
	result["omitted"] = std::move(omitted);
	return result;
}
Json public_board_json(const Json &source)
{
	Json selected{{"shared", Json::array()}, {"messages", Json::array()}};
	if (!source.is_object())
		return selected;
	for (const auto *key : {"shared", "messages"})
	{
		const auto rows = source.value(key, Json::array());
		if (!rows.is_array())
			throw std::runtime_error("Invalid public board rows");
		for (auto row : rows)
		{
			if (!row.is_object())
				throw std::runtime_error("Invalid public board entry");
			if (std::string(key) == "messages")
			{
				if (!row.value("public", false))
					continue;
				row["actionable"] = false;
				row["offer_only"] = true;
			}
			selected[key].push_back(std::move(row));
		}
	}
	if (source.contains("combat_result"))
	{
		const auto result = source.at("combat_result");
		if (result != "Victory" && result != "Defeat" && result != "Draw")
			throw std::runtime_error("Invalid combat outcome");
		selected["combat_result"] = result;
	}
	if (source.contains("display"))
		selected["display"] = display::sanitise(source.at("display"));
	if (source.contains("objective_board"))
		selected["objective_board"] = source.at("objective_board");
	if (source.contains("alert") && !source.at("alert").empty())
		selected["alert"] = alert_json(source.at("alert"));
	if (source.contains("navigation"))
	{
		const auto &navigation = source.at("navigation");
		selected["navigation"] = {
		    {"observed_tick", display::number(navigation.at("observed_tick"), 0, 9007199254740991LL)},
		    {"blocked", navigation.at("blocked").get<bool>()},
		    {"left_heading", display::number(navigation.at("left_heading"), 0, 359999)},
		    {"lateral_thrust", display::number(navigation.at("lateral_thrust"), -1000, 1000)},
		    {"lateral_safe", navigation.at("lateral_safe").get<bool>()}};
	}
	auto result = bounded_gameplay(selected);
	const auto omissions = source.value("omitted", Json::object());
	if (!omissions.is_object())
		throw std::runtime_error("Invalid public board omission counts");
	for (const auto *key : {"shared", "messages"})
		if (omissions.contains(key))
		{
			const auto &count = omissions.at(key);
			if (!count.is_number_integer() || count < 0 || count > 1000000000)
				throw std::runtime_error("Invalid public board omission count");
			result["omitted"][key] = result["omitted"][key].get<std::uint64_t>() + count.get<std::uint64_t>();
		}
	return result;
}
} // namespace phoenix::network
