module;
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.content.sensing;
import phoenix.content.catalog;
import phoenix.content.combat;
import phoenix.content.combat.types;
import phoenix.content.controllers;
import phoenix.content.sensing.types;

namespace phoenix
{
namespace
{
void sensor_require(bool ok, const std::string &text)
{
	if (!ok)
		throw std::runtime_error("sensors: " + text);
}
const std::set<std::string> sensor_capabilities{"basic_identity",        "own_arcs", "contact_arcs", "shield_sectors",
                                                "compartment_condition", "findings"};
} // namespace
std::map<std::string, SensorDefinition> compile_sensors(const Catalog &catalog, const std::string &entity)
{
	const DirectedCatalog directed_catalog{{entity, compile_directed(catalog, entity)}};
	return compile_sensors(catalog, entity, directed_catalog);
}
std::map<std::string, SensorDefinition> compile_sensors(const Catalog &catalog, const std::string &entity,
                                                        const DirectedCatalog &directed_catalog)
{
	std::map<std::string, SensorDefinition> out;
	const auto &hull = catalog.entities.at(entity);
	const auto &directed = directed_catalog.at(entity);
	for (const auto &fit : resolve_systems(catalog, entity))
		if (fit.kind == OperatingSystem::Radar ||
		    (directed.contains(fit.id) &&
		     std::any_of(directed.at(fit.id).modes.begin(), directed.at(fit.id).modes.end(),
		                 [](const auto &m)
		                 {
			                 return m.operation == DirectedOperationKind::Scan;
		                 })))
		{
			const auto &j = hull.at("systems").at(fit.id);
			SensorDefinition d;
			if (j.contains("long_range_mm"))
			{
				sensor_require(j.at("long_range_mm").is_number_integer(), "long_range_mm must be an integer");
				d.long_range = j.at("long_range_mm");
				sensor_require(d.long_range > fit.maximum && d.long_range <= 1000000000,
				               "long_range_mm must exceed range_mm and be within coordinate limits");
			}
			d.arc = j.value("arc_mdeg", 360000LL);
			d.bearing = j.value("bearing_mdeg", 0LL);
			d.turret = j.value("turret", std::string{});
			sensor_require(d.arc > 0 && d.arc <= 360000 && d.bearing >= 0 && d.bearing < 360000,
			               "invalid sensor coverage");
			const auto passive = j.value("broad_capabilities", Json::object());
			sensor_require(passive.is_object() && passive.size() <= sensor_capabilities.size(),
			               "invalid broad capabilities");
			sensor_require(passive.empty() || fit.kind == OperatingSystem::Radar,
			               "broad capabilities require native radar");
			for (const auto &[key, value] : passive.items())
			{
				sensor_require(sensor_capabilities.contains(key) && value.is_number_integer() && value == 0,
				               "broad capabilities must be known instantaneous acquisition types");
				d.passive_capabilities[key] = 0;
			}
			if (directed.contains(fit.id))
				for (const auto &mode : directed.at(fit.id).modes)
					if (mode.operation == DirectedOperationKind::Scan)
						for (const auto &[key, ticks] : mode.capabilities)
						{
							const auto found = d.capabilities.find(key);
							if (found == d.capabilities.end() || ticks < found->second)
								d.capabilities[key] = ticks;
						}
			if (!directed.contains(fit.id) && !j.contains("broad_capabilities"))
				d.passive_capabilities = {{"basic_identity", 0}, {"own_arcs", 0}};
			for (const auto &[key, ticks] : d.passive_capabilities)
				d.capabilities[key] = ticks;
			if (!d.turret.empty())
			{
				sensor_require(hull.at("systems").contains(d.turret) &&
				                   hull.at("systems").at(d.turret).value("kind", d.turret) == "turret",
				               "unknown sensor turret");
				sensor_require(d.arc < 360000, "turret scanner must be directional");
			}
			out.emplace(fit.id, std::move(d));
		}
	return out;
}
std::vector<SensorFinding> compile_sensor_findings(const Catalog &catalog, const Json &map)
{
	std::vector<SensorFinding> out;
	std::set<std::string> ids;
	auto rows = map.value("sensor_findings", Json::array());
	sensor_require(rows.is_array() && rows.size() <= 128, "too many findings");
	for (const auto &j : rows)
	{
		SensorFinding f;
		f.id = j.at("id");
		f.text = j.at("text_key");
		f.target = j.at("target_entity");
		f.ticks = j.at("scan_ticks");
		f.capabilities = j.value("requires", std::vector<std::string>{"findings"});
		f.requires_event = j.value("requires_event", std::string{});
		f.unless_event = j.value("unless_event", std::string{});
		f.event = j.value("discovery_event", std::string{});
		f.tag = j.value("tag", std::string{});
		f.destination_station = j.value("destination_station", std::string{});
		sensor_require(f.tag.size() <= 64 && f.destination_station.size() <= 64 &&
		                   (f.tag.empty() == f.destination_station.empty()),
		               "finding tags require a destination station");
		sensor_require(!f.id.empty() && f.id.size() <= 64 && ids.insert(f.id).second && !f.text.empty() &&
		                   f.text.size() <= 4096 && f.ticks > 0 && f.ticks <= 1728000,
		               "invalid finding");
		bool target = false;
		for (const auto &e : map.value("entities", Json::array()))
			target |= e.at("id") == f.target;
		sensor_require(target && !f.capabilities.empty(), "finding needs target and qualifying capabilities");
		for (const auto &cap : f.capabilities)
			sensor_require(sensor_capabilities.contains(cap), "unknown finding capability");
		for (const auto &event : {f.requires_event, f.unless_event, f.event})
			if (!event.empty())
			{
				auto it = catalog.scenarios.find(map.at("id"));
				sensor_require(it != catalog.scenarios.end(), "finding event needs scenario");
				sensor_require(std::find(it->second.events.begin(), it->second.events.end(), event) !=
				                   it->second.events.end(),
				               "undeclared finding event");
			}
		out.push_back(std::move(f));
	}
	return out;
}
} // namespace phoenix
