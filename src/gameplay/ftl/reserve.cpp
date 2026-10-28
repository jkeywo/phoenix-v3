module;
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

module phoenix.gameplay.ftl;

namespace phoenix
{
void prepare_ftl_reserve(const FtlReserve &reserve, ShipResources &power, std::size_t resource_slot,
                         FtlEquipmentFacts facts)
{
	const auto &definition = reserve.definition;
	const auto &store = power.network.stores().at(definition.store);
	const bool enabled = facts.process_rate > 0 && facts.enabled && !facts.structurally_disabled &&
	                     facts.fault_effectiveness > 0 && !store.thermal_stop && !facts.destroyed;
	const bool full = store.energy() == power.definition.network.stores.at(definition.store).capacity;
	power.network.set_store_enabled(definition.store, enabled);
	const auto charge_rate = enabled ? definition.charge_rate * facts.process_rate / 1000 : 0;
	power.network.set_storage_limits(definition.store, charge_rate,
	                                 power.definition.network.stores.at(definition.store).discharge_rate);
	request_system_resource(power, resource_slot, 0, false,
	                        enabled ? (full ? definition.ready_loss : definition.charging_loss) : 0);
}
void actuate_ftl_reserve(FtlReserve &reserve, ResourceNetwork &network, std::int64_t ticks_per_second)
{
	const auto store = reserve.definition.store;
	if (!network.stores().at(store).enabled)
	{
		reserve.discharge_remainder += reserve.definition.off_discharge;
		network.dissipate(store, reserve.discharge_remainder / ticks_per_second);
		reserve.discharge_remainder %= ticks_per_second;
	}
}
std::string ftl_reserve_blocker(const FtlReserve &reserve, const ShipResources &power, std::size_t resource_slot,
                                FtlEquipmentFacts facts, bool displacement_transfer_active)
{
	const auto &store = power.network.stores().at(reserve.definition.store);
	const auto &definition = power.definition.network.stores.at(reserve.definition.store);
	if (!facts.enabled)
		return "FTL battery off";
	if (facts.destroyed || facts.structurally_disabled || !facts.fault_effectiveness || !store.enabled ||
	    store.thermal_stop || (definition.stop_heat && store.heat >= definition.stop_heat) ||
	    power.grants.at(resource_slot).blocked)
		return "FTL drive unavailable";
	if (displacement_transfer_active)
		return "Displacement transfer active";
	if (store.energy() != definition.capacity)
		return "FTL battery charging";
	if (store.available() != store.energy())
		return "FTL reserve committed";
	return {};
}
bool ftl_reserve_ready(const FtlReserve &reserve, const ResourceNetwork &network)
{
	return network.stores().at(reserve.definition.store).energy() ==
	           network.definition().stores.at(reserve.definition.store).capacity &&
	       network.operation_ready(reserve.definition.store);
}
} // namespace phoenix
