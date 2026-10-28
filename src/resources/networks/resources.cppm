module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

export module phoenix.resources;
export import phoenix.resources.stocks;
export import phoenix.resources.processors;

export namespace phoenix
{
struct ResourceStoreDefinition
{
	std::string id, resource;
	StockStoreDefinition stock;
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
	std::string id, resource;
	StockStoreDefinition stock;
	ResourceIndex power_store = no_resource;
	ResourceAmount capacity = 0, initial = 0, removal_rate = 0, coolant_per_heat_ppm = 1000000;
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
	SharedResourceCatalogue resources;
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
ResourceNetworkDefinition compile_resource_network(const Json &definition, const ResourceCatalogue &resources);
ResourceNetworkDefinition compile_resource_network(const Json &definition, SharedResourceCatalogue resources);

struct ResourceStoreState
{
	StockStore stock;
	ResourceAmount energy() const
	{
		return stock.quantity(stock.selected());
	}
	ResourceAmount available() const
	{
		return stock.available(stock.selected());
	}
	ResourceAmount headroom() const
	{
		return stock.headroom(stock.selected());
	}
	ResourceAmount heat = 0, stress = 0;
	ResourceAmount charge_limit = 0, discharge_limit = 0;
	ResourceAmount requested_rate = 0, discrete_request = 0;
	ResourceAmount request_remainder = 0, charge_remainder = 0, discharge_remainder = 0;
	ResourceAmount loss_remainder = 0, charge_heat_remainder = 0, operation_heat_remainder = 0;
	ResourceAmount depletion_heat_remainder = 0, passive_remainder = 0, stress_remainder = 0;
	ResourceAmount overdrive_heat_remainder = 0;
	ResourceAmount overdrive_request_remainder = 0;
	// Prepaid continuous work in 1/resource_work_unit energy units. Negative
	// values occur only while settling a legacy saved fractional liability.
	ResourceAmount work_credit = 0;
	bool enabled = true, thermal_stop = false, overdrive = false, thermal_override = false;
};
struct ResourceGeneratorState
{
	ResourceAmount commanded_rate = 0, actual_rate = 0, response_remainder = 0;
	ResourceAmount generation_remainder = 0, heat_remainder = 0, throughput_remainder = 0;
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
	StockStore stock;
	ResourceAmount remaining() const
	{
		return stock.quantity(stock.selected());
	}
	ResourceAmount available() const
	{
		return stock.available(stock.selected());
	}
	ResourceAmount consumption_remainder = 0;
	ResourceAmount last_heat_removed = 0, last_consumed = 0;
};
struct CoolingLinkState
{
	ResourceAmount allocation_rate = 0, tick_remainder = 0, throughput_remainder = 0;
	bool isolated = false;
};
struct ResourceStoreTelemetry
{
	ResourceAmount requested = 0, accepted_incoming = 0, outgoing = 0, generated = 0;
	ResourceAmount producer_consumed = 0;
	ResourceAmount consumed = 0, lost = 0, heat_added = 0, heat_removed = 0;
	ResourceAmount work_requested = 0, work_funded = 0;
	bool discrete_paid = false, limited = false;
};
struct ResourceForecast
{
	// -1 means no depletion/thermal stop predicted under the stated constant-rate assumptions.
	ResourceAmount depletion_ticks = -1, thermal_stop_ticks = -1;
	ResourceAmount full_ticks = -1, thermal_recovery_ticks = -1;
	ResourceAmount observed_net_energy = 0, observed_net_heat = 0;
	bool constant_last_tick_rates = true, excludes_future_commands_and_network_changes = true;
};
struct CoolantForecast
{
	ResourceAmount depletion_ticks = -1, observed_heat_removed = 0, observed_coolant_used = 0;
	bool constant_last_tick_rates = true, excludes_future_commands_and_network_changes = true;
};

// Tick-local mechanism controls are prepared on allocation copies. They never
// replace the commanded native settings or the immutable network definition.
struct ResourceMachineryControl
{
	ResourceAmount throughput_permille = 1000, work_limit = resource_limit;
	bool enabled = true;
};
struct ResourceMachineryReceipt
{
	ResourceAmount available = 0, completed = 0;
};
class ResourceNetwork
{
public:
	explicit ResourceNetwork(ResourceNetworkDefinition definition, std::uint64_t seed = 1);
	const ResourceNetworkDefinition &definition() const;
	const std::vector<ResourceStoreState> &stores() const;
	StockStore &stock(ResourceIndex store);
	StockStore &coolant_stock(ResourceIndex source);
	const std::vector<ResourceGeneratorState> &generators() const;
	const std::vector<ResourceGroupState> &groups() const;
	const std::vector<ResourceLinkState> &links() const;
	const std::vector<CoolantSourceState> &coolant_sources() const;
	const std::vector<CoolingLinkState> &cooling_links() const;
	const std::vector<ResourceStoreTelemetry> &telemetry() const;
	std::uint64_t ticks() const;
	// Transient mutation epoch for prepared native work; it is not saved gameplay state.
	std::uint64_t revision() const;
	void request_operation(ResourceIndex store, ResourceAmount energy_per_second, ResourceAmount discrete_energy = 0);
	void set_store_enabled(ResourceIndex store, bool enabled);
	void set_storage_limits(ResourceIndex store, ResourceAmount charge_per_second, ResourceAmount discharge_per_second);
	void set_overdrive(ResourceIndex store, bool enabled);
	void set_thermal_override(ResourceIndex store, bool enabled);
	void set_generator(ResourceIndex generator, ResourceAmount output_per_second, bool enabled);
	void set_group_budget(ResourceIndex group, ResourceAmount energy_per_second);
	void set_link_isolated(ResourceIndex link, bool isolated);
	void set_cooling(ResourceIndex link, ResourceAmount removal_per_second, bool isolated = false);
	void set_generator_work(ResourceIndex, ResourceMachineryControl);
	void set_cooling_work(ResourceIndex, ResourceMachineryControl);
	void clear_machinery_work();
	const std::vector<ResourceMachineryReceipt> &generation_work() const;
	const std::vector<ResourceMachineryReceipt> &cooling_work() const;
	// Explicit thermal effects use the same state and protection hysteresis.
	ResourceAmount add_thermal(ResourceIndex, ResourceAmount);
	ResourceAmount remove_thermal(ResourceIndex, ResourceAmount);
	ResourceAmount producer_input_budget(ResourceIndex) const;
	bool producer_input_operational(ResourceIndex) const;
	bool operation_ready(ResourceIndex, bool next_tick = false) const;
	// Prepared seed only: debit pre-existing energy and reserve this tick's native
	// discharge allowance. The ordinary tick settles its heat and observations.
	bool prepare_producer_payment(ResourceIndex, ResourceAmount);
	void tick();
	ResourceAmount receive_external(ResourceIndex store, ResourceAmount offered);
	// Atomic discrete operating payment against this tick's remaining discharge budget.
	bool spend_external(ResourceIndex store, ResourceAmount amount);
	ResourceAmount external_payment_budget(ResourceIndex store) const;
	ResourceAmount transfer_external(ResourceNetwork &destination, ResourceIndex source, ResourceIndex target,
	                                 ResourceAmount offered);
	void dormant_tick();
	// Physical dissipation also operates on an isolated or disabled reserve.
	ResourceAmount dissipate(ResourceIndex store, ResourceAmount amount, bool heat = true);
	void apply_operating_heat(ResourceIndex store, ResourceAmount paid_heat);
	ResourceForecast forecast(ResourceIndex store) const;
	CoolantForecast coolant_forecast(ResourceIndex source) const;
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
	std::vector<ResourceMachineryControl> generator_work_, cooling_work_;
	std::vector<ResourceMachineryReceipt> generation_receipts_, cooling_receipts_;
	std::vector<ResourceAmount> charge_left_, discharge_left_, link_left_, group_left_;
	std::vector<ResourceAmount> demand_, initial_energy_, initial_heat_;
	std::vector<ResourceAmount> producer_paid_;
	std::uint64_t random_ = 1, ticks_ = 0, revision_ = 1;
	void changed();
	std::uint64_t random_next();
	bool operational(ResourceIndex store) const;
	void add_heat(ResourceIndex store, ResourceAmount amount);
	void debit_operation(ResourceIndex store, ResourceAmount amount, bool discrete);
	void add_consumption_heat(ResourceIndex store, ResourceAmount consumed, ResourceAmount depleted);
};
} // namespace phoenix
