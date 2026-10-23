#pragma once
namespace phoenix
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
struct MaterialRequest
{
	std::uint64_t id = 0;
	EntityId source = 0, destination = 0;
	std::size_t from = 0, to = 0;
	std::string material;
	std::int64_t remaining = 0;
	bool automatic = false;
	std::uint64_t cargo = 0;
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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CargoDelivery, id, source, carrier, target, from, cargo, to, material, phase, amount,
                                   reserved, report, report_tick, collection, cancelled, mission, payment,
                                   payment_amount, payment_taken, goods_taken, paid, collected, delivered)
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
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialStoreState, stock, targets, held)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialInventory, stores, revision, handling_remainder, work_credit,
                                   authorised_craft)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialRequest, id, source, destination, from, to, material, remaining, automatic,
                                   cargo)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialBatch, id, request, source, destination, location, from, to, compartment,
                                   material, quantity, edge_to, travel, returning, crossing, cargo)
} // namespace phoenix
