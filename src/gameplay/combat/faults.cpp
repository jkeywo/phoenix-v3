module;
#include <algorithm>
#include <cstdint>
#include <string>

module phoenix.gameplay.combat.faults;

namespace phoenix
{
bool equipment_destroyed(const CombatStructure *structure, const std::string &equipment)
{
	const auto *s = structure;
	if (!s || !s->faults.contains(equipment))
		return false;
	const auto &states = s->faults.at(equipment);
	const auto &defs = s->definition.faults.at(equipment);
	for (std::size_t i = 0; i < states.size(); ++i)
		if (states[i].active && defs[i].destroys)
			return true;
	return false;
}
bool hard_fault_disabled(const CombatStructure *structure, const std::string &equipment)
{
	if (!structure || !structure->faults.contains(equipment))
		return false;
	const auto &states = structure->faults.at(equipment);
	const auto &definitions = structure->definition.faults.at(equipment);
	for (std::size_t i = 0; i < states.size(); ++i)
		if (states[i].active &&
		    (definitions[i].destroys || definitions[i].effect == "disabled" || definitions[i].effect == "feed"))
			return true;
	return false;
}
// Typed effects shared by weapons, physical mechanisms and repair work.
std::int64_t fault_effectiveness(const CombatStructure *structure, const std::string &equipment)
{
	const auto *s = structure;
	if (!s)
		return 1000;
	const auto at = s->faults.find(equipment);
	if (at == s->faults.end())
		return 1000;
	std::int64_t result = 1000;
	const auto &definitions = s->definition.faults.at(equipment);
	for (std::size_t i = 0; i < at->second.size(); ++i)
		if (at->second[i].active)
		{
			const auto &d = definitions[i];
			result = std::min(result, d.destroys || d.effect == "disabled" || d.effect == "feed" ? std::int64_t{0}
			                                                                                     : d.effectiveness);
		}
	return result;
}

bool equipment_structurally_disabled(const CombatStructure *structure, const std::string &equipment, bool mobile_team,
                                     bool unavailable)
{
	if (!structure)
		return false;
	if (mobile_team)
		return unavailable || hard_fault_disabled(structure, equipment);
	if (unavailable || hard_fault_disabled(structure, equipment))
		return true;
	const auto at = structure->definition.equipment_compartments.find(equipment);
	return at != structure->definition.equipment_compartments.end() &&
	       structure->compartments.at(structure->definition.compartment_ids.at(at->second)).disabled;
}

} // namespace phoenix
