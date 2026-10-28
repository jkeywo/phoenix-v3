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

namespace phoenix::property_projection
{
Json observations(const Json &properties)
{
	Json result = Json::array();
	if (!properties.is_array() || properties.size() > 256)
		throw std::runtime_error("Invalid property observations");
	std::set<std::string> property_ids;
	for (const auto &property : properties)
	{
		const auto property_id = projection::text(property.at("id"), 128);
		const auto kind = projection::text(property.at("kind"), 16);
		if (property_id.empty() || !property_ids.insert(property_id).second)
			throw std::runtime_error("Invalid observed property identity");
		Json value{{"id", property_id},
		           {"kind", kind},
		           {"label", projection::text(property.at("label"), 128)},
		           {"unit", projection::text(property.at("unit"), 128)},
		           {"display_scale", projection::number(property.at("display_scale"), 1, 1000000000)}};
		if (kind == "flag")
		{
			if (!property.at("value").is_boolean())
				throw std::runtime_error("Invalid observed flag");
			value["value"] = property.at("value");
		}
		else if (kind == "enum")
		{
			value["value"] = projection::text(property.at("value"), 128);
			value["choice_label"] = projection::text(property.at("choice_label"), 128);
		}
		else if (kind == "quantity" || kind == "attribute")
			value["value"] =
			    projection::number(property.at("value"), kind == "quantity" ? 0 : -1000000000000LL, 1000000000000LL);
		else
			throw std::runtime_error("Invalid observed property kind");
		result.push_back(std::move(value));
	}
	return result;
}
} // namespace phoenix::property_projection
