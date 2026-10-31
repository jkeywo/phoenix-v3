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

namespace phoenix::objective_projection
{
Json board(const Json &source)
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
		if (row.contains("delivery_target"))
		{
			objective["delivery_target"] = number(row.at("delivery_target"), 1, 9007199254740991LL);
			objective["delivery_quantity"] = number(row.at("delivery_quantity"), 1, 1000000000);
			for (const auto *field : {"delivery_port", "delivery_source", "delivery_store", "delivery_material"})
				objective[field] = text(row.at(field), 128);
		}
		out["objectives"].push_back(std::move(objective));
	}
	return out;
}
} // namespace phoenix::objective_projection
