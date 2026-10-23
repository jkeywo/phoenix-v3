// Included inside the exported phoenix namespace of phoenix.resources.
struct ResourceDefinition
{
	std::string id, label;
	std::set<std::string> tags;
	std::map<std::string, ResourceAmount> sizes;
	std::vector<std::string> effects;
};
using ResourceCatalogue = std::map<std::string, ResourceDefinition>;
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
ResourceCatalogue compile_resource_catalogue(const Json &);
StockStoreDefinition compile_stock_store(const Json &, const ResourceCatalogue &);
void validate_resource_catalogue(const ResourceCatalogue &);
void validate_stock_store(const StockStoreDefinition &, const ResourceCatalogue &);
bool stock_filter_accepts(const StockFilter &, const ResourceDefinition &);

class StockStore
{
public:
	StockStore() = default;
	StockStore(const StockStoreDefinition &, const ResourceCatalogue &);
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
	StockStoreDefinition definition_;
	ResourceCatalogue catalogue_;
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
