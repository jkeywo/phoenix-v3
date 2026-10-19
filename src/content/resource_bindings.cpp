module;
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <initializer_list>

module phoenix.content;
namespace phoenix
{
namespace
{
ResourceAmount adapter_amount(const Json &json, const char *key, const std::string &path,
                              ResourceAmount maximum = resource_limit)
{
	if (!json.contains(key))
		return 0;
	const auto &v = json.at(key);
	if (!v.is_number_integer() || v.is_boolean() || v < 0 || v > maximum)
		throw std::runtime_error(path + "." + key + ": invalid resource quantity");
	return v.get<ResourceAmount>();
}
ResourceIndex adapter_reference(const Json &json, const char *key, const std::map<std::string, ResourceIndex> &ids,
                                const std::string &path)
{
	if (!json.contains(key))
		return no_resource;
	if (!json.at(key).is_string())
		throw std::runtime_error(path + "." + key + ": expected resource identity");
	const auto found = ids.find(json.at(key).get<std::string>());
	if (found == ids.end())
		throw std::runtime_error(path + "." + key + ": unresolved resource identity");
	return found->second;
}
std::optional<FlightDefinition> adapter_flight(const Json &spec, const std::string &id, const std::string &path)
{
	const auto kind = spec.value("kind", id);
	if (kind != "engine" && kind != "steering" && kind != "lateral" && kind != "boost" && kind != "impulse")
		return {};
	auto authored = spec;
	authored["kind"] = kind;
	return compile_flight(authored, path);
}
bool adapter_flight_costs(const std::optional<FlightDefinition> &flight)
{
	return flight &&
	       (flight->power_rate || flight->boost_power_rate || flight->impulse_charge_power_rate ||
	        flight->impulse_power_rate || flight->heat_rate || flight->boost_heat_rate || flight->impulse_heat_rate);
}
} // namespace
std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity_id)
{
	const auto &entity = catalog.entities.at(entity_id);
	const auto cached = catalog.resource_definitions.find(entity_id);
	const auto fitted = resolve_systems(catalog, entity_id);
	const bool advanced = uses_advanced_machinery(catalog, entity_id);
	if (cached == catalog.resource_definitions.end() && !entity.contains("resources"))
	{
		for (auto it = entity.at("systems").begin(); it != entity.at("systems").end(); ++it)
		{
			const auto &spec = it.value();
			const auto path = "entities." + entity_id + ".systems." + it.key();
			if (spec.contains("power_store") || spec.contains("generator") || spec.contains("power_group") ||
			    spec.contains("conduit") || spec.contains("cooling_link") ||
			    adapter_amount(spec, "energy_per_second", "entities." + entity_id) ||
			    adapter_amount(spec, "discrete_energy", "entities." + entity_id) ||
			    (advanced && adapter_flight_costs(adapter_flight(spec, it.key(), path))))
				throw std::runtime_error("entities." + entity_id +
				                         ": resource-dependent System has no resource network");
		}
		return {};
	}
	ShipResourcesDefinition compiled;
	compiled.network = cached == catalog.resource_definitions.end() ? compile_resource_network(entity.at("resources"))
	                                                                : cached->second;
	for (auto it = entity.at("systems").begin(); it != entity.at("systems").end(); ++it)
	{
		const auto &spec = it.value();
		const auto path = "entities." + entity_id + ".systems." + it.key();
		SystemResourceBinding b;
		b.id = it.key();
		b.kind = fitted.at(compiled.systems.size()).kind;
		b.store = adapter_reference(spec, "power_store", compiled.network.store_ids, path);
		b.generator = adapter_reference(spec, "generator", compiled.network.generator_ids, path);
		b.group = adapter_reference(spec, "power_group", compiled.network.group_ids, path);
		if (b.group == no_resource)
			b.group = adapter_reference(spec, "group", compiled.network.group_ids, path);
		b.conduit = adapter_reference(spec, "conduit", compiled.network.link_ids, path);
		b.cooling = adapter_reference(spec, "cooling_link", compiled.network.cooling_link_ids, path);
		b.energy_per_second = adapter_amount(spec, "energy_per_second", path);
		b.discrete_energy = adapter_amount(spec, "discrete_energy", path);
		if ((b.energy_per_second || b.discrete_energy || adapter_flight_costs(adapter_flight(spec, it.key(), path))) &&
		    b.store == no_resource)
			throw std::runtime_error(path +
			                         ".power_store: power-consuming or heat-producing System requires a local reserve");
		if (b.generator != no_resource)
		{
			const auto store = compiled.network.generators[b.generator].store;
			if (b.store != no_resource && b.store != store)
				throw std::runtime_error(path + ": generator control and power_store disagree");
			b.store = store;
		}
		compiled.systems.push_back(std::move(b));
	}
	for (ResourceIndex store = 0; store < compiled.network.stores.size(); ++store)
	{
		std::vector<std::size_t> users;
		std::optional<std::size_t> storage, generator;
		for (std::size_t i = 0; i < compiled.systems.size(); ++i)
			if (compiled.systems[i].store == store)
			{
				users.push_back(i);
				if (compiled.systems[i].kind == OperatingSystem::Storage)
					storage = i;
				if (compiled.systems[i].kind == OperatingSystem::Generator)
					generator = i;
			}
		if (storage)
			compiled.systems[*storage].store_modes = true;
		else if (generator)
			compiled.systems[*generator].store_modes = true;
		else if (users.size() == 1)
			compiled.systems[users.front()].store_modes = true;
	}
	Json identity = {{"network", compiled.network.signature}, {"systems", Json::array()}};
	for (const auto &b : compiled.systems)
		identity["systems"].push_back({b.id, static_cast<unsigned>(b.kind), b.store, b.generator, b.group, b.conduit,
		                               b.cooling, b.energy_per_second, b.discrete_energy, b.store_modes});
	compiled.signature = sha256(json_bytes(identity));
	return compiled;
}
std::map<std::string, SystemObservationSchema> compile_system_observations(const Catalog &catalog,
                                                                           const std::string &entity)
{
	std::map<std::string, SystemObservationSchema> result;
	const auto fitted = resolve_systems(catalog, entity);
	const auto resources = compile_ship_resources(catalog, entity);
	const bool advanced = uses_advanced_machinery(catalog, entity);
	for (std::size_t i = 0; i < fitted.size(); ++i)
	{
		auto &schema = result[fitted[i].id];
		auto add = [&](std::initializer_list<const char *> fields)
		{
			for (const auto *field : fields)
				schema.emplace(field, system_observation_type(field));
		};
		add({"enabled", "requested"});
		const auto kind = fitted[i].kind;
		if (advanced &&
		    (kind == OperatingSystem::Engine || kind == OperatingSystem::Steering || kind == OperatingSystem::Lateral ||
		     kind == OperatingSystem::Boost || kind == OperatingSystem::Impulse))
			add({"desired_speed",
			     "desired_heading",
			     "minimum_speed",
			     "assisted",
			     "longitudinal_thrust",
			     "lateral_thrust",
			     "manual_steering",
			     "boost",
			     "impulse",
			     "impulse_phase",
			     "impulse_progress",
			     "match_speed",
			     "selected_contact",
			     "matched_contact",
			     "match_stale",
			     "following",
			     "follow_stale",
			     "follow_distance",
			     "follow_speed_limit",
			     "matched_speed",
			     "actual_longitudinal",
			     "actual_lateral",
			     "actual_steering"});
		if (!resources)
			continue;
		const auto &binding = resources->systems.at(i);
		add({"supply_permille", "used_power", "reserved_power", "supply_limited"});
		if (binding.store != no_resource)
			add({"stored_energy", "energy_capacity", "incoming_power", "heat", "heat_capacity", "stress",
			     "thermal_stopped", "thermal_override", "overdrive", "charge_limit", "discharge_limit",
			     "passive_cooling", "actual_cooling", "depletion_ticks", "thermal_stop_ticks", "full_ticks",
			     "thermal_recovery_ticks"});
		if (binding.generator != no_resource)
			add({"generator_output", "actual_output"});
		if (binding.group != no_resource)
			add({"group_allocation", "flow_cut"});
		if (binding.conduit != no_resource)
			add({"link_isolated"});
		if (binding.cooling != no_resource)
			add({"cooling_allocation", "coolant_depletion_ticks", "actual_coolant_used", "coolant_stock"});
	}
	return result;
}
} // namespace phoenix
