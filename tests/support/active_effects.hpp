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
} // namespace phoenix::test
