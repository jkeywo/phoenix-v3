#pragma once
#include <nlohmann/json.hpp>
namespace phoenix::test
{
inline const nlohmann::json &saved_material_stock(const nlohmann::json &body, std::size_t index)
{
	return body.at("stocks").at(body.at("inventory").at("stores").at(index).at("stock").get<std::string>());
}
inline nlohmann::json &saved_material_stock(nlohmann::json &body, std::size_t index)
{
	return body.at("stocks").at(body.at("inventory").at("stores").at(index).at("stock").get<std::string>());
}
inline const nlohmann::json &saved_magazine_stock(const nlohmann::json &body, const std::string &system)
{
	return body.at("stocks").at(body.at("combat").at("systems").at(system).at("stock").get<std::string>());
}
inline std::int64_t saved_ammunition(const nlohmann::json &body, const std::string &system)
{
	const auto &stock = saved_magazine_stock(body, system);
	return stock.at("quantities").value(stock.at("selection").get<std::string>(), std::int64_t{0});
}
} // namespace phoenix::test
