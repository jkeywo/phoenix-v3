module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.simulation;
import phoenix.content;
#include "gameplay_resources.hpp"

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
bool store_operational(const ShipResources &ship, ResourceIndex i)
{
	if (i == no_resource)
		return true;
	const auto &s = ship.network.stores()[i];
	const auto &d = ship.definition.network.stores[i];
	return s.enabled && (!s.thermal_stop || s.thermal_override) && (!d.stress_limit || s.stress < d.stress_limit);
}
ResourceAmount weighted_share(ResourceAmount quantity, ResourceAmount weight, ResourceAmount total)
{
	if (!total || !weight)
		return 0;
	// Long multiplication/division, avoiding unsupported 128-bit integers on MSVC
	// and avoiding quantity*weight overflow for valid trillion-unit stores.
	ResourceAmount result = 0, remainder = 0;
	for (int bit = 62; bit >= 0; --bit)
	{
		result *= 2;
		remainder *= 2;
		if ((quantity >> bit) & 1)
			remainder += weight;
		result += remainder / total;
		remainder %= total;
	}
	return result;
}
} // namespace

std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity_id)
{
	const auto &entity = catalog.entities.at(entity_id);
	const auto cached = catalog.resource_definitions.find(entity_id);
	const auto fitted = resolve_systems(catalog, entity_id);
	bool advanced = entity.contains("resources") || entity.contains("presets");
	for (const auto &system : fitted)
	{
		const auto &spec = entity.at("systems").at(system.id);
		advanced |= static_cast<unsigned>(system.kind) > static_cast<unsigned>(OperatingSystem::Radar) ||
		            catalog.tree_programs.contains(system.controller);
		for (const char *field : {"reverse_speed_mm_s", "boost", "impulse", "records", "power_rate", "heat_rate",
		                          "energy_per_second", "discrete_energy"})
			advanced |= spec.contains(field);
	}
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
ShipResources::ShipResources(ShipResourcesDefinition compiled, std::uint64_t seed)
    : definition(std::move(compiled)), network(definition.network, seed), requests(definition.systems.size()),
      grants(definition.systems.size())
{
	// Continuous paid equipment has no grant until its first funded tick.
	for (std::size_t i = 0; i < definition.systems.size(); ++i)
		if (definition.systems[i].energy_per_second)
			grants[i].supply_permille = 0;
}

void request_system_resource(ShipResources &ship, std::size_t system, ResourceAmount duty, bool discrete,
                             ResourceAmount extra)
{
	if (duty < 0 || duty > 1000 || extra < 0 || extra > resource_limit)
		throw std::runtime_error("resources: invalid System operating request");
	const auto &b = ship.definition.systems.at(system);
	if (extra && b.store == no_resource)
		throw std::runtime_error("resources: extra operating request has no store");
	if (b.energy_per_second * duty / 1000 > resource_limit - extra)
		throw std::runtime_error("resources: System request exceeds supported rate");
	auto &r = ship.requests.at(system);
	r.duty_permille = duty;
	r.discrete = discrete;
	r.extra_rate = extra;
}

bool can_control_resource(const ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value)
{
	if (system >= ship.definition.systems.size() || value < 0 || value > resource_limit)
		return false;
	const auto &b = ship.definition.systems[system];
	const auto &d = ship.definition.network;
	switch (control)
	{
	case ResourceControl::Enabled:
		return value <= 1 && ((b.kind == OperatingSystem::Storage && b.store != no_resource) ||
		                      (b.kind == OperatingSystem::Generator && b.generator != no_resource));
	case ResourceControl::GeneratorOutput:
		return b.kind == OperatingSystem::Generator && b.generator != no_resource &&
		       value <= d.generators[b.generator].maximum_rate;
	case ResourceControl::ChargeLimit:
		return b.kind == OperatingSystem::Storage && b.store != no_resource && value <= d.stores[b.store].charge_rate;
	case ResourceControl::DischargeLimit:
		return b.kind == OperatingSystem::Storage && b.store != no_resource &&
		       value <= d.stores[b.store].discharge_rate;
	case ResourceControl::GroupBudget:
		return b.kind == OperatingSystem::Distribution && b.group != no_resource;
	case ResourceControl::ConduitIsolated:
		return b.kind == OperatingSystem::Distribution && value <= 1 && b.conduit != no_resource;
	case ResourceControl::CoolingIsolated:
		return b.kind == OperatingSystem::Coolant && value <= 1 && b.cooling != no_resource;
	case ResourceControl::CoolingAllocation:
	{
		if (b.kind != OperatingSystem::Coolant || b.cooling == no_resource ||
		    value > d.cooling_links[b.cooling].capacity_rate)
			return false;
		const auto source = d.cooling_links[b.cooling].source;
		ResourceAmount total = value;
		for (ResourceIndex i = 0; i < d.cooling_links.size(); ++i)
			if (i != b.cooling && d.cooling_links[i].source == source)
				total += ship.network.cooling_links()[i].allocation_rate;
		return total <= d.coolant_sources[source].removal_rate;
	}
	case ResourceControl::Overdrive:
		return value <= 1 && b.store != no_resource && b.store_modes;
	case ResourceControl::ThermalOverride:
		return value <= 1 && b.store != no_resource && b.store_modes && (!value || d.stores[b.store].allows_override);
	}
	return false;
}
bool control_resource(ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value)
{
	if (!can_control_resource(ship, system, control, value))
		return false;
	const auto &b = ship.definition.systems[system];
	switch (control)
	{
	case ResourceControl::Enabled:
		if (b.generator != no_resource)
			ship.network.set_generator(b.generator, ship.network.generators()[b.generator].commanded_rate, value != 0);
		else if (b.kind == OperatingSystem::Storage && b.store != no_resource)
			ship.network.set_store_enabled(b.store, value != 0);
		else
			ship.network.set_cooling(b.cooling, ship.network.cooling_links()[b.cooling].allocation_rate, value == 0);
		break;
	case ResourceControl::GeneratorOutput:
		ship.network.set_generator(b.generator, value, ship.network.generators()[b.generator].enabled);
		break;
	case ResourceControl::ChargeLimit:
		ship.network.set_storage_limits(b.store, value, ship.network.stores()[b.store].discharge_limit);
		break;
	case ResourceControl::DischargeLimit:
		ship.network.set_storage_limits(b.store, ship.network.stores()[b.store].charge_limit, value);
		break;
	case ResourceControl::GroupBudget:
		ship.network.set_group_budget(b.group, value);
		break;
	case ResourceControl::ConduitIsolated:
		ship.network.set_link_isolated(b.conduit, value != 0);
		break;
	case ResourceControl::CoolingAllocation:
		ship.network.set_cooling(b.cooling, value, ship.network.cooling_links()[b.cooling].isolated);
		break;
	case ResourceControl::CoolingIsolated:
		ship.network.set_cooling(b.cooling, ship.network.cooling_links()[b.cooling].allocation_rate, value != 0);
		break;
	case ResourceControl::Overdrive:
		ship.network.set_overdrive(b.store, value != 0);
		break;
	case ResourceControl::ThermalOverride:
		ship.network.set_thermal_override(b.store, value != 0);
		break;
	}
	return true;
}

void tick_ship_resources(ShipResources &ship)
{
	const auto count = ship.definition.network.stores.size();
	std::vector<ResourceAmount> rates(ship.definition.systems.size()), store_rates(count), discrete(count);
	std::vector<std::size_t> discrete_owner(count, ship.definition.systems.size());
	for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
	{
		const auto &b = ship.definition.systems[i];
		auto &r = ship.requests[i];
		ship.grants[i] = {};
		if (b.store == no_resource)
			continue;
		const auto numerator = b.energy_per_second * r.duty_permille + r.rate_remainder;
		rates[i] = numerator / 1000 + r.extra_rate;
		r.rate_remainder = numerator % 1000;
		if (store_rates[b.store] > resource_limit - rates[i])
			throw std::runtime_error("resources: shared store requests exceed supported rate");
		store_rates[b.store] += rates[i];
		// Shared local reserves admit discrete requests in stable System order. Each
		// action remains atomic; a later action is not accidentally paid as a batch.
		if (r.discrete && b.discrete_energy && discrete_owner[b.store] == ship.definition.systems.size())
		{
			discrete_owner[b.store] = i;
			discrete[b.store] = b.discrete_energy;
		}
	}
	for (ResourceIndex i = 0; i < count; ++i)
		ship.network.request_operation(i, store_rates[i], discrete[i]);
	ship.network.tick();
	for (ResourceIndex store = 0; store < count; ++store)
	{
		const auto &t = ship.network.telemetry()[store];
		const auto requested = std::max<ResourceAmount>(0, t.requested - discrete[store]);
		const auto paid = std::max<ResourceAmount>(0, t.consumed - (t.discrete_paid ? discrete[store] : 0));
		ResourceAmount assigned_request = 0, assigned_paid = 0;
		for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
			if (ship.definition.systems[i].store == store)
			{
				auto &g = ship.grants[i];
				g.requested = weighted_share(requested, rates[i], store_rates[store]);
				assigned_request += g.requested;
			}
		for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
			if (ship.definition.systems[i].store == store && rates[i])
			{
				auto &g = ship.grants[i];
				if (assigned_request < requested)
				{
					++g.requested;
					++assigned_request;
				}
				g.supply_permille = static_cast<int>(weighted_share(1000, t.work_funded, t.work_requested));
			}
		for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
			if (ship.definition.systems[i].store == store)
			{
				auto &g = ship.grants[i];
				g.paid = weighted_share(paid, g.requested, requested);
				assigned_paid += g.paid;
			}
		for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
			if (ship.definition.systems[i].store == store && assigned_paid < paid &&
			    ship.grants[i].paid < ship.grants[i].requested)
			{
				++ship.grants[i].paid;
				++assigned_paid;
			}
		if (t.discrete_paid && discrete_owner[store] < ship.grants.size())
		{
			auto i = discrete_owner[store];
			ship.grants[i].discrete_paid = true;
			ship.grants[i].discrete = discrete[store];
			ship.requests[i].discrete = false;
		}
	}
	for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
	{
		const auto store = ship.definition.systems[i].store;
		auto &g = ship.grants[i];
		g.blocked = !store_operational(ship, store);
		if (g.blocked)
			g.supply_permille = 0;
		if (store != no_resource && ship.network.stores()[store].overdrive)
		{
			g.overdrive_permille = static_cast<int>(ship.definition.network.stores[store].overdrive_permille);
			g.overdrive_heat_permille = static_cast<int>(ship.definition.network.stores[store].overdrive_heat_permille);
		}
	}
}

Json project_system_resource(const ShipResources &ship, std::size_t i)
{
	const auto &b = ship.definition.systems.at(i);
	const auto &g = ship.grants.at(i);
	Json j = {{"system", b.id},
	          {"energy_requested", g.requested},
	          {"energy_paid", g.paid},
	          {"supply_permille", g.supply_permille},
	          {"overdrive_permille", g.overdrive_permille},
	          {"overdrive_heat_permille", g.overdrive_heat_permille},
	          {"discrete_paid", g.discrete_paid},
	          {"blocked", g.blocked}};
	if (b.store != no_resource)
	{
		const auto &s = ship.network.stores()[b.store];
		const auto &d = ship.definition.network.stores[b.store];
		const auto &t = ship.network.telemetry()[b.store];
		const auto f = ship.network.forecast(b.store);
		j["store"] = {{"id", d.id},
		              {"energy", s.energy},
		              {"capacity", d.capacity},
		              {"heat", s.heat},
		              {"stress", s.stress},
		              {"thermal_stop", s.thermal_stop},
		              {"enabled", s.enabled},
		              {"charge_limit", s.charge_limit},
		              {"discharge_limit", s.discharge_limit},
		              {"incoming", t.accepted_incoming},
		              {"outgoing", t.outgoing},
		              {"generated", t.generated},
		              {"consumed", t.consumed},
		              {"prepaid_work_credit", s.work_credit},
		              {"work_credit_denominator", resource_work_unit},
		              {"work_requested", t.work_requested},
		              {"work_funded", t.work_funded},
		              {"lost", t.lost},
		              {"heat_removed", t.heat_removed},
		              {"depletion_ticks", f.depletion_ticks},
		              {"full_ticks", f.full_ticks},
		              {"thermal_stop_ticks", f.thermal_stop_ticks},
		              {"thermal_recovery_ticks", f.thermal_recovery_ticks},
		              {"forecast_assumes_last_tick", true}};
	}
	if (b.generator != no_resource)
	{
		const auto &s = ship.network.generators()[b.generator];
		j["generator"] = {{"commanded_rate", s.commanded_rate}, {"actual_rate", s.actual_rate}, {"enabled", s.enabled}};
	}
	if (b.group != no_resource)
	{
		const auto &s = ship.network.groups()[b.group];
		j["group"] = {{"budget_rate", s.budget_rate}, {"cut", s.cut}, {"sustainable_ticks", s.sustainable_ticks}};
	}
	if (b.conduit != no_resource)
		j["conduit"] = {{"isolated", ship.network.links()[b.conduit].isolated}};
	if (b.cooling != no_resource)
	{
		const auto &s = ship.network.cooling_links()[b.cooling];
		const auto source = ship.definition.network.cooling_links[b.cooling].source;
		const auto f = ship.network.coolant_forecast(source);
		j["cooling"] = {{"allocation_rate", s.allocation_rate},
		                {"isolated", s.isolated},
		                {"coolant_remaining", ship.network.coolant_sources()[source].remaining},
		                {"coolant_depletion_ticks", f.depletion_ticks},
		                {"actual_heat_removed", f.observed_heat_removed},
		                {"actual_coolant_used", f.observed_coolant_used},
		                {"forecast_assumes_last_tick", true}};
	}
	return j;
}
Json save_ship_resources(const ShipResources &ship)
{
	Json j = {{"version", 1},
	          {"definition", ship.definition.signature},
	          {"network", ship.network.save()},
	          {"systems", Json::array()}};
	for (std::size_t i = 0; i < ship.definition.systems.size(); ++i)
	{
		const auto &r = ship.requests[i];
		const auto &g = ship.grants[i];
		j["systems"].push_back({{"id", ship.definition.systems[i].id},
		                        {"duty_permille", r.duty_permille},
		                        {"extra_rate", r.extra_rate},
		                        {"rate_remainder", r.rate_remainder},
		                        {"discrete", r.discrete},
		                        {"requested", g.requested},
		                        {"paid", g.paid},
		                        {"discrete_energy", g.discrete},
		                        {"supply_permille", g.supply_permille},
		                        {"overdrive_permille", g.overdrive_permille},
		                        {"overdrive_heat_permille", g.overdrive_heat_permille},
		                        {"discrete_paid", g.discrete_paid},
		                        {"blocked", g.blocked}});
	}
	return j;
}
void restore_ship_resources(ShipResources &ship, const Json &j)
{
	if (!j.is_object() || j.value("version", 0) != 1 ||
	    j.value("definition", std::string{}) != ship.definition.signature || !j.contains("systems") ||
	    !j.at("systems").is_array() || j.at("systems").size() != ship.definition.systems.size())
		throw std::runtime_error("resources.save: invalid fitted System roster");
	ShipResources next(ship.definition);
	next.network.restore(j.at("network"));
	for (std::size_t i = 0; i < next.requests.size(); ++i)
	{
		const auto &v = j.at("systems").at(i);
		const auto path = "resources.save.systems." + std::to_string(i);
		if (v.value("id", std::string{}) != next.definition.systems[i].id)
			throw std::runtime_error(path + ": identity mismatch");
		request_system_resource(next, i, adapter_amount(v, "duty_permille", path, 1000), v.at("discrete").get<bool>(),
		                        adapter_amount(v, "extra_rate", path));
		next.requests[i].rate_remainder = adapter_amount(v, "rate_remainder", path, 999);
		auto &g = next.grants[i];
		g.requested = adapter_amount(v, "requested", path);
		g.paid = adapter_amount(v, "paid", path);
		g.discrete = adapter_amount(v, "discrete_energy", path);
		g.supply_permille = static_cast<int>(adapter_amount(v, "supply_permille", path, 1000));
		g.overdrive_permille = static_cast<int>(adapter_amount(v, "overdrive_permille", path, 10000));
		g.overdrive_heat_permille = v.contains("overdrive_heat_permille")
		                                ? static_cast<int>(adapter_amount(v, "overdrive_heat_permille", path, 10000))
		                                : 1000;
		g.discrete_paid = v.at("discrete_paid").get<bool>();
		g.blocked = v.at("blocked").get<bool>();
		if (g.paid > g.requested || g.overdrive_permille < 1000 || g.overdrive_heat_permille < 1000 ||
		    (g.discrete_paid && g.discrete != next.definition.systems[i].discrete_energy))
			throw std::runtime_error(path + ": invalid paid operation result");
	}
	ship = std::move(next);
}
} // namespace phoenix
