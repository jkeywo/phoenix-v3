#pragma once
#include "body_projection.hpp"
namespace phoenix::logistics_projection
{
inline Json amounts(const Json &source)
{
	if (!source.is_object() || source.size() > 128)
		throw std::runtime_error("Invalid projected material quantities");
	Json out = Json::object();
	for (const auto &[m, n] : source.items())
		out[projection::text(Json(m), 64)] = projection::number(n, 0, 1000000000000000LL);
	return out;
}
inline Json board(const Json &source)
{
	if (!source.is_object() || source.dump().size() > 512 * 1024)
		throw std::runtime_error("Invalid material projection");
	Json out{{"revision", projection::number(source.at("revision"), 1, 9007199254740991LL)}};
	const auto &stores = source.at("stores");
	if (!stores.is_array() || stores.size() > 128)
		throw std::runtime_error("Invalid projected stores");
	out["stores"] = Json::array();
	for (const auto &s : stores)
	{
		Json v;
		for (const auto *k : {"id", "controller"})
			v[k] = projection::text(s.at(k), 64);
		for (const auto *k : {"main", "held", "working"})
			v[k] = s.at(k).get<bool>();
		for (const auto *k : {"quantities", "space", "targets"})
			v[k] = amounts(s.at(k));
		out["stores"].push_back(v);
	}
	for (const auto *field : {"requests", "batches"})
	{
		const auto &rows = source.at(field);
		if (!rows.is_array() || rows.size() > 256)
			throw std::runtime_error("Invalid projected deliveries");
		out[field] = Json::array();
		for (const auto &r : rows)
		{
			Json v;
			for (const auto *k : {"id", "source", "destination", "from", "to"})
				v[k] = projection::number(r.at(k), 0, 9007199254740991LL);
			v["material"] = projection::text(r.at("material"), 64);
			if (std::string(field) == "requests")
			{
				v["remaining"] = projection::number(r.at("remaining"), 1, 1000000000);
				v["automatic"] = r.at("automatic").get<bool>();
			}
			else
			{
				for (const auto *k : {"request", "location", "compartment", "quantity", "travel"})
					v[k] = projection::number(r.at(k), 0, 9007199254740991LL);
				v["edge_to"] = projection::number(r.at("edge_to"), -1, 1000000);
				v["returning"] = r.at("returning").get<bool>();
			}
			out[field].push_back(v);
		}
	}
	return out;
}
} // namespace phoenix::logistics_projection
