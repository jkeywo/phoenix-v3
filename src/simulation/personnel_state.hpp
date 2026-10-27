#pragma once
#include "compartment_route.hpp"
namespace phoenix
{
struct OfficerState
{
	std::string duty;
	std::size_t location = 0;
	std::int64_t edge_to = -1, remaining = 0;
	bool present = false, detained = false;
	std::string condition = "healthy", facility, escort;
	bool stabilised = false;
	std::int64_t stun_until = 0, treatment = 0, recovery = 0, casualty_tick = -1;
	CustomPropertyState properties;
	ProcessRuntime processes;
};
struct PersonnelState
{
	std::uint64_t revision = 1;
	std::vector<OfficerState> officers;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfficerState, duty, location, edge_to, remaining, present, condition,
                                                facility, escort, stabilised, stun_until, treatment, recovery,
                                                casualty_tick, detained)
inline bool officer_healthy(const OfficerState &o)
{
	return !o.detained && o.condition == "healthy";
}
inline bool officer_mobile(const OfficerState &o)
{
	return !o.detained && (o.condition == "healthy" || o.condition == "minor");
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PersonnelState, revision, officers)
// Compiled effect definitions also supply assignment preference and deployed work adapters.
inline std::int64_t officer_property_delta(const PersonnelDefinition &d, const OfficerDefinition &officer,
                                           std::string_view property, std::string_view role = {})
{
	std::int64_t delta = 0;
	for (const auto &trait : officer.traits)
		for (const auto &effect : d.traits.at(trait).effects)
			for (const auto &c : effect.contributions)
				if (c.property == property && (role.empty() || c.role == role))
					delta += c.delta_permille;
	return delta;
}
inline PersonnelState initial_personnel(const PersonnelDefinition &d)
{
	PersonnelState state;
	for (const auto &o : d.officers)
		state.officers.push_back(
		    {o.duty,
		     static_cast<std::size_t>(std::find(d.compartments.begin(), d.compartments.end(), o.compartment) -
		                              d.compartments.begin()),
		     -1, 0, !o.duty.empty()});
	for (std::size_t i = 0; i < d.officers.size(); ++i)
	{
		state.officers[i].condition = d.officers[i].condition;
		if (!officer_mobile(state.officers[i]))
			state.officers[i].present = false;
	}
	return state;
}
} // namespace phoenix
