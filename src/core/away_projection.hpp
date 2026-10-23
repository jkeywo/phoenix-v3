#pragma once
#include "body_projection.hpp"
namespace phoenix::away_projection
{
inline Json board(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 256 || rows.dump().size() > 256 * 1024)
		throw std::runtime_error("Invalid away projection");
	Json out = Json::array();
	using projection::text;
	for (const auto &r : rows)
	{
		Json v;
		for (const auto *k : {"id", "title", "phase", "transport", "blocker", "result"})
			v[k] = text(r.at(k), 256);
		const auto revision = r.at("revision").get<std::uint64_t>();
		if (revision > 9007199254740991ULL)
			throw std::runtime_error("Invalid away revision");
		v["accompanying"] = Json::array();
		if (r.at("accompanying").size() > 128)
			throw std::runtime_error("Invalid attached officers");
		for (const auto &o : r.at("accompanying"))
			v["accompanying"].push_back(text(o, 64));
		v["revision"] = revision;
		v["dispatch"] = r.at("dispatch").get<bool>();
		v["target"] = projection::number(r.value("target", Json(0)), 0, 1000000000);
		v["ship_transport_reason"] = text(r.value("ship_transport_reason", Json("")), 256);
		v["slots"] = Json::array();
		v["transports"] = Json::array();
		if (r.at("slots").size() > 16 || r.at("transports").size() > 128)
			throw std::runtime_error("Invalid away lists");
		for (const auto &t : r.at("transports"))
			v["transports"].push_back(text(t, 128));
		for (const auto &s : r.at("slots"))
		{
			Json q{{"id", text(s.at("id"), 64)},
			       {"assigned", text(s.at("assigned"), 160)},
			       {"required", s.at("required").get<bool>()},
			       {"owned", s.at("owned").get<bool>()},
			       {"choices", Json::array()}};
			if (s.at("choices").size() > 256)
				throw std::runtime_error("Invalid away choices");
			for (const auto &c : s.at("choices"))
				q["choices"].push_back(text(c, 160));
			v["slots"].push_back(q);
		}
		v["labels"] = Json::object();
		if (!r.at("labels").is_object() || r.at("labels").size() > 256)
			throw std::runtime_error("Invalid asset labels");
		for (const auto &[key, label] : r.at("labels").items())
			v["labels"][text(Json(key), 160)] = text(label, 256);
		if (r.contains("boarding_check"))
		{
			const auto &c = r.at("boarding_check");
			v["boarding_check"] = {{"chance_permille", projection::number(c.at("chance_permille"), 0, 1000)},
			                       {"defenders", projection::number(c.at("defenders"), 0, 128)},
			                       {"encounter", text(c.at("encounter"), 64)},
			                       {"factors", Json::object()}};
			if (!c.at("factors").is_object() || c.at("factors").size() > 35)
				throw std::runtime_error("Invalid boarding factors");
			// At most 128 defenders, each capped at 1e9 security output; attacker slots are capped at 16.
			for (const auto &[key, value] : c.at("factors").items())
				v["boarding_check"]["factors"][text(Json(key), 64)] =
				    projection::number(value, -128000000000LL, 128000000000LL);
		}
		out.push_back(v);
	}
	return out;
}
} // namespace phoenix::away_projection
