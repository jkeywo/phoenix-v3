#pragma once
#include "body_projection.hpp"
namespace phoenix::objective_projection
{
inline Json board(const Json &source)
{
	using projection::number;
	using projection::text;
	if (!source.is_object() || !source.at("objectives").is_array() || source.at("objectives").size() > 4096 ||
	    source.dump().size() > 512 * 1024)
		throw std::runtime_error("Invalid objective board");
	Json out{{"selected", number(source.at("selected"), 0, 9007199254740991LL)},
	         {"revision", number(source.at("revision"), 0, 9007199254740991LL)},
	         {"can_select", source.at("can_select").get<bool>()},
	         {"objectives", Json::array()}};
	for (const auto &row : source.at("objectives"))
	{
		Json objective{{"id", number(row.at("id"), 1, 9007199254740991LL)},
		               {"priority", row.at("priority").get<bool>()}};
		for (const auto *field : {"title", "description", "status"})
			objective[field] = text(row.at(field), 1024);
		for (const auto *field : {"destination_x", "destination_y"})
			if (row.contains(field))
				objective[field] = number(row.at(field), -1000000000, 1000000000);
		out["objectives"].push_back(std::move(objective));
	}
	return out;
}
} // namespace phoenix::objective_projection
