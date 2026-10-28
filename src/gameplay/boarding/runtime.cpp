module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay.boarding.runtime;
import phoenix.gameplay.personnel.runtime;
import phoenix.gameplay.teams.routes;

namespace phoenix
{
bool assess_boarding(BoardingState &state, const BoardingEncounter &encounter, const BoardingAssessmentFacts &facts)
{
	const auto previous = state.factors;
	state.factors = {{"Base chance", encounter.success},
	                 {"Security support", (facts.security_strength - 1000) * encounter.team_bonus / 1000},
	                 {"Defending teams", 0}};
	state.chance = encounter.success + state.factors.at("Security support");
	for (const auto penalty : facts.defender_penalties)
	{
		state.chance -= penalty;
		state.factors["Defending teams"] -= penalty;
	}
	for (const auto &[system, effect] : facts.defence_effects)
	{
		state.chance -= effect;
		state.factors[system] = -effect;
	}
	state.chance = std::clamp<std::int64_t>(state.chance, 0, 1000);
	state.defenders = facts.defender_penalties.size();
	if (!state.assessed)
		state.assessment = encounter.assessment_ticks;
	state.assessed = true;
	if (previous != state.factors)
	{
		state.reported = false;
		state.assessment_message = 0;
		return true;
	}
	return false;
}
bool boarding_route_required_after_tick(const BoardingState &state, bool traversable)
{
	const auto travel = state.travel && traversable ? state.travel - 1 : state.travel;
	const auto location = state.travel && traversable && travel == 0 ? state.edge_to : state.location;
	return state.destination >= 0 && location != state.destination && !travel;
}
bool advance_boarding_route(BoardingState &state, std::size_t compartments,
                            std::span<const PersonnelDefinition::Link> open_links, bool traversable)
{
	if (state.travel && traversable && --state.travel == 0)
	{
		state.location = state.edge_to;
		state.edge_to = -1;
	}
	if (state.destination >= 0 && state.location != state.destination && !state.travel)
	{
		const auto route = compartment_route(compartments, open_links, state.location, state.destination);
		if (route.first < 0)
			return false;
		state.edge_to = route.first;
		state.travel = route.second;
	}
	return true;
}
std::int64_t boarding_random_roll(std::uint64_t &random)
{
	random = random * 6364136223846793005ULL + 1;
	return (random >> 16) % 1000;
}
void apply_boarding_casualties(BoardingCasualtyActors actors, const BoardingEncounter &encounter, bool medical)
{
	auto &state = actors.team_state;
	const auto roll = boarding_random_roll(actors.random);
	const auto injury = std::max<std::int64_t>(0, encounter.injury - (medical ? encounter.medical_reduction : 0));
	if (roll < encounter.loss)
		state.condition = "lost";
	else if (roll < encounter.loss + injury)
		state.condition = "injured";
	else
		return;
	state.treatment = 0;
	if (state.condition == "lost")
		actors.lost[actors.ship].insert("team:" + actors.team);
	++state.revision;
	const auto &definition = actors.definition;
	auto &personnel = actors.personnel;
	for (std::size_t i = 0; i < personnel.officers.size(); ++i)
		if (const auto *duty = find_personnel_duty(definition, personnel.officers[i].duty);
		    duty && duty->team == actors.team)
		{
			auto &officer = personnel.officers[i];
			const auto fate = boarding_random_roll(actors.random);
			if (fate < encounter.officer_loss)
			{
				officer.condition = "dead";
				officer.duty.clear();
				officer.present = false;
			}
			else if (fate < encounter.officer_loss + encounter.officer_injury)
				officer.condition = "minor";
			if (state.condition == "lost")
			{
				// A survivor remains physically missing; it is not returned to a reserve slot.
				actors.lost[actors.ship].insert("officer:" + definition.officers[i].id);
				officer.present = false;
				officer.duty.clear();
			}
		}
	++personnel.revision;
}
} // namespace phoenix
