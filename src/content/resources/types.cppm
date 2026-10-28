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

export module phoenix.content.resources.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.resources;
export import phoenix.state_tree;

export namespace phoenix
{
struct FittedSystemSpec
{
	std::string id;
	OperatingSystem kind;
	std::int64_t maximum = 0, response = 0;
	std::string controller;
};
struct SystemResourceBinding
{
	std::string id;
	OperatingSystem kind = OperatingSystem::Engine;
	bool store_modes = false;
	ResourceIndex store = no_resource, generator = no_resource, group = no_resource, conduit = no_resource,
	              cooling = no_resource;
	ResourceAmount energy_per_second = 0, discrete_energy = 0;
};
struct ShipResourcesDefinition
{
	ResourceNetworkDefinition network;
	std::vector<SystemResourceBinding> systems;
	std::string signature;
};
struct FtlDefinition
{
	ResourceIndex store = no_resource;
	ResourceAmount charge_rate = 0, charging_loss = 0, ready_loss = 0, off_discharge = 0;
};
using SystemObservationSchema = std::map<std::string, state_tree::Type>;
} // namespace phoenix
