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
void material_require(bool ok, const std::string &at)
{
	if (!ok)
		throw std::runtime_error("logistics: " + at);
}
std::int64_t material_number(const Json &j, const char *key, std::int64_t fallback, std::int64_t minimum = 0)
{
	const auto v = j.value(key, Json(fallback));
	material_require(v.is_number_integer() && !v.is_boolean() && v >= minimum && v <= 1000000000, key);
	return v.get<std::int64_t>();
}
} // namespace
LogisticsDefinition compile_logistics(const Catalog &catalog, const std::string &id)
{
	LogisticsDefinition d;
	const auto &e = catalog.entities.at(id);
	if (catalog.presentation)
		return d;
	if (!e.contains("logistics"))
		return d;
	const auto &j = e.at("logistics");
	material_require(j.is_object(), id + ": expected object");
	const auto structure = compile_combat(catalog, id);
	material_require(structure.has_value(), id + ": stores require compartments");
	material_require(!j.contains("materials") && j.at("stores").is_array() && j.at("stores").size() <= 128 &&
	                     j.value("consumers", Json::array()).is_array(),
	                 id + ": expected bounded material/store arrays");
	d.controller = j.at("controller");
	std::set<std::string> controllers;
	for (const auto &c : e.at("control_layout").at("controllers"))
		controllers.insert(c.at("id"));
	material_require(controllers.contains(d.controller), id + ": unknown logistics controller");
	d.throughput = material_number(j, "units_per_second", 20, 1);
	d.concurrency = material_number(j, "concurrency", 4, 1);
	d.batch_size = material_number(j, "batch_size", 10, 1);
	material_require(d.concurrency <= 128, id + ": excessive concurrency");
	const auto resources = compile_resource_catalog(catalog);
	for (const auto &[key, resource] : resources)
		if (resource.sizes.contains("cargo"))
			d.materials.emplace(key, MaterialDefinition{key, resource.sizes.at("cargo")});
	std::set<std::string> stores, systems;
	for (const auto &s : j.at("stores"))
	{
		material_require(!s.contains("capacity") && !s.contains("accepts") && !s.contains("initial"),
		                 id + ": retired store fields; use nested stock");
		MaterialStoreDefinition v;
		v.id = s.at("id");
		v.compartment = s.at("compartment");
		v.system = s.value("system", std::string{});
		v.controller = s.value("controller", d.controller);
		v.stock = compile_stock_store(catalog, s.at("stock"), v.id);
		v.capacity = v.stock.capacity;
		v.main = s.value("main", false);
		material_require(
		    !v.id.empty() && v.id.size() <= 64 &&
		        v.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
		            std::string::npos &&
		        stores.insert(v.id).second && controllers.contains(v.controller),
		    id + ": invalid store/owner");
		material_require(structure->compartment_ids.contains(v.compartment),
		                 id + ": unknown store compartment " + v.compartment);
		for (const auto &[key, resource] : resources)
			if (stock_filter_accepts(v.stock.filter, resource))
			{
				v.accepts.push_back(key);
				// Legacy logistics projections retain a resource-name index, not separate definitions.
				if (!d.materials.contains(key))
					d.materials.emplace(key, MaterialDefinition{key, resource.sizes.at(v.stock.capacity_tag)});
			}
		material_require(!v.accepts.empty() && v.accepts.size() <= 128, id + ": invalid store compatibility count");
		const auto targets = s.value("targets", Json::object());
		material_require(targets.is_object(), id + ": expected stock target object");
		for (const auto &[m, n] : targets.items())
		{
			material_require(std::find(v.accepts.begin(), v.accepts.end(), m) != v.accepts.end() &&
			                     n.is_number_integer() && !n.is_boolean() && n >= 0 && n <= 1000000000,
			                 id + ": invalid stock target");
			material_require(n.get<std::int64_t>() <= v.capacity / resources.at(m).sizes.at(v.stock.capacity_tag),
			                 id + ": stock target exceeds capacity");
			v.targets[m] = n.get<std::int64_t>();
		}
		if (!v.system.empty())
		{
			material_require(e.at("systems").contains(v.system) && systems.insert(v.system).second,
			                 id + ": invalid/duplicate store System");
			const auto &fit = e.at("systems").at(v.system);
			if (fit.value("kind", v.system) == "magazine")
			{
				const auto expected = compile_stock_store(catalog, fit.at("stock"), v.system);
				material_require(v.stock.capacity == expected.capacity &&
				                     v.stock.capacity_tag == expected.capacity_tag &&
				                     v.stock.filter.tags == expected.filter.tags &&
				                     v.stock.filter.resources == expected.filter.resources &&
				                     v.stock.filter.require_all == expected.filter.require_all &&
				                     v.stock.single_resource == expected.single_resource &&
				                     v.stock.selection == expected.selection && v.stock.initial == expected.initial,
				                 id + ": magazine logistics binding must describe the same stock");
			}
		}
		d.stores.push_back(std::move(v));
	}
	material_require(d.stores.size() <= 128, id + ": excessive stores");
	for (const auto &c : j.value("consumers", Json::array()))
	{
		MaterialConsumerDefinition v;
		v.system = c.at("system");
		v.store = c.at("store");
		bool exists = e.at("systems").contains(v.system);
		for (const auto &f : e.value("personnel", Json::object()).value("facilities", Json::array()))
			exists |= f.at("id") == v.system;
		material_require(exists && stores.contains(v.store), id + ": unknown consumer/store");
		for (const auto &[action, costs] : c.at("costs").items())
		{
			material_require(action == "repair" || action == "structural" || action == "treat" ||
			                     action == "stabilise" || action == "recover",
			                 id + ": unknown consumption activity");
			for (const auto &[m, n] : costs.items())
			{
				const auto &store = *std::find_if(d.stores.begin(), d.stores.end(),
				                                  [&](const auto &s)
				                                  {
					                                  return s.id == v.store;
				                                  });
				material_require(std::find(store.accepts.begin(), store.accepts.end(), m) != store.accepts.end() &&
				                     n.is_number_integer() && !n.is_boolean() && n > 0 && n <= 1000000000,
				                 id + ": invalid consumption cost");
				v.costs[action][m] = n.get<std::int64_t>();
			}
		}
		for (const auto &old : d.consumers)
			material_require(old.system != v.system, id + ": duplicate consumer");
		d.consumers.push_back(std::move(v));
	}
	return d;
}
} // namespace phoenix
