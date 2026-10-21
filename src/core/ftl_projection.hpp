#pragma once
#include "body_projection.hpp"
namespace phoenix::ftl_projection
{
inline Json report(const Json &v)
{
	if (v.is_null())
		return nullptr;
	if (!v.is_object() || v.value("reason", "") != "ftl" || !v.at("objectives").is_array() ||
	    v.at("objectives").size() > 256 || !v.at("left_behind").is_array() || v.at("left_behind").size() > 256)
		throw std::runtime_error("Invalid mission departure report");
	Json out{{"reason", "ftl"},
	         {"tick", projection::number(v.at("tick"), 0, 9007199254740991LL)},
	         {"objectives", Json::array()},
	         {"left_behind", Json::array()}};
	for (const auto &o : v.at("objectives"))
	{
		const auto status = projection::text(o.at("status"), 32);
		if (status != "pending" && status != "active" && status != "completed" && status != "failed" &&
		    status != "cancelled")
			throw std::runtime_error("Invalid departure objective status");
		out["objectives"].push_back({{"title", projection::text(o.at("title"), 256)}, {"status", status}});
	}
	for (const auto &name : v.at("left_behind"))
		out["left_behind"].push_back(projection::text(name, 256));
	return out;
}
inline Json board(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 1)
		throw std::runtime_error("Invalid FTL projection");
	Json out = Json::array();
	for (const auto &r : rows)
	{
		Json v;
		for (const auto *key : {"system", "blocker"})
			v[key] = projection::text(r.at(key), 256);
		for (const auto *key : {"energy", "capacity", "power_draw", "heat"})
			v[key] = projection::number(r.at(key), 0, 1000000000000LL);
		v["enabled"] = r.at("enabled").get<bool>();
		v["ready"] = r.at("ready").get<bool>();
		if (v.at("capacity") < 1 || v.at("energy") > v.at("capacity") ||
		    (v.at("ready").get<bool>() &&
		     (!v.at("enabled").get<bool>() || v.at("energy") != v.at("capacity") || v.at("blocker") != "")))
			throw std::runtime_error("Inconsistent FTL readiness");
		v["left_behind"] = Json::array();
		if (!r.at("left_behind").is_array() || r.at("left_behind").size() > 256)
			throw std::runtime_error("Invalid FTL personnel warning");
		for (const auto &name : r.at("left_behind"))
			v["left_behind"].push_back(projection::text(name, 256));
		out.push_back(v);
	}
	return out;
}
} // namespace phoenix::ftl_projection
