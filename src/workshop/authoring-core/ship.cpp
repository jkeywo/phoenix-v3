module;
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
module phoenix.workshop.authoring_core;
namespace phoenix
{
Json workshop_ship_add(Json &authored, const Json &a)
{
	const auto item_id = a.at("id").get<std::string>();
	const auto kind = a.at("kind").get<std::string>();
	const auto definition = a.value("definition", std::string{});
	const auto selection = a.value("selection", std::string{});
	if (item_id.empty())
		throw std::runtime_error("Item identity cannot be empty");
	if (kind != "System" && kind != "Station" && kind != "Console seat" && kind != "Controller widget" &&
	    kind != "Compartment" && kind != "Assembly" && kind != "Power store")
		throw std::runtime_error("Unknown ship item type");

	if (item_id.find_first_of("/~") != std::string::npos)
		throw std::runtime_error("Item identities cannot contain / or ~");
	auto changed = authored;
	std::string path;
	auto append = [&](const std::string &at, Json value)
	{
		auto &rows = changed[Json::json_pointer(at)];
		if (rows.is_null())
			rows = Json::array();
		for (const auto &row : rows)
			if (row.value("id", std::string{}) == item_id)
				throw std::runtime_error("Item identity already exists");
		value["id"] = item_id;
		rows.push_back(value);
		path = at + "/" + std::to_string(rows.size() - 1);
	};
	if (kind == "System")
	{
		if (changed.value("systems", Json::object()).contains(item_id))
			throw std::runtime_error("System identity already exists");
		changed["systems"][item_id] = {{"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}};
		path = "/systems/" + item_id;
	}
	else if (kind == "Station")
		append("/stations", {{"consoles", Json::array()}});
	else if (kind == "Console seat")
	{
		if (!changed.contains("stations") || changed["stations"].empty())
			throw std::runtime_error("Add a Station first");
		std::string station = "/stations/0";
		if (selection.starts_with("/stations/"))
		{
			const auto end = selection.find('/', 10);
			station = selection.substr(0, end);
		}
		append(station + "/consoles",
		       {{"definition", definition}, {"systems", Json::array()}, {"feeds", Json::array()}});
	}
	else if (kind == "Assembly")
		append("/assemblies", {{"assembly", definition}});
	else if (kind == "Compartment")
	{
		if (!changed.contains("combat"))
			changed["combat"] = {
			    {"decks", Json::array()}, {"connections", Json::array()}, {"destruction", Json::object()}};
		if (changed["combat"]["decks"].empty())
			changed["combat"]["decks"].push_back({{"id", "deck"}, {"weight", 1}, {"compartments", Json::array()}});
		append("/combat/decks/0/compartments", {{"x_mm", 0},
		                                        {"y_mm", 0},
		                                        {"half_width_mm", 1000},
		                                        {"half_length_mm", 1000},
		                                        {"hp", 100},
		                                        {"resistance_per_m", 10},
		                                        {"recovery_hp", 20}});
	}
	else if (kind == "Power store")
		append("/resources/stores", {{"resource", "energy"},
		                             {"capacity", 1000},
		                             {"initial", 1000},
		                             {"charge_rate", 100},
		                             {"discharge_rate", 100},
		                             {"stop_heat", 1000},
		                             {"recover_heat", 500}});
	else
	{
		if (!changed.contains("control_layout"))
			changed["control_layout"] = {{"schema", 2},
			                             {"controllers", Json::array()},
			                             {"profiles", Json::array({{{"id", "default"}, {"stars", 1}}})},
			                             {"default_profile", "default"}};
		if (changed["control_layout"].value("schema", 1) != 2)
			throw std::runtime_error("Use the Controller form for legacy control layouts");
		append("/control_layout/controllers",
		       {{"console", ""},
		        {"widget", definition},
		        {"equipment", Json::object()},
		        {"star_levels", {{"1", "automated"}, {"2", "automated"}, {"3", "automated"}}}});
	}
	authored = std::move(changed);
	return path;
}
} // namespace phoenix
