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
using PropertyAggregate = std::map<std::string, PropertyTotals>;
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
