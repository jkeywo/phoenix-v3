#pragma once

// Private simulation components. Equipment has no public Body identity or pose.
namespace phoenix
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
} // namespace phoenix
