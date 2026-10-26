#pragma once
#include <nlohmann/json.hpp>
namespace phoenix::test
{
inline nlohmann::json modifier_applications(const nlohmann::json &saved, std::uint64_t owner)
{
	auto result = nlohmann::json::array();
	for (const auto &row : saved.at("active_effects").at("modifiers"))
		if (row.at("owner") == owner)
			result.push_back(row);
	return result;
}
inline nlohmann::json modifier_contributions(const nlohmann::json &saved, std::uint64_t owner)
{
	auto rows = nlohmann::json::array();
	for (const auto &application : modifier_applications(saved, owner))
		for (const auto &value : application.at("values"))
		{
			auto row = application;
			row.erase("owner");
			row.erase("values");
			row["name"] = value.at("name");
			row["delta_permille"] = value.at("delta_permille");
			rows.push_back(std::move(row));
		}
	return rows;
}

} // namespace phoenix::test
