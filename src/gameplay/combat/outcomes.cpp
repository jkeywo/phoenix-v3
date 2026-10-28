module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

module phoenix.gameplay.combat.outcomes;

namespace phoenix
{
NativeStructureOutcome native_structure_outcome(const CombatStructure &structure)
{
	NativeStructureOutcome out;
	for (const auto &compartment : structure.compartments)
		out.compartments.push_back({compartment.hp, compartment.work, compartment.disabled});
	for (const auto &[system, faults] : structure.faults)
		for (const auto &fault : faults)
			out.faults[system].push_back({fault.active, fault.diagnosed, fault.work});
	return out;
}

void apply_native_structure_outcome(CombatStructure &structure, const NativeStructureOutcome &out)
{
	for (std::size_t i = 0; i < out.compartments.size(); ++i)
	{
		auto &compartment = structure.compartments.at(i);
		compartment.hp = out.compartments[i].hp;
		compartment.work = out.compartments[i].work;
		compartment.disabled = out.compartments[i].disabled;
	}
	for (const auto &[system, faults] : out.faults)
		for (std::size_t i = 0; i < faults.size(); ++i)
		{
			auto &fault = structure.faults.at(system).at(i);
			fault.active = faults[i].active;
			fault.diagnosed = faults[i].diagnosed;
			fault.work = faults[i].work;
		}
}
} // namespace phoenix
