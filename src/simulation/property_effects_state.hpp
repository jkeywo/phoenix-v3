#pragma once
namespace phoenix
{
inline constexpr std::size_t independent_modifier_contribution_limit = 8192;
// Shared clock count × maximum Apply operations × named rows per modifier template.
inline constexpr std::size_t maintained_modifier_contribution_limit = rule_instance_limit * 32 * 64;
inline constexpr std::size_t modifier_contribution_limit =
    independent_modifier_contribution_limit + maintained_modifier_contribution_limit;
// Each named row permits +/-100,000. Retain the existing native fault/boost
// arithmetic budget in addition to the full supported application roster.
inline constexpr std::int64_t modifier_delta_limit =
    static_cast<std::int64_t>(modifier_contribution_limit) * 100000 + 1000000000000LL;
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
struct PropertyContribution
{
	std::uint64_t id = 0;
	std::string source, definition, name, equipment, property;
	std::int64_t delta_permille = 0, expiry = -1;
	bool maintained = false;
};
struct PropertyEffects
{
	std::uint64_t next_id = 1;
	std::vector<PropertyContribution> contributions;
};
// Authoritative lasting applications are ECS entities. PropertyEffects above is
// a transaction/persistence snapshot, never a live Flecs component.
struct ActiveEffectIdentity
{
	std::uint64_t id = 0;
	EntityId owner = 0;
	std::string source, definition;
	bool operator==(const ActiveEffectIdentity &) const = default;
};
struct EffectLifetime
{
	std::int64_t expiry = -1;
};
enum class EffectTargetKind
{
	Equipment,
	Shield,
	Logical
};
struct EffectTarget
{
	flecs::entity body, equipment;
	EffectTargetKind kind = EffectTargetKind::Logical;
	std::uint64_t contribution = 0;
	std::string equipment_name, contribution_name;
};
struct PropertyValue
{
	std::string name;
	PropertyKey property;
	std::int64_t delta = 0;
	bool operator==(const PropertyValue &) const = default;
};
struct ModifierValues
{
	bool maintained = false;
	std::vector<PropertyValue> values;
	bool operator==(const ModifierValues &) const = default;
};
struct PropertyEffectOwner
{
	std::uint64_t next_id = 1;
	std::map<std::uint64_t, flecs::entity> applications;
	std::map<std::pair<std::string, std::string>, flecs::entity> grants;
	mutable PropertyAggregate independent;
	mutable std::map<PropertyTargetKey, std::map<std::string, PropertyTotals>> guarded;
	mutable bool aggregate_valid = false;
};
// Divide before multiplying so high-resolution work quantities retain checked arithmetic.
inline std::int64_t apply_property_delta(std::int64_t base, std::int64_t delta, std::int64_t maximum)
{
	if (base < 0 || maximum < 0 || delta < -modifier_delta_limit || delta > modifier_delta_limit)
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
