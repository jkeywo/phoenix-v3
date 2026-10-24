#pragma once
#include "body_projection.hpp"
namespace phoenix::directed_projection
{
inline Json board(const Json &source)
{
	if (!source.is_array() || source.size() > 256)
		throw std::runtime_error("Invalid directed projection");
	Json result = Json::array();
	std::vector<std::string> ids;
	for (const auto &entry : source)
	{
		const auto id = projection::text(entry.at("id"), 256);
		if (std::find(ids.begin(), ids.end(), id) != ids.end())
			throw std::runtime_error("Duplicate directed projection");
		ids.push_back(id);
		Json row{{"id", id},
		         {"target", projection::number(entry.at("target"), 0, 7999999999999999LL)},
		         {"ready", entry.at("ready").get<bool>()},
		         {"blocker", projection::text(entry.at("blocker"), 256)},
		         {"sequence", projection::number(entry.at("sequence"), 0, 9007199254740991LL)}};
		for (const auto *kind : {"operating", "targeting"})
		{
			const auto selected = std::string(kind) + "_mode", choices = std::string(kind) + "_modes";
			const auto &values = entry.at(choices);
			if (!values.is_array() || values.empty() || values.size() > 256)
				throw std::runtime_error("Invalid directed mode choices");
			row[selected] = projection::number(entry.at(selected), 0, static_cast<std::int64_t>(values.size() - 1));
			row[choices] = Json::array();
			for (std::size_t i = 0; i < values.size(); ++i)
			{
				const auto &value = values[i];
				if (projection::number(value.at("value"), 0, 255) != static_cast<std::int64_t>(i))
					throw std::runtime_error("Invalid directed mode index");
				row[choices].push_back({{"id", projection::text(value.at("id"), 256)},
				                        {"label", projection::text(value.at("label"), 512)},
				                        {"value", i},
				                        {"compatible", value.at("compatible").get<bool>()}});
			}
		}
		result.push_back(std::move(row));
	}
	return result;
}
} // namespace phoenix::directed_projection
