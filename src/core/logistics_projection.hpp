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
	out["manual"] = source.value("manual", true);
	out["controller"] = projection::text(source.value("controller", Json("")), 64);
	out["route_controller"] = projection::text(source.value("route_controller", Json("")), 64);
	out["routes"] = Json::array();
	const auto routes = source.value("routes", Json::array());
	if (!routes.is_array() || routes.size() > 1024)
		throw std::runtime_error("Invalid projected material routes");
	for (const auto &r : routes)
		out["routes"].push_back({{"from", projection::text(r.at("from"), 64)},
		                         {"to", projection::text(r.at("to"), 64)},
		                         {"closed", r.at("closed").get<bool>()}});
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
			v["cargo"] = projection::number(r.value("cargo", Json(0)), 0, 9007199254740991LL);
			if (std::string(field) == "requests")
			{
				v["remaining"] = projection::number(r.at("remaining"), 1, 1000000000);
				v["automatic"] = r.at("automatic").get<bool>();
			}
			else
			{
				for (const auto *k : {"request", "location", "compartment", "quantity", "travel", "crossing"})
					v[k] = projection::number(r.at(k), 0, 9007199254740991LL);
				v["edge_to"] = projection::number(r.at("edge_to"), -1, 1000000);
				v["returning"] = r.at("returning").get<bool>();
			}
			out[field].push_back(v);
		}
	}
	for (const auto *key : {"cargo", "cargo_craft", "offers"})
	{
		const auto rows = source.value(key, Json::array());
		if (!rows.is_array() || rows.size() > 128)
			throw std::runtime_error("Invalid cargo projection");
		out[key] = Json::array();
		for (const auto &r : rows)
		{
			Json v;
			if (std::string(key) == "cargo")
			{
				for (const auto *k : {"id", "carrier", "target", "quantity", "report_tick"})
					v[k] = projection::number(r.at(k), 0, 9007199254740991LL);
				for (const auto *k : {"material", "phase"})
					v[k] = projection::text(r.at(k), 64);
			}
			else if (std::string(key) == "offers")
			{
				v["entity"] = projection::number(r.at("entity"), 1, 9007199254740991LL);
				for (const auto *k : {"store", "material"})
					v[k] = projection::text(r.at(k), 64);
				v["connected"] = r.at("connected").get<bool>();
			}
			else
			{
				v["id"] = projection::number(r.at("id"), 1, 9007199254740991LL);
				for (const auto *k : {"authorised", "available", "controllable"})
					v[k] = r.at(k).get<bool>();
				v["stores"] = Json::array();
				if (!r.at("stores").is_array() || r.at("stores").size() > 128)
					throw std::runtime_error("Invalid cargo store projection");
				for (const auto &store : r.at("stores"))
				{
					Json types = Json::array();
					if (!store.at("accepts").is_array() || store.at("accepts").size() > 128)
						throw std::runtime_error("Invalid cargo compatibility projection");
					for (const auto &m : store.at("accepts"))
						types.push_back(projection::text(m, 64));
					v["stores"].push_back({{"id", projection::text(store.at("id"), 64)}, {"accepts", types}});
				}
			}
			out[key].push_back(v);
		}
	}

	return out;
}
} // namespace phoenix::logistics_projection
