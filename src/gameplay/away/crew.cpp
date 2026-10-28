module;
#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

module phoenix.gameplay.away.crew;

namespace phoenix
{
bool away_navigator_qualified(const PersonnelDefinition &personnel, const PersonnelState &state,
                              const ControllableLayout *layout, const CompiledFittings *fitted,
                              const std::string &console, const std::string &system)
{
	for (const auto &duty : personnel.duties)
		if (duty.console == console && !duty.required_trait.empty())
			for (std::size_t i = 0; i < personnel.officers.size(); ++i)
				if (state.officers[i].duty == duty.id && state.officers[i].present &&
				    officer_healthy(state.officers[i]) &&
				    std::find(personnel.officers[i].traits.begin(), personnel.officers[i].traits.end(),
				              duty.required_trait) != personnel.officers[i].traits.end())
					for (const auto &controller : layout->controllers)
						if (controller.console == duty.console && controller.id == duty.controller)
							for (const auto &command : controller.commands)
								for (auto consumer : command.consumers)
									if (fitted->values.at(consumer).id == system)
										return true;
	return false;
}
bool away_crew_healthy(const PersonnelDefinition &definition, const PersonnelState &personnel, const std::string &asset,
                       bool team_available, const PersonnelDuty *team_duty)
{
	if (asset.starts_with("team:") && !team_available)
		return false;
	if (asset.starts_with("team:"))
		for (const auto &officer : personnel.officers)
			if (officer.escort == asset.substr(5))
				return false;
	for (std::size_t i = 0; i < definition.officers.size(); ++i)
	{
		if (asset == "officer:" + definition.officers[i].id && !officer_healthy(personnel.officers[i]))
			return false;
		if (asset.starts_with("team:") && team_duty && personnel.officers[i].duty == team_duty->id &&
		    !officer_healthy(personnel.officers[i]))
			return false;
	}
	return true;
}
bool away_crew_slot_accepts(const AwaySlot &slot, const std::string &asset, const PersonnelDefinition &personnel,
                            const TeamDefinition *team, bool team_duty)
{
	if (!asset.starts_with(slot.kind + ":"))
		return false;
	if (slot.kind == "team")
	{
		if (!team || !team_duty || !slot.trait.empty())
			return false;
		return (slot.team_type.empty() || slot.team_type == team->type) &&
		       (slot.capability.empty() || team->capabilities.contains(slot.capability));
	}
	for (const auto &officer : personnel.officers)
		if (officer.id == asset.substr(8))
			return slot.trait.empty() ||
			       std::find(officer.traits.begin(), officer.traits.end(), slot.trait) != officer.traits.end();
	return false;
}
} // namespace phoenix
