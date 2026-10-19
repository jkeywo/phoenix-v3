module;
#include <optional>
#include <variant>
#include <map>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.resources;

namespace phoenix
{
namespace
{
#include "validation.hpp"
constexpr ResourceAmount million = 1000000;
void array(const Json &object, const char *key)
{
	if (object.contains(key) && (!object.at(key).is_array() || object.at(key).size() > 4096))
		throw std::runtime_error(std::string("resources.") + key + ": expected array of at most 4096 records");
}
template <class T>
void identities(const Json &object, const char *key, std::vector<T> &out, std::map<std::string, ResourceIndex> &ids)
{
	array(object, key);
	if (!object.contains(key))
		return;
	for (const auto &record : object.at(key))
	{
		if (!record.is_object() || !record.contains("id") || !record.at("id").is_string())
			throw std::runtime_error(std::string("resources.") + key + ": record needs id");
		auto id = record.at("id").get<std::string>();
		if (!valid_identifier(id) || !ids.emplace(id, static_cast<ResourceIndex>(out.size())).second)
			throw std::runtime_error(std::string("resources.") + key + ": invalid/duplicate id " + id);
		T item;
		item.id = std::move(id);
		out.push_back(std::move(item));
	}
}
ResourceIndex reference(const Json &item, const char *field, const std::map<std::string, ResourceIndex> &ids,
                        bool optional = false)
{
	if (!item.contains(field) && optional)
		return no_resource;
	if (!item.contains(field) || !item.at(field).is_string())
		throw std::runtime_error(std::string("resources: missing reference ") + field);
	auto found = ids.find(item.at(field).get<std::string>());
	if (found == ids.end())
		throw std::runtime_error(std::string("resources: unresolved ") + field + " " +
		                         item.at(field).get<std::string>());
	return found->second;
}
} // namespace

ResourceNetworkDefinition compile_resource_network(const Json &authored)
{
	if (!authored.is_object())
		throw std::runtime_error("resources: expected object");
	Json json = authored;
	for (const char *key : {"stores", "generators", "links", "groups", "coolant_sources", "cooling_links"})
	{
		array(json, key);
		if (!json.contains(key))
			continue;
		for (const auto &record : json.at(key))
			if (!record.is_object() || !record.contains("id") || !record.at("id").is_string())
				throw std::runtime_error(std::string("resources.") + key + ": record needs id");
		std::sort(json.at(key).begin(), json.at(key).end(),
		          [](const Json &a, const Json &b)
		          {
			          return a.at("id").get<std::string>() < b.at("id").get<std::string>();
		          });
	}
	ResourceNetworkDefinition d;
	identities(json, "stores", d.stores, d.store_ids);
	identities(json, "generators", d.generators, d.generator_ids);
	identities(json, "links", d.links, d.link_ids);
	identities(json, "groups", d.groups, d.group_ids);
	identities(json, "coolant_sources", d.coolant_sources, d.coolant_source_ids);
	identities(json, "cooling_links", d.cooling_links, d.cooling_link_ids);
	for (ResourceIndex i = 0; i < d.stores.size(); ++i)
	{
		auto &v = d.stores[i];
		const auto &j = json.at("stores").at(i);
		v.capacity = amount(j, "capacity");
		v.initial = amount(j, "initial", 0, v.capacity);
		if (!v.capacity)
			throw std::runtime_error("resources.store " + v.id + ": capacity must be positive");
		v.charge_rate = amount(j, "charge_rate");
		v.discharge_rate = amount(j, "discharge_rate");
		v.loss_empty_ppm = amount(j, "loss_empty_ppm", 0, million);
		v.loss_full_ppm = amount(j, "loss_full_ppm", 0, million);
		v.stop_heat = amount(j, "stop_heat");
		v.recover_heat = amount(j, "recover_heat");
		v.initial_heat = amount(j, "initial_heat");
		if (v.stop_heat && v.recover_heat >= v.stop_heat)
			throw std::runtime_error("resources.store " + v.id + ": recovery must be below stop");
		v.passive_cooling = amount(j, "passive_cooling");
		v.charge_heat_ppm = amount(j, "charge_heat_ppm", 0, million);
		v.operation_heat_ppm = amount(j, "operation_heat_ppm", 0, million);
		v.depletion_heat_ppm = amount(j, "depletion_heat_ppm", 0, million);
		v.overdrive_permille = amount(j, "overdrive_permille", 1000, 10000);
		v.overdrive_heat_permille = amount(j, "overdrive_heat_permille", 1000, 10000);
		if (v.overdrive_permille < 1000 || v.overdrive_heat_permille < 1000)
			throw std::runtime_error("resources.store " + v.id + ": overdrive multipliers must be >= 1000");
		v.stress_rate = amount(j, "stress_rate");
		v.override_stress_rate = amount(j, "override_stress_rate");
		v.stress_limit = amount(j, "stress_limit");
		if ((v.stress_rate || v.override_stress_rate) && !v.stress_limit)
			throw std::runtime_error("resources.store " + v.id + ": stress needs finite positive limit");
		v.enabled = flag(j, "enabled", true);
		v.allows_override = flag(j, "allows_override");
		v.group = reference(j, "group", d.group_ids, true);
	}
	for (ResourceIndex i = 0; i < d.groups.size(); ++i)
	{
		auto &v = d.groups[i];
		const auto &j = json.at("groups").at(i);
		v.parent = reference(j, "parent", d.group_ids, true);
		v.budget_rate = amount(j, "budget_rate");
		v.restore_margin_rate = amount(j, "restore_margin_rate");
		v.restore_ticks = static_cast<std::uint32_t>(amount(j, "restore_ticks", 1, 1000000));
		if (!v.restore_ticks)
			throw std::runtime_error("resources.group " + v.id + ": restore_ticks must be positive");
		auto at = v.parent;
		std::size_t count = 0;
		while (at != no_resource)
		{
			if (at == i || ++count > d.groups.size())
				throw std::runtime_error("resources.groups: parent cycle");
			at = d.groups[at].parent;
		}
	}
	for (ResourceIndex i = 0; i < d.generators.size(); ++i)
	{
		auto &v = d.generators[i];
		const auto &j = json.at("generators").at(i);
		v.store = reference(j, "store", d.store_ids);
		v.maximum_rate = amount(j, "maximum_rate");
		v.response_rate = amount(j, "response_rate");
		v.initial_output = amount(j, "initial_output", 0, v.maximum_rate);
		v.heat_ppm = amount(j, "heat_ppm", 0, million);
		v.enabled = flag(j, "enabled", true);
		if (!v.maximum_rate || !v.response_rate)
			throw std::runtime_error("resources.generator " + v.id + ": positive maximum/response required");
	}
	for (ResourceIndex i = 0; i < d.groups.size(); ++i)
	{
		auto at = d.groups[i].parent;
		std::size_t count = 0;
		while (at != no_resource)
		{
			if (at == i || ++count > d.groups.size())
				throw std::runtime_error("resources.groups: parent cycle");
			at = d.groups[at].parent;
		}
	}
	for (ResourceIndex i = 0; i < d.links.size(); ++i)
	{
		auto &v = d.links[i];
		const auto &j = json.at("links").at(i);
		v.from = reference(j, "from", d.store_ids);
		v.to = reference(j, "to", d.store_ids);
		v.capacity_rate = amount(j, "capacity_rate");
		v.isolated = flag(j, "isolated");
		if (v.from == v.to || !v.capacity_rate)
			throw std::runtime_error("resources.link " + v.id + ": distinct endpoints and positive capacity required");
	}
	for (ResourceIndex i = 0; i < d.coolant_sources.size(); ++i)
	{
		auto &v = d.coolant_sources[i];
		const auto &j = json.at("coolant_sources").at(i);
		v.power_store = reference(j, "power_store", d.store_ids, true);
		v.initial = amount(j, "initial");
		v.removal_rate = amount(j, "removal_rate");
		v.coolant_per_heat_ppm = amount(j, "coolant_per_heat_ppm", million, million);
		if (!v.coolant_per_heat_ppm)
			throw std::runtime_error("resources.coolant: consumption ratio must be positive");
	}
	std::vector<ResourceAmount> cooling_reserved(d.coolant_sources.size());
	for (ResourceIndex i = 0; i < d.cooling_links.size(); ++i)
	{
		auto &v = d.cooling_links[i];
		const auto &j = json.at("cooling_links").at(i);
		v.source = reference(j, "source", d.coolant_source_ids);
		v.store = reference(j, "store", d.store_ids);
		v.capacity_rate = amount(j, "capacity_rate");
		v.allocation_rate = amount(j, "allocation_rate", 0, v.capacity_rate);
		v.isolated = flag(j, "isolated");
		cooling_reserved[v.source] += v.allocation_rate;
	}
	for (ResourceIndex i = 0; i < cooling_reserved.size(); ++i)
		if (cooling_reserved[i] > d.coolant_sources[i].removal_rate)
			throw std::runtime_error("resources.coolant: initial reservations exceed provider capacity");
	// Definition identity binds recovery to the resolved numeric network and authored parameters.
	const auto text = json.dump();
	d.signature = sha256(std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
	return d;
}

} // namespace phoenix
