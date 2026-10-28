module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.logistics;
export import phoenix.gameplay.logistics.state;

export namespace phoenix
{
// Restricted access to physical stock identities. Transaction staging preserves
// deduplication when several cargo roles name the same underlying store.
struct MaterialStockAccess
{
	std::function<StockStore &(EntityId, std::size_t)> at;
};
struct MaterialTransitPorts
{
	std::function<bool(EntityId)> unavailable;
	std::function<std::optional<MaterialGateway>(EntityId, EntityId, bool finishing)> gateway;
	std::function<bool(EntityId, std::size_t, std::size_t)> edge_open;
	std::function<std::pair<std::int64_t, std::int64_t>(EntityId, std::size_t, std::size_t)> route;
	std::function<std::size_t(EntityId, std::size_t)> store_compartment;
};
using MaterialCrossingBudgets = std::map<std::pair<EntityId, EntityId>, std::int64_t>;
enum class MaterialSelection
{
	Rejected,
	Unchanged,
	Changed
};
MaterialSelection select_material_resource(StockStore &, MaterialInventory *, EntityId, std::vector<MaterialRequest> &,
                                           const std::string &stock_key, const std::string &resource, bool execute);
bool return_material_batch(MaterialBatch &, const MaterialStockAccess &, bool source_accepts, bool execute);
std::string material_batch_claim(std::uint64_t id);
std::string cargo_claim_prefix(std::uint64_t id);
void clear_cargo_claims(StockTransaction &, const CargoDelivery &, const MaterialStockAccess &);
bool stage_cargo_claims(StockTransaction &, const CargoDelivery &, std::span<const MaterialBatch>,
                        const MaterialStockAccess &, const MaterialBatch *dispatch = nullptr);
bool stage_material_dispatch(StockTransaction &, const MaterialRequest &, std::int64_t quantity, std::uint64_t batch_id,
                             std::span<const CargoDelivery>, std::span<const MaterialBatch>,
                             const MaterialStockAccess &);
std::int64_t available_material(const StockStore &, const std::string &material, std::uint64_t exempt_cargo = 0);
std::int64_t pending_material(const StockStore &, EntityId, std::size_t store, const std::string &material,
                              std::span<const MaterialRequest>, std::span<const MaterialBatch>,
                              std::span<const CargoDelivery>);
// Advance one batch, then let its caller commit receipt/effects before advancing
// the next batch. Returning true means physically at its receiving compartment.
bool advance_material_transit(MaterialBatch &, const MaterialTransitPorts &, MaterialCrossingBudgets &,
                              std::int64_t ticks_per_second);
void record_cargo_delivery(std::span<CargoDelivery>, const MaterialBatch &);
} // namespace phoenix
