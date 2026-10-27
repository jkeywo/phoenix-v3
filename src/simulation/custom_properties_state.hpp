#pragma once
namespace phoenix
{
inline constexpr std::size_t custom_property_rate_limit = 8192;
// Values live on the real native owner; addresses are temporary boundary views.
struct CustomPropertyState
{
	std::map<std::string, CustomPropertyValue> bases;
	std::map<std::string, std::int64_t> remainders;
	bool operator==(const CustomPropertyState &) const = default;
};
struct CustomPropertyAddress
{
	EntityId entity = 0;
	std::string equipment, officer;
	std::int64_t compartment = -1;
	bool operator==(const CustomPropertyAddress &) const = default;
	bool operator<(const CustomPropertyAddress &other) const
	{
		return std::tie(entity, equipment, officer, compartment) <
		       std::tie(other.entity, other.equipment, other.officer, other.compartment);
	}
};
} // namespace phoenix
