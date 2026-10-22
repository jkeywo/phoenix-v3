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
	std::set<std::string> closed_routes;
	std::map<std::string, std::int64_t> work_credit;
};
struct MaterialRequest
{
	std::uint64_t id = 0;
	EntityId source = 0, destination = 0;
	std::size_t from = 0, to = 0;
	std::string material;
	std::int64_t remaining = 0;
	bool automatic = false;
};
struct MaterialBatch
{
	std::uint64_t id = 0, request = 0;
	EntityId source = 0, destination = 0, location = 0;
	std::size_t from = 0, to = 0, compartment = 0;
	std::string material;
	std::int64_t quantity = 0, edge_to = -1, travel = 0;
	bool returning = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialStoreState, quantities, targets, held)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialInventory, stores, revision, handling_remainder, closed_routes, work_credit)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialRequest, id, source, destination, from, to, material, remaining, automatic)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaterialBatch, id, request, source, destination, location, from, to, compartment,
                                   material, quantity, edge_to, travel, returning)
} // namespace phoenix
