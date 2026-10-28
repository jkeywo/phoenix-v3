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

namespace phoenix::docking_projection
{
Json board(const Json &source)
{
	if (!source.is_object() || source.dump().size() > 128 * 1024)
		throw std::runtime_error("Invalid docking projection");
	Json out{{"revision", projection::number(source.at("revision"), 1, 9007199254740991LL)},
	         {"ports", Json::array()},
	         {"targets", Json::array()}};
	if (!source.at("ports").is_array() || source.at("ports").size() > 16 || !source.at("targets").is_array() ||
	    source.at("targets").size() > 128)
		throw std::runtime_error("Invalid docking projection rows");
	for (const auto &r : source.at("ports"))
	{
		Json v;
		for (const auto *k : {"id", "controller", "compatibility", "phase"})
			v[k] = projection::text(r.at(k), 64);
		for (const auto *k : {"target", "progress"})
			v[k] = projection::number(r.at(k), 0, 9007199254740991LL);
		for (const auto *k : {"accepting", "supply", "working", "emergency"})
			v[k] = r.at(k).get<bool>();
		out["ports"].push_back(v);
	}
	for (const auto &r : source.at("targets"))
	{
		Json row{{"entity", projection::number(r.at("entity"), 1, 9007199254740991LL)},
		         {"port", projection::text(r.at("port"), 64)},
		         {"compatibility", projection::text(r.at("compatibility"), 64)},
		         {"available", r.at("available").get<bool>()}};
		if (r.contains("local_ports"))
		{
			const auto &ports = r.at("local_ports");
			if (!ports.is_array() || ports.size() > 16)
				throw std::runtime_error("Invalid docking option ports");
			row["local_ports"] = Json::array();
			for (const auto &port : ports)
			{
				const auto key = projection::text(port, 64);
				bool owned = false;
				for (const auto &local : out.at("ports"))
					owned |= local.at("id") == key;
				if (!owned)
					throw std::runtime_error("Docking option references an unowned port");
				row["local_ports"].push_back(key);
			}
		}
		out["targets"].push_back(row);
	}
	return out;
}
} // namespace phoenix::docking_projection
