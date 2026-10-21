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
};
struct PersonnelState
{
	std::uint64_t revision = 1;
	std::vector<OfficerState> officers;
	// Derived from assignments/presence; rebuilt on changes and restore, never saved.
	std::map<std::pair<std::string, std::string>, std::int64_t> bonuses;
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
inline void refresh_personnel_bonuses(PersonnelState &state, const PersonnelDefinition &d)
{
	state.bonuses.clear();
	for (std::size_t i = 0; i < state.officers.size(); ++i)
	{
		const auto &o = state.officers[i];
		if (!o.present || !officer_healthy(o))
			continue;
		const auto &duty = d.duties.at(d.duty_indices.at(o.duty));
		for (const auto &[system, slots] : duty.effects)
			for (const auto &slot : slots)
				if (const auto b = d.officers[i].bonuses.find(slot); b != d.officers[i].bonuses.end())
					state.bonuses[{system, slot}] += b->second;
	}
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
	refresh_personnel_bonuses(state, d);
	for (std::size_t i = 0; i < d.officers.size(); ++i)
	{
		state.officers[i].condition = d.officers[i].condition;
		if (!officer_mobile(state.officers[i]))
			state.officers[i].present = false;
	}
	refresh_personnel_bonuses(state, d);
	return state;
}
inline std::int64_t personnel_scale(std::int64_t value, std::int64_t bonus, bool inverse = false)
{
	const auto numerator = bonus >= 0 ? 1000 + bonus : 1000;
	const auto denominator = bonus >= 0 ? 1000 : 1000 - bonus;
	return inverse ? value * denominator / numerator : value * numerator / denominator;
}
} // namespace phoenix
