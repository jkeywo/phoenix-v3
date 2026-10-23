#pragma once
namespace phoenix
{
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
