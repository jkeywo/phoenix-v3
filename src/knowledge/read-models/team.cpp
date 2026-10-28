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

namespace phoenix::team_projection
{
Json board(const Json &source)
{
	using projection::number;
	using projection::text;
	if (!source.is_array() || source.size() > 128 || source.dump().size() > 512 * 1024)
		throw std::runtime_error("Invalid team projection");
	Json out = Json::array();
	for (const auto &team : source)
	{
		Json t;
		if (team.contains("repair"))
			t["repair"] = team.at("repair").get<bool>();
		if (team.contains("travel"))
			t["travel"] = number(team.at("travel"), 0, 1000000);
		if (team.contains("work"))
			t["work"] = text(team.at("work"), 64);
		if (team.contains("work_system"))
			t["work_system"] = text(team.at("work_system"), 128);

		if (team.contains("assignment_authority"))
			t["assignment_authority"] = team.at("assignment_authority").get<bool>();
		if (team.contains("manual"))
			t["manual"] = team.at("manual").get<bool>();
		if (team.contains("patient_control"))
			t["patient_control"] = team.at("patient_control").get<bool>();
		if (team.contains("assigned_compartment"))
			t["assigned_compartment"] = number(team.at("assigned_compartment"), -1, 255);
		for (const auto *key : {"id", "type", "order", "activity", "condition"})
			t[key] = text(team.at(key), 64);
		t["revision"] = number(team.at("revision"), 1, 9007199254740991LL);
		t["location"] = number(team.at("location"), 0, 255);
		t["destination"] = number(team.at("destination"), -1, 255);
		t["reserved"] = team.at("reserved").get<bool>();
		t["orders"] = Json::array();
		if (!team.at("orders").is_array() || team.at("orders").size() > 32)
			throw std::runtime_error("Invalid team orders");
		for (const auto &order : team.at("orders"))
		{
			Json o;
			for (const auto *key : {"id", "icon", "target"})
				o[key] = text(order.at(key), 64);
			o["label"] = text(order.at("label"), 128);
			o["targets"] = Json::array();
			if (!order.at("targets").is_array() || order.at("targets").size() > 256)
				throw std::runtime_error("Invalid team targets");
			for (const auto &target : order.at("targets"))
				o["targets"].push_back(number(target, 1, 256));
			t["orders"].push_back(std::move(o));
		}
		t["patients"] = Json::array();
		if (!team.at("patients").is_array() || team.at("patients").size() > 384)
			throw std::runtime_error("Invalid patients");
		for (const auto &patient : team.at("patients"))
		{
			Json p;
			for (const auto *key : {"id", "condition", "facility"})
				p[key] = text(patient.at(key), 72);
			p["name"] = text(patient.at("name"), 64);
			p["location"] = number(patient.at("location"), 0, 255);
			p["stabilised"] = patient.at("stabilised").get<bool>();
			t["patients"].push_back(std::move(p));
		}
		out.push_back(std::move(t));
	}
	return out;
}
} // namespace phoenix::team_projection
