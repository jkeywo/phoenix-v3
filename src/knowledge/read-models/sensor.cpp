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

namespace phoenix::sensor_projection
{
Json data(const std::string &cap, const Json &source)
{
	using projection::text;
	if (cap == "basic_identity")
		return Json{{"class", text(source.at("class"), 256)},
		            {"faction", text(source.at("faction"), 128)},
		            {"faction_label", text(source.at("faction_label"), 256)},
		            {"disposition", text(source.at("disposition"), 32)}};
	if (cap.starts_with("finding:"))
		return Json{{"id", text(source.at("id"), 64)}, {"text", text(source.at("text"), 4096)}};
	if (!source.is_array() || source.size() > (cap == "shield_sectors" ? 8192 : 256))
		throw std::runtime_error("Invalid sensor finding rows");
	Json out = Json::array();
	for (const auto &row : source)
	{
		Json r{{"id", text(row.at("id"), cap == "shield_sectors" ? 256 : 128)}};
		if (cap == "findings")
		{
			r["text"] = text(row.at("text"), 4096);
			r["observed"] = row.at("observed").get<std::uint64_t>();
		}
		else if (cap == "contact_arcs" || cap == "shield_sectors")
		{
			r["bearing_mdeg"] = row.at("bearing_mdeg").get<std::int64_t>();
			r["arc_mdeg"] = row.at("arc_mdeg").get<std::int64_t>();
			if (cap == "shield_sectors")
			{
				r["strength"] = row.at("strength").get<std::int64_t>();
				r["capacity"] = row.at("capacity").get<std::int64_t>();
				r["enabled"] = row.at("enabled").get<bool>();
			}
		}
		else if (cap == "compartment_condition")
		{
			r["deck"] = row.at("deck");
			r["disabled"] = row.at("disabled").get<bool>();
			for (const auto *key : {"hp", "maximum", "x_mm", "y_mm", "half_width_mm", "half_length_mm"})
				r[key] = row.at(key).get<std::int64_t>();
		}
		else
			throw std::runtime_error("Unknown sensor finding");
		out.push_back(r);
	}
	return out;
}

Json board(const Json &source)
{
	if (!source.is_object() || source.dump().size() > 1024 * 1024 || source.at("scanners").size() > 128 ||
	    source.at("contacts").size() > 512)
		throw std::runtime_error("Invalid sensor projection bounds");
	Json out{{"scanners", Json::array()}, {"contacts", Json::array()}, {"tracks", Json::array()}};
	for (const auto &s : source.at("scanners"))
	{
		Json r{{"id", projection::text(s.at("id"), 128)},
		       {"blocker", projection::text(s.at("blocker"), 256)},
		       {"progress", Json::array()}};
		for (const auto *key : {"target", "x", "y", "bearing_mdeg", "arc_mdeg", "range_mm"})
			r[key] = s.at(key).get<std::int64_t>();
		if (s.at("progress").size() > 134)
			throw std::runtime_error("Invalid scan progress");
		for (const auto &p : s.at("progress"))
			r["progress"].push_back({{"capability", projection::text(p.at("capability"), 128)},
			                         {"ticks", p.at("ticks").get<std::int64_t>()},
			                         {"required", p.at("required").get<std::int64_t>()}});
		out["scanners"].push_back(r);
	}
	for (const auto &c : source.at("contacts"))
	{
		Json r{{"id", c.at("id").get<EntityId>()}, {"findings", Json::object()}};
		for (const auto *key : {"x", "y"})
			if (c.contains(key))
				r[key] = c.at(key).get<std::int64_t>();
		if (c.at("findings").size() > 134)
			throw std::runtime_error("Invalid findings count");
		for (const auto &[cap, f] : c.at("findings").items())
			r["findings"][cap] = {{"data", data(cap, f.at("data"))},
			                      {"observed", f.at("observed").get<std::uint64_t>()},
			                      {"live", f.at("live").get<bool>()}};
		out["contacts"].push_back(r);
	}
	if (source.contains("tracks"))
	{
		if (!source.at("tracks").is_array() || source.at("tracks").size() > 512)
			throw std::runtime_error("Invalid retained sensor tracks");
		for (const auto &t : source.at("tracks"))
			out["tracks"].push_back({{"body", projection::body_json(projection::read_body(
			                                      t.at("body"), projection::BodyReadPolicy::SharedDisplay))},
			                         {"observed", projection::number(t.at("observed"), 0, 9007199254740991LL)},
			                         {"uncertainty", projection::number(t.at("uncertainty"), 0, 1000000000)}});
	}
	return out;
}
} // namespace phoenix::sensor_projection
