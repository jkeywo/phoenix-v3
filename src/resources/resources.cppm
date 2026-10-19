module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

export module phoenix.resources;
import phoenix.data;

export namespace phoenix
{
using ResourceAmount = std::int64_t;
using ResourceIndex = std::uint32_t;
inline constexpr ResourceIndex no_resource = std::numeric_limits<ResourceIndex>::max();
inline constexpr ResourceAmount resource_limit = 1000000000000LL;
inline constexpr ResourceAmount resource_tick_hz = 20;

struct ResourceStoreDefinition
{
	std::string id;
	ResourceAmount capacity = 0, initial = 0, charge_rate = 0, discharge_rate = 0;
	ResourceAmount loss_empty_ppm = 0, loss_full_ppm = 0;
	ResourceAmount initial_heat = 0, passive_cooling = 0, stop_heat = 0, recover_heat = 0;
	ResourceAmount charge_heat_ppm = 0, operation_heat_ppm = 0, depletion_heat_ppm = 0;
	ResourceAmount overdrive_permille = 1000, overdrive_heat_permille = 1000;
	ResourceAmount stress_rate = 0, override_stress_rate = 0, stress_limit = 0;
	ResourceIndex group = no_resource;
	bool enabled = true, allows_override = false;
};
struct ResourceGeneratorDefinition
{
	std::string id;
	ResourceIndex store = no_resource;
	ResourceAmount maximum_rate = 0, response_rate = 0, initial_output = 0;
	ResourceAmount heat_ppm = 0;
	bool enabled = true;
};
struct ResourceLinkDefinition
{
	std::string id;
	ResourceIndex from = no_resource, to = no_resource;
	ResourceAmount capacity_rate = 0;
	bool isolated = false;
};
struct ResourceGroupDefinition
{
	std::string id;
	ResourceIndex parent = no_resource;
	ResourceAmount budget_rate = 0, restore_margin_rate = 0;
	std::uint32_t restore_ticks = 1;
};
struct CoolantSourceDefinition
{
	std::string id;
	ResourceIndex power_store = no_resource;
	ResourceAmount initial = 0, removal_rate = 0, coolant_per_heat_ppm = 1000000;
};
struct CoolingLinkDefinition
{
	std::string id;
	ResourceIndex source = no_resource, store = no_resource;
	ResourceAmount capacity_rate = 0, allocation_rate = 0;
	bool isolated = false;
};
struct ResourceNetworkDefinition
{
	std::vector<ResourceStoreDefinition> stores;
	std::vector<ResourceGeneratorDefinition> generators;
	std::vector<ResourceLinkDefinition> links;
	std::vector<ResourceGroupDefinition> groups;
	std::vector<CoolantSourceDefinition> coolant_sources;
	std::vector<CoolingLinkDefinition> cooling_links;
	std::map<std::string, ResourceIndex> store_ids, generator_ids, link_ids, group_ids;
	std::map<std::string, ResourceIndex> coolant_source_ids, cooling_link_ids;
	std::string signature;
};
ResourceNetworkDefinition compile_resource_network(const Json &definition);

struct ResourceStoreState
{
	ResourceAmount energy = 0, heat = 0, stress = 0;
	ResourceAmount charge_limit = 0, discharge_limit = 0;
	ResourceAmount requested_rate = 0, discrete_request = 0;
	ResourceAmount request_remainder = 0, charge_remainder = 0, discharge_remainder = 0;
	ResourceAmount loss_remainder = 0, charge_heat_remainder = 0, operation_heat_remainder = 0;
	ResourceAmount depletion_heat_remainder = 0, passive_remainder = 0, stress_remainder = 0;
	ResourceAmount overdrive_heat_remainder = 0;
	bool enabled = true, thermal_stop = false, overdrive = false, thermal_override = false;
};
struct ResourceGeneratorState
{
	ResourceAmount commanded_rate = 0, actual_rate = 0, response_remainder = 0;
	ResourceAmount generation_remainder = 0, heat_remainder = 0;
	bool enabled = true;
};
struct ResourceGroupState
{
	ResourceAmount budget_rate = 0, tick_remainder = 0;
	std::uint32_t sustainable_ticks = 0;
	bool cut = false;
};
struct ResourceLinkState
{
	ResourceAmount tick_remainder = 0;
	bool isolated = false;
};
struct CoolantSourceState
{
	ResourceAmount remaining = 0, consumption_remainder = 0;
};
struct CoolingLinkState
{
	ResourceAmount allocation_rate = 0, tick_remainder = 0;
	bool isolated = false;
};
struct ResourceStoreTelemetry
{
	ResourceAmount requested = 0, accepted_incoming = 0, outgoing = 0, generated = 0;
	ResourceAmount consumed = 0, lost = 0, heat_added = 0, heat_removed = 0;
	bool discrete_paid = false, limited = false;
};
struct ResourceForecast
{
	// -1 means no depletion/thermal stop predicted under the stated constant-rate assumptions.
	ResourceAmount depletion_ticks = -1, thermal_stop_ticks = -1;
	ResourceAmount observed_net_energy = 0, observed_net_heat = 0;
	bool constant_last_tick_rates = true, excludes_future_commands_and_network_changes = true;
};

class ResourceNetwork
{
public:
	explicit ResourceNetwork(ResourceNetworkDefinition definition, std::uint64_t seed = 1);
	const ResourceNetworkDefinition &definition() const;
	const std::vector<ResourceStoreState> &stores() const;
	const std::vector<ResourceGeneratorState> &generators() const;
	const std::vector<ResourceGroupState> &groups() const;
	const std::vector<ResourceLinkState> &links() const;
	const std::vector<CoolantSourceState> &coolant_sources() const;
	const std::vector<CoolingLinkState> &cooling_links() const;
	const std::vector<ResourceStoreTelemetry> &telemetry() const;
	std::uint64_t ticks() const;
	void request_operation(ResourceIndex store, ResourceAmount energy_per_second, ResourceAmount discrete_energy = 0);
	void set_store_enabled(ResourceIndex store, bool enabled);
	void set_storage_limits(ResourceIndex store, ResourceAmount charge_per_second, ResourceAmount discharge_per_second);
	void set_overdrive(ResourceIndex store, bool enabled);
	void set_thermal_override(ResourceIndex store, bool enabled);
	void set_generator(ResourceIndex generator, ResourceAmount output_per_second, bool enabled);
	void set_group_budget(ResourceIndex group, ResourceAmount energy_per_second);
	void set_link_isolated(ResourceIndex link, bool isolated);
	void set_cooling(ResourceIndex link, ResourceAmount removal_per_second, bool isolated = false);
	void tick();
	void apply_operating_heat(ResourceIndex store, ResourceAmount paid_heat);
	ResourceForecast forecast(ResourceIndex store) const;
	Json save() const;
	void restore(const Json &save);

private:
	ResourceNetworkDefinition definition_;
	std::vector<ResourceStoreState> stores_;
	std::vector<ResourceGeneratorState> generators_;
	std::vector<ResourceGroupState> groups_;
	std::vector<ResourceLinkState> links_;
	std::vector<CoolantSourceState> coolant_sources_;
	std::vector<CoolingLinkState> cooling_links_;
	std::vector<ResourceStoreTelemetry> telemetry_;
	std::vector<ResourceAmount> charge_left_, discharge_left_, link_left_, group_left_;
	std::vector<ResourceAmount> demand_, initial_energy_, initial_heat_;
	std::uint64_t random_ = 1, ticks_ = 0;
	std::uint64_t random_next();
	bool operational(ResourceIndex store) const;
	void add_heat(ResourceIndex store, ResourceAmount amount);
};
} // namespace phoenix
