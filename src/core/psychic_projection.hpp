#pragma once
#include "body_projection.hpp"
namespace phoenix::psychic_projection
{
inline Json board(const Json &source)
{
	using projection::number;
	using projection::text;
	if (!source.is_object() || source.dump().size() > 512 * 1024 || !source.at("choirs").is_array() ||
	    source.at("choirs").size() > 8 || !source.at("fading").is_array() || source.at("fading").size() > 4096)
		throw std::runtime_error("Invalid psychic projection");
	Json out = {{"choirs", Json::array()}, {"fading", Json::array()}};
	for (const auto &c : source.at("choirs"))
	{
		Json choir = {
		    {"id", text(c.at("id"), 128)}, {"emission", number(c.at("emission"), 0, 1000)}, {"seats", Json::array()}};
		if (!c.at("seats").is_array() || c.at("seats").size() > 16)
			throw std::runtime_error("Invalid projected choir seats");
		for (const auto &s : c.at("seats"))
		{
			Json row;
			for (const char *key :
			     {"duty", "officer", "power", "controller", "enabled_channel", "target_channel", "blocker"})
				row[key] = text(s.at(key), 256);
			if (!s.at("enabled").is_boolean())
				throw std::runtime_error("Invalid psychic intent");
			row["enabled"] = s.at("enabled");
			row["target"] = number(s.at("target"), 0, 1000000000);
			row["work"] = number(s.at("work"), 0, 1000);
			row["ready"] = number(s.at("ready"), 0, 9007199254740991LL);
			choir["seats"].push_back(row);
		}
		out["choirs"].push_back(choir);
	}
	for (const auto &f : source.at("fading"))
		out["fading"].push_back({{"body", projection::body_json(projection::read_body(
		                                      f.at("body"), projection::BodyReadPolicy::SharedDisplay))},
		                         {"opacity", number(f.at("opacity"), 0, 1000)}});
	return out;
}
} // namespace phoenix::psychic_projection
