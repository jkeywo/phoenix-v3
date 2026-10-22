#pragma once
namespace phoenix
{
struct MaterialStoreState
{
	std::map<std::string, std::int64_t> quantities, targets;
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
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CargoDelivery, id, source, carrier, target, from, cargo, to, material, phase, amount,
                                   reserved, report, report_tick)
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
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialStoreState, quantities, targets, held)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialInventory, stores, revision, handling_remainder, work_credit,
                                   authorised_craft)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialRequest, id, source, destination, from, to, material, remaining, automatic,
                                   cargo)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialBatch, id, request, source, destination, location, from, to, compartment,
                                   material, quantity, edge_to, travel, returning, crossing)
} // namespace phoenix
