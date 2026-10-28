module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

export module phoenix.gameplay.logistics.state;
export import phoenix.contracts;
export import phoenix.resources.stocks;

export namespace phoenix
{
struct ShipStockStores
{
	std::map<std::string, StockStore> stores;
};
struct MaterialStoreState
{
	std::string stock;
	std::map<std::string, std::int64_t> targets;
	bool held = false;
};
struct MaterialInventory
{
	std::vector<MaterialStoreState> stores;
	std::uint64_t revision = 1;
	std::int64_t handling_remainder = 0;
	std::map<std::string, std::int64_t> work_credit;
	std::set<EntityId> authorised_craft;
};
// A queued servicing transfer is native logistics work, not a work receipt.
// Attribution survives dispatch; only receiving the actual batch can publish work.
struct MaterialServiceAttribution
{
	std::string bay, native, process;
	std::uint64_t generation = 0;
};
struct MaterialRequest
{
	std::uint64_t id = 0;
	EntityId source = 0, destination = 0;
	std::size_t from = 0, to = 0;
	std::string material;
	std::int64_t remaining = 0;
	bool automatic = false;
	std::uint64_t cargo = 0;
	MaterialServiceAttribution service;
};
struct CargoDelivery
{
	std::uint64_t id = 0;
	EntityId source = 0, carrier = 0, target = 0;
	std::size_t from = 0, cargo = 0, to = 0;
	std::string material, phase = "loading";
	std::int64_t amount = 0, reserved = 0;
	std::string report = "loading";
	std::uint64_t report_tick = 0;
	bool collection = false, cancelled = false;
	std::string mission, payment;
	std::int64_t payment_amount = 0, payment_taken = 0, goods_taken = 0, paid = 0, collected = 0, delivered = 0;
};
struct MaterialGateway
{
	std::size_t from = 0, to = 0;
	std::int64_t throughput = 0;
};
struct MaterialBatch
{
	std::uint64_t id = 0, request = 0;
	EntityId source = 0, destination = 0, location = 0;
	std::size_t from = 0, to = 0, compartment = 0;
	std::string material;
	std::int64_t quantity = 0, edge_to = -1, travel = 0;
	bool returning = false;
	std::int64_t crossing = 0;
	std::uint64_t cargo = 0;
	MaterialServiceAttribution service;
};
void to_json(Json &, const MaterialServiceAttribution &);
void from_json(const Json &, MaterialServiceAttribution &);
void to_json(Json &, const CargoDelivery &);
void from_json(const Json &, CargoDelivery &);
void to_json(Json &, const MaterialStoreState &);
void from_json(const Json &, MaterialStoreState &);
void to_json(Json &, const MaterialInventory &);
void from_json(const Json &, MaterialInventory &);
void to_json(Json &, const MaterialRequest &);
void from_json(const Json &, MaterialRequest &);
void to_json(Json &, const MaterialBatch &);
void from_json(const Json &, MaterialBatch &);
} // namespace phoenix
