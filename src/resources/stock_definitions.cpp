module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.resources;
namespace phoenix
{
namespace
{
bool stock_id(const std::string &s)
{
	return !s.empty() && s.size() <= 256 &&
	       std::all_of(s.begin(), s.end(),
	                   [](unsigned char c)
	                   {
		                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		                          c == '.' || c == '_' || c == '-' || c == ':';
	                   });
}
void stock_fields(const Json &j, const std::set<std::string> &allowed, const std::string &where)
{
	if (!j.is_object())
		throw std::runtime_error(where + ": expected object");
	for (const auto &[key, v] : j.items())
		if (!allowed.contains(key))
			throw std::runtime_error(where + ": unsupported field " + key);
}
ResourceAmount stock_number(const Json &j, ResourceAmount minimum, const std::string &where)
{
	if (!j.is_number_integer() || j.is_boolean() || j < minimum || j > resource_limit)
		throw std::runtime_error(where + ": amount outside supported bounds");
	return j.get<ResourceAmount>();
}
std::set<std::string> stock_names(const Json &j, const std::string &where)
{
	if (!j.is_array() || j.size() > 4096)
		throw std::runtime_error(where + ": expected bounded array");
	std::set<std::string> names;
	for (const auto &value : j)
	{
		if (!value.is_string() || !stock_id(value.get<std::string>()) || !names.insert(value.get<std::string>()).second)
			throw std::runtime_error(where + ": invalid or duplicate identity");
	}
	return names;
}
ResourceAmount stock_product(ResourceAmount a, ResourceAmount b)
{
	if (a < 0 || b <= 0 || a > resource_limit / b)
		throw std::runtime_error("stock: capacity product exceeds supported bounds");
	return a * b;
}
} // namespace
bool stock_filter_accepts(const StockFilter &filter, const ResourceDefinition &resource)
{
	const bool explicit_resource = filter.resources.contains(resource.id);
	const bool any_tag = std::any_of(filter.tags.begin(), filter.tags.end(),
	                                 [&](const auto &tag)
	                                 {
		                                 return resource.tags.contains(tag);
	                                 });
	if (!filter.require_all)
		return (filter.tags.empty() && filter.resources.empty()) || explicit_resource || any_tag;
	return (filter.resources.empty() || explicit_resource) && std::all_of(filter.tags.begin(), filter.tags.end(),
	                                                                      [&](const auto &tag)
	                                                                      {
		                                                                      return resource.tags.contains(tag);
	                                                                      });
}
void validate_resource_catalogue(const ResourceCatalogue &catalogue)
{
	if (catalogue.size() > 4096)
		throw std::runtime_error("resources: catalogue capacity exceeded");
	for (const auto &[id, d] : catalogue)
	{
		if (!stock_id(id) || d.id != id || d.label.size() > 512 || d.tags.size() > 64 || d.sizes.empty() ||
		    d.sizes.size() > 64 || d.effects.size() > 64)
			throw std::runtime_error("resources: invalid resource definition " + id);
		for (const auto &tag : d.tags)
			if (!stock_id(tag))
				throw std::runtime_error("resources: invalid tag " + id);
		for (const auto &[tag, size] : d.sizes)
			if (!stock_id(tag) || size <= 0 || size > resource_limit)
				throw std::runtime_error("resources: invalid capacity size " + id);
		std::set<std::string> effects;
		for (const auto &effect : d.effects)
			if (!stock_id(effect) || !effects.insert(effect).second)
				throw std::runtime_error("resources: invalid effect reference " + id);
	}
}
ResourceCatalogue compile_resource_catalogue(const Json &json)
{
	if ((!json.is_array() && !json.is_object()) || json.size() > 4096)
		throw std::runtime_error("resources: expected catalogue array or object");
	ResourceCatalogue result;
	auto record = [&](const Json &value, const std::string &key)
	{
		stock_fields(value, {"id", "label", "tags", "sizes", "effects"}, "resource");
		ResourceDefinition d;
		d.id = value.value("id", key);
		if (!key.empty() && d.id != key)
			throw std::runtime_error("resource: key/id mismatch");
		d.label = value.value("label", d.id);
		d.tags = stock_names(value.value("tags", Json::array()), "resource.tags");
		const auto &sizes = value.at("sizes");
		if (!sizes.is_object())
			throw std::runtime_error("resource.sizes: expected object");
		for (const auto &[tag, size] : sizes.items())
			d.sizes.emplace(tag, stock_number(size, 1, "resource.sizes"));
		const auto effects = stock_names(value.value("effects", Json::array()), "resource.effects");
		d.effects.assign(effects.begin(), effects.end());
		if (!result.emplace(d.id, d).second)
			throw std::runtime_error("resource: duplicate identity");
	};
	if (json.is_array())
		for (const auto &value : json)
			record(value, {});
	else
		for (const auto &[id, value] : json.items())
			record(value, id);
	validate_resource_catalogue(result);
	return result;
}
void validate_stock_store(const StockStoreDefinition &d, const ResourceCatalogue &catalogue)
{
	if (!stock_id(d.id) || !stock_id(d.capacity_tag) || d.capacity < 0 || d.capacity > resource_limit ||
	    d.filter.tags.size() > 64 || d.filter.resources.size() > 4096 || d.initial.size() > 4096)
		throw std::runtime_error("stock store: invalid definition " + d.id);
	for (const auto &id : d.filter.resources)
		if (!catalogue.contains(id))
			throw std::runtime_error("stock store: unknown accepted resource " + id);
	for (const auto &tag : d.filter.tags)
		if (!stock_id(tag))
			throw std::runtime_error("stock store: invalid filter tag");
	auto compatible = [&](const std::string &id)
	{
		const auto at = catalogue.find(id);
		return at != catalogue.end() && stock_filter_accepts(d.filter, at->second) &&
		       at->second.sizes.contains(d.capacity_tag);
	};
	for (const auto &[id, resource] : catalogue)
		if (stock_filter_accepts(d.filter, resource) && !resource.sizes.contains(d.capacity_tag))
			throw std::runtime_error("stock store " + d.id + ": accepted resource lacks capacity size " + id);
	std::string selection = d.selection;
	if (d.single_resource && d.filter.tags.empty() && d.filter.resources.size() == 1)
	{
		if (!selection.empty() && selection != *d.filter.resources.begin())
			throw std::runtime_error("stock store: fixed resource selection mismatch");
		selection = *d.filter.resources.begin();
	}
	if ((!d.single_resource && !selection.empty()) || (!selection.empty() && !compatible(selection)))
		throw std::runtime_error("stock store: invalid selected resource");
	ResourceAmount occupied = 0;
	for (const auto &[id, quantity] : d.initial)
	{
		if (quantity < 0 || quantity > resource_limit || !compatible(id) || (d.single_resource && selection != id))
			throw std::runtime_error("stock store: invalid initial stock");
		const auto size = stock_product(quantity, catalogue.at(id).sizes.at(d.capacity_tag));
		if (size > d.capacity - occupied)
			throw std::runtime_error("stock store: initial stock exceeds capacity");
		occupied += size;
	}
}
StockStoreDefinition compile_stock_store(const Json &j, const ResourceCatalogue &catalogue)
{
	stock_fields(j, {"id", "capacity_tag", "capacity", "filter", "single_resource", "selection", "initial"},
	             "stock store");
	StockStoreDefinition d;
	d.id = j.at("id").get<std::string>();
	d.capacity_tag = j.at("capacity_tag").get<std::string>();
	d.capacity = stock_number(j.at("capacity"), 0, "stock store.capacity");
	d.single_resource = j.value("single_resource", false);
	d.selection = j.value("selection", std::string{});
	if (j.contains("filter"))
	{
		const auto &f = j.at("filter");
		stock_fields(f, {"tags", "resources", "mode", "require_all"}, "stock store.filter");
		d.filter.tags = stock_names(f.value("tags", Json::array()), "stock store.filter.tags");
		d.filter.resources = stock_names(f.value("resources", Json::array()), "stock store.filter.resources");
		const auto mode = f.value("mode", std::string("or"));
		if (mode != "and" && mode != "or")
			throw std::runtime_error("stock store.filter: unsupported mode");
		d.filter.require_all = f.value("require_all", mode == "and");
		if (f.contains("mode") && f.contains("require_all") && d.filter.require_all != (mode == "and"))
			throw std::runtime_error("stock store.filter: conflicting composition");
	}
	if (j.contains("initial"))
	{
		if (!j.at("initial").is_object())
			throw std::runtime_error("stock store.initial: expected object");
		for (const auto &[id, amount] : j.at("initial").items())
			d.initial.emplace(id, stock_number(amount, 0, "stock store.initial"));
	}
	validate_stock_store(d, catalogue);
	return d;
}
} // namespace phoenix
