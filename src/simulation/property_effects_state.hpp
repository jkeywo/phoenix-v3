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
