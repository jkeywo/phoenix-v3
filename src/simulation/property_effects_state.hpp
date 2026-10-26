#pragma once
namespace phoenix
{
// Private numeric keys follow the compiled native property descriptor order.
enum class PropertyKey : std::size_t
{
	propulsion,
	speed,
	turn,
	sensors,
	cycling,
	traverse,
	reload,
	processing,
	recharge,
	off_recharge,
	sustain,
	discharge,
	protection,
	repair,
	medical,
	security,
	comms,
	count
};
inline PropertyKey property_key(std::string_view name)
{
	const auto &descriptors = modifier_properties();
	for (std::size_t i = 0; i < descriptors.size(); ++i)
		if (descriptors[i].id == name)
			return static_cast<PropertyKey>(i);
	throw std::runtime_error("Unknown native property");
}
using PropertyTotals = std::array<std::int64_t, static_cast<std::size_t>(PropertyKey::count)>;
enum class PropertyTargetKind
{
	Equipment,
	Shield,
	Logical
};
struct PropertyTargetKey
{
	PropertyTargetKind kind = PropertyTargetKind::Logical;
	std::uint64_t identity = 0;
	std::string name;
	bool operator<(const PropertyTargetKey &other) const
	{
		return std::tie(kind, identity, name) < std::tie(other.kind, other.identity, other.name);
	}
	bool operator==(const PropertyTargetKey &other) const
	{
		return kind == other.kind && identity == other.identity && name == other.name;
	}
};
using PropertyAggregate = std::map<PropertyTargetKey, PropertyTotals>;
struct PersonnelProperties
{
	std::uint64_t revision = 0;
	PropertyAggregate values;
};
// Eligibility writers invalidate both before and after their operation. Reads inside
// a mutation derive fresh values, so nested same-tick work cannot reuse stale facts.
struct PropertyEligibilityMutation
{
	std::uint64_t &revision;
	std::size_t &depth;
	PropertyEligibilityMutation(std::uint64_t &r, std::size_t &d) : revision(r), depth(d)
	{
		++revision;
		++depth;
	}
	~PropertyEligibilityMutation()
	{
		--depth;
		++revision;
	}
};
struct ActiveEffectIdentity
{
	std::uint64_t id = 0;
	EntityId owner = 0;
	std::string source, definition;
};
struct EffectLifetime
{
	std::int64_t expiry = -1;
};
enum class EffectTargetKind
{
	Equipment,
	Shield,
	Entity,
	Compartment,
	System,
	Ordnance
};
struct EffectTarget
{
	flecs::entity body, equipment;
	EffectTargetKind kind = EffectTargetKind::Entity;
	std::uint64_t contribution = 0;
	std::string equipment_name, contribution_name;
};
struct PreparedModifierApplication
{
	ActiveEffectIdentity identity;
	EffectTarget target;
	EffectLifetime lifetime;
};
// Sparse transaction overlay: only touched applications are copied or changed.
struct PreparedPropertyEffects
{
	flecs::entity ship;
	std::uint64_t next_id = 1;
	std::size_t contribution_count = 0;
	std::map<std::uint64_t, std::optional<PreparedModifierApplication>> changes;
	std::map<std::pair<std::string, std::string>, std::uint64_t> handles;
};
struct PropertyValue
{
	PropertyKey property;
	std::int64_t delta = 0;
};
struct ModifierValues
{
	std::vector<PropertyValue> values;
};
// Native owner interface and derived indexes only. Effect entities own the values.
struct PropertyEffects
{
	std::uint64_t next_id = 1;
	std::map<std::uint64_t, flecs::entity> applications;
	std::map<std::pair<std::string, std::string>, std::uint64_t> grants;
	std::size_t contribution_count = 0;
	mutable PropertyAggregate aggregate;
	mutable bool aggregate_valid = false;
};
// Divide before multiplying so high-resolution work quantities retain checked arithmetic.
inline std::int64_t apply_property_delta(std::int64_t base, std::int64_t delta, std::int64_t maximum)
{
	// The bounded roster and independent application store together fit this sum.
	if (base < 0 || maximum < 0 || delta < -1000000000000LL || delta > 1000000000000LL)
		throw std::runtime_error("Property arithmetic outside supported bounds");
	const auto scale = std::max<std::int64_t>(0, 1000 + delta);
	if (!scale || !base)
		return 0;
	const auto fraction = (base % 1000) * scale / 1000;
	// Saturate before multiplication, including for high-resolution repair work.
	if (fraction >= maximum || base / 1000 > (maximum - fraction) / scale)
		return maximum;
	return base / 1000 * scale + fraction;
}
} // namespace phoenix
