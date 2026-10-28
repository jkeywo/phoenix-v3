module;
#include <flecs.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.effects.native;

namespace phoenix
{
namespace
{
template <typename State>
State *native_owner_state(flecs::entity entity, const CustomPropertyAddress &address,
                          const PersonnelDefinition &definition, State OfficerState::*officer_state,
                          State CompartmentState::*compartment_state)
{
	if (!entity)
		return nullptr;
	if (!address.equipment.empty())
	{
		const auto *index = entity.try_get<EquipmentIndex>();
		if (!index)
			return nullptr;
		const auto child = index->by_id.find(address.equipment);
		return child == index->by_id.end() ? nullptr : child->second.try_get_mut<State>();
	}
	if (!address.officer.empty())
	{
		auto *personnel = entity.try_get_mut<PersonnelState>();
		if (!personnel)
			return nullptr;
		const auto &officers = definition.officers;
		for (std::size_t i = 0; i < officers.size(); ++i)
			if (officers[i].id == address.officer)
				return &(personnel->officers.at(i).*officer_state);
		return nullptr;
	}
	if (address.compartment >= 0)
	{
		auto *structure = entity.try_get_mut<CombatStructure>();
		return !structure || static_cast<std::size_t>(address.compartment) >= structure->compartments.size()
		           ? nullptr
		           : &(structure->compartments[address.compartment].*compartment_state);
	}
	return entity.try_get_mut<State>();
}

} // namespace

NamedStock named_stock(flecs::entity ship, const LogisticsDefinition &logistics, const std::string &name,
                       ResourceNetwork *prepared_network)
{
	StockStore *result = nullptr;
	std::string canonical;
	NamedStockKind kind = NamedStockKind::Physical;
	ResourceIndex index = no_resource;
	const auto accept = [&](StockStore &stock, const std::string &key, NamedStockKind candidate_kind,
	                        ResourceIndex candidate_index = no_resource,
	                        const std::string &candidate_canonical = std::string{})
	{
		if (name != key && name != stock.definition().id)
			return;
		if (result && result != &stock)
			throw std::runtime_error("Ambiguous store identity: " + name);
		result = &stock;
		canonical = candidate_canonical.empty() ? key : candidate_canonical;
		kind = candidate_kind;
		index = candidate_index;
	};
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		for (auto &[key, stock] : physical->stores)
			accept(stock, key, NamedStockKind::Physical);
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		if (const auto *inventory = ship.try_get<MaterialInventory>())
		{
			const auto &stores = logistics.stores;
			for (std::size_t i = 0; i < stores.size(); ++i)
				if (name == stores[i].id)
					accept(physical->stores.at(inventory->stores.at(i).stock), name, NamedStockKind::Physical,
					       no_resource, inventory->stores.at(i).stock);
		}
	if (auto *power = ship.try_get_mut<ShipResources>())
	{
		auto &network = prepared_network ? *prepared_network : power->network;
		for (ResourceIndex i = 0; i < network.stores().size(); ++i)
			accept(network.stock(i), "energy:" + network.definition().stores[i].id, NamedStockKind::Electrical, i);
		for (ResourceIndex i = 0; i < network.coolant_sources().size(); ++i)
			accept(network.coolant_stock(i), "coolant:" + network.definition().coolant_sources[i].id,
			       NamedStockKind::Coolant, i);
	}
	if (!result)
		throw std::runtime_error("Unknown store: " + name);
	return {*result, canonical, kind, index};
}

bool effect_group_equipment_available(flecs::entity entity, const std::string &equipment, bool unavailable)
{
	if (!entity)
		return false;
	if (equipment.starts_with("effect:"))
	{
		const auto *shields = entity.try_get<ShieldComponent>();
		return shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
		                              [&](const auto &grant)
		                              {
			                              return equipment == "effect:" + std::to_string(grant.effect_id) + ":" +
			                                                      grant.definition.name &&
			                                     grant.operating;
		                              });
	}
	const auto fitted = find_equipment(entity, equipment);
	if (!fitted || !fitted->enabled)
		return false;
	const auto state = equipment_availability(entity, *fitted, unavailable);
	return !state.structurally_disabled && state.effectiveness > 0;
}
CustomPropertyState *custom_property_state(flecs::entity entity, const CustomPropertyAddress &address,
                                           const PersonnelDefinition &definition)
{
	return native_owner_state(entity, address, definition, &OfficerState::properties, &CompartmentState::properties);
}
ProcessRuntime *process_runtime(flecs::entity entity, const CustomPropertyAddress &address,
                                const PersonnelDefinition &definition)
{
	return native_owner_state(entity, address, definition, &OfficerState::processes, &CompartmentState::processes);
}
std::vector<CustomPropertyAddress> custom_property_addresses(flecs::entity ship, const PersonnelDefinition &definition)
{
	const auto id = ship.get<StableId>().id;
	std::vector<CustomPropertyAddress> result{{id}};
	if (const auto *equipment = ship.try_get<EquipmentIndex>())
		for (const auto &[name, child] : equipment->by_id)
			result.push_back({id, name});
	if (const auto *personnel = ship.try_get<PersonnelState>())
		for (const auto &officer : definition.officers)
			result.push_back({id, {}, officer.id});
	if (const auto *structure = ship.try_get<CombatStructure>())
		for (std::size_t i = 0; i < structure->compartments.size(); ++i)
			result.push_back({id, {}, {}, static_cast<std::int64_t>(i)});
	return result;
}
} // namespace phoenix
