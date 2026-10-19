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
	bool held = false, advanced = false, enabled = true;
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
struct RecordReadBinding
{
	bool shared = false, selected = false, latest = false;
	KnowledgeKey schema = 0, field = 0;
	KnowledgeId subject = 0;
};
struct BrainRuntime
{
	state_tree::Program program;
	std::string tree;
	std::vector<RecordReadBinding> reads;
	std::vector<GameplayTreeSummaryBinding> summary_reads;
	state_tree::Instance continuation;
	state_tree::Debug debug;
};
struct ActionResult
{
	bool available = false, success = false;
	std::string reason;
	std::uint64_t id = 0;
};
struct SummaryIntents
{
	std::map<std::string, std::int64_t> values;
};
struct EffectJournal
{
	std::set<std::string> values;
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
	Controller *controller;
	bool &held, &advanced, &enabled;
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
	        entity.try_get_mut<Controller>(),
	        v.held,
	        v.advanced,
	        v.enabled,
	        v.maximum,
	        v.ordinal};
}
struct GameplayRef
{
	FlightRuntime *flight;
	BrainRuntime *brain;
	ActionResult &result;
	std::int64_t &heat_remainder;
	std::set<std::string> &effects;
	std::map<std::string, std::int64_t> &summary;
};
inline GameplayRef gameplay_ref(flecs::entity entity)
{
	return {entity.try_get_mut<FlightRuntime>(),    entity.try_get_mut<BrainRuntime>(),
	        entity.get_mut<ActionResult>(),         entity.get_mut<OperatingHeat>().remainder,
	        entity.get_mut<EffectJournal>().values, entity.get_mut<SummaryIntents>().values};
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
template <typename T> T *legacy_component(flecs::entity ship)
{
	if (ship.has<NamedEquipment>())
		return nullptr;
	for (auto system : ship.get<EquipmentIndex>().values)
		if (auto value = system.try_get_mut<T>())
			return value;
	return nullptr;
}
} // namespace phoenix
