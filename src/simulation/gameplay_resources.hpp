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
struct SystemResourceRequest
{
	ResourceAmount duty_permille = 0, extra_rate = 0, rate_remainder = 0;
	bool discrete = false;
	ResourceAmount discrete_amount = 0;
};
struct SystemResourceGrant
{
	ResourceAmount requested = 0, paid = 0, discrete = 0;
	int supply_permille = 1000, overdrive_permille = 1000, overdrive_heat_permille = 1000;
	bool discrete_paid = false, blocked = false;
	ResourceAmount work_requested = 0, work_funded = 0;
};
struct NamedResourceRequest
{
	ResourceIndex store = no_resource;
	ResourceAmount rate = 0;
};
inline constexpr std::size_t named_resource_consumer_limit = 4096;
struct ShipResources
{
	ShipResourcesDefinition definition;
	ResourceNetwork network;
	std::vector<SystemResourceRequest> requests;
	std::vector<SystemResourceGrant> grants;
	std::map<std::string, NamedResourceRequest> named_requests;
	std::map<std::string, SystemResourceGrant> named_grants;
	explicit ShipResources(ShipResourcesDefinition definition, std::uint64_t seed = 1);
};
// One frozen allocation for the originating ship. Inspect grants, then commit
// before actuation. Destroying an uncommitted allocation leaves the ship intact.
class ShipResourceAllocation
{
public:
	ShipResourceAllocation(ShipResources &, std::span<const std::size_t> unavailable);
	ShipResourceAllocation(const ShipResourceAllocation &) = delete;
	ShipResourceAllocation &operator=(const ShipResourceAllocation &) = delete;
	const std::vector<SystemResourceGrant> &grants() const;
	const std::map<std::string, SystemResourceGrant> &named_grants() const;
	ResourceNetwork &prepared_network();
	void commit();

private:
	ShipResources candidate_;
	ShipResources *origin_;
	std::uint64_t origin_tick_;
	std::vector<std::uint64_t> origin_store_revisions_, origin_coolant_revisions_;
};
void request_system_resource(ShipResources &ship, std::size_t system, ResourceAmount duty_permille,
                             bool discrete = false, ResourceAmount extra_rate = 0, ResourceAmount discrete_amount = 0);
void reserve_system_resource_rate(ShipResources &ship, std::size_t system, ResourceAmount minimum_extra_rate);
void clear_named_requests(ShipResources &ship);
void request_named_resource(ShipResources &ship, const std::string &key, ResourceIndex store, ResourceAmount rate);
bool can_control_resource(const ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value);
bool control_resource(ShipResources &ship, std::size_t system, ResourceControl control, ResourceAmount value);
void tick_ship_resources(ShipResources &ship);
Json project_system_resource(const ShipResources &ship, std::size_t system);
Json save_ship_resources(const ShipResources &ship);
void restore_ship_resources(ShipResources &ship, const Json &saved);
} // namespace phoenix
