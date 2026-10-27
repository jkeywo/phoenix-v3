#pragma once
namespace phoenix
{
// Outcomes mutate the same native state for physical work and direct effects.
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
template <class Patient> inline bool native_treat_patient(Patient &patient)
{
	if (patient.condition != "minor")
		return false;
	patient.condition = "healthy";
	patient.treatment = 0;
	return true;
}
template <class Patient> inline bool native_stabilise_patient(Patient &patient)
{
	if (patient.condition != "severe" || patient.stabilised)
		return false;
	patient.stabilised = true;
	patient.treatment = 0;
	return true;
}
template <class Patient> inline bool native_recover_patient(Patient &patient)
{
	if (patient.condition != "severe" || !patient.stabilised)
		return false;
	patient.condition = "healthy";
	patient.stabilised = false;
	patient.recovery = 0;
	patient.facility.clear();
	return true;
}
template <class Team> inline bool native_treat_team(Team &team)
{
	if (team.condition != "injured")
		return false;
	team.condition = "healthy";
	team.treatment = 0;
	++team.revision;
	return true;
}
// Only native outcome fields are staged. Committing a repair or treatment must
// not replace custom properties, process runs or other independent owner state.
struct NativeCompartmentOutcome
{
	std::int64_t hp = 0, work = 0;
	bool disabled = false;
	bool operator==(const NativeCompartmentOutcome &) const = default;
};
struct NativeFaultOutcome
{
	bool active = false, diagnosed = false;
	std::int64_t work = 0;
	bool operator==(const NativeFaultOutcome &) const = default;
};
struct NativeStructureOutcome
{
	std::vector<NativeCompartmentOutcome> compartments;
	std::map<std::string, std::vector<NativeFaultOutcome>> faults;
	bool operator==(const NativeStructureOutcome &) const = default;
};
inline NativeStructureOutcome native_structure_outcome(const CombatStructure &structure)
{
	NativeStructureOutcome out;
	for (const auto &compartment : structure.compartments)
		out.compartments.push_back({compartment.hp, compartment.work, compartment.disabled});
	for (const auto &[system, faults] : structure.faults)
		for (const auto &fault : faults)
			out.faults[system].push_back({fault.active, fault.diagnosed, fault.work});
	return out;
}
inline void apply_native_structure_outcome(CombatStructure &structure, const NativeStructureOutcome &out)
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
struct NativePatientOutcome
{
	std::string condition, facility;
	bool stabilised = false;
	std::int64_t treatment = 0, recovery = 0;
	bool operator==(const NativePatientOutcome &) const = default;
};
inline NativePatientOutcome native_patient_outcome(const OfficerState &patient)
{
	return {patient.condition, patient.facility, patient.stabilised, patient.treatment, patient.recovery};
}
inline void apply_native_patient_outcome(OfficerState &patient, const NativePatientOutcome &out)
{
	patient.condition = out.condition;
	patient.facility = out.facility;
	patient.stabilised = out.stabilised;
	patient.treatment = out.treatment;
	patient.recovery = out.recovery;
}
struct NativeTeamOutcome
{
	std::string condition;
	std::int64_t treatment = 0;
	std::uint64_t revision = 0;
	bool operator==(const NativeTeamOutcome &) const = default;
};
inline NativeTeamOutcome native_team_outcome(const TeamState &team)
{
	return {team.condition, team.treatment, team.revision};
}
inline void apply_native_team_outcome(TeamState &team, const NativeTeamOutcome &out)
{
	team.condition = out.condition;
	team.treatment = out.treatment;
	team.revision = out.revision;
}
} // namespace phoenix
