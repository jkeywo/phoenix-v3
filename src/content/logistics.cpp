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
 if (catalog.presentation) return d;
	if (!e.contains("logistics"))
		return d;
	const auto &j = e.at("logistics");
	material_require(j.is_object(), id + ": expected object");
	const auto structure = compile_combat(catalog, id);
	material_require(structure.has_value(), id + ": stores require compartments");
    material_require(j.at("materials").is_array() && j.at("materials").size()<=128 && j.at("stores").is_array() && j.at("stores").size()<=128 && j.value("consumers",Json::array()).is_array(), id + ": expected bounded material/store arrays");
	d.controller = j.at("controller");
	std::set<std::string> controllers;
	for (const auto &c : e.at("control_layout").at("controllers"))
		controllers.insert(c.at("id"));
	material_require(controllers.contains(d.controller), id + ": unknown logistics controller");
	d.throughput = material_number(j, "units_per_second", 20, 1);
	d.concurrency = material_number(j, "concurrency", 4, 1);
	d.batch_size = material_number(j, "batch_size", 10, 1);
	material_require(d.concurrency <= 128, id + ": excessive concurrency");
	for (const auto &m : j.at("materials"))
	{
		const auto key = m.at("id").get<std::string>();
		material_require(!key.empty() && key.size() <= 64 && key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos && !d.materials.contains(key),
		                 id + ": duplicate/invalid material");
		d.materials.emplace(key, MaterialDefinition{key, material_number(m, "volume", 1, 1)});
	}
	material_require(!d.materials.empty() && d.materials.size() <= 128, id + ": invalid material count");
	std::set<std::string> stores, systems;
	for (const auto &s : j.at("stores"))
	{
		MaterialStoreDefinition v;
		v.id = s.at("id");
		v.compartment = s.at("compartment");
		v.system = s.value("system", std::string{});
		v.controller = s.value("controller", d.controller);
		v.capacity = material_number(s, "capacity", 0, 1);
		v.main = s.value("main", false);
		material_require(!v.id.empty() && v.id.size() <= 64 && v.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos && stores.insert(v.id).second &&
		                     controllers.contains(v.controller),
		                 id + ": invalid store/owner");
		material_require(structure->compartment_ids.contains(v.compartment),
		                 id + ": unknown store compartment " + v.compartment);
		v.accepts = s.at("accepts").get<std::vector<std::string>>();
		material_require(!v.accepts.empty(), id + ": empty store compatibility");
		std::set<std::string> accepted;
		for (const auto &m : v.accepts)
			material_require(d.materials.contains(m) && accepted.insert(m).second, id + ": invalid accepted material");
		std::int64_t volume = 0;
		for (const auto *field : {"initial", "targets"})
		{
			const auto values = s.value(field, Json::object());
			for (const auto &[m, n] : values.items())
			{
				material_require(accepted.contains(m) && n.is_number_integer() && !n.is_boolean() && n >= 0 &&
				                     n <= 1000000000,
				                 id + ": invalid stock/target");
				const auto count = n.get<std::int64_t>();
				material_require(count * d.materials.at(m).volume <= v.capacity,
				                 id + ": stock/target exceeds capacity");
				(std::string(field) == "initial" ? v.initial : v.targets)[m] = count;
				if (std::string(field) == "initial")
					volume += count * d.materials.at(m).volume;
			}
		}
		material_require(volume <= v.capacity, id + ": initial shared capacity exceeded");
		if (!v.system.empty())
		{
			material_require(e.at("systems").contains(v.system) && systems.insert(v.system).second,
			                 id + ": invalid/duplicate store System");
			const auto &fit = e.at("systems").at(v.system);
			if (fit.value("kind", v.system) == "magazine")
			{
				material_require(v.accepts.size() == 1 && v.initial.empty(),
				                 id + ": magazine stock uses magazine initial");
				material_require(v.capacity ==
				                     fit.at("capacity").get<std::int64_t>() * d.materials.at(v.accepts[0]).volume,
				                 id + ": magazine capacity mismatch");
				material_require(fit.value("reload", Json::object()).value("strategy", std::string{"none"}) == "none",
				                 id + ": supplied magazine cannot generate ammunition");
				material_require(fit.value("ammunition_type", v.accepts[0]) == v.accepts[0],
				                 id + ": ammunition type mismatch");
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
