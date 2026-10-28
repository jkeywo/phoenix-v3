module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.logistics.types;
export import phoenix.data;
export import phoenix.resources;

export namespace phoenix
{
struct MaterialDefinition
{
	std::string id;
	std::int64_t volume = 1;
};
struct MaterialStoreDefinition
{
	StockStoreDefinition stock;
	std::string id, compartment, system, controller;
	std::int64_t capacity = 0;
	bool main = false;
	std::vector<std::string> accepts;
	std::map<std::string, std::int64_t> initial, targets;
};
struct MaterialConsumerDefinition
{
	std::string system, store;
	std::map<std::string, std::map<std::string, std::int64_t>> costs;
};
struct LogisticsDefinition
{
	std::string controller, cargo_authorisation_controller;
	std::int64_t throughput = 20, concurrency = 4, batch_size = 10;
	std::map<std::string, MaterialDefinition> materials;
	std::vector<MaterialStoreDefinition> stores;
	std::vector<MaterialConsumerDefinition> consumers;
};
// Mission setup changes physical stocks once, without changing reusable ship definitions.
struct StartingStockDefinition
{
	std::string slot, definition;
	std::map<std::string, std::map<std::string, std::int64_t>> stores;
};
struct DockPortDefinition
{
	std::string id, controller, compartment, compatibility;
	std::int64_t x = 0, y = 0, z = 0, heading = 0, capture_range = 1000, capture_speed = 500, alignment = 5000,
	             mating_ticks = 20, separation_ticks = 20, throughput = 20, emergency_damage = 0;
	bool auto_accept_allies = false, supply = false, emergency_release = false;
};
struct TransportDefinition
{
	std::string compartment;
	std::int64_t capacity = 64;
	std::vector<std::string> capabilities;
	std::map<std::string, std::vector<std::string>> capability_systems;
};
struct CraftDefinition
{
	std::string role;
	std::int64_t vertical_speed = 10000, vertical_acceleration = 5000;
	std::int64_t cruise_altitude = 15000, engagement_range = 200000, return_reserve = 200;
};
struct CraftSupplyDefinition
{
	std::string source, destination;
	std::int64_t rate = 0;
	std::size_t source_store = 0;
	std::string material;
	std::map<std::string, std::size_t> destination_stores;
};
struct CraftBayDefinition
{
	std::string compartment, power_store;
	std::int64_t slots = 0, transfer_ticks = 20, docking_range = 10000, docking_speed = 5000;
	std::int64_t flight_spacing = 0;
	std::int64_t charge_rate = 0;
	bool legacy_transport = false;
	std::int64_t transport_range = 0, transport_capacity = 0, arrival_distance = 0, arrival_speed = 0, cruise_speed = 0;
	std::vector<std::string> initial;
	std::vector<CraftSupplyDefinition> ammunition;
};
} // namespace phoenix
