module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <string>

module phoenix.gameplay.machinery;
import phoenix.gameplay.fitting;
import phoenix.gameplay.combat.faults;
import phoenix.gameplay.ftl;
import phoenix.resources.processing;

namespace phoenix
{
std::string native_machinery_blocker(flecs::entity ship, const CustomPropertyAddress &address,
                                     const ProcessState &state, const ProcessDefinition &definition,
                                     const PersonnelDefinition *personnel, NativeMachineryState facts)
{
	if (definition.native == NativeProcessKind::Effects)
		return {};
	if (!ship || facts.unavailable)
		return "Process source unavailable";
	// Dormant craft and their equipment are disabled in the native ECS queries.
	// Keep authored runs paused without charging upkeep for unavailable machinery.
	if (facts.dormant)
		return "Native craft dormant";
	if (definition.native == NativeProcessKind::Recovery)
	{
		if (personnel)
			for (const auto &facility : personnel->facilities)
				if (facility.id == state.native_id)
					return {};
		return "Native recovery facility unavailable";
	}
	const auto equipment = address.equipment;
	const auto fitted = find_equipment(ship, equipment);
	if (!fitted || !fitted->enabled ||
	    equipment_structurally_disabled(ship.try_get<CombatStructure>(), equipment,
	                                    fitted->kind == OperatingSystem::Team, facts.unavailable))
		return "Native machinery unavailable";
	if (definition.native == NativeProcessKind::Ftl)
	{
		const auto *ftl = fitted->entity.try_get<FtlReserve>();
		const auto *resources = ship.try_get<ShipResources>();
		if (!ftl || !resources || !fault_effectiveness(ship.try_get<CombatStructure>(), equipment))
			return "Native FTL unavailable";
		if (resources->network.stores().at(ftl->definition.store).thermal_stop)
			return "Native machinery protected";
		return {};
	}
	if (definition.native == NativeProcessKind::Repair || definition.native == NativeProcessKind::Medical ||
	    definition.native == NativeProcessKind::ServicingEnergy ||
	    definition.native == NativeProcessKind::ServicingAmmunition)
		return {};
	const auto reserve_ready = [&](ResourceIndex index)
	{
		const auto *resources = ship.try_get<ShipResources>();
		if (!resources || index == no_resource)
			return true;
		return resources->network.operation_ready(index, true);
	};
	if ((definition.native == NativeProcessKind::Propulsion || definition.native == NativeProcessKind::Processor) &&
	    ship.has<ShipResources>() && fitted->resource_slot.has_value() &&
	    !reserve_ready(ship.get<ShipResources>().definition.systems.at(*fitted->resource_slot).store))
		return "Native machinery protected";
	if (definition.native == NativeProcessKind::Propulsion)
	{
		if (const auto *flight = fitted->entity.try_get<FlightRuntime>())
			return flight->state.enabled && !facts.docked ? std::string{} : "Native machinery disabled";
		if (const auto *engine = fitted->entity.try_get<Engine>())
			return engine->enabled && !facts.docked ? std::string{} : "Native machinery disabled";
		if (fitted->entity.has<Steering>())
			return !facts.docked ? std::string{} : "Native machinery disabled";
		return "Native propulsion unavailable";
	}
	if (definition.native == NativeProcessKind::Processor)
	{
		const auto *processors = ship.try_get<ShipProcessors>();
		if (!processors || !processors->values.contains(state.native_id) ||
		    processors->values.at(state.native_id).definition().equipment != equipment)
			return "Native processor unavailable";
		return {};
	}
	const auto *resources = ship.try_get<ShipResources>();
	if (!resources || !fitted->resource_slot.has_value())
		return "Native resource machinery unavailable";
	const auto &binding = resources->definition.systems.at(*fitted->resource_slot);
	if (definition.native == NativeProcessKind::Generation)
	{
		if (binding.generator == no_resource ||
		    resources->network.definition().generators.at(binding.generator).id != state.native_id)
			return "Native generator unavailable";
		if (!resources->network.generators().at(binding.generator).enabled)
			return "Native machinery disabled";
		if (!reserve_ready(resources->network.definition().generators.at(binding.generator).store))
			return "Native machinery protected";
		return {};
	}
	if (binding.cooling == no_resource ||
	    resources->network.definition().cooling_links.at(binding.cooling).id != state.native_id)
		return "Native cooling unavailable";
	if (resources->network.cooling_links().at(binding.cooling).isolated)
		return "Native machinery disabled";
	const auto &link = resources->network.definition().cooling_links.at(binding.cooling);
	const auto &source = resources->network.definition().coolant_sources.at(link.source);
	if (!reserve_ready(source.power_store))
		return "Native machinery protected";
	if (resources->network.stores().at(link.store).heat > 0 &&
	    !resources->network.coolant_sources().at(link.source).available())
		return "Native coolant unavailable";
	return {};
}
} // namespace phoenix
