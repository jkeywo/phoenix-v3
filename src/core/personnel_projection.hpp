#pragma once
namespace phoenix::personnel_projection
{
inline Json board(const Json &source)
{
	if (!source.is_object() || source.dump().size() > 256 * 1024)
		throw std::runtime_error("Invalid personnel projection");
	Json out{{"revision", source.at("revision")}};
	if (!out["revision"].is_number_unsigned() || out["revision"].get<std::uint64_t>() > 9007199254740991ULL)
		throw std::runtime_error("Invalid personnel revision");
	auto text = [](const Json &v, std::size_t max = 128)
	{
		if (!v.is_string() || v.get_ref<const std::string &>().size() > max)
			throw std::runtime_error("Invalid personnel text");
		return v;
	};
	for (const auto *key : {"departments", "duties", "officers"})
	{
		const auto &rows = source.at(key);
		if (!rows.is_array() || rows.size() > 128)
			throw std::runtime_error("Invalid personnel list");
		out[key] = Json::array();
		for (const auto &row : rows)
		{
			Json r{{"id", text(row.at("id"), 64)}};
			if (std::string(key) == "departments")
				r["label"] = text(row.at("label"));
			else
			{
				for (const auto *field : {"department", "location"})
					r[field] = text(row.at(field), 64);
				if (std::string(key) == "duties")
				{
					r["label"] = text(row.at("label"));
					r["effects"] = Json::array();
					const auto &effects = row.at("effects");
					if (!effects.is_array() || effects.size() > 256)
						throw std::runtime_error("Invalid personnel effects");
					for (const auto &e : effects)
					{
						auto n = e.at("multiplier").get<std::int64_t>();
						if (n < 0 || n > 30000000)
							throw std::runtime_error("Invalid multiplier");
						r["effects"].push_back({{"system", text(e.at("system"), 64)},
						                        {"slot", text(e.at("slot"), 64)},
						                        {"multiplier", n}});
					}
				}
				else
				{
					for (const auto *field : {"name", "portrait", "duty", "destination"})
						r[field] = text(row.at(field));
					const auto portrait = r.at("portrait").get<std::string>();
					if (!portrait.empty() && !portrait.starts_with("console-assets:assets/comms/"))
						throw std::runtime_error("Invalid officer portrait");
					r["present"] = row.at("present").get<bool>();
					auto n = row.at("remaining").get<std::int64_t>();
					if (n < 0 || n > 36000)
						throw std::runtime_error("Invalid officer travel");
					r["remaining"] = n;
					r["traits"] = Json::array();
					const auto &traits = row.at("traits");
					if (!traits.is_array() || traits.size() > 16)
						throw std::runtime_error("Invalid traits");
					for (const auto &t : traits)
					{
						Json bonuses = Json::object();
						if (!t.at("bonuses").is_object() || t.at("bonuses").size() > 16)
							throw std::runtime_error("Invalid bonuses");
						for (const auto &[slot, b] : t.at("bonuses").items())
						{
							auto v = b.get<std::int64_t>();
							if (slot.size() > 32 || v < -10000 || v > 10000)
								throw std::runtime_error("Invalid trait modifier");
							bonuses[slot] = v;
						}
						r["traits"].push_back({{"label", text(t.at("label"))}, {"bonuses", bonuses}});
					}
				}
			}
			out[key].push_back(r);
		}
	}
	return out;
}
} // namespace phoenix::personnel_projection
