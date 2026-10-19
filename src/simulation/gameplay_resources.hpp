#pragma once

// Private authoritative simulation adapter. Include after phoenix.content is imported.
namespace phoenix
{
enum class ResourceControl
{
	Enabled,
	GeneratorOutput,
	ChargeLimit,
	DischargeLimit,
	GroupBudget,
	ConduitIsolated,
	CoolingAllocation,
	CoolingIsolated,
	Overdrive,
	ThermalOverride
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
struct SystemResourceRequest
{
	ResourceAmount duty_permille = 0, extra_rate = 0, rate_remainder = 0;
	bool discrete = false;
};
struct SystemResourceGrant
{
	ResourceAmount requested = 0, paid = 0, discrete = 0;
	int supply_permille = 1000, overdrive_permille = 1000, overdrive_heat_permille = 1000;
	bool discrete_paid = false, blocked = false;
};
struct ShipResources
{
	ShipResourcesDefinition definition;
	ResourceNetwork network;
	std::vector<SystemResourceRequest> requests;
	std::vector<SystemResourceGrant> grants;
	explicit ShipResources(ShipResourcesDefinition definition, std::uint64_t seed = 1);
};
std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity);
void request_system_resource(ShipResources &ship, std::size_t system, ResourceAmount duty_permille,
                             bool discrete = false, ResourceAmount extra_rate = 0);
bool can_control_resource(const ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value);
bool control_resource(ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value);
void tick_ship_resources(ShipResources &ship);
Json project_system_resource(const ShipResources &ship, std::size_t system);
Json save_ship_resources(const ShipResources &ship);
void restore_ship_resources(ShipResources &ship, const Json &saved);
} // namespace phoenix
