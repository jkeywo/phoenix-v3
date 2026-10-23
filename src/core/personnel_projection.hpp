#pragma once
#include "body_projection.hpp"
namespace phoenix::personnel_projection
{
inline Json board(const Json &source)
{
	if (!source.is_object() || source.dump().size() > 256 * 1024)
		throw std::runtime_error("Invalid personnel projection");
	Json out{{"revision", source.at("revision")}};
	if (source.contains("manual"))
		out["manual"] = source.at("manual").get<bool>();
	if (!out["revision"].is_number_unsigned() || out["revision"].get<std::uint64_t>() > 9007199254740991ULL)
		throw std::runtime_error("Invalid personnel revision");
	using projection::number;
	using projection::text;
	for (const auto *key : {"departments", "duties", "officers"})
	{
		const auto &rows = source.at(key);
		if (!rows.is_array() || rows.size() > (std::string_view(key) == "duties" ? 256 : 128))
			throw std::runtime_error("Invalid personnel list");
		out[key] = Json::array();
		for (const auto &row : rows)
		{
			Json r{{"id", text(row.at("id"), 64)}};
			if (std::string(key) == "departments")
				r["label"] = text(row.at("label"), 128);
			else
			{
				for (const auto *field : {"department", "location"})
					r[field] = text(row.at(field), 64);
				if (std::string(key) == "duties")
				{
					r["label"] = text(row.at("label"), 128);
					r["effects"] = Json::array();
					const auto &effects = row.at("effects");
					if (!effects.is_array() || effects.size() > 256)
						throw std::runtime_error("Invalid personnel effects");
					for (const auto &e : effects)
					{
						auto n = number(e.at("multiplier"), 0, 1000000001000LL);
						r["effects"].push_back({{"system", text(e.at("system"), 64)},
						                        {"slot", text(e.at("slot"), 64)},
						                        {"property", text(e.at("property"), 64)},
						                        {"multiplier", n}});
					}
				}
				else
				{
					for (const auto *field : {"name", "portrait", "duty", "destination"})
						r[field] = text(row.at(field), 128);
					if (row.contains("power"))
						r["power"] = text(row.at("power"), 128);
					const auto portrait = r.at("portrait").get<std::string>();
					if (!portrait.empty() && !portrait.starts_with("console-assets:assets/comms/"))
						throw std::runtime_error("Invalid officer portrait");
					r["present"] = row.at("present").get<bool>();
					r["condition"] = text(row.at("condition"), 16);
					r["stabilised"] = row.at("stabilised").get<bool>();
					const auto condition = r.at("condition").get<std::string>();
					if (condition != "healthy" && condition != "stunned" && condition != "minor" &&
					    condition != "severe" && condition != "dead" && condition != "detained")
						throw std::runtime_error("Invalid officer condition projection");
					r["stun_until"] = number(row.at("stun_until"), 0, 9007199254740991LL);
					r["facility"] = text(row.at("facility"), 64);
					r["treatment"] = number(row.at("treatment"), 0, 1728000000000LL);
					auto n = number(row.at("remaining"), 0, 36000);
					r["remaining"] = n;
					r["traits"] = Json::array();
					const auto &traits = row.at("traits");
					if (!traits.is_array() || traits.size() > 16)
						throw std::runtime_error("Invalid traits");
					for (const auto &t : traits)
					{
						Json contributions = Json::array();
						if (!t.at("contributions").is_array() || t.at("contributions").size() > 256)
							throw std::runtime_error("Invalid trait contributions");
						for (const auto &c : t.at("contributions"))
							contributions.push_back(
							    {{"effect", text(c.at("effect"), 128)},
							     {"name", text(c.at("name"), 64)},
							     {"role", text(c.at("role"), 64)},
							     {"property", text(c.at("property"), 64)},
							     {"delta_permille", number(c.at("delta_permille"), -100000, 100000)}});
						r["traits"].push_back({{"id", text(t.at("id"), 64)},
						                       {"label", text(t.at("label"), 128)},
						                       {"contributions", contributions}});
					}
				}
			}
			out[key].push_back(r);
		}
	}
	return out;
}
} // namespace phoenix::personnel_projection
