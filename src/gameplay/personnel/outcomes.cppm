module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

export module phoenix.gameplay.personnel.outcomes;
export import phoenix.gameplay.combat.state;
export import phoenix.gameplay.personnel.state;

export namespace phoenix
{
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
NativePatientOutcome native_patient_outcome(const OfficerState &);
void apply_native_patient_outcome(OfficerState &, const NativePatientOutcome &);
NativeTeamOutcome native_team_outcome(const TeamState &);
void apply_native_team_outcome(TeamState &, const NativeTeamOutcome &);
} // namespace phoenix
