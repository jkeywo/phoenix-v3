module;
#include <flecs.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module phoenix.gameplay.fitting;
export import phoenix.gameplay.flight;
export import phoenix.resources.equipment;
export import phoenix.content.operations.types;
export import phoenix.content.logistics.types;
export import phoenix.content.sensing.types;
export import phoenix.gameplay.combat.state;

// Equipment is authoritative native state and has no separate Body identity or pose.
export namespace phoenix
{
struct Equipment
{
	std::string id;
	OperatingSystem kind = OperatingSystem::Engine;
	std::size_t ordinal = 0;
	std::int64_t maximum = 0;
	bool advanced = false, enabled = true;
};
struct NamedEquipment
{
};
struct GameplayShip
{
};
struct BasicEquipment
{
};
struct AdvancedEquipment
{
};
// Derived boundary index: handles only, never an alternative store of state.
struct EquipmentIndex
{
	std::vector<flecs::entity> values;
	std::map<std::string, flecs::entity> by_id;
};
struct ResourceBinding
{
	std::size_t slot = 0;
};
struct FlightRuntime
{
	FlightDefinition definition;
	FlightState state;
	FlightDemand demand; // Derived fixed-tick quote, never serialized.
};
struct OperatingHeat
{
	std::int64_t remainder = 0;
};

// Short-lived addressed-access views. These contain references/pointers only;
// simulation work is selected by capability queries, not by these indexes.
struct EquipmentRef
{
	flecs::entity entity;
	std::string &id;
	OperatingSystem kind;
	Engine *engine;
	Steering *steering;
	Radar *radar;
	bool &advanced, &enabled;
	std::int64_t maximum;
	std::size_t ordinal;
};
inline EquipmentRef equipment_ref(flecs::entity entity)
{
	auto &v = entity.get_mut<Equipment>();
	return {entity,
	        v.id,
	        v.kind,
	        entity.try_get_mut<Engine>(),
	        entity.try_get_mut<Steering>(),
	        entity.try_get_mut<Radar>(),
	        v.advanced,
	        v.enabled,
	        v.maximum,
	        v.ordinal};
}
struct GameplayRef
{
	FlightRuntime *flight;
	std::int64_t &heat_remainder;
};
inline GameplayRef gameplay_ref(flecs::entity entity)
{
	return {entity.try_get_mut<FlightRuntime>(), entity.get_mut<OperatingHeat>().remainder};
}
template <bool Gameplay> struct EquipmentRange
{
	const std::vector<flecs::entity> &handles;
	auto operator[](std::size_t i) const
	{
		if constexpr (Gameplay)
			return gameplay_ref(handles.at(i));
		else
			return equipment_ref(handles.at(i));
	}
	std::size_t size() const
	{
		return handles.size();
	}
	struct Iterator
	{
		const EquipmentRange *range;
		std::size_t index;
		auto operator*() const
		{
			return (*range)[index];
		}
		Iterator &operator++()
		{
			++index;
			return *this;
		}
		bool operator!=(const Iterator &other) const
		{
			return index != other.index;
		}
	};
	Iterator begin() const
	{
		return {this, 0};
	}
	Iterator end() const
	{
		return {this, size()};
	}
};
struct FittingsView
{
	EquipmentRange<false> values;
};
struct GameplayView
{
	EquipmentRange<true> values;
	ShipResources *power;
};
inline std::optional<FittingsView> fittings(flecs::entity ship)
{
	if (!ship.has<NamedEquipment>())
		return {};
	return FittingsView{{ship.get<EquipmentIndex>().values}};
}
inline std::optional<GameplayView> gameplay_systems(flecs::entity ship)
{
	if (!ship.has<GameplayShip>())
		return {};
	return GameplayView{{ship.get<EquipmentIndex>().values}, ship.try_get_mut<ShipResources>()};
}

// Read-only addressed facts. The ID view and handle are borrowed for the current
// World lifetime; callers do not retain them across content replacement.
struct EquipmentSnapshot
{
	flecs::entity entity;
	const std::string &id;
	OperatingSystem kind;
	std::size_t ordinal;
	std::int64_t maximum;
	bool advanced, enabled;
	std::optional<std::size_t> resource_slot;
};
struct EquipmentAvailability
{
	bool destroyed = false, structurally_disabled = false;
	std::int64_t effectiveness = 1000;
};
std::optional<EquipmentSnapshot> find_equipment(flecs::entity ship, const std::string &id);
EquipmentAvailability equipment_availability(flecs::entity ship, const EquipmentSnapshot &, bool unavailable);

// A domain receives only the immutable definition sections it operates on,
// rather than the World's complete compiled entity definition.
struct NativeOwnerDefinitions
{
	const PersonnelDefinition &personnel;
	const LogisticsDefinition &logistics;
	const AbilityDefinitionSet &abilities;
	const ProcessDefinitionSet &processes;
	const PsychicDefinition &psychic;
	std::span<const std::string> tags;
};
} // namespace phoenix
