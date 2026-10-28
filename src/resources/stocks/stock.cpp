module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.resources.stocks;
namespace phoenix
{
namespace
{
constexpr std::uint64_t stock_revision_limit = 9007199254740991ULL;
bool claim_key(const std::string &key)
{
	// Internal owner keys combine bounded processor and resource identities.
	return !key.empty() && key.size() <= 512;
}
bool stock_amount(ResourceAmount value)
{
	return value >= 0 && value <= resource_limit;
}
ResourceAmount checked_stock_product(ResourceAmount amount, ResourceAmount size)
{
	if (!stock_amount(amount) || size <= 0 || size > resource_limit || amount > resource_limit / size)
		throw std::runtime_error("stock: capacity product exceeds supported bounds");
	return amount * size;
}
void stock_sum(ResourceAmount &total, ResourceAmount amount)
{
	if (amount < 0 || amount > resource_limit - total)
		throw std::runtime_error("stock: capacity sum exceeds supported bounds");
	total += amount;
}
void exact_fields(const Json &value, const std::set<std::string> &fields)
{
	if (!value.is_object() || value.size() != fields.size())
		throw std::runtime_error("stock: invalid saved fields");
	for (const auto &[field, v] : value.items())
		if (!fields.contains(field))
			throw std::runtime_error("stock: unknown saved field");
}
ResourceAmount saved_amount(const Json &value)
{
	if (!value.is_number_integer() || value.is_boolean() || value <= 0 || value > resource_limit)
		throw std::runtime_error("stock: invalid saved quantity");
	return value.get<ResourceAmount>();
}
} // namespace
StockStore::StockStore(const StockStoreDefinition &definition, const ResourceCatalogue &catalogue)
    : StockStore(definition, share_resource_catalogue(catalogue))
{
}
StockStore::StockStore(const StockStoreDefinition &definition, SharedResourceCatalogue catalogue)
    : binding_(std::make_shared<const Binding>(Binding{definition, std::move(catalogue)})),
      selected_(definition.selection), revision_(1)
{
	if (!binding_->catalogue)
		throw std::runtime_error("stock: missing resource catalogue");
	validate_stock_store(definition, *binding_->catalogue);
	if (definition.single_resource && definition.filter.tags.empty() && definition.filter.resources.size() == 1)
		selected_ = *definition.filter.resources.begin();
	for (const auto &[resource, amount] : definition.initial)
		if (amount)
			quantities_.emplace(resource, amount);
	validate_state();
}
const StockStoreDefinition &StockStore::definition() const
{
	if (!binding_)
		throw std::runtime_error("stock: unbound store definition");
	return binding_->definition;
}
const ResourceCatalogue &StockStore::catalogue() const
{
	if (!binding_)
		throw std::runtime_error("stock: unbound resource catalogue");
	return *binding_->catalogue;
}
const std::map<std::string, ResourceAmount> &StockStore::quantities() const
{
	return quantities_;
}
const std::map<std::string, StockClaim> &StockStore::capacity_claims() const
{
	return capacity_claims_;
}
const std::map<std::string, StockClaim> &StockStore::stock_claims() const
{
	return stock_claims_;
}
const std::string &StockStore::selected() const
{
	return selected_;
}
std::uint64_t StockStore::revision() const
{
	return revision_;
}
bool StockStore::allows(const std::string &resource) const
{
	if (!binding_)
		return false;
	const auto found = catalogue().find(resource);
	return revision_ && found != catalogue().end() && stock_filter_accepts(definition().filter, found->second) &&
	       found->second.sizes.contains(definition().capacity_tag);
}
bool StockStore::accepts(const std::string &resource) const
{
	return allows(resource) && (!definition().single_resource || selected_ == resource);
}
ResourceAmount StockStore::size(const std::string &resource) const
{
	if (!binding_)
		return 0;
	const auto found = catalogue().find(resource);
	return found != catalogue().end() && found->second.sizes.contains(definition().capacity_tag)
	           ? found->second.sizes.at(definition().capacity_tag)
	           : 0;
}
ResourceAmount StockStore::quantity(const std::string &resource) const
{
	const auto found = quantities_.find(resource);
	return found == quantities_.end() ? 0 : found->second;
}
ResourceAmount StockStore::available(const std::string &resource) const
{
	auto amount = quantity(resource);
	for (const auto &[key, claim] : stock_claims_)
		if (claim.resource == resource)
			amount -= claim.quantity;
	return amount;
}
ResourceAmount StockStore::occupied() const
{
	ResourceAmount total = 0;
	for (const auto &[resource, amount] : quantities_)
		stock_sum(total, checked_stock_product(amount, size(resource)));
	return total;
}
ResourceAmount StockStore::reserved_capacity() const
{
	ResourceAmount total = 0;
	for (const auto &[key, claim] : capacity_claims_)
		stock_sum(total, checked_stock_product(claim.quantity, size(claim.resource)));
	return total;
}
ResourceAmount StockStore::headroom(const std::string &resource) const
{
	if (!accepts(resource))
		return 0;
	return (definition().capacity - occupied() - reserved_capacity()) / size(resource);
}
void StockStore::changed()
{
	if (!revision_ || revision_ >= stock_revision_limit)
		throw std::runtime_error("stock: revision exhausted or unbound store");
	++revision_;
}
bool StockStore::add(const std::string &resource, ResourceAmount amount)
{
	if (!stock_amount(amount) || !accepts(resource) || amount > headroom(resource))
		return false;
	if (!amount)
		return true;
	changed();
	quantities_[resource] += amount;
	return true;
}
bool StockStore::consume(const std::string &resource, ResourceAmount amount)
{
	if (!stock_amount(amount) || !accepts(resource) || amount > available(resource))
		return false;
	if (!amount)
		return true;
	changed();
	const auto remaining = quantity(resource) - amount;
	if (remaining)
		quantities_[resource] = remaining;
	else
		quantities_.erase(resource);
	return true;
}
bool StockStore::set(const std::string &resource, ResourceAmount amount)
{
	if (!stock_amount(amount) || !accepts(resource))
		return false;
	const auto current = quantity(resource);
	return amount >= current ? add(resource, amount - current) : consume(resource, current - amount);
}
ResourceAmount StockStore::fill(const std::string &resource)
{
	if (!accepts(resource))
		return -1;
	const auto amount = headroom(resource);
	return add(resource, amount) ? amount : -1;
}
bool StockStore::select(const std::string &resource)
{
	if (!binding_ || !definition().single_resource || !allows(resource))
		return false;
	if (selected_ == resource)
		return true;
	if (!quantities_.empty() || !capacity_claims_.empty() || !stock_claims_.empty())
		return false;
	if (definition().filter.tags.empty() && definition().filter.resources.size() == 1 &&
	    resource != *definition().filter.resources.begin())
		return false;
	changed();
	selected_ = resource;
	return true;
}
bool StockStore::reserve_capacity(const std::string &key, const std::string &resource, ResourceAmount amount)
{
	if (!claim_key(key) || capacity_claims_.contains(key) || capacity_claims_.size() >= 4096 || amount <= 0 ||
	    !stock_amount(amount) || !accepts(resource) || amount > headroom(resource))
		return false;
	changed();
	capacity_claims_.emplace(key, StockClaim{resource, amount});
	return true;
}
bool StockStore::claim_stock(const std::string &key, const std::string &resource, ResourceAmount amount)
{
	if (!claim_key(key) || stock_claims_.contains(key) || stock_claims_.size() >= 4096 || amount <= 0 ||
	    !stock_amount(amount) || !accepts(resource) || amount > available(resource))
		return false;
	changed();
	stock_claims_.emplace(key, StockClaim{resource, amount});
	return true;
}
bool StockStore::receive_reserved(const std::string &key)
{
	const auto found = capacity_claims_.find(key);
	if (found == capacity_claims_.end())
		return false;
	const auto claim = found->second;
	changed();
	capacity_claims_.erase(found);
	quantities_[claim.resource] += claim.quantity;
	return true;
}
bool StockStore::consume_claim(const std::string &key)
{
	const auto found = stock_claims_.find(key);
	if (found == stock_claims_.end())
		return false;
	const auto claim = found->second;
	changed();
	stock_claims_.erase(found);
	const auto remaining = quantity(claim.resource) - claim.quantity;
	if (remaining)
		quantities_[claim.resource] = remaining;
	else
		quantities_.erase(claim.resource);
	return true;
}
bool StockStore::release_capacity(const std::string &key)
{
	if (!capacity_claims_.contains(key))
		return false;
	changed();
	capacity_claims_.erase(key);
	return true;
}
bool StockStore::release_stock(const std::string &key)
{
	if (!stock_claims_.contains(key))
		return false;
	changed();
	stock_claims_.erase(key);
	return true;
}
void StockStore::validate_state() const
{
	if (!revision_ || revision_ > stock_revision_limit || quantities_.size() > 4096 || capacity_claims_.size() > 4096 ||
	    stock_claims_.size() > 4096)
		throw std::runtime_error("stock: invalid saved store bounds");
	if ((!definition().single_resource && !selected_.empty()) ||
	    (definition().single_resource && !selected_.empty() && !allows(selected_)))
		throw std::runtime_error("stock: incompatible saved selection");
	if (definition().single_resource && definition().filter.tags.empty() && definition().filter.resources.size() == 1 &&
	    selected_ != *definition().filter.resources.begin())
		throw std::runtime_error("stock: fixed resource selection changed");
	for (const auto &[resource, quantity] : quantities_)
		if (!accepts(resource) || quantity <= 0 || !stock_amount(quantity))
			throw std::runtime_error("stock: invalid saved contents");
	for (const auto *claims : {&capacity_claims_, &stock_claims_})
		for (const auto &[key, claim] : *claims)
			if (!claim_key(key) || !accepts(claim.resource) || claim.quantity <= 0 || !stock_amount(claim.quantity))
				throw std::runtime_error("stock: invalid saved claim");
	const auto used = occupied(), reserved = reserved_capacity();
	if (used > definition().capacity || reserved > definition().capacity - used)
		throw std::runtime_error("stock: saved capacity exceeded");
	std::map<std::string, ResourceAmount> claims;
	for (const auto &[key, claim] : stock_claims_)
		stock_sum(claims[claim.resource], claim.quantity);
	for (const auto &[resource, amount] : claims)
		if (amount > quantity(resource))
			throw std::runtime_error("stock: claims exceed actual saved stock");
}
Json StockStore::save() const
{
	validate_state();
	Json capacities = Json::object(), stocks = Json::object();
	for (const auto &[key, claim] : capacity_claims_)
		capacities[key] = {{"resource", claim.resource}, {"quantity", claim.quantity}};
	for (const auto &[key, claim] : stock_claims_)
		stocks[key] = {{"resource", claim.resource}, {"quantity", claim.quantity}};
	return {{"store", definition().id},      {"selection", selected_}, {"quantities", quantities_},
	        {"capacity_claims", capacities}, {"stock_claims", stocks}, {"revision", revision_}};
}
void StockStore::restore(const Json &json)
{
	if (!revision_)
		throw std::runtime_error("stock: restore requires a bound definition");
	exact_fields(json, {"store", "selection", "quantities", "capacity_claims", "stock_claims", "revision"});
	if (json.at("store") != definition().id || !json.at("selection").is_string() ||
	    !json.at("revision").is_number_integer() || json.at("revision").is_boolean() || json.at("revision") <= 0 ||
	    json.at("revision") > stock_revision_limit)
		throw std::runtime_error("stock: saved identity/revision mismatch");
	auto candidate = *this;
	candidate.selected_ = json.at("selection").get<std::string>();
	candidate.revision_ = json.at("revision").get<std::uint64_t>();
	candidate.quantities_.clear();
	candidate.capacity_claims_.clear();
	candidate.stock_claims_.clear();
	for (const char *field : {"quantities", "capacity_claims", "stock_claims"})
		if (!json.at(field).is_object() || json.at(field).size() > 4096)
			throw std::runtime_error("stock: invalid saved collection");
	for (const auto &[resource, quantity] : json.at("quantities").items())
		candidate.quantities_.emplace(resource, saved_amount(quantity));
	for (const auto &[field, claims] : std::map<std::string, std::map<std::string, StockClaim> *>{
	         {"capacity_claims", &candidate.capacity_claims_}, {"stock_claims", &candidate.stock_claims_}})
		for (const auto &[key, value] : json.at(field).items())
		{
			exact_fields(value, {"resource", "quantity"});
			claims->emplace(key,
			                StockClaim{value.at("resource").get<std::string>(), saved_amount(value.at("quantity"))});
		}
	candidate.validate_state();
	swap(candidate);
}
void to_json(Json &json, const StockStore &store)
{
	json = store.save();
}
void from_json(const Json &json, StockStore &store)
{
	store.restore(json);
}
StockStore &StockTransaction::stage(StockStore &store)
{
	if (failed_ || committed_)
		throw std::runtime_error("stock: transaction is closed");
	if (const auto found = stores_.find(&store); found != stores_.end())
		return found->second.staged;
	auto [it, inserted] = stores_.emplace(&store, Entry{store.revision_, store, store.selected_, store.quantities_,
	                                                    store.capacity_claims_, store.stock_claims_});
	return it->second.staged;
}
bool StockTransaction::result(bool value)
{
	failed_ = failed_ || !value;
	return value;
}
const StockStore &StockTransaction::view(StockStore &store)
{
	return stage(store);
}
#define STOCK_OPERATION(method, args, call)                                                                            \
	bool StockTransaction::method args                                                                                 \
	{                                                                                                                  \
		if (failed_ || committed_)                                                                                     \
			return false;                                                                                              \
		return result(stage(store).method call);                                                                       \
	}
STOCK_OPERATION(add, (StockStore & store, const std::string &resource, ResourceAmount amount), (resource, amount))
STOCK_OPERATION(consume, (StockStore & store, const std::string &resource, ResourceAmount amount), (resource, amount))
STOCK_OPERATION(set, (StockStore & store, const std::string &resource, ResourceAmount amount), (resource, amount))
STOCK_OPERATION(select, (StockStore & store, const std::string &resource), (resource))
STOCK_OPERATION(reserve_capacity,
                (StockStore & store, const std::string &key, const std::string &resource, ResourceAmount amount),
                (key, resource, amount))
STOCK_OPERATION(claim_stock,
                (StockStore & store, const std::string &key, const std::string &resource, ResourceAmount amount),
                (key, resource, amount))
STOCK_OPERATION(receive_reserved, (StockStore & store, const std::string &key), (key))
STOCK_OPERATION(consume_claim, (StockStore & store, const std::string &key), (key))
STOCK_OPERATION(release_capacity, (StockStore & store, const std::string &key), (key))
STOCK_OPERATION(release_stock, (StockStore & store, const std::string &key), (key))
#undef STOCK_OPERATION
bool StockTransaction::fill(StockStore &store, const std::string &resource)
{
	if (failed_ || committed_)
		return false;
	return result(stage(store).fill(resource) >= 0);
}
bool StockTransaction::transfer(StockStore &from, StockStore &to, const std::string &resource, ResourceAmount amount)
{
	return consume(from, resource, amount) && add(to, resource, amount);
}
void StockStore::swap(StockStore &other) noexcept
{
	binding_.swap(other.binding_);
	quantities_.swap(other.quantities_);
	capacity_claims_.swap(other.capacity_claims_);
	stock_claims_.swap(other.stock_claims_);
	selected_.swap(other.selected_);
	std::swap(revision_, other.revision_);
}
bool StockTransaction::commit()
{
	if (failed_ || committed_)
		return false;
	for (const auto &[store, entry] : stores_)
	{
		if (store->revision_ != entry.revision || store->selected_ != entry.selected ||
		    store->quantities_ != entry.quantities || store->capacity_claims_ != entry.capacity_claims ||
		    store->stock_claims_ != entry.stock_claims)
			return result(false);
		if (store->binding_ != entry.staged.binding_)
			return result(false);
	}
	static_assert(noexcept(std::declval<StockStore &>().swap(std::declval<StockStore &>())));
	for (auto &[store, entry] : stores_)
		store->swap(entry.staged);
	committed_ = true;
	return true;
}
} // namespace phoenix
