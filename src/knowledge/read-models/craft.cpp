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

namespace phoenix::craft_projection
{
Json board(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 128 || rows.dump().size() > 128 * 1024)
		throw std::runtime_error("Invalid flight operations projection");
	Json out = Json::array();
	for (const auto &r : rows)
	{
		Json v;
		for (const auto *key : {"id", "bay", "role", "phase", "order"})
			v[key] = projection::text(r.at(key), 128);
		v["blocker"] = projection::text(r.value("blocker", Json("")), 256);
		v["manual"] = r.value("manual", true);
		for (const auto *key : {"revision", "report_tick", "target"})
			v[key] = projection::number(r.at(key), 0, 9007199254740991LL);
		for (const auto *key : {"energy", "capacity", "ammunition", "condition", "maximum_condition"})
			v[key] = projection::number(r.at(key), 0, 1000000000000000LL);
		for (const auto *key : {"linked", "pending", "available"})
			v[key] = r.at(key).get<bool>();
		out.push_back(v);
	}
	return out;
}
} // namespace phoenix::craft_projection
