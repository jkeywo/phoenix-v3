module;
#include <flecs.h>
#include <algorithm>
#include <optional>
#include <string>

module phoenix.gameplay.fitting;
import phoenix.gameplay.combat.faults;

namespace phoenix
{
std::optional<EquipmentSnapshot> find_equipment(flecs::entity ship, const std::string &id)
{
	const auto *index = ship.try_get<EquipmentIndex>();
	if (!index)
		return {};
	const auto found = index->by_id.find(id);
	if (found == index->by_id.end())
		return {};
	const auto entity = found->second;
	const auto &equipment = entity.get<Equipment>();
	const auto *binding = entity.try_get<ResourceBinding>();
	return EquipmentSnapshot{entity,
	                         equipment.id,
	                         equipment.kind,
	                         equipment.ordinal,
	                         equipment.maximum,
	                         equipment.advanced,
	                         equipment.enabled,
	                         binding ? std::optional<std::size_t>{binding->slot} : std::nullopt};
}
EquipmentAvailability equipment_availability(flecs::entity ship, const EquipmentSnapshot &equipment, bool unavailable)
{
	const auto *structure = ship.try_get<CombatStructure>();
	const auto &id = equipment.id;
	return {equipment_destroyed(structure, id),
	        equipment_structurally_disabled(structure, id, equipment.kind == OperatingSystem::Team, unavailable),
	        fault_effectiveness(structure, id)};
}
} // namespace phoenix
