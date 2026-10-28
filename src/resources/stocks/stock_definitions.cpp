module;
#include <algorithm>
#include <cstdint>
#include <memory>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
module phoenix.resources.stocks;
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
SharedResourceCatalogue share_resource_catalogue(ResourceCatalogue catalogue)
{
	validate_resource_catalogue(catalogue);
	return std::make_shared<const ResourceCatalogue>(std::move(catalogue));
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
} // namespace phoenix
