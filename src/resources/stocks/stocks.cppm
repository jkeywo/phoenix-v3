module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

export module phoenix.resources.stocks;
export import phoenix.data;

export namespace phoenix
{
using ResourceAmount = std::int64_t;
using ResourceIndex = std::uint32_t;
inline constexpr ResourceIndex no_resource = std::numeric_limits<ResourceIndex>::max();
inline constexpr ResourceAmount resource_limit = 1000000000000LL;
inline constexpr ResourceAmount resource_tick_hz = 20;
inline constexpr ResourceAmount resource_work_unit = resource_tick_hz * 1000;

// Missing fields default to zero; present fields must be bounded integers.
ResourceAmount read_resource_amount(const Json &, const char *key, const std::string &path,
                                    ResourceAmount maximum = resource_limit);

struct ResourceDefinition
{
	std::string id, label;
	std::set<std::string> tags;
	std::map<std::string, ResourceAmount> sizes;
	std::vector<std::string> effects;
};
using ResourceCatalogue = std::map<std::string, ResourceDefinition>;
using SharedResourceCatalogue = std::shared_ptr<const ResourceCatalogue>;
SharedResourceCatalogue share_resource_catalogue(ResourceCatalogue);
struct StockFilter
{
	std::set<std::string> tags, resources;
	bool require_all = false;
};
struct StockStoreDefinition
{
	std::string id, capacity_tag;
	ResourceAmount capacity = 0;
	StockFilter filter;
	bool single_resource = false;
	std::string selection;
	std::map<std::string, ResourceAmount> initial;
};
struct StockClaim
{
	std::string resource;
	ResourceAmount quantity = 0;
	bool operator==(const StockClaim &) const = default;
};
void validate_resource_catalogue(const ResourceCatalogue &);
void validate_stock_store(const StockStoreDefinition &, const ResourceCatalogue &);
bool stock_filter_accepts(const StockFilter &, const ResourceDefinition &);

class StockStore
{
public:
	StockStore() = default;
	StockStore(const StockStoreDefinition &, const ResourceCatalogue &);
	StockStore(const StockStoreDefinition &, SharedResourceCatalogue);
	const StockStoreDefinition &definition() const;
	const ResourceCatalogue &catalogue() const;
	const std::map<std::string, ResourceAmount> &quantities() const;
	const std::map<std::string, StockClaim> &capacity_claims() const;
	const std::map<std::string, StockClaim> &stock_claims() const;
	const std::string &selected() const;
	std::uint64_t revision() const;
	bool accepts(const std::string &) const;
	bool allows(const std::string &) const;
	ResourceAmount size(const std::string &) const;
	ResourceAmount quantity(const std::string &) const;
	ResourceAmount available(const std::string &) const;
	ResourceAmount occupied() const;
	ResourceAmount reserved_capacity() const;
	ResourceAmount headroom(const std::string &) const;
	bool add(const std::string &, ResourceAmount);
	bool consume(const std::string &, ResourceAmount);
	bool set(const std::string &, ResourceAmount);
	ResourceAmount fill(const std::string &);
	bool select(const std::string &);
	bool reserve_capacity(const std::string &key, const std::string &resource, ResourceAmount);
	bool claim_stock(const std::string &key, const std::string &resource, ResourceAmount);
	bool receive_reserved(const std::string &key);
	bool consume_claim(const std::string &key);
	bool release_capacity(const std::string &key);
	bool release_stock(const std::string &key);
	Json save() const;
	void restore(const Json &);
	void swap(StockStore &) noexcept;

private:
	friend class StockTransaction;
	struct Binding
	{
		StockStoreDefinition definition;
		SharedResourceCatalogue catalogue;
	};
	std::shared_ptr<const Binding> binding_;
	std::map<std::string, ResourceAmount> quantities_;
	std::map<std::string, StockClaim> capacity_claims_, stock_claims_;
	std::string selected_;
	std::uint64_t revision_ = 0;
	void validate_state() const;
	void changed();
};
void to_json(Json &, const StockStore &);
void from_json(const Json &, StockStore &);

// Each address is copied once. All exact operations see preceding staged work;
// failure poisons the group, and commit rejects stale original revisions.
class StockTransaction
{
public:
	bool add(StockStore &, const std::string &, ResourceAmount);
	bool consume(StockStore &, const std::string &, ResourceAmount);
	bool set(StockStore &, const std::string &, ResourceAmount);
	bool fill(StockStore &, const std::string &);
	bool select(StockStore &, const std::string &);
	bool transfer(StockStore &from, StockStore &to, const std::string &, ResourceAmount);
	bool reserve_capacity(StockStore &, const std::string &, const std::string &, ResourceAmount);
	bool claim_stock(StockStore &, const std::string &, const std::string &, ResourceAmount);
	bool receive_reserved(StockStore &, const std::string &);
	bool consume_claim(StockStore &, const std::string &);
	bool release_capacity(StockStore &, const std::string &);
	bool release_stock(StockStore &, const std::string &);
	const StockStore &view(StockStore &);
	bool commit();

private:
	struct Entry
	{
		std::uint64_t revision;
		StockStore staged;
		std::string selected;
		std::map<std::string, ResourceAmount> quantities;
		std::map<std::string, StockClaim> capacity_claims, stock_claims;
	};
	std::map<StockStore *, Entry> stores_;
	bool failed_ = false, committed_ = false;
	StockStore &stage(StockStore &);
	bool result(bool);
};

} // namespace phoenix
