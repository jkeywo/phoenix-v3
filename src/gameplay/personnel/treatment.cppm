module;
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

export module phoenix.gameplay.personnel.treatment;
export import phoenix.gameplay.personnel.outcomes;
export import phoenix.content.combat.types;

export namespace phoenix
{
struct ClinicalWorkRequest
{
	std::string activity;
	std::int64_t progress = 0, required = 0, work = 0;
};
struct ClinicalWorkProgress
{
	std::int64_t progress = 0;
	bool completed = false;
};
std::optional<ClinicalWorkRequest> clinical_team_work(const TeamDefinition &, const TeamState &,
                                                      const std::string &activity, std::int64_t desired);
std::optional<ClinicalWorkRequest> clinical_officer_work(const TeamDefinition &, const OfficerState &,
                                                         const std::string &activity, std::int64_t desired);
std::optional<ClinicalWorkProgress> prepare_clinical_progress(NativeTeamOutcome &, const ClinicalWorkRequest &,
                                                              std::int64_t quoted_work);
std::optional<ClinicalWorkProgress> prepare_clinical_progress(NativePatientOutcome &, const ClinicalWorkRequest &,
                                                              std::int64_t quoted_work);
} // namespace phoenix
