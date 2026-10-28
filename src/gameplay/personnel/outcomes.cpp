module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

module phoenix.gameplay.personnel.outcomes;

namespace phoenix
{
NativePatientOutcome native_patient_outcome(const OfficerState &patient)
{
	return {patient.condition, patient.facility, patient.stabilised, patient.treatment, patient.recovery};
}

void apply_native_patient_outcome(OfficerState &patient, const NativePatientOutcome &out)
{
	patient.condition = out.condition;
	patient.facility = out.facility;
	patient.stabilised = out.stabilised;
	patient.treatment = out.treatment;
	patient.recovery = out.recovery;
}

NativeTeamOutcome native_team_outcome(const TeamState &team)
{
	return {team.condition, team.treatment, team.revision};
}

void apply_native_team_outcome(TeamState &team, const NativeTeamOutcome &out)
{
	team.condition = out.condition;
	team.treatment = out.treatment;
	team.revision = out.revision;
}
} // namespace phoenix
