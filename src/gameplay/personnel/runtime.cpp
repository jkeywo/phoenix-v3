module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

module phoenix.gameplay.personnel.runtime;
import phoenix.gameplay.teams.routes;

namespace phoenix
{
const PersonnelDuty *find_personnel_duty(const PersonnelDefinition &definition, const std::string &id)
{
	const auto found = definition.duty_indices.find(id);
	return found == definition.duty_indices.end() ? nullptr : &definition.duties[found->second];
}
void advance_personnel_movement(PersonnelState &state, const PersonnelDefinition &definition,
                                const PersonnelTravelFacts &facts, std::uint64_t tick)
{
	for (std::size_t index = 0; index < state.officers.size(); ++index)
	{
		auto &o = state.officers[index];
		const auto destination = [&](const PersonnelDuty &duty) -> const OfficerState &
		{
			return facts.destinations[definition.duty_indices.at(duty.id)];
		};
		if (o.condition == "stunned" && o.stun_until <= static_cast<std::int64_t>(tick))
		{
			o.condition = "healthy";
			o.stun_until = 0;
			++state.revision;
		}
		if (o.condition == "stunned" && o.present)
			if (const auto *duty = find_personnel_duty(definition, o.duty); duty && !duty->team.empty())
			{
				const auto &at = destination(*duty);
				o.location = at.location;
				o.edge_to = at.edge_to;
				o.remaining = at.remaining;
			}
		if (!officer_mobile(o) || !o.escort.empty() || !o.facility.empty())
			continue;
		const auto *duty = find_personnel_duty(definition, o.duty);
		if (o.present && duty)
		{
			const auto &at = destination(*duty);
			o.location = at.location;
			o.edge_to = at.edge_to;
			o.remaining = at.remaining;
			continue;
		}
		if (o.remaining > 0)
		{
			if (!facts.traversable[index])
				continue;
			if (--o.remaining > 0)
				continue;
			o.location = static_cast<std::size_t>(o.edge_to);
			o.edge_to = -1;
		}
		if (!duty)
			continue;
		const auto &target = destination(*duty);
		if (o.location == target.location && target.remaining == 0)
		{
			o.present = true;
			continue;
		}
		const auto route =
		    compartment_route(definition.compartments.size(), facts.links, o.location,
		                      target.remaining > 0 ? static_cast<std::size_t>(target.edge_to) : target.location);
		if (route.first >= 0)
		{
			o.edge_to = route.first;
			o.remaining = route.second;
		}
	}
}
} // namespace phoenix
