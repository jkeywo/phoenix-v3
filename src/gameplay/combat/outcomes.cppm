module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

export module phoenix.gameplay.combat.outcomes;
export import phoenix.gameplay.combat.state;

export namespace phoenix
{
template <class Compartment>
inline std::int64_t native_structural_repair(Compartment &compartment, const CompartmentDefinition &definition,
                                             std::int64_t work, std::int64_t divisor)
{
	const auto before = compartment.hp;
	compartment.work += work;
	compartment.hp += std::min(definition.maximum - compartment.hp, compartment.work / divisor);
	compartment.work %= divisor;
	if (compartment.disabled && compartment.hp >= definition.recovery)
		compartment.disabled = false;
	return compartment.hp - before;
}
inline std::int64_t native_structural_repair(CombatStructure &structure, std::size_t index, std::int64_t work,
                                             std::int64_t divisor)
{
	return native_structural_repair(structure.compartments.at(index), structure.definition.compartments.at(index), work,
	                                divisor);
}
template <class Fault> inline bool native_finish_fault_repair(Fault &fault, const FaultDefinition &definition)
{
	if (definition.destroys || !fault.active)
		return false;
	fault.active = false;
	fault.work = 0;
	return true;
}
inline bool native_finish_fault_repair(CombatStructure &structure, const std::string &system, std::size_t index)
{
	return native_finish_fault_repair(structure.faults.at(system).at(index),
	                                  structure.definition.faults.at(system).at(index));
}
NativeStructureOutcome native_structure_outcome(const CombatStructure &);
void apply_native_structure_outcome(CombatStructure &, const NativeStructureOutcome &);
} // namespace phoenix
