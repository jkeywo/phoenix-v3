module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>

module phoenix.gameplay.personnel.treatment;
import phoenix.gameplay.personnel.clinical;

namespace phoenix
{
std::optional<ClinicalWorkRequest> clinical_team_work(const TeamDefinition &team, const TeamState &before,
                                                      const std::string &activity, std::int64_t desired)
{
	if (activity != "treat" || before.condition != "injured")
		return std::nullopt;
	const auto required = team.treatment_ticks * 1000;
	return ClinicalWorkRequest{"treat", before.treatment, required, std::min(desired, required - before.treatment)};
}
std::optional<ClinicalWorkRequest> clinical_officer_work(const TeamDefinition &team, const OfficerState &before,
                                                         const std::string &activity, std::int64_t desired)
{
	if (!team.capabilities.contains("medical") || !clinical_patient_eligible(before) ||
	    (activity == "stabilise" && before.condition != "severe") ||
	    (before.condition == "severe" && before.stabilised))
		return std::nullopt;
	const auto clinical_activity = before.condition == "severe" ? "stabilise" : "treat";
	const auto required = (before.condition == "severe" ? team.stabilise_ticks : team.treatment_ticks) * 1000;
	return ClinicalWorkRequest{clinical_activity, before.treatment, required,
	                           std::min(desired, required - before.treatment)};
}
std::optional<ClinicalWorkProgress>
prepare_clinical_progress(NativeTeamOutcome &prepared, const ClinicalWorkRequest &request, std::int64_t quoted_work)
{
	if (prepared.condition != "injured")
		return std::nullopt;
	prepared.treatment += quoted_work;
	const auto progress = prepared.treatment;
	const bool completed = progress >= request.required;
	if (completed)
		native_treat_team(prepared);
	return ClinicalWorkProgress{std::min(request.required, progress), completed};
}
std::optional<ClinicalWorkProgress>
prepare_clinical_progress(NativePatientOutcome &prepared, const ClinicalWorkRequest &request, std::int64_t quoted_work)
{
	if ((request.activity == "stabilise" && (prepared.condition != "severe" || prepared.stabilised)) ||
	    (request.activity == "treat" && prepared.condition != "minor"))
		return std::nullopt;
	prepared.treatment += quoted_work;
	const auto progress = prepared.treatment;
	const bool completed = progress >= request.required;
	if (completed)
	{
		if (prepared.condition == "severe")
			native_stabilise_patient(prepared);
		else
			native_treat_patient(prepared);
	}
	return ClinicalWorkProgress{std::min(request.required, progress), completed};
}
} // namespace phoenix
