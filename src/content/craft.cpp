module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
namespace
{
void craft_require(bool ok, const std::string &why)
{
	if (!ok)
		throw std::runtime_error("carried craft: " + why);
}
std::int64_t craft_number(const Json &j, const char *key, std::int64_t initial, std::int64_t lo, std::int64_t hi)
{
	const auto v = j.value(key, Json(initial));
	craft_require(v.is_number_integer() && !v.is_boolean() && v >= lo && v <= hi, std::string("invalid ") + key);
	return v.get<std::int64_t>();
}
void craft_key(const std::string &key)
{
	craft_require(!key.empty() && key.size() <= 64 &&
	                  key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
	                      std::string::npos,
	              "invalid capability");
}
} // namespace
TransportDefinition compile_transport(const Catalog &catalog, const std::string &id)
{
	const auto &e = catalog.entities.at(id);
	const auto j = e.value("transport", Json::object());
	craft_require(j.is_object(), "transport must be an object");
	TransportDefinition d;
	d.capacity = craft_number(j, "capacity", 64, 1, 256);
	d.compartment = j.value("compartment", std::string{});
	if (e.contains("personnel"))
	{
		const auto p = compile_personnel(catalog, id);
		if (d.compartment.empty() && !p.compartments.empty())
			d.compartment = p.compartments.front();
		craft_require(d.compartment.empty() || std::find(p.compartments.begin(), p.compartments.end(), d.compartment) !=
		                                           p.compartments.end(),
		              "unknown transport compartment");
	}
	const auto tags = j.value("capabilities", Json::array());
	craft_require(tags.is_array() && tags.size() <= 64, "invalid transport capabilities");
	std::set<std::string> seen;
	for (const auto &v : tags)
	{
		const auto tag = v.get<std::string>();
		craft_key(tag);
		craft_require(seen.insert(tag).second, "duplicate transport capability");
		d.capabilities.push_back(tag);
	}
	const auto bindings = j.value("capability_systems", Json::object());
	craft_require(bindings.is_object() && bindings.size() <= 64, "invalid capability bindings");
	for (const auto &[tag, systems] : bindings.items())
	{
		craft_key(tag);
		craft_require(seen.contains(tag) && systems.is_array() && systems.size() <= 32,
		              "capability binding needs a declared tag and systems");
		for (const auto &s : systems)
		{
			const auto key = s.get<std::string>();
			craft_require(e.at("systems").contains(key), "unknown capability System");
			d.capability_systems[tag].push_back(key);
		}
	}
	return d;
}
std::optional<CraftDefinition> compile_craft(const Catalog &catalog, const std::string &id)
{
	const auto &e = catalog.entities.at(id);
	if (!e.contains("craft"))
		return {};
	const auto &j = e.at("craft");
	craft_require(j.is_object(), "craft must be an object");
	CraftDefinition d;
	d.role = j.value("role", std::string{"shuttle"});
	craft_require(d.role == "shuttle" || d.role == "interceptor" || d.role == "bomber", "unknown craft role");
	d.vertical_speed = craft_number(j, "vertical_speed_mm_s", 10000, 1, 1000000);
	d.vertical_acceleration = craft_number(j, "vertical_acceleration_mm_s2", 5000, 1, 1000000);
	d.cruise_altitude = craft_number(j, "cruise_altitude_mm", 15000, -1000000, 1000000);
	d.engagement_range = craft_number(j, "engagement_range_mm", 200000, 1, 1000000000);
	d.return_reserve = craft_number(j, "return_reserve_permille", 200, 0, 1000);
	if (!catalog.presentation)
	{
		const auto systems = resolve_systems(catalog, id);
		for (auto kind : {OperatingSystem::Engine, OperatingSystem::Steering})
			craft_require(std::any_of(systems.begin(), systems.end(),
			                          [kind](const auto &s)
			                          {
				                          return s.kind == kind;
			                          }),
			              "craft needs propulsion and steering");
		craft_require(e.contains("control_layout"), "craft needs ordinary Controllers");
	}
	return d;
}
std::map<std::string, CraftBayDefinition> compile_craft_bays(const Catalog &catalog, const std::string &id)
{
	std::map<std::string, CraftBayDefinition> out;
	if (catalog.presentation)
		return out;
	const auto &e = catalog.entities.at(id);
	for (const auto &[key, j] : e.at("systems").items())
	{
		if (j.value("kind", key) != "shuttle_bay")
			continue;
		CraftBayDefinition d;
		d.compartment = j.at("compartment");
		d.slots = craft_number(j, "slots", 0, 1, 32);
		d.transfer_ticks = craft_number(j, "transfer_ticks", 20, 1, 36000);
		d.docking_range = craft_number(j, "docking_range_mm", 10000, 1, 1000000);
		d.docking_speed = craft_number(j, "docking_speed_mm_s", 5000, 1, 1000000);
		d.charge_rate = craft_number(j, "charge_per_second", 0, 0, 1000000000);
		d.power_store = j.value("power_store", std::string{});
		const auto initial = j.value("initial", Json::array());
		craft_require(initial.is_array() && initial.size() <= static_cast<std::size_t>(d.slots), "bay exceeds slots");
		if (!catalog.presentation)
		{
			const auto combat = compile_combat(catalog, id);
			craft_require(combat && combat->compartment_ids.contains(d.compartment),
			              "bay requires a physical compartment");
			if (d.charge_rate)
				craft_require(catalog.resource_definitions.contains(id) &&
				                  catalog.resource_definitions.at(id).store_ids.contains(d.power_store),
				              "bay charger requires a carrier power store");
		}
		for (const auto &v : initial)
		{
			const auto name = v.get<std::string>();
			craft_require(catalog.entities.contains(name) && compile_craft(catalog, name).has_value(),
			              "bay initial entry must be a craft entity");
			for (const auto &[child, spec] : catalog.entities.at(name).at("systems").items())
				craft_require(spec.value("kind", child) != "shuttle_bay", "nested carried craft are unsupported");
			d.initial.push_back(name);
		}
		const auto supplies = j.value("ammunition", Json::array());
		craft_require(supplies.is_array() && supplies.size() <= 32, "invalid ammunition connections");
		for (const auto &s : supplies)
		{
			CraftSupplyDefinition a{s.at("source"), s.at("destination"),
			                        craft_number(s, "rounds_per_second", 1, 1, 1000000)};
			const auto &source = e.at("systems").at(a.source);
			craft_require(source.value("kind", a.source) == "magazine", "ammunition source must be a magazine");
			bool recipient = false;
			for (const auto &name : d.initial)
			{
				const auto &systems = catalog.entities.at(name).at("systems");
				if (!systems.contains(a.destination))
					continue;
				const auto &target = systems.at(a.destination);
				recipient = true;
				craft_require(target.value("kind", a.destination) == "magazine",
				              "ammunition destination must be a magazine");
				craft_require(!source.value("ammunition_type", std::string{}).empty() &&
				                  source.at("ammunition_type") == target.value("ammunition_type", Json()),
				              "incompatible ammunition connection");
			}
			craft_require(recipient, "ammunition connection has no receiving magazine");
			d.ammunition.push_back(a);
		}
		out.emplace(key, std::move(d));
	}
	std::int64_t slots = 0;
	for (const auto &[key, bay] : out)
		slots += bay.slots;
	craft_require(slots <= 128, "ship exceeds 128 carried slots");
	return out;
}
} // namespace phoenix
